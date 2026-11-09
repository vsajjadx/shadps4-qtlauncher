// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <iostream>

#include <QDir>
#include <QMessageBox>
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
    p->kill();
    p->setParent(nullptr);
    connect(p, &QProcess::finished, p, &QObject::deleteLater);
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
    watchdog->stop();
    sawWindow = false;
    noWindowTicks = 0;
    process = new QProcess(this);

    connect(process, &QProcess::readyReadStandardError, this, [this] { onStderr(); });
    connect(process, &QProcess::readyReadStandardOutput, this, [this] { onStdout(); });
    connect(process, &QProcess::finished, this, [this] { onProcessClosed(); });

    process->setProcessChannelMode(QProcess::SeparateChannels);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (!disable_ipc) {
        env.insert("SHADPS4_ENABLE_IPC", "true");
    }
    process->setProcessEnvironment(env);

    process->setWorkingDirectory(workDir.isEmpty() ? exe.absolutePath() : workDir);
    process->start(exe.absoluteFilePath(), args, QIODevice::ReadWrite);

    if (process->error() == QProcess::FailedToStart) {
        QMessageBox::critical(nullptr, "shadPS4",
                              QString(tr("Could not find the emulator executable")));
        // No "finished" signal is ever emitted for a process that failed to start, so release the
        // launcher's "game running" state here instead of leaving it locked forever.
        onProcessClosed();
        return;
    }
    watchdog->start();
}

void IpcClient::startGame() {
    writeLine("START");
}

void IpcClient::pauseGame() {
    writeLine("PAUSE");
}

void IpcClient::resumeGame() {
    writeLine("RESUME");
}

void IpcClient::stopEmulator() {
    if (process == nullptr) {
        return;
    }
    writeLine("STOP");

    // STOP is only handled when the emulator's main loop is running. If the emulator is stuck
    // (e.g. while loading a game), it never exits and the launcher would stay blocked, so
    // terminate it when it has not quit by itself in time.
    QPointer<QProcess> stopping(process);
    QTimer::singleShot(8000, this, [this, stopping] {
        if (stopping && process == stopping.data() && stopping->state() != QProcess::NotRunning) {
            LOG_WARNING(IPC, "Emulator ignored STOP for 8 s, killing it");
            killAndRelease(stopping.data());
        }
    });
}

void IpcClient::restartEmulator() {
    if (process == nullptr) {
        return;
    }
    stopEmulator();
    pendingRestart = true;
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
    buffer.append(process->readAllStandardError());
    int idx;
    while ((idx = buffer.indexOf('\n')) != -1) {
        QByteArray line = buffer.left(idx);
        buffer.remove(0, idx + 1);

        if (!line.isEmpty() && line.back() == '\r') {
            line.chop(1);
        }

        if (!line.startsWith(";")) {
            LOG_ERROR(Tty, "{}", line.toStdString());
            continue;
        }

        const QString s = QString::fromUtf8(line.mid(1)).trimmed();
        if (s == "#IPC_ENABLED") {
            LOG_INFO(IPC, "IPC detected");
        } else if (s == "ENABLE_MEMORY_PATCH") {
            supportedCapabilities["memory_patch"] = true;
            LOG_INFO(IPC, "Feature detected: 'memory_patch'");
        } else if (s == "ENABLE_EMU_CONTROL") {
            supportedCapabilities["emu_control"] = true;
            LOG_INFO(IPC, "Feature detected: 'emu_control'");
        } else if (s == "#IPC_END") {
            for (const auto& [capability, supported] : supportedCapabilities) {
                if (not supported) {
                    LOG_WARNING(IPC,
                                "Feature: '{}' is not supported by the choosen emulator version",
                                capability);
                }
            }
            LOG_INFO(IPC, "Start emu");
            writeLine("RUN");
            startGameFunc();
        } else if (s == "RESTART") {
            parsingState = ParsingState::args_counter;
        }

        else if (parsingState == ParsingState::args_counter) {
            argsCounter = s.toInt();
            parsingState = ParsingState::args;
        }

        else if (parsingState == ParsingState::args) {
            parsedArgs.push_back(s.toStdString());
            if (parsedArgs.size() == argsCounter) {
                parsingState = ParsingState::normal;
                pendingRestart = true;
                stopEmulator();
            }
        }
    }
}

void IpcClient::onStdout() {
    static const QRegularExpression ansiRegex(
        R"(\x1B\[[0-9;]*[mK])"); // ANSI escape codes from UNIX terminals

    const QByteArray data = process->readAllStandardOutput();
    const QStringList entries = QString::fromUtf8(data).split('\n');

    // Merge consecutive lines of the same colour into one signal, so the UI thread appends once
    // per chunk instead of once per line (per-line appends froze the launcher during heavy logs).
    QString batch;
    QColor batchColor;
    const auto flush = [&] {
        if (!batch.isEmpty()) {
            emit LogEntrySent(batch, batchColor);
            batch.clear();
        }
    };

    for (QString entry : entries) {
        QColor color;
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
        } else {
            color = Qt::white;
        }

        if (entry.isEmpty() || entry == "\x1B[m") {
            continue;
        }

        if (m_log_to_terminal) {
            std::cout << entry.toStdString() << '\n';
            continue;
        }

        entry.replace(ansiRegex, "");
        entry = entry.trimmed();
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
    flush();
    if (m_log_to_terminal) {
        std::cout.flush();
    }
}

void IpcClient::onProcessClosed() {
    if (!process) {
        return;
    }
    watchdog->stop();
    // Release the process first so a problem in the UI handler below can never keep it locked.
    QProcess* closed = process;
    process = nullptr;
    releaseProcess(closed);

    if (gameClosedFunc) {
        gameClosedFunc();
    }
    if (pendingRestart) {
        pendingRestart = false;
        restartEmulatorFunc();
    }
}

void IpcClient::writeLine(const QString& text) {
    // Never block or pop up a modal box here: when the emulator is gone or stuck, a blocking
    // write (or a message box hidden behind the game window) freezes the whole launcher.
    if (process == nullptr || process->state() != QProcess::Running) {
        LOG_WARNING(IPC, "No emulator is running, ignoring '{}'", text.toStdString());
        return;
    }

    QByteArray data = text.toUtf8();
    data.append('\n');
    process->write(data);
}
