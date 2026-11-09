// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <iostream>

#include <QDir>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTimer>

#include "common/logging/log.h"
#include "ipc_client.h"

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef NOGDI
#define NOGDI
#endif
#include <windows.h>
#pragma comment(lib, "user32.lib")
#endif

namespace {

// How long the emulator may have no visible window (after it had one) before it is considered a
// hung shutdown and gets killed. A normal exit takes well under a second.
constexpr int WindowGoneGraceSeconds = 15;
// How long to wait for the OS to finish terminating a killed process before the launcher stops
// waiting for it.
constexpr int KillWaitMs = 2000;
constexpr qsizetype MaxBufferedBytes = 128 * 1024;
constexpr qsizetype MaxLineBytes = 4096;
constexpr int MaxLinesPerTick = 128;

#ifdef Q_OS_WIN
struct WindowSearch {
    DWORD pid;
    bool found;
};

BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM param) {
    auto* search = reinterpret_cast<WindowSearch*>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == search->pid && IsWindowVisible(hwnd)) {
        search->found = true;
        return FALSE;
    }
    return TRUE;
}

bool HasVisibleWindow(qint64 pid) {
    WindowSearch search{static_cast<DWORD>(pid), false};
    EnumWindows(EnumWindowsCallback, reinterpret_cast<LPARAM>(&search));
    return search.found;
}
#endif

} // namespace

IpcClient::IpcClient(QObject* parent, bool log_to_terminal)
    : QObject(parent), m_log_to_terminal(log_to_terminal) {
    watchdog = new QTimer(this);
    watchdog->setInterval(1000);
    connect(watchdog, &QTimer::timeout, this, [this] { onWatchdogTick(); });
    startupTimer = new QTimer(this);
    startupTimer->setSingleShot(true);
    startupTimer->setInterval(60000);
    connect(startupTimer, &QTimer::timeout, this, [this] {
        if (process) {
            const QString message =
                tr("The emulator did not complete its IPC handshake within 60 seconds.");
            killAndRelease(process);
            emit EmulatorError(message);
        }
    });
    stopTimer = new QTimer(this);
    stopTimer->setSingleShot(true);
    stopTimer->setInterval(8000);
    connect(stopTimer, &QTimer::timeout, this, [this] {
        if (process) {
            LOG_WARNING(IPC, "Emulator ignored STOP for 8 s, killing it");
            killAndRelease(process);
        }
    });
    logTimer = new QTimer(this);
    logTimer->setInterval(50);
    connect(logTimer, &QTimer::timeout, this, &IpcClient::flushLogs);
    stderrTimer = new QTimer(this);
    stderrTimer->setSingleShot(true);
    stderrTimer->setInterval(1);
    connect(stderrTimer, &QTimer::timeout, this, &IpcClient::onStderr);
}

IpcClient::~IpcClient() {
    if (process) {
        QProcess* p = process;
        process = nullptr;
        releaseProcess(p);
    }
}

// Detaches a QProcess without ever destroying it while it is still running: ~QProcess() on a live
// process blocks the calling (UI) thread in waitForFinished() for up to 30 s, which is what made
// the launcher look frozen when the emulator would not die.
void IpcClient::releaseProcess(QProcess* p) {
    if (!p) {
        return;
    }
    p->disconnect();
    if (p->state() == QProcess::NotRunning) {
        p->deleteLater();
        return;
    }
    p->setParent(nullptr);
    connect(p, &QProcess::finished, p, &QObject::deleteLater);
    connect(p, &QProcess::errorOccurred, p, [p](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            p->deleteLater();
        }
    });
    p->kill();
}

// Kills the emulator and makes sure the launcher is released even if the OS never reports the
// process as finished (e.g. it is stuck inside a driver call).
void IpcClient::killAndRelease(QProcess* p) {
    if (!p || p->state() == QProcess::NotRunning) {
        return;
    }
    p->kill();
    QPointer<QProcess> guard(p);
    QTimer::singleShot(KillWaitMs, this, [this, guard] {
        if (guard && process == guard.data()) {
            LOG_WARNING(IPC, "Emulator did not exit after being killed, releasing the launcher");
            onProcessClosed();
        }
    });
}

void IpcClient::onWatchdogTick() {
    if (!process || process->state() == QProcess::NotRunning) {
        watchdog->stop();
        return;
    }
#ifdef Q_OS_WIN
    if (HasVisibleWindow(process->processId())) {
        sawWindow = true;
        noWindowTicks = 0;
    } else if (sawWindow && ++noWindowTicks >= WindowGoneGraceSeconds) {
        LOG_WARNING(IPC, "Emulator window was closed but the process did not exit, killing it");
        watchdog->stop();
        killAndRelease(process);
    }
#endif
}

