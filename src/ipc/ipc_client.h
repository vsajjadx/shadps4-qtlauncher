// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <unordered_map>

#include <QColor>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>

#include "common/memory_patcher.h"

class QTimer;

class IpcClient : public QObject {
    Q_OBJECT

signals:
    void LogEntrySent(QString entry, QColor textColor);
    void EmulatorError(QString message);

public:
    explicit IpcClient(QObject* parent = nullptr, bool log_to_terminal = false);
    ~IpcClient() override;
    void startEmulator(const QFileInfo& exe, const QStringList& args,
                       const QString& workDir = QString(), bool disable_ipc = false);
    quint64 sessionId() const {
        return session;
    }
    void startGame();
    void pauseGame();
    void resumeGame();
    void stopEmulator();
    void restartEmulator();
    void toggleFullscreen();
    void adjustVol(int volume, bool game_specific);
    void setFsr(bool enable);
    void setRcas(bool enable);
    void setRcasAttenuation(int value);
    void reloadInputs(std::string config);
    void setActiveController(std::string GUID);
    void sendMemoryPatches(std::string modNameStr, std::string offsetStr, std::string valueStr,
                           std::string targetStr, std::string sizeStr, bool isOffset,
                           bool littleEndian,
                           MemoryPatcher::PatchMask patchMask = MemoryPatcher::PatchMask::None,
                           int maskOffset = 0);
    void loadSkylander(std::string file_name, int slot);
    void removeSkylander(int slot);
    void loadInfinityFigure(std::string file_name, int slot);
    void removeInfinityFigure(int slot);
    void loadDimensionFigure(std::string file_name, uint8_t pad, uint8_t index);
    void removeDimensionFigure(uint8_t pad, uint8_t index, bool fullRemove);
    void moveDimensionFigure(uint8_t new_pad, uint8_t new_index, uint8_t old_pad,
                             uint8_t old_index);
    void tempRemoveDimensionFigure(uint8_t index);
    void cancelRemoveDimensionFigure(uint8_t index);
    std::function<void()> gameClosedFunc;
    std::function<void()> startGameFunc;
    std::function<void()> restartEmulatorFunc;

    enum ParsingState { normal, args_counter, args };
    std::vector<std::string> parsedArgs;
    std::unordered_map<std::string, bool> supportedCapabilities{
        {"memory_patch", false},
        {"emu_control", false},
    };

private:
    void onStderr();
    void onStdout();
    void onProcessClosed();
    void writeLine(const QString& text);
    void queueLog(const QByteArray& data);
    void flushLogs();

    // Anti-hang helpers: the launcher must never stay locked behind an emulator that does not exit.
    void releaseProcess(QProcess* p);
    void killAndRelease(QProcess* p);
    void onWatchdogTick();

    QTimer* watchdog = nullptr;
    QTimer* startupTimer = nullptr;
    QTimer* stopTimer = nullptr;
    QTimer* logTimer = nullptr;
    QTimer* stderrTimer = nullptr;
    bool sawWindow = false;
    int noWindowTicks = 0;

    QProcess* process = nullptr;
    QByteArray buffer;
    QByteArray logBuffer;
    bool droppedLogs = false;
    bool discardStderrLine = false;
    bool handshakeComplete = false;
    bool stopping = false;
    quint64 session = 0;
    bool pendingRestart = false;

    ParsingState parsingState = ParsingState::normal;
    int argsCounter = 0;
    bool m_log_to_terminal;
};
