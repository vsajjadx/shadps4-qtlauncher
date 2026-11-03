// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// PKG installation, brought back from shadPS4 v0.7.0. Fake PKGs made with the default passcode
// need no keys; others use the keysets from Manage Cryptographic Keys.

#include <algorithm>
#include <numeric>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QInputDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QTimer>
#include <QtConcurrent>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPushButton>

#include "common/key_manager.h"
#include "common/path_util.h"
#include "core/crypto/crypto.h"
#include "core/emulator_settings.h"
#include "core/file_format/pkg.h"
#include "core/file_format/psf.h"
#include "crypto_key_dialog.h"
#include "main_window.h"

// A keys file next to the launcher (or one picked once) is imported, so later PKG installs need
// no setup. The keys are not part of the program.
static bool ImportKeysNextToLauncher() {
    const QStringList names{"keys.json", "pkg_keys.json"};
    const QStringList dirs{QCoreApplication::applicationDirPath(), QDir::currentPath()};
    for (const auto& dir : dirs) {
        for (const auto& name : names) {
            const QString path = QDir(dir).filePath(name);
            if (QFileInfo::exists(path) &&
                KeyManager::GetInstance()->ImportFromFile(Common::FS::PathFromQString(path)) &&
                Crypto::HasPkgKeys()) {
                return true;
            }
        }
    }
    return false;
}

void MainWindow::InstallPkg() {
    // Fake PKGs made with the default passcode install without keys. A keys file next to the
    // launcher is picked up for the others.
    // Load standard user/keys.json before searching beside the launcher.
    if (!Crypto::HasPkgKeys()) {
        KeyManager::GetInstance()->LoadFromFile();
    }
    if (!Crypto::HasPkgKeys()) {
        ImportKeysNextToLauncher();
    }

    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Install Packages (PKG)"), QString(), tr("PKG File (*.PKG *.pkg)"));
    if (files.isEmpty()) {
        return;
    }

    const auto install_dirs = EmulatorSettings.GetGameInstallDirs();
    std::filesystem::path install_dir;
    if (install_dirs.empty()) {
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Select the folder to install the games to"));
        if (dir.isEmpty()) {
            return;
        }
        install_dir = Common::FS::PathFromQString(dir);
    } else if (install_dirs.size() == 1) {
        install_dir = install_dirs.front();
    } else {
        QStringList names;
        for (const auto& dir : install_dirs) {
            QString name;
            Common::FS::PathToQString(name, dir);
            names << name;
        }
        bool ok = false;
        const QString chosen = QInputDialog::getItem(this, tr("Install Packages (PKG)"),
                                                     tr("Install to:"), names, 0, false, &ok);
        if (!ok) {
            return;
        }
        install_dir = Common::FS::PathFromQString(chosen);
    }

    for (int i = 0; i < files.size(); ++i) {
        InstallPkgFile(Common::FS::PathFromQString(files[i]), install_dir, i + 1, files.size());
    }
    RefreshGameTable();
}

