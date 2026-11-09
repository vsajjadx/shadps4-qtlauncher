// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <QTimer>

#include "ipc/ipc_client.h"

using namespace std::chrono_literals;

static int fakeEmulator(const std::string &mode) {
  if (mode == "exit") {
    return 0;
  }
  if (mode == "crash") {
#ifdef _MSC_VER
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    std::abort();
  }
  if (mode == "no-ipc") {
    std::cout << (std::getenv("SHADPS4_ENABLE_IPC") ? "IPC_ENABLED\n"
                                                    : "IPC_DISABLED\n");
    return 0;
  }
  if (mode == "no-handshake") {
    std::this_thread::sleep_for(30s);
    return 0;
  }
  std::cerr << ";#IPC_EN" << std::flush;
  std::this_thread::sleep_for(10ms);
  std::cerr << "ABLED\r\n;ENABLE_MEMORY_PATCH\n;ENABLE_EMU_CONTROL\n;#IPC_"
               "END\n;#IPC_END\n"
            << std::flush;
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line == "START") {
      if (mode == "normal") {
        std::cout << "final fragment without newline" << std::flush;
        return 0;
      }
      if (mode == "restart") {
        std::cerr << ";RESTART\n;2\n;--game\n;example\n" << std::flush;
      }
      if (mode == "zero-restart") {
        std::cerr << ";RESTART\n;0\n" << std::flush;
      }
      if (mode == "malformed") {
        std::cerr << ";RESTART\n;-1\n;RESTART\n;999999999\n" << std::flush;
      }
      if (mode == "flood") {
        const std::string chunk =
            std::string("<Error> ") + std::string(1000, 'x') + "\n";
        for (int i = 0; i < 30000; ++i) {
          std::fwrite(chunk.data(), 1, chunk.size(), stdout);
          if (i % 512 == 0) {
            std::cerr << std::string(6000, 'z') << "\n";
          }
        }
        std::fflush(stdout);
      }
    }
    if (line == "STOP" && mode != "hang") {
      return 0;
    }
  }
  return 0;
}