void IpcClient::startEmulator(const QFileInfo& exe, const QStringList& args, const QString& workDir,
                              bool disable_ipc) {
    if (process) {
        QProcess* old = process;
        process = nullptr;
        releaseProcess(old);
    }
    ++session;
    watchdog->stop();
    startupTimer->stop();
    stopTimer->stop();
    stderrTimer->stop();
    buffer.clear();
    logBuffer.clear();
    droppedLogs = false;
    discardStderrLine = false;
    pendingRestart = false;
    handshakeComplete = false;
    stopping = false;
    parsingState = ParsingState::normal;
    argsCounter = 0;
    parsedArgs.clear();
    for (auto& [capability, supported] : supportedCapabilities) {
        supported = false;
    }
    sawWindow = false;
    noWindowTicks = 0;
    process = new QProcess(this);

    connect(process, &QProcess::readyReadStandardError, this, [this] { onStderr(); });
    connect(process, &QProcess::readyReadStandardOutput, this, [this] { onStdout(); });
    connect(process, &QProcess::finished, this, [this] { onProcessClosed(); });
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!process || error != QProcess::FailedToStart) {
            return;
        }
        const QString message = tr("Could not start the emulator: %1").arg(process->errorString());
        onProcessClosed();
        emit EmulatorError(message);
    });
    connect(process, &QProcess::started, this, [this, disable_ipc] {
        watchdog->start();
        if (!disable_ipc) {
            startupTimer->start();
        }
    });

    process->setProcessChannelMode(QProcess::SeparateChannels);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove("SHADPS4_ENABLE_IPC");
    if (!disable_ipc) {
        env.insert("SHADPS4_ENABLE_IPC", "true");
    }
    process->setProcessEnvironment(env);

    process->setWorkingDirectory(workDir.isEmpty() ? exe.absolutePath() : workDir);
    process->start(exe.absoluteFilePath(), args, QIODevice::ReadWrite);
}

void IpcClient::startGame() {
    if (!stopping) {
        writeLine("START");
    }
}

void IpcClient::pauseGame() {
    writeLine("PAUSE");
}

void IpcClient::resumeGame() {
    writeLine("RESUME");
}

void IpcClient::stopEmulator() {
    if (!process || stopping) {
        return;
    }
    stopping = true;
    ++session;
    startupTimer->stop();
    writeLine("STOP");
    stopTimer->start();
}

void IpcClient::restartEmulator() {
    if (process == nullptr) {
        return;
    }
    pendingRestart = true;
    stopEmulator();
}

void IpcClient::toggleFullscreen() {
    writeLine("TOGGLE_FULLSCREEN");
}

void IpcClient::adjustVol(int volume, bool is_game_specific) {
    writeLine("ADJUST_VOLUME");
    writeLine(QString::number(volume));
    writeLine(is_game_specific ? "1" : "0");
}

void IpcClient::setFsr(bool enable) {
    writeLine("SET_FSR");
    writeLine(enable ? "1" : "0");
}

void IpcClient::setRcas(bool enable) {
    writeLine("SET_RCAS");
    writeLine(enable ? "1" : "0");
}

void IpcClient::setRcasAttenuation(int value) {
    writeLine("SET_RCAS_ATTENUATION");
    writeLine(QString::number(value));
}

void IpcClient::reloadInputs(std::string config) {
    writeLine("RELOAD_INPUTS");
    writeLine(QString::fromStdString(config));
}

void IpcClient::setActiveController(std::string GUID) {
    writeLine("SET_ACTIVE_CONTROLLER");
    writeLine(QString::fromStdString(GUID));
}

void IpcClient::sendMemoryPatches(std::string modNameStr, std::string offsetStr,
                                  std::string valueStr, std::string targetStr, std::string sizeStr,
                                  bool isOffset, bool littleEndian,
                                  MemoryPatcher::PatchMask patchMask, int maskOffset) {
    writeLine("PATCH_MEMORY");
    writeLine(QString::fromStdString(modNameStr));
    writeLine(QString::fromStdString(offsetStr));
    writeLine(QString::fromStdString(valueStr));
    writeLine(QString::fromStdString(targetStr));
    writeLine(QString::fromStdString(sizeStr));
    writeLine(isOffset ? "1" : "0");
    writeLine(littleEndian ? "1" : "0");
    writeLine(QString::number(static_cast<int>(patchMask)));
    writeLine(QString::number(maskOffset));
}