void MainWindow::InstallPkgFile(const std::filesystem::path& file,
                                const std::filesystem::path& install_dir, int pkg_num,
                                int pkg_count) {
    PKG pkg;
    std::string failreason;
    if (!pkg.Open(file, failreason)) {
        QMessageBox::critical(this, tr("PKG ERROR"),
                              failreason.empty() ? tr("File doesn't appear to be a valid PKG file")
                                                 : QString::fromStdString(failreason));
        return;
    }

    PSF package_psf;
    if (!package_psf.Open(pkg.sfo)) {
        QMessageBox::critical(this, tr("PKG ERROR"), tr("Could not read SFO."));
        return;
    }

    const std::string title_id{pkg.GetTitleID()};
    const QString pkg_type = QString::fromStdString(pkg.GetPkgFlags());
    const auto category = package_psf.GetString("CATEGORY");
    const bool is_patch = pkg_type.contains("PATCH");
    const bool is_addon = category.has_value() && *category == "ac";

    std::filesystem::path game_folder = install_dir / title_id;
    if (const auto found = Common::FS::FindGameByID(install_dir, title_id, 5); found) {
        game_folder = found->parent_path();
    }
    const bool game_installed = std::filesystem::exists(game_folder);
    if ((is_patch || is_addon) && !game_installed) {
        QMessageBox::information(this, tr("PKG Extraction"),
                                 tr("PKG is a patch or DLC, please install the game first!"));
        return;
    }

    auto update_base = game_folder;
    if (update_base.extension() == ".zar") {
        update_base.replace_extension();
    }
    const std::filesystem::path update_folder =
        update_base.parent_path() / (update_base.filename().string() + "-UPDATE");
    std::filesystem::path extract_path = is_patch ? update_folder : game_folder;

    QString game_folder_text;
    Common::FS::PathToQString(game_folder_text, game_folder);
    if (is_patch) {
        QString package_version;
        if (const auto version = package_psf.GetString("APP_VER"); version.has_value()) {
            package_version = QString::fromStdString(std::string{*version});
        } else {
            QMessageBox::critical(this, tr("PKG ERROR"), tr("PSF file has no APP_VER."));
            return;
        }

        const auto update_param = update_folder / "sce_sys" / "param.sfo";
        const auto game_param = game_folder / "sce_sys" / "param.sfo";
        const auto installed_param = std::filesystem::exists(update_param) ? update_param : game_param;
        PSF installed_psf;
        if (!installed_psf.Open(installed_param)) {
            QMessageBox::critical(this, tr("PKG ERROR"),
                                  tr("Could not read the installed game's version."));
            return;
        }
        QString installed_version;
        if (const auto version = installed_psf.GetString("APP_VER"); version.has_value()) {
            installed_version = QString::fromStdString(std::string{*version});
        } else {
            QMessageBox::critical(this, tr("PKG ERROR"), tr("Installed game has no APP_VER."));
            return;
        }

        QString prompt;
        const double installed_number = installed_version.toDouble();
        const double package_number = package_version.toDouble();
        if (package_number == installed_number) {
            prompt = tr("PKG and game versions match: %1\nWould you like to overwrite?")
                         .arg(package_version);
        } else if (package_number < installed_number) {
            prompt = tr("PKG version %1 is older than installed version %2.\nWould you like to overwrite?")
                         .arg(package_version, installed_version);
        } else {
            prompt = tr("Game is installed at version %1.\nWould you like to install patch %2?")
                         .arg(installed_version, package_version);
        }
        if (QMessageBox::question(this, tr("Patch detected!"), prompt,
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) !=
            QMessageBox::Yes) {
            return;
        }
    } else if (is_addon) {
        const auto content_id = package_psf.GetString("CONTENT_ID");
        if (!content_id.has_value()) {
            QMessageBox::critical(this, tr("PKG ERROR"), tr("PSF file has no CONTENT_ID."));
            return;
        }
        const QStringList content_parts =
            QString::fromStdString(std::string{*content_id}).split('-');
        if (content_parts.size() < 3) {
            QMessageBox::critical(this, tr("PKG ERROR"), tr("PKG CONTENT_ID is invalid."));
            return;
        }
        const QString entitlement_label = content_parts[2];
        extract_path = EmulatorSettings.GetAddonInstallDir() / title_id /
                       entitlement_label.toStdString();
        QString addon_path_text;
        Common::FS::PathToQString(addon_path_text, extract_path);
        const QString prompt = std::filesystem::exists(extract_path)
                                   ? tr("DLC already installed:\n%1\n\nWould you like to overwrite?")
                                         .arg(addon_path_text)
                                   : tr("Would you like to install DLC: %1?").arg(entitlement_label);
        if (QMessageBox::question(this, tr("DLC Installation"), prompt,
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) !=
            QMessageBox::Yes) {
            return;
        }
    } else if (std::filesystem::exists(game_folder)) {
        if (QMessageBox::question(this, tr("PKG Extraction"),
                                  tr("Game already installed:\n%1\n\nWould you like to overwrite?")
                                      .arg(game_folder_text),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) !=
            QMessageBox::Yes) {
            return;
        }
    }

    QString extract_dir;
    Common::FS::PathToQString(extract_dir, extract_path);
    const bool existed = std::filesystem::exists(extract_path);
    if (!pkg.Extract(file, extract_path, failreason)) {
        if (!existed) {
            std::error_code ec;
            std::filesystem::remove_all(extract_path, ec);
        }
        if (Crypto::HasPkgKeys()) {
            QMessageBox::critical(this, tr("PKG ERROR"), QString::fromStdString(failreason));
            return;
        }
        QMessageBox box(this);
        box.setWindowTitle(tr("PKG ERROR"));
        box.setIcon(QMessageBox::Warning);
        box.setText(QString::fromStdString(failreason));
        const auto userKeysPath =
            Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "keys.json";
        QString userKeysPathQt;
        Common::FS::PathToQString(userKeysPathQt, userKeysPath);
        userKeysPathQt = QDir::toNativeSeparators(userKeysPathQt);
        box.setInformativeText(
            QFileInfo::exists(userKeysPathQt)
                ? tr("No valid PKG keysets were loaded from:\n%1").arg(userKeysPathQt)
                : tr("User keys file not found. Expected location:\n%1").arg(userKeysPathQt));
        QPushButton* choose = box.addButton(tr("Choose keys file..."), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() != choose) {
            return;
        }
        const QString path = QFileDialog::getOpenFileName(this, tr("Choose keys file"), QString(),
                                                          tr("Keys file (*.json)"));
        if (path.isEmpty() ||
            !KeyManager::GetInstance()->ImportFromFile(Common::FS::PathFromQString(path)) ||
            !Crypto::HasPkgKeys()) {
            QMessageBox::warning(this, tr("PKG ERROR"), tr("No PKG keys were found in that file."));
            return;
        }
        InstallPkgFile(file, install_dir, pkg_num, pkg_count);
        return;
    }

    const int file_count = static_cast<int>(pkg.GetNumberOfFiles());
    if (file_count > 0) {
        QVector<int> indices(file_count);
        std::iota(indices.begin(), indices.end(), 0);

        const u64 total_bytes = pkg.GetTotalExtractSize();
        constexpr int ProgressSteps = 10000; // 0.01% resolution

        const auto format_size = [](u64 bytes) {
            const double value = static_cast<double>(bytes);
            if (bytes >= (1ULL << 30)) {
                return QString::number(value / (1ULL << 30), 'f', 2) + QStringLiteral(" GB");
            }
            if (bytes >= (1ULL << 20)) {
                return QString::number(value / (1ULL << 20), 'f', 1) + QStringLiteral(" MB");
            }
            return QString::number(value / 1024.0, 'f', 0) + QStringLiteral(" KB");
        };
        const auto format_time = [](qint64 seconds) {
            return QStringLiteral("%1:%2:%3")
                .arg(seconds / 3600)
                .arg((seconds / 60) % 60, 2, 10, QChar('0'))
                .arg(seconds % 60, 2, 10, QChar('0'));
        };

        QProgressDialog dialog(this);
        dialog.setWindowTitle(tr("PKG Extraction"));
        dialog.setWindowModality(Qt::WindowModal);
        dialog.setMinimumDuration(0);
        dialog.setMinimumWidth(460);
        dialog.setLabelText(tr("Extracting PKG %1/%2").arg(pkg_num).arg(pkg_count));
        dialog.setRange(0, total_bytes > 0 ? ProgressSteps : file_count);
        dialog.setAutoClose(false);
        dialog.setAutoReset(false);

        QFutureWatcher<void> watcher;
        QEventLoop loop;
        QElapsedTimer clock;
        u64 last_bytes = 0;
        qint64 last_ms = 0;
        double speed = 0.0; // smoothed bytes per second

        const auto refresh = [&] {
            const u64 written = std::min(pkg.GetBytesWritten(), total_bytes);
            const qint64 now_ms = clock.elapsed();
            if (now_ms - last_ms >= 250) {
                const double instant =
                    static_cast<double>(written - last_bytes) * 1000.0 / (now_ms - last_ms);
                speed = speed <= 0.0 ? instant : speed * 0.7 + instant * 0.3;
                last_bytes = written;
                last_ms = now_ms;
            }
            const double percent =
                total_bytes > 0 ? 100.0 * static_cast<double>(written) / total_bytes : 0.0;
            QString text = tr("Extracting PKG %1/%2").arg(pkg_num).arg(pkg_count);
            text += QStringLiteral("\n%1%  -  %2 / %3 written")
                        .arg(percent, 0, 'f', 2)
                        .arg(format_size(written), format_size(total_bytes));
            if (speed > 1.0 && now_ms > 1000) {
                const qint64 left = static_cast<qint64>(
                    static_cast<double>(total_bytes - written) / speed + 0.5);
                text += tr("\nSpeed: %1/s  -  Time left: %2")
                            .arg(format_size(static_cast<u64>(speed)), format_time(left));
            } else {
                text += tr("\nSpeed: calculating...  -  Time left: calculating...");
            }
            dialog.setLabelText(text);
            dialog.setValue(total_bytes > 0
                                ? static_cast<int>(percent * ProgressSteps / 100.0)
                                : static_cast<int>(watcher.progressValue()));
        };

        QTimer ticker;
        ticker.setInterval(250);
        connect(&ticker, &QTimer::timeout, &dialog, refresh);
        connect(&dialog, &QProgressDialog::canceled, &watcher, &QFutureWatcher<void>::cancel);
        connect(&watcher, &QFutureWatcher<void>::finished, &loop, &QEventLoop::quit);
        clock.start();
        watcher.setFuture(
            QtConcurrent::map(indices, [&pkg](int index) { pkg.ExtractFiles(index); }));
        ticker.start();
        refresh();
        dialog.show();
        if (!watcher.isFinished()) {
            loop.exec();
        }
        ticker.stop();
        watcher.waitForFinished();
        dialog.close();
        if (watcher.isCanceled()) {
            return;
        }
    }

    if (pkg_num == pkg_count) {
        QMessageBox::information(this, tr("Extraction Finished"),
                                 tr("Game successfully installed at %1").arg(extract_dir));
    }
}