static void require(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

static bool waitUntil(const std::function<bool()> &done, int timeout = 3000) {
  QElapsedTimer elapsed;
  elapsed.start();
  while (!done() && elapsed.elapsed() < timeout) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  return done();
}

static void launch(IpcClient &client, const QString &mode, bool noIpc = false) {
  client.startEmulator(QFileInfo(QCoreApplication::applicationFilePath()),
                       {"--fake", mode}, {}, noIpc);
}

int main(int argc, char **argv) {
  if (argc == 3 && std::string(argv[1]) == "--fake") {
    return fakeEmulator(argv[2]);
  }
  QCoreApplication app(argc, argv);
  try {
    {
      IpcClient client;
      int closed = 0, errors = 0;
      client.gameClosedFunc = [&] { ++closed; };
      QObject::connect(&client, &IpcClient::EmulatorError, &client,
                       [&](const QString &) { ++errors; });
      client.startEmulator(QFileInfo("/missing/launcher-test-emulator"), {});
      require(waitUntil([&] { return closed == 1 && errors == 1; }),
              "FailedToStart did not unlock");
      int started = 0;
      QString output;
      QObject::connect(&client, &IpcClient::LogEntrySent, &client,
                       [&](const QString &entry, QColor) { output += entry; });
      client.startGameFunc = [&] {
        ++started;
        client.startGame();
      };
      launch(client, "normal");
      require(waitUntil([&] { return closed == 2; }),
              "Recovery after failed start failed");
      require(started == 1,
              "Duplicate or fragmented handshake launched more than once");
      require(waitUntil([&] { return output.contains("final fragment"); }),
              "Final unterminated log was lost");
      std::cout << "PASS failed-start recovery, fragmented/duplicate "
                   "handshake, log tail\n";
    }
    {
      IpcClient client;
      int closed = 0, started = 0;
      client.gameClosedFunc = [&] { ++closed; };
      client.startGameFunc = [&] {
        ++started;
        client.startGame();
      };
      launch(client, "exit");
      require(waitUntil([&] { return closed == 1; }),
              "Early exit left launcher locked");
      launch(client, "crash");
      require(waitUntil([&] { return closed == 2; }),
              "Crash left launcher locked");
      QString output;
      QObject::connect(&client, &IpcClient::LogEntrySent, &client,
                       [&](const QString &entry, QColor) { output += entry; });
      qputenv("SHADPS4_ENABLE_IPC", "true");
      launch(client, "no-ipc", true);
      require(waitUntil([&] { return closed == 3; }),
              "No-IPC process failed to exit");
      qunsetenv("SHADPS4_ENABLE_IPC");
      require(waitUntil([&] { return output.contains("IPC_DISABLED"); }),
              "Inherited IPC environment was not removed");
      require(started == 0, "No handshake must not invoke start callback");
      std::cout << "PASS early exit, crash, no-IPC launch\n";
    }
    {
      IpcClient client;
      int closed = 0, errors = 0;
      client.gameClosedFunc = [&] { ++closed; };
      QObject::connect(&client, &IpcClient::EmulatorError, &client,
                       [&](const QString &) { ++errors; });
      for (auto *timer : client.findChildren<QTimer *>()) {
        if (timer->interval() == 60000) {
          timer->setInterval(150);
        }
      }
      launch(client, "no-handshake");
      require(waitUntil([&] { return closed == 1 && errors == 1; }),
              "Startup timeout did not unlock");
      std::cout << "PASS missing-handshake watchdog\n";
    }
    {
      IpcClient client;
      int closed = 0, restarts = 0;
      client.gameClosedFunc = [&] { ++closed; };
      client.startGameFunc = [&] { client.startGame(); };
      client.restartEmulatorFunc = [&] {
        ++restarts;
        launch(client, "normal");
      };
      launch(client, "restart");
      require(waitUntil([&] { return closed == 2; }),
              "Restart did not complete");
      require(restarts == 1, "Restart callback count incorrect");
      launch(client, "zero-restart");
      require(waitUntil([&] { return closed == 4; }),
              "Zero-argument restart did not complete");
      require(restarts == 2, "Zero-argument restart callback count incorrect");
      launch(client, "malformed");
      require(waitUntil(
                  [&] { return client.supportedCapabilities["emu_control"]; }),
              "Malformed restart process not ready");
      QTimer::singleShot(100, &client, &IpcClient::stopEmulator);
      require(waitUntil([&] { return closed == 5; }),
              "Malformed restart left launcher locked");
      require(restarts == 2, "Malformed restart triggered a restart");
      std::cout
          << "PASS restart, zero arguments, malformed restart rejection\n";
    }
    {
      IpcClient client;
      int closed = 0, heartbeats = 0;
      client.gameClosedFunc = [&] { ++closed; };
      client.startGameFunc = [&] {
        client.startGame();
        QTimer::singleShot(200, &client, &IpcClient::stopEmulator);
        QTimer::singleShot(300, &client, &IpcClient::stopEmulator);
      };
      QTimer heartbeat;
      heartbeat.setInterval(5);
      QObject::connect(&heartbeat, &QTimer::timeout, &client,
                       [&] { ++heartbeats; });
      heartbeat.start();
      QElapsedTimer duration;
      duration.start();
      launch(client, "hang");
      require(waitUntil([&] { return closed == 1; }, 12000),
              "Ignored STOP did not recover");
      require(duration.elapsed() < 11000, "Repeated STOP extended deadline");
      require(heartbeats > 500, "UI event loop blocked during stop timeout");
      std::cout << "PASS ignored/repeated STOP, bounded kill, UI heartbeats="
                << heartbeats << "\n";
    }
    {
      IpcClient client;
      int closed = 0, heartbeats = 0, logSignals = 0;
      qint64 largestGap = 0;
      QElapsedTimer gap;
      gap.start();
      QTimer heartbeat;
      heartbeat.setInterval(5);
      QObject::connect(&heartbeat, &QTimer::timeout, &client, [&] {
        largestGap = qMax(largestGap, gap.restart());
        ++heartbeats;
      });
      heartbeat.start();
      QObject::connect(&client, &IpcClient::LogEntrySent, &client,
                       [&](const QString &entry, QColor) {
                         ++logSignals;
                         require(entry.size() <= 20000, "Unbounded log batch");
                       });
      client.gameClosedFunc = [&] { ++closed; };
      client.startGameFunc = [&] {
        client.startGame();
        QTimer::singleShot(500, &client, &IpcClient::stopEmulator);
      };
      launch(client, "flood");
      require(waitUntil([&] { return closed == 1; }, 12000),
              "Log flood prevented stop");
      require(heartbeats > 30, "Log flood starved UI heartbeat");
      require(largestGap < 1000,
              "Log flood blocked event loop for over one second");
      require(logSignals < 1000, "Unbounded per-line log signals");
      std::cout << "PASS 30MB log flood/oversized stderr, heartbeats="
                << heartbeats << ", max_gap_ms=" << largestGap
                << ", log_signals=" << logSignals << "\n";
    }
    {
      QElapsedTimer elapsed;
      elapsed.start();
      auto *client = new IpcClient;
      launch(*client, "hang");
      require(waitUntil(
                  [&] { return client->supportedCapabilities["emu_control"]; }),
              "Destructor test not ready");
      elapsed.restart();
      delete client;
      require(elapsed.elapsed() < 250, "Destructor blocked on live QProcess");
      require(waitUntil([&] { return true; }), "Event cleanup failed");
      std::cout << "PASS nonblocking destruction of live emulator\n";
    }
    std::cout << "ALL IPC REGRESSIONS PASSED\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