void IpcClient::loadSkylander(std::string file_name, int slot) {
    writeLine("USB_LOAD_FIGURE");
    writeLine(QString::fromStdString(file_name));
    writeLine(QString::number(slot)); // skylanders don't use pads
    writeLine(QString::number(slot));
}

void IpcClient::removeSkylander(int slot) {
    writeLine("USB_REMOVE_FIGURE");
    writeLine(QString::number(slot)); // skylanders don't use pads
    writeLine(QString::number(slot));
    writeLine("1"); // always a full remove
}

void IpcClient::loadInfinityFigure(std::string file_name, int slot) {
    writeLine("USB_LOAD_FIGURE");
    writeLine(QString::fromStdString(file_name));
    writeLine(QString::number(slot)); // infinity figures don't use pads
    writeLine(QString::number(slot));
}

void IpcClient::removeInfinityFigure(int slot) {
    writeLine("USB_REMOVE_FIGURE");
    writeLine(QString::number(slot)); // infinity figures don't use pads
    writeLine(QString::number(slot));
    writeLine("1"); // always a full remove
}

void IpcClient::loadDimensionFigure(std::string file_name, uint8_t pad, uint8_t index) {
    writeLine("USB_LOAD_FIGURE");
    writeLine(QString::fromStdString(file_name));
    writeLine(QString::number(pad));
    writeLine(QString::number(index));
}

void IpcClient::removeDimensionFigure(uint8_t pad, uint8_t index, bool fullRemove) {
    writeLine("USB_REMOVE_FIGURE");
    writeLine(QString::number(pad));
    writeLine(QString::number(index));
    writeLine(fullRemove ? "1" : "0");
}

void IpcClient::moveDimensionFigure(uint8_t new_pad, uint8_t new_index, uint8_t old_pad,
                                    uint8_t old_index) {
    writeLine("USB_MOVE_FIGURE");
    writeLine(QString::number(new_pad));
    writeLine(QString::number(new_index));
    writeLine(QString::number(old_pad));
    writeLine(QString::number(old_index));
}

void IpcClient::tempRemoveDimensionFigure(uint8_t index) {
    writeLine("USB_TEMP_REMOVE_FIGURE");
    writeLine(QString::number(index));
}

void IpcClient::cancelRemoveDimensionFigure(uint8_t index) {
    writeLine("USB_CANCEL_REMOVE_FIGURE");
    writeLine(QString::number(index));
}

void IpcClient::onStderr() {
    if (!process) {
        return;
    }
    buffer.append(process->readAllStandardError());
    if (buffer.size() > MaxBufferedBytes) {
        buffer = buffer.right(MaxBufferedBytes);
        discardStderrLine = true;
        droppedLogs = true;
    }
    qsizetype consumed = 0;
    int lines = 0;
    while (lines++ < MaxLinesPerTick) {
        const auto idx = buffer.indexOf('\n', consumed);
        if (idx < 0) {
            break;
        }
        QByteArray line = buffer.mid(consumed, idx - consumed);
        consumed = idx + 1;
        if (discardStderrLine || line.size() > MaxLineBytes) {
            discardStderrLine = false;
            droppedLogs = true;
            continue;
        }
        if (!line.isEmpty() && line.back() == '\r') {
            line.chop(1);
        }
        if (!line.startsWith(';')) {
            queueLog(line + '\n');
            continue;
        }
        const QString text = QString::fromUtf8(line.mid(1)).trimmed();
        if (parsingState == ParsingState::args_counter) {
            bool ok = false;
            argsCounter = text.toInt(&ok);
            parsedArgs.clear();
            parsingState = ok && argsCounter > 0 && argsCounter <= 256 ? ParsingState::args
                                                                       : ParsingState::normal;
            if (ok && argsCounter == 0) {
                pendingRestart = true;
                stopEmulator();
            }
        } else if (parsingState == ParsingState::args) {
            parsedArgs.push_back(text.toStdString());
            if (parsedArgs.size() == static_cast<size_t>(argsCounter)) {
                parsingState = ParsingState::normal;
                pendingRestart = true;
                stopEmulator();
            }
        } else if (text == "#IPC_ENABLED") {
            LOG_INFO(IPC, "IPC detected");
        } else if (text == "ENABLE_MEMORY_PATCH") {
            supportedCapabilities["memory_patch"] = true;
        } else if (text == "ENABLE_EMU_CONTROL") {
            supportedCapabilities["emu_control"] = true;
        } else if (text == "#IPC_END" && !handshakeComplete && !stopping) {
            handshakeComplete = true;
            startupTimer->stop();
            writeLine("RUN");
            const auto currentSession = session;
            QTimer::singleShot(0, this, [this, currentSession] {
                if (process && session == currentSession && !stopping && startGameFunc) {
                    startGameFunc();
                }
            });
        } else if (text == "RESTART") {
            parsingState = ParsingState::args_counter;
        }
    }
    buffer.remove(0, consumed);
    if (buffer.size() > MaxLineBytes && !buffer.contains('\n')) {
        buffer.clear();
        discardStderrLine = true;
        droppedLogs = true;
    }
    if (buffer.contains('\n') && !stderrTimer->isActive()) {
        stderrTimer->start();
    }
}

