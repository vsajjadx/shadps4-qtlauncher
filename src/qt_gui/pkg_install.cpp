// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// PKG installation, brought back from shadPS4 v0.7.0. Fake PKGs made with the default passcode
// need no keys; others use the keysets from Manage Cryptographic Keys.

#include <numeric>
#include <QEventLoop>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QInputDialog>
#include <QMessageBox>
#include <QProgressDialog>
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
    PSF psf;
    if (!psf.Open(pkg.sfo)) {
        QMessageBox::critical(this, tr("PKG ERROR"), tr("Could not read SFO."));
        return;
    }
    const std::string title_id{pkg.GetTitleID()};
    const auto category = psf.GetString("CATEGORY");
    const bool is_patch = QString::fromStdString(pkg.GetPkgFlags()).contains("PATCH");
    const bool is_addon = category.has_value() && *category == "ac";

    std::filesystem::path game_folder = install_dir / title_id;
    if (const auto found = Common::FS::FindGameByID(install_dir, title_id, 5); found) {
        game_folder = found->parent_path();
    }
    std::filesystem::path extract_path = game_folder;

    if (is_patch || is_addon) {
        if (!std::filesystem::exists(game_folder)) {
            QMessageBox::information(this, tr("PKG Extraction"),
                                     tr("PKG is a patch or DLC, please install the game first!"));
            return;
        }
        if (is_patch) {
            // The emulator loads updates from the "-UPDATE" folder next to the game.
            auto base = game_folder;
            if (base.extension() == ".zar") {
                base.replace_extension();
            }
            extract_path = base.parent_path() / (base.filename().string() + "-UPDATE");
        } else {
            const auto content_id = psf.GetString("CONTENT_ID");
            if (!content_id.has_value()) {
                QMessageBox::critical(this, tr("PKG ERROR"), tr("The PKG has no CONTENT_ID."));
                return;
            }
            // CONTENT_ID looks like UP0000-CUSA00000_00-LABEL; the DLC folder is the label.
            const QStringList parts = QString::fromStdString(std::string{*content_id}).split('-');
            const std::string label = parts.size() > 2 ? parts[2].toStdString() : title_id;
            extract_path = EmulatorSettings.GetAddonInstallDir() / title_id / label;
        }
    }

    QString extract_dir;
    Common::FS::PathToQString(extract_dir, extract_path);
    if (std::filesystem::exists(extract_path)) {
        const auto answer =
            QMessageBox::question(this, tr("PKG Extraction"),
                                  tr("Already installed:") + "\n" + extract_dir + "\n\n" +
                                      tr("Would you like to overwrite?"),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }

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
        // Offer a keys file for PKGs that do not use the default passcode, then try again.
        QMessageBox box(this);
        box.setWindowTitle(tr("PKG ERROR"));
        box.setIcon(QMessageBox::Warning);
        box.setText(QString::fromStdString(failreason));
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

        QProgressDialog dialog(this);
        dialog.setWindowTitle(tr("PKG Extraction"));
        dialog.setWindowModality(Qt::WindowModal);
        dialog.setLabelText(tr("Extracting PKG %1/%2").arg(pkg_num).arg(pkg_count));
        dialog.setRange(0, file_count);
        dialog.setAutoClose(true);

        QFutureWatcher<void> watcher;
        QEventLoop loop;
        connect(&dialog, &QProgressDialog::canceled, &watcher, &QFutureWatcher<void>::cancel);
        connect(&watcher, &QFutureWatcher<void>::progressValueChanged, &dialog,
                &QProgressDialog::setValue);
        connect(&watcher, &QFutureWatcher<void>::finished, &loop, &QEventLoop::quit);
        watcher.setFuture(
            QtConcurrent::map(indices, [&pkg](int index) { pkg.ExtractFiles(index); }));
        dialog.show();
        if (!watcher.isFinished()) {
            loop.exec();
        }
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