void IpcClient::onStdout() {
    if (process) {
        queueLog(process->readAllStandardOutput());
    }
}

void IpcClient::queueLog(const QByteArray& data) {
    if (data.size() >= MaxBufferedBytes) {
        logBuffer = data.right(MaxBufferedBytes);
        droppedLogs = true;
    } else {
        logBuffer.append(data);
    }
    if (logBuffer.size() > MaxBufferedBytes) {
        logBuffer.remove(0, logBuffer.size() - MaxBufferedBytes);
        droppedLogs = true;
    }
    if (droppedLogs) {
        const auto newline = logBuffer.indexOf('\n');
        if (newline >= 0) {
            logBuffer.remove(0, newline + 1);
        }
    }
    if (!logTimer->isActive()) {
        logTimer->start();
    }
}

void IpcClient::flushLogs() {
    static const QRegularExpression ansiRegex(R"(\x1B\[[0-9;]*[mK])");
    if (droppedLogs) {
        emit LogEntrySent(tr("[Log display throttled; excess output was discarded]"), Qt::yellow);
        droppedLogs = false;
    }
    QString batch;
    QColor batchColor;
    const auto flush = [&] {
        if (!batch.isEmpty()) {
            emit LogEntrySent(batch, batchColor);
            batch.clear();
        }
    };
    qsizetype consumed = 0;
    for (int lines = 0; lines < MaxLinesPerTick && consumed < 16 * 1024; ++lines) {
        const auto newline = logBuffer.indexOf('\n', consumed);
        if (newline < 0 && process && logBuffer.size() - consumed <= MaxLineBytes) {
            break;
        }
        const auto end = newline < 0 ? logBuffer.size() : newline;
        QString entry =
            QString::fromUtf8(logBuffer.mid(consumed, qMin(end - consumed, MaxLineBytes)));
        consumed = newline < 0 ? end : end + 1;
        QColor color = Qt::white;
        if (entry.contains("<Warning>")) {
            color = Qt::yellow;
        } else if (entry.contains("<Error>")) {
            color = Qt::red;
        } else if (entry.contains("<Critical>")) {
            color = Qt::magenta;
        } else if (entry.contains("<Trace>")) {
            color = Qt::gray;
        } else if (entry.contains("<Debug>")) {
            color = Qt::cyan;
        }
        if (m_log_to_terminal) {
            std::cout << entry.toStdString() << '\n';
            continue;
        }
        entry.replace(ansiRegex, "");
        if (entry.isEmpty()) {
            continue;
        }
        if (!batch.isEmpty() && color != batchColor) {
            flush();
        }
        batchColor = color;
        if (!batch.isEmpty()) {
            batch.append('\n');
        }
        batch.append(entry);
    }
    logBuffer.remove(0, consumed);
    flush();
    if (logBuffer.isEmpty()) {
        logTimer->stop();
    }
}

void IpcClient::onProcessClosed() {
    if (!process) {
        return;
    }
    ++session;
    watchdog->stop();
    startupTimer->stop();
    stopTimer->stop();
    stderrTimer->stop();
    buffer.clear();
    stopping = false;
    const bool restart = pendingRestart;
    pendingRestart = false;
    // Release the process first so a problem in the UI handler below can never keep it locked.
    QProcess* closed = process;
    process = nullptr;
    releaseProcess(closed);

    if (gameClosedFunc) {
        gameClosedFunc();
    }
    if (restart && restartEmulatorFunc) {
        QTimer::singleShot(0, this, [this] {
            if (!process && restartEmulatorFunc) {
                restartEmulatorFunc();
            }
        });
    }
}

void IpcClient::writeLine(const QString& text) {
    // Never block or pop up a modal box here: when the emulator is gone or stuck, a blocking
    // write (or a message box hidden behind the game window) freezes the whole launcher.
    if (process == nullptr || process->state() == QProcess::NotRunning) {
        LOG_WARNING(IPC, "No emulator is running, ignoring '{}'", text.toStdString());
        return;
    }

    QByteArray data = text.toUtf8();
    data.append('\n');
    process->write(data);
}
