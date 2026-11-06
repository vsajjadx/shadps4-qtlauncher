// SPDX-FileCopyrightText: Copyright 2025-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <iostream>
#include <vector>
#include <AL/al.h>
#include <AL/alc.h>
#include <QCompleter>
#include <QDesktopServices>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QListWidget>
#include <QPainter>
#include <QScrollArea>
#include <QStyle>
#include <QTabBar>
#include <QTimer>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QVBoxLayout>
#include <QHoverEvent>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QSlider>
#include <QTableWidget>
#include <SDL3/SDL.h>
#include <fmt/format.h>

#include "common/assert.h"
#include "common/logging/log.h"
#include "common/path_util.h"
#include "qt_gui/compatibility_info.h"
#ifdef ENABLE_UPDATER
#include "check_update.h"
#endif
#include "background_music_player.h"
#include "common/logging/log.h"
#include "core/emulator_state.h"
#include "log_presets_dialog.h"
#include "sdl_event_wrapper.h"
#include "settings_dialog.h"
#include "ui_settings_dialog.h"

#ifndef __APPLE__
#define VOLK_IMPLEMENTATION
#include "volk.h"
#else
#include "qt_gui/apple.h"
#endif

// extern std::unique_ptr<Vulkan::Presenter> presenter;

QStringList languageNames = {"Arabic",
                             "Czech",
                             "Danish",
                             "Dutch",
                             "English (United Kingdom)",
                             "English (United States)",
                             "Finnish",
                             "French (Canada)",
                             "French (France)",
                             "German",
                             "Greek",
                             "Hungarian",
                             "Indonesian",
                             "Italian",
                             "Japanese",
                             "Korean",
                             "Norwegian (Bokmaal)",
                             "Polish",
                             "Portuguese (Brazil)",
                             "Portuguese (Portugal)",
                             "Romanian",
                             "Russian",
                             "Simplified Chinese",
                             "Spanish (Latin America)",
                             "Spanish (Spain)",
                             "Swedish",
                             "Thai",
                             "Traditional Chinese",
                             "Turkish",
                             "Ukrainian",
                             "Vietnamese"};

const QVector<int> languageIndexes = {21, 23, 14, 6, 18, 1, 12, 22, 2, 4,  25, 24, 29, 5,  0, 9,
                                      15, 16, 17, 7, 26, 8, 11, 20, 3, 13, 27, 10, 19, 30, 28};
QMap<QString, QString> channelMap;
QMap<QString, QString> logTypeMap;
QMap<QString, QString> screenModeMap;
QMap<QString, QString> presentModeMap;
QMap<QString, QString> chooseHomeTabMap;
QMap<QString, QString> micMap;
QMap<int, QString> audioBackendMap;

int backgroundImageOpacitySlider_backup;
int bgm_volume_backup;

static std::vector<QString> m_physical_devices;

void LogUpdateLevels() {
    spdlog::level default_log_level = spdlog::level::info;
    std::unordered_map<std::string, spdlog::level> log_level_per_class;

    if (EmulatorSettings.IsLogEnable()) {
        for (const auto class_level : std::views::split(EmulatorSettings.GetLogFilter(), ' ')) {
            const auto class_level_pair =
                std::views::split(class_level, ':') | std::ranges::to<std::vector<std::string>>();

            if (class_level_pair.size() != 2) {
                std::cerr << "bad log filter provided" << std::endl;
                continue;
            }

            if (class_level_pair.front()[0] == '*') {
                default_log_level = spdlog::level_from_str(class_level_pair.back() |
                                                           std::ranges::to<std::string>());
            } else {
                log_level_per_class[class_level_pair.front() | std::ranges::to<std::string>()] =
                    spdlog::level_from_str(class_level_pair.back() |
                                           std::ranges::to<std::string>());
            }
        }
    }

    for (auto& [name, logger] : Common::Log::ALL_LOGGERS) {
        ASSERT_MSG(logger != nullptr, "logger {} is null", name);

        if (EmulatorSettings.IsLogEnable()) {
            const auto level_it = log_level_per_class.find(std::string(name));

            logger->set_level(level_it != log_level_per_class.end() ? level_it->second
                                                                    : default_log_level);
        } else {
            logger->set_level(spdlog::level::off);
        }
    }
}

SettingsDialog::SettingsDialog(std::shared_ptr<gui_settings> gui_settings,
                               std::shared_ptr<CompatibilityInfoClass> m_compat_info,
                               std::shared_ptr<IpcClient> ipc_client, QWidget* parent,
                               bool is_running, bool is_specific, std::string gsc_serial)
    : QDialog(parent), ui(new Ui::SettingsDialog), m_gui_settings(std::move(gui_settings)),
      m_ipc_client(ipc_client), is_game_running(is_running), is_game_specific(is_specific),
      gs_serial(gsc_serial) {

    ui->setupUi(this);
    ui->tabWidgetSettings->setUsesScrollButtons(false);
    SetupGraphicsExtras();
    SetupLogLevels();
    GetPhysicalDevices();

    initialHeight = this->height();

    // Add a small clear "x" button inside the Log Filter input
    ui->logFilterLineEdit->setClearButtonEnabled(true);

    if (is_game_specific) {
        EmulatorSettings.Load(gs_serial);
    } else {
        EmulatorSettings.Load();
    }

    if (is_game_specific) {
        // Paths tab
        ui->tabWidgetSettings->setTabVisible(6, false);
        ui->chooseHomeTabComboBox->removeItem(6);

        // Frontend tab
        ui->tabWidgetSettings->setTabVisible(1, false);
        ui->chooseHomeTabComboBox->removeItem(1);

    } else {
        // Experimental tab
        ui->tabWidgetSettings->setTabVisible(9, false);
        ui->chooseHomeTabComboBox->removeItem(9);
    }

#ifndef _WIN32
    ui->redZoneGroupBox->setVisible(false);
#endif
    SetupNavigation();

    ui->buttonBox->button(QDialogButtonBox::StandardButton::Close)->setFocus();

    logTypeMap = {{tr("wincolor"), "wincolor"}, {tr("msvc"), "msvc"}};
    screenModeMap = {{tr("Fullscreen (Borderless)"), "Fullscreen (Borderless)"},
                     {tr("Windowed"), "Windowed"},
                     {tr("Fullscreen"), "Fullscreen"}};
    presentModeMap = {{tr("Mailbox (Vsync)"), "Mailbox"},
                      {tr("Fifo (Vsync)"), "Fifo"},
                      {tr("Immediate (No Vsync)"), "Immediate"}};
    chooseHomeTabMap = {{tr("General"), "General"},   {tr("Frontend"), "Frontend"},
                        {tr("Graphics"), "Graphics"}, {tr("User"), "User"},
                        {tr("Input"), "Input"},       {tr("Network"), "Network"},
                        {tr("Paths"), "Paths"},       {tr("Log"), "Log"},
                        {tr("Debug"), "Debug"},       {tr("Experimental"), "Experimental"}};
    micMap = {{tr("None"), "None"}, {tr("Default Device"), "Default Device"}};
    audioBackendMap = {{0, "SDL"}, {1, "OpenAL"}};

    if (m_physical_devices.empty()) {
        // Populate cache of physical devices.
        // Vulkan::Instance instance(false, false);
        /* auto physical_devices = getPhysicalDevices();
        for (const vk::PhysicalDevice physical_device : physical_devices) {
            auto prop = physical_device.getProperties();
            QString name = QString::fromUtf8(prop.deviceName, -1);
            if (prop.apiVersion < Vulkan::TargetVulkanApiVersion) {
                name += tr(" * Unsupported Vulkan Version");
            }
            m_physical_devices.push_back(name);
        }*/
        GetPhysicalDevices();
    }

    // Add list of available GPUs
    ui->graphicsAdapterBox->addItem(tr("Auto Select")); // -1, auto selection
    for (const auto& device : m_physical_devices) {
        ui->graphicsAdapterBox->addItem(device);
    }

    ui->consoleLanguageComboBox->addItems(languageNames);

    QCompleter* completer = new QCompleter(languageNames, this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    ui->consoleLanguageComboBox->setCompleter(completer);

    ui->hideCursorComboBox->addItem(tr("Never"));
    ui->hideCursorComboBox->addItem(tr("Idle"));
    ui->hideCursorComboBox->addItem(tr("Always"));

    ui->usbComboBox->addItem(tr("Real USB Device"));
    ui->usbComboBox->addItem(tr("Skylander Portal"));
    ui->usbComboBox->addItem(tr("Infinity Base"));
    ui->usbComboBox->addItem(tr("Dimensions Toypad"));

    ui->cameraComboBox->addItem(tr("None"));

    if (!SDL_Init(SDL_INIT_CAMERA)) {
        LOG_ERROR(Config, "SDL_INIT_CAMERA failed: {}", SDL_GetError());
    }

    int count = 0;
    SDL_CameraID* devices = SDL_GetCameras(&count);
    if (devices) {
        for (int i = 0; i < count; i++) {
            const char* name = SDL_GetCameraName(devices[i]);
            ui->cameraComboBox->addItem(name);
        }
        SDL_free(devices);
    }

    SDL_QuitSubSystem(SDL_INIT_CAMERA);

    if (!SDL_Init(SDL_INIT_AUDIO)) {
        LOG_ERROR(Config, "SDL_INIT_AUDIO failed: {}", SDL_GetError());
    }
    ui->audioBackendComboBox->setCurrentIndex(EmulatorSettings.GetAudioBackend());
    RefreshAudioDevices();

    InitializeEmulatorLanguages();
    LoadValuesFromConfig();

    defaultTextEdit = tr("Point your mouse at an option to display its description.");
    ui->descriptionText->setText(defaultTextEdit);

    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QWidget::close);

    connect(ui->buttonBox, &QDialogButtonBox::clicked, this, [this](QAbstractButton* button) {
        if (button == ui->buttonBox->button(QDialogButtonBox::Save)) {
            is_game_saving = true;
            UpdateSettings(is_game_specific);
            SaveSettings();
            QWidget::close();
        } else if (button == ui->buttonBox->button(QDialogButtonBox::Apply)) {
            UpdateSettings(is_game_specific);
            SaveSettings();
        } else if (button == ui->buttonBox->button(QDialogButtonBox::RestoreDefaults)) {
            SetDefaultValues();
            EmulatorSettings.SetDefaultValues();
            SaveSettings();
            LoadValuesFromConfig();
        } else if (button == ui->buttonBox->button(QDialogButtonBox::Close)) {
            ui->backgroundImageOpacitySlider->setValue(backgroundImageOpacitySlider_backup);
            emit BackgroundOpacityChanged(backgroundImageOpacitySlider_backup);
            ui->BGMVolumeSlider->setValue(bgm_volume_backup);
            BackgroundMusicPlayer::getInstance().setVolume(bgm_volume_backup);
            SyncRealTimeWidgetstoConfig();
        }

        LogUpdateLevels();
    });

    ui->buttonBox->button(QDialogButtonBox::Save)->setText(tr("Save"));
    ui->buttonBox->button(QDialogButtonBox::Apply)->setText(tr("Apply"));
    ui->buttonBox->button(QDialogButtonBox::RestoreDefaults)->setText(tr("Restore Defaults"));
    ui->buttonBox->button(QDialogButtonBox::Close)->setText(tr("Close"));

    connect(ui->tabWidgetSettings, &QTabWidget::currentChanged, this,
            [this]() { ui->buttonBox->button(QDialogButtonBox::Close)->setFocus(); });

    // GENERAL TAB
    {
        connect(ui->horizontalVolumeSlider, &QSlider::valueChanged, this, [this](int value) {
            VolumeSliderChange(value);

            if (EmulatorState::GetInstance()->IsGameRunning())
                m_ipc_client->adjustVol(value, is_game_specific);
        });

        connect(ui->audioBackendComboBox, &QComboBox::currentIndexChanged, this,
                &SettingsDialog::RefreshAudioDevices);

#ifdef ENABLE_UPDATER
        connect(ui->updateCheckBox, &QCheckBox::checkStateChanged, this,
                [this](Qt::CheckState state) {
                    m_gui_settings->SetValue(gui::gen_checkForUpdates, state == Qt::Checked);
                });

        connect(ui->changelogCheckBox, &QCheckBox::checkStateChanged, this,
                [this](Qt::CheckState state) {
                    m_gui_settings->SetValue(gui::gen_showChangeLog, state == Qt::Checked);
                });
        connect(ui->checkUpdateButton, &QPushButton::clicked, this, [this]() {
            auto checkUpdate = new CheckUpdate(m_gui_settings, true);
            checkUpdate->exec();
        });
#else
        ui->updaterGroupBox->setVisible(false);
#endif
        connect(ui->updateCompatibilityButton, &QPushButton::clicked, this,
                [this, parent, m_compat_info]() {
                    m_compat_info->UpdateCompatibilityDatabase(this, true);
                    emit CompatibilityChanged();
                });

        connect(ui->enableCompatibilityCheckBox, &QCheckBox::checkStateChanged, this,
                [this, m_compat_info](Qt::CheckState state) {
                    m_gui_settings->SetValue(gui::glc_showCompatibility, state);
                    if (state) {
                        m_compat_info->LoadCompatibilityFile();
                    }
                    emit CompatibilityChanged();
                });
    }

    // GUI TAB
    {
        connect(ui->backgroundImageOpacitySlider, &QSlider::valueChanged, this,
                [this](int value) { emit BackgroundOpacityChanged(value); });

        connect(ui->BGMVolumeSlider, &QSlider::valueChanged, this,
                [](int value) { BackgroundMusicPlayer::getInstance().setVolume(value); });
        connect(ui->showBackgroundImageCheckBox, &QCheckBox::checkStateChanged, this,
                [this](Qt::CheckState state) {
                    m_gui_settings->SetValue(gui::gl_showBackgroundImage, state == Qt::Checked);
                });
    }

    // USER TAB
    {
        connect(ui->OpenCustomTrophyLocationButton, &QPushButton::clicked, this, []() {
            QString userPath;
            Common::FS::PathToQString(userPath,
                                      Common::FS::GetUserPath(Common::FS::PathType::CustomTrophy));
            QDesktopServices::openUrl(QUrl::fromLocalFile(userPath));
        });

        connect(ui->PortableUserButton, &QPushButton::clicked, this, []() {
            QString launcherDir;
            Common::FS::PathToQString(launcherDir, std::filesystem::current_path() / "launcher");
            if (std::filesystem::exists(std::filesystem::current_path() / "launcher")) {
                QMessageBox::information(NULL, tr("Cannot create portable launcher folder"),
                                         tr("%1 already exists").arg(launcherDir));
            } else {
                std::filesystem::copy(Common::FS::GetUserPath(Common::FS::PathType::LauncherDir),
                                      std::filesystem::current_path() / "launcher",
                                      std::filesystem::copy_options::recursive);
                QMessageBox::information(NULL, tr("Portable launcherDir folder created"),
                                         tr("%1 successfully created.").arg(launcherDir));
            }
        });
    }

    // INPUT TAB
    {
        connect(ui->hideCursorComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this](s16 index) { OnCursorStateChanged(index); });
    }

    // PATH TAB
    {
        connect(ui->addFolderButton, &QPushButton::clicked, this, [this]() {
            QString file_path_string =
                QFileDialog::getExistingDirectory(this, tr("Directory to install games"));
            auto file_path = Common::FS::PathFromQString(file_path_string);
            if (!file_path.empty() && EmulatorSettings.AddGameInstallDir(file_path, true)) {
                QListWidgetItem* item = new QListWidgetItem(file_path_string);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(Qt::Checked);
                ui->gameFoldersListWidget->addItem(item);
            }
        });

        connect(ui->gameFoldersListWidget, &QListWidget::itemSelectionChanged, this, [this]() {
            ui->removeFolderButton->setEnabled(
                !ui->gameFoldersListWidget->selectedItems().isEmpty());
        });

        connect(ui->removeFolderButton, &QPushButton::clicked, this, [this]() {
            QListWidgetItem* selected_item = ui->gameFoldersListWidget->currentItem();
            QString item_path_string = selected_item ? selected_item->text() : QString();
            if (!item_path_string.isEmpty()) {
                auto file_path = Common::FS::PathFromQString(item_path_string);
                EmulatorSettings.RemoveGameInstallDir(file_path);
                delete selected_item;
            }
        });

        connect(ui->homeFolderButton, &QPushButton::clicked, this, [this]() {
            const auto home_path = EmulatorSettings.GetHomeDir();
            QString initial_path;
            Common::FS::PathToQString(initial_path, home_path);

            QString home_path_string = QFileDialog::getExistingDirectory(
                this, tr("Select shadPS4 Home Path"), initial_path);

            auto file_path = Common::FS::PathFromQString(home_path_string);
            if (!file_path.empty()) {
                EmulatorSettings.SetHomeDir(file_path);
                ui->homeFolderPath->setText(home_path_string);
            }
        });

        connect(ui->folderButton, &QPushButton::clicked, this, [this]() {
            const auto dlc_folder_path = EmulatorSettings.GetAddonInstallDir();
            QString initial_path;
            Common::FS::PathToQString(initial_path, dlc_folder_path);

            QString dlc_folder_path_string =
                QFileDialog::getExistingDirectory(this, tr("Select the DLC folder"), initial_path);

            auto file_path = Common::FS::PathFromQString(dlc_folder_path_string);
            if (!file_path.empty()) {
                EmulatorSettings.SetAddonInstallDir(file_path);
                ui->currentDLCFolder->setText(dlc_folder_path_string);
            }
        });

        connect(ui->browse_sysmodules, &QPushButton::clicked, this, [this]() {
            const auto sysmodules_path = EmulatorSettings.GetSysModulesDir();
            QString initial_path;
            Common::FS::PathToQString(initial_path, sysmodules_path);

            QString sysmodules_path_string = QFileDialog::getExistingDirectory(
                this, tr("Select the system modules folder"), initial_path);

            auto file_path = Common::FS::PathFromQString(sysmodules_path_string);
            if (!file_path.empty()) {
                EmulatorSettings.SetSysModulesDir(file_path);
                ui->sysmodulesPath->setText(sysmodules_path_string);
            }
        });

        connect(ui->browse_fonts, &QPushButton::clicked, this, [this]() {
            QString initial_path;
            Common::FS::PathToQString(initial_path, EmulatorSettings.GetFontsDir());

            const QString fonts_path_string = QFileDialog::getExistingDirectory(
                this, tr("Select the fonts folder"), initial_path);

            const auto file_path = Common::FS::PathFromQString(fonts_path_string);
            if (!file_path.empty()) {
                EmulatorSettings.SetFontsDir(file_path);
                ui->fontsPath->setText(fonts_path_string);
            }
        });
    }

    // LOG TAB
    {
        connect(ui->logOpenLocationButton, &QPushButton::clicked, this, []() {
            QString userPath;
            Common::FS::PathToQString(userPath,
                                      Common::FS::GetUserPath(Common::FS::PathType::LogDir));
            QDesktopServices::openUrl(QUrl::fromLocalFile(userPath));
        });

        // Log presets popup button
        connect(ui->logPresetsButton, &QPushButton::clicked, this, [this]() {
            auto dlg = new LogPresetsDialog(m_gui_settings, this);
            connect(dlg, &LogPresetsDialog::PresetChosen, this,
                    [this](const QString& filter) { ui->logFilterLineEdit->setText(filter); });
            dlg->exec();
        });

        connect(ui->logSkipDuplicateCheckBox, &QPushButton::clicked, this, [this]() {
            ui->logMaxSkipDurationGroupBox->setVisible(
                !ui->logMaxSkipDurationGroupBox->isVisible());
        });
    }

    // DEBUG TAB
    {
        connect(ui->vkValidationCheckBox, &QCheckBox::checkStateChanged, this,
                [this](Qt::CheckState state) {
                    state ? ui->vkLayersGroupBox->setVisible(true)
                          : ui->vkLayersGroupBox->setVisible(false);
                });
    }

    // Experimental
    {
        connect(ui->shaderCacheCheckBox, &QCheckBox::checkStateChanged, this,
                [this](Qt::CheckState state) {
                    state ? ui->shaderCacheArchiveCheckBox->setVisible(true)
                          : ui->shaderCacheArchiveCheckBox->setVisible(false);
                });
    }

    // GRAPHICS TAB
    connect(ui->RCASSlider, &QSlider::valueChanged, this, [this](int value) {
        QString RCASValue = QString::number(value / 1000.0, 'f', 3);
        ui->RCASValue->setText(RCASValue);
    });

    if (EmulatorState::GetInstance()->IsGameRunning()) {
        connect(ui->RCASSlider, &QSlider::valueChanged, this, [this](int value) {
            if (is_game_specific == EmulatorState::GetInstance()->IsGameSpecifigConfigUsed()) {
                m_ipc_client->setRcasAttenuation(value);
            }
        });
        connect(ui->FSRCheckBox, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState state) {
            if (is_game_specific == EmulatorState::GetInstance()->IsGameSpecifigConfigUsed()) {
                m_ipc_client->setFsr(state);
            }
        });

        connect(
            ui->RCASCheckBox, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState state) {
                if (is_game_specific == EmulatorState::GetInstance()->IsGameSpecifigConfigUsed()) {
                    m_ipc_client->setRcas(state);
                }
            });
    }

    // Descriptions
    {
        // General
        ui->consoleLanguageGroupBox->installEventFilter(this);
        ui->emulatorLanguageGroupBox->installEventFilter(this);
        ui->showSplashCheckBox->installEventFilter(this);
        ui->discordRPCCheckbox->installEventFilter(this);
        ui->volumeSliderElement->installEventFilter(this);
#ifdef ENABLE_UPDATER
        ui->updaterGroupBox->installEventFilter(this);
#endif

        // GUI
        ui->GUIBackgroundImageGroupBox->installEventFilter(this);
        ui->GUIMusicGroupBox->installEventFilter(this);
        ui->enableCompatibilityCheckBox->installEventFilter(this);
        ui->checkCompatibilityOnStartupCheckBox->installEventFilter(this);
        ui->updateCompatibilityButton->installEventFilter(this);

        // User
        ui->disableTrophycheckBox->installEventFilter(this);
        ui->OpenCustomTrophyLocationButton->installEventFilter(this);
        ui->PortableUserFolderGroupBox->installEventFilter(this);

        // Input
        ui->hideCursorGroupBox->installEventFilter(this);
        ui->idleTimeoutGroupBox->installEventFilter(this);
        ui->backgroundControllerCheckBox->installEventFilter(this);
        ui->motionControlsCheckBox->installEventFilter(this);
        ui->micComboBox->installEventFilter(this);
        ui->usbComboBox->installEventFilter(this);

        // Graphics
        ui->graphicsAdapterGroupBox->installEventFilter(this);
        ui->windowSizeGroupBox->installEventFilter(this);
        frameGenCheckBox->installEventFilter(this);
        frameGenBackendComboBox->installEventFilter(this);
        lemonFgHighQualityCheckBox->installEventFilter(this);
        lemonFgHudProtectionCheckBox->installEventFilter(this);
        lemonFgMultiplierComboBox->installEventFilter(this);
        antiAliasingCheckBox->installEventFilter(this);
        resolutionScaleComboBox->installEventFilter(this);
        anisotropicComboBox->installEventFilter(this);
        brightnessSlider->installEventFilter(this);
        contrastSlider->installEventFilter(this);
        ui->presentModeGroupBox->installEventFilter(this);
        ui->heightDivider->installEventFilter(this);
        ui->enableHDRCheckBox->installEventFilter(this);
        ui->chooseHomeTabGroupBox->installEventFilter(this);

        // Paths
        ui->gameFoldersGroupBox->installEventFilter(this);
        ui->gameFoldersListWidget->installEventFilter(this);
        ui->addFolderButton->installEventFilter(this);
        ui->removeFolderButton->installEventFilter(this);
        ui->homeFolderGroupBox->installEventFilter(this);
        ui->sysmodulesGroupBox->installEventFilter(this);
        ui->browse_sysmodules->installEventFilter(this);
        ui->fontsGroupBox->installEventFilter(this);
        ui->fontsPath->installEventFilter(this);
        ui->browse_fonts->installEventFilter(this);
        ui->homeFolderPath->installEventFilter(this);
        ui->currentDLCFolder->installEventFilter(this);
        ui->homeFolderButton->installEventFilter(this);
        ui->folderButton->installEventFilter(this);

        // Log
        ui->logAppendCheckBox->installEventFilter(this);
        ui->logEnableCheckBox->installEventFilter(this);
        ui->logFilter->installEventFilter(this);
        ui->logMaxSkipDurationLineEdit->installEventFilter(this);
        ui->logOpenLocationButton->installEventFilter(this);
        ui->logSeparateCheckBox->installEventFilter(this);
        ui->logSizeLimitLineEdit->installEventFilter(this);
        ui->logSkipDuplicateCheckBox->installEventFilter(this);
        ui->logSyncCheckBox->installEventFilter(this);
        ui->logTypeGroupBox->installEventFilter(this);

        // Debug
        ui->debugDump->installEventFilter(this);
        ui->vkValidationCheckBox->installEventFilter(this);
        ui->vkSyncValidationCheckBox->installEventFilter(this);
        ui->vkCoreValidationCheckBox->installEventFilter(this);
        ui->vkGpuValidationCheckBox->installEventFilter(this);
        ui->rdocCheckBox->installEventFilter(this);
        ui->crashDiagnosticsCheckBox->installEventFilter(this);
        ui->guestMarkersCheckBox->installEventFilter(this);
        ui->hostMarkersCheckBox->installEventFilter(this);
        ui->collectShaderCheckBox->installEventFilter(this);
        ui->copyGPUBuffersCheckBox->installEventFilter(this);

        // Experimental
        ui->readbacksGroupBox->installEventFilter(this);
        ui->readbackLinearImagesCheckBox->installEventFilter(this);
        ui->dumpShadersCheckBox->installEventFilter(this);
        ui->dmaCheckBox->installEventFilter(this);
        ui->devkitCheckBox->installEventFilter(this);
        ui->neoCheckBox->installEventFilter(this);
        ui->networkConnectedCheckBox->installEventFilter(this);
        ui->shaderCacheCheckBox->installEventFilter(this);
        ui->shaderCacheArchiveCheckBox->installEventFilter(this);
        ui->shadnetCheckBox->installEventFilter(this);
        ui->dmemGroupBox->installEventFilter(this);
    }

    SDL_InitSubSystem(SDL_INIT_EVENTS);
    Polling = QtConcurrent::run(&SettingsDialog::PollSDLevents, this);
}

void SettingsDialog::closeEvent(QCloseEvent* event) {
    if (!is_game_saving) {
        ui->backgroundImageOpacitySlider->setValue(backgroundImageOpacitySlider_backup);
        emit BackgroundOpacityChanged(backgroundImageOpacitySlider_backup);
        ui->BGMVolumeSlider->setValue(bgm_volume_backup);
        BackgroundMusicPlayer::getInstance().setVolume(bgm_volume_backup);
        SyncRealTimeWidgetstoConfig();
    }

    SDL_Event quitLoop{};
    quitLoop.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quitLoop);
    Polling.waitForFinished();

    SDL_QuitSubSystem(SDL_INIT_EVENTS);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    SDL_Quit();

    EmulatorSettings.Load();
    QDialog::closeEvent(event);
}

void SettingsDialog::LoadValuesFromConfig() {
    std::filesystem::path gs_config_file =
        Common::FS::GetUserPath(Common::FS::PathType::CustomConfigs) / (gs_serial);

    std::error_code error;
    bool is_newly_created = false;
    if (is_game_specific) {
        if (!std::filesystem::exists(gs_config_file, error)) {
            EmulatorSettings.Save(gs_serial);
            is_newly_created = true;
        }
    }

    const QVector<int> languageIndexes = {21, 23, 14, 6, 18, 1, 12, 22, 2, 4,  25, 24, 29, 5,  0, 9,
                                          15, 16, 17, 7, 26, 8, 11, 20, 3, 13, 27, 10, 19, 30, 28};

    /////// Entries with no game-specific settings
    if (!is_game_specific) {

        // Frontend tab
        ui->emulatorLanguageComboBox->setCurrentIndex(
            languages[m_gui_settings->GetValue(gui::gen_guiLanguage).toString().toStdString()]);

        ui->playBGMCheckBox->setChecked(
            m_gui_settings->GetValue(gui::gl_playBackgroundMusic).toBool());

        ui->BGMVolumeSlider->setValue(
            m_gui_settings->GetValue(gui::gl_backgroundMusicVolume).toInt());
        ui->discordRPCCheckbox->setChecked(EmulatorSettings.IsDiscordRPCEnabled());

        ui->enableCompatibilityCheckBox->setChecked(
            m_gui_settings->GetValue(gui::glc_showCompatibility).toBool());
        ui->checkCompatibilityOnStartupCheckBox->setChecked(
            m_gui_settings->GetValue(gui::gen_checkCompatibilityAtStartup).toBool());

        ui->backgroundImageOpacitySlider->setValue(
            m_gui_settings->GetValue(gui::gl_backgroundImageOpacity).toInt());
        ui->showBackgroundImageCheckBox->setChecked(
            m_gui_settings->GetValue(gui::gl_showBackgroundImage).toBool());

        backgroundImageOpacitySlider_backup =
            m_gui_settings->GetValue(gui::gl_backgroundImageOpacity).toInt();
        bgm_volume_backup = m_gui_settings->GetValue(gui::gl_backgroundMusicVolume).toInt();

#ifdef ENABLE_UPDATER
        ui->updateCheckBox->setChecked(m_gui_settings->GetValue(gui::gen_checkForUpdates).toBool());
        ui->changelogCheckBox->setChecked(
            m_gui_settings->GetValue(gui::gen_showChangeLog).toBool());
#endif

        // Paths tab
        const auto home_path = EmulatorSettings.GetHomeDir();
        QString home_path_string;
        Common::FS::PathToQString(home_path_string, home_path);
        ui->homeFolderPath->setText(home_path_string);

        const auto dlc_folder_path = EmulatorSettings.GetAddonInstallDir();
        QString dlc_folder_path_string;
        Common::FS::PathToQString(dlc_folder_path_string, dlc_folder_path);
        ui->currentDLCFolder->setText(dlc_folder_path_string);

        const auto sysmodules_path = EmulatorSettings.GetSysModulesDir();
        QString sysmodules_path_string;
        Common::FS::PathToQString(sysmodules_path_string, sysmodules_path);
        ui->sysmodulesPath->setText(sysmodules_path_string);

        QString fonts_path_string;
        Common::FS::PathToQString(fonts_path_string, EmulatorSettings.GetFontsDir());
        ui->fontsPath->setText(fonts_path_string);

        ui->removeFolderButton->setEnabled(!ui->gameFoldersListWidget->selectedItems().isEmpty());

        SyncRealTimeWidgetstoConfig();
    }

    /////// Entries with game-specific settings

    // General tab
    ui->consoleLanguageComboBox->setCurrentIndex(
        std::distance(languageIndexes.begin(),
                      std::find(languageIndexes.begin(), languageIndexes.end(),
                                EmulatorSettings.GetConsoleLanguage())) %
        languageIndexes.size());
    ui->showSplashCheckBox->setChecked(EmulatorSettings.IsShowSplash());
    ui->redZoneCheckBox->setChecked(EmulatorSettings.IsRedZonePatchingEnabled());

    ui->horizontalVolumeSlider->setValue(EmulatorSettings.GetVolumeSlider());
    ui->volumeText->setText(QString::number(ui->horizontalVolumeSlider->sliderPosition()) + "%");

    ui->audioBackendComboBox->setCurrentIndex(EmulatorSettings.GetAudioBackend());
    const QString backend = ui->audioBackendComboBox->currentText();
    if (backend == "SDL") {
        ui->GenAudioComboBox->setCurrentText(
            QString::fromStdString(EmulatorSettings.GetSDLMainOutputDevice()));
        ui->DsAudioComboBox->setCurrentText(
            QString::fromStdString(EmulatorSettings.GetSDLPadSpkOutputDevice()));
        ui->micComboBox->setCurrentText(QString::fromStdString(EmulatorSettings.GetSDLMicDevice()));
    } else if (backend == "OpenAL") {
        ui->GenAudioComboBox->setCurrentText(
            QString::fromStdString(EmulatorSettings.GetOpenALMainOutputDevice()));
        ui->DsAudioComboBox->setCurrentText(
            QString::fromStdString(EmulatorSettings.GetOpenALPadSpkOutputDevice()));
        ui->micComboBox->setCurrentText(
            QString::fromStdString(EmulatorSettings.GetOpenALMicDevice()));
    }

    // Graphics tab

    // First options is auto selection -1, so gpuId on the GUI will always have to subtract 1
    // when setting and add 1 when getting to select the correct gpu in Qt
    ui->graphicsAdapterBox->setCurrentIndex(EmulatorSettings.GetGpuId() + 1);

    std::string fullScreenMode = EmulatorSettings.GetFullScreenMode();
    QString translatedText_FullscreenMode =
        screenModeMap.key(QString::fromStdString(fullScreenMode));
    ui->displayModeComboBox->setCurrentText(translatedText_FullscreenMode);

    std::string presentMode = EmulatorSettings.GetPresentMode();
    QString translatedText_PresentMode = presentModeMap.key(QString::fromStdString(presentMode));
    ui->presentModeComboBox->setCurrentText(translatedText_PresentMode);

    ui->widthSpinBox->setValue(EmulatorSettings.GetWindowWidth());
    ui->heightSpinBox->setValue(EmulatorSettings.GetWindowHeight());
    ui->enableHDRCheckBox->setChecked(EmulatorSettings.IsHdrAllowed());
    SyncResolutionComboBox();
    frameGenCheckBox->setChecked(EmulatorSettings.IsFrameGenerationEnabled());
    {
        const int backendIndex = frameGenBackendComboBox->findData(
            QString::fromStdString(EmulatorSettings.GetFrameGenerationBackend()));
        frameGenBackendComboBox->setCurrentIndex(backendIndex >= 0 ? backendIndex : 0);
    }
    lemonFgHighQualityCheckBox->setChecked(EmulatorSettings.IsLemonFgHighQuality());
    lemonFgHudProtectionCheckBox->setChecked(EmulatorSettings.IsLemonFgHudProtection());
    {
        const int multiplierIndex =
            lemonFgMultiplierComboBox->findData(EmulatorSettings.GetLemonFgMultiplier());
        lemonFgMultiplierComboBox->setCurrentIndex(multiplierIndex >= 0 ? multiplierIndex : 0);
    }
    LoadDlss5Settings();
    antiAliasingCheckBox->setChecked(EmulatorSettings.IsAntiAliasing());
    SyncImageQualityComboBoxes();
    brightnessSlider->setValue(EmulatorSettings.GetBrightness());
    contrastSlider->setValue(EmulatorSettings.GetContrast());

    ui->FSRCheckBox->setChecked(EmulatorSettings.IsFsrEnabled());
    ui->RCASCheckBox->setChecked(EmulatorSettings.IsRcasEnabled());
    ui->RCASSlider->setValue(EmulatorSettings.GetRcasAttenuation());
    ui->RCASValue->setText(QString::number(ui->RCASSlider->value() / 1000.0, 'f', 3));

    // User tab
    ui->disableTrophycheckBox->setChecked(EmulatorSettings.IsTrophyPopupDisabled());
    ui->popUpDurationSpinBox->setValue(EmulatorSettings.GetTrophyNotificationDuration());

    std::string sideTrophy = EmulatorSettings.GetTrophyNotificationSide();
    QString side = QString::fromStdString(sideTrophy);
    ui->radioButton_Left->setChecked(side == "left");
    ui->radioButton_Right->setChecked(side == "right");
    ui->radioButton_Top->setChecked(side == "top");
    ui->radioButton_Bottom->setChecked(side == "bottom");

    // Input tab
    ui->motionControlsCheckBox->setChecked(EmulatorSettings.IsMotionControlsEnabled());
    ui->backgroundControllerCheckBox->setChecked(EmulatorSettings.IsBackgroundControllerInput());
    ui->usbComboBox->setCurrentIndex(EmulatorSettings.GetUsbDeviceBackend());
    ui->cameraComboBox->setCurrentIndex(EmulatorSettings.GetCameraId() + 1);

    ui->hideCursorComboBox->setCurrentIndex(EmulatorSettings.GetCursorState());
    OnCursorStateChanged(EmulatorSettings.GetCursorState());
    ui->idleTimeoutSpinBox->setValue(EmulatorSettings.GetCursorHideTimeout());

    // Network tab
    ui->networkConnectedCheckBox->setChecked(EmulatorSettings.IsConnectedToNetwork());
    ui->shadnetCheckBox->setChecked(EmulatorSettings.IsShadNetEnabled());
    ui->disableHttpsCheckBox->setChecked(EmulatorSettings.IsForcedHttpsDisabled());
    ui->upnpCheckBox->setChecked(EmulatorSettings.IsUPnPEnabled());

    ui->serverLineEdit->setText(QString::fromStdString(EmulatorSettings.GetShadNetServer()));
    ui->servWebApiLineEdit->setText(
        QString::fromStdString(EmulatorSettings.GetShadNetWebApiServer()));
    ui->p2pPortSpinBox->setValue(EmulatorSettings.GetP2PPort());
    ui->signalingInfoLineEdit->setText(QString::fromStdString(EmulatorSettings.GetSignalingInfo()));

    // Log tab
    ui->logAppendCheckBox->setChecked(EmulatorSettings.IsLogAppend());
    ui->logEnableCheckBox->setChecked(EmulatorSettings.IsLogEnable());
    ui->logFilterLineEdit->setText(QString::fromStdString(EmulatorSettings.GetLogFilter()));
    SyncLogLevelsFromFilter();
    logFlushLevelBox->setCurrentIndex(std::max(
        0, logFlushLevelBox->findData(QString::fromStdString(EmulatorSettings.GetLogFlushLevel()))));
    ui->logMaxSkipDurationLineEdit->setValue(EmulatorSettings.GetLogMaxSkipDuration());
    ui->logSeparateCheckBox->setChecked(EmulatorSettings.IsLogSeparate());
    ui->logSizeLimitLineEdit->setValue(EmulatorSettings.GetLogSizeLimit());
    ui->logSkipDuplicateCheckBox->setChecked(EmulatorSettings.IsLogSkipDuplicate());
    ui->logMaxSkipDurationGroupBox->setVisible(ui->logSkipDuplicateCheckBox->isChecked());
    ui->logSyncCheckBox->setChecked(EmulatorSettings.IsLogSync());
#ifdef _WIN32
    std::string logType = EmulatorSettings.GetLogType();
    QString translatedText_logType = logTypeMap.key(QString::fromStdString(logType));
    if (!translatedText_logType.isEmpty()) {
        ui->logTypeComboBox->setCurrentText(translatedText_logType);
    }
#else
    ui->logTypeGroupBox->setVisible(false);
#endif

    // Debug tab
    ui->dumpShadersCheckBox->setChecked(EmulatorSettings.IsDumpShaders());
    ui->debugDump->setChecked(EmulatorSettings.IsDebugDump());
    ui->rdocCheckBox->setChecked(EmulatorSettings.IsRenderdocEnabled());
    ui->copyGPUBuffersCheckBox->setChecked(EmulatorSettings.IsCopyGpuBuffers());

    ui->vkValidationCheckBox->setChecked(EmulatorSettings.IsVkValidationEnabled());
    ui->vkSyncValidationCheckBox->setChecked(EmulatorSettings.IsVkValidationSyncEnabled());
    ui->vkCoreValidationCheckBox->setChecked(EmulatorSettings.IsVkValidationCoreEnabled());
    ui->vkGpuValidationCheckBox->setChecked(EmulatorSettings.IsVkValidationGpuEnabled());
    ui->vkValidationCheckBox->isChecked() ? ui->vkLayersGroupBox->setVisible(true)
                                          : ui->vkLayersGroupBox->setVisible(false);

    ui->collectShaderCheckBox->setChecked(EmulatorSettings.IsShaderCollect());
    ui->crashDiagnosticsCheckBox->setChecked(EmulatorSettings.IsVkCrashDiagnosticEnabled());
    ui->guestMarkersCheckBox->setChecked(EmulatorSettings.IsVkGuestMarkersEnabled());
    ui->hostMarkersCheckBox->setChecked(EmulatorSettings.IsVkHostMarkersEnabled());

    // Experimental tab
    ui->readbacksModeComboBox->setCurrentIndex(EmulatorSettings.GetReadbacksMode());
    ui->readbackLinearImagesCheckBox->setChecked(EmulatorSettings.IsReadbackLinearImagesEnabled());
    ui->dmaCheckBox->setChecked(EmulatorSettings.IsDirectMemoryAccessEnabled());
    ui->neoCheckBox->setChecked(EmulatorSettings.IsNeo());
    ui->devkitCheckBox->setChecked(EmulatorSettings.IsDevKit());

    ui->shaderCacheCheckBox->setChecked(EmulatorSettings.IsPipelineCacheEnabled());
    ui->shaderCacheArchiveCheckBox->setChecked(EmulatorSettings.IsPipelineCacheArchived());
    ui->vblankSpinBox->setValue(EmulatorSettings.GetVblankFrequency());
    ui->dmemSpinBox->setValue(EmulatorSettings.GetExtraDmemInMBytes());

    ui->shaderCacheCheckBox->isChecked() ? ui->shaderCacheArchiveCheckBox->setVisible(true)
                                         : ui->shaderCacheArchiveCheckBox->setVisible(false);

    // Frontend settings
    QString chooseHomeTab = m_gui_settings->GetValue(gui::gen_homeTab).toString();
    QString translatedText = chooseHomeTabMap.key(chooseHomeTab);
    if (translatedText.isEmpty()) {
        translatedText = tr("General");
    }
    ui->chooseHomeTabComboBox->setCurrentText(translatedText);

    QStringList tabNames = {tr("General"), tr("Frontend"),    tr("Graphics"), tr("User"),
                            tr("Input"),   tr("Network"),     tr("Paths"),    tr("Log"),
                            tr("Debug"),   tr("Experimental")};
    int indexTab = tabNames.indexOf(translatedText);
    if (indexTab == -1 || !ui->tabWidgetSettings->isTabVisible(indexTab) || is_newly_created)
        indexTab = 0;
    ui->tabWidgetSettings->setCurrentIndex(indexTab);
}

void SettingsDialog::InitializeEmulatorLanguages() {
    QDirIterator it(QStringLiteral(":/translations"), QDirIterator::NoIteratorFlags);

    QVector<QPair<QString, QString>> languagesList;

    while (it.hasNext()) {
        QString locale = it.next();
        locale.truncate(locale.lastIndexOf(QLatin1Char{'.'}));
        locale.remove(0, locale.lastIndexOf(QLatin1Char{'/'}) + 1);
        const QString lang = QLocale::languageToString(QLocale(locale).language());
        const QString country = QLocale::territoryToString(QLocale(locale).territory());

        QString displayName = QStringLiteral("%1 (%2)").arg(lang, country);
        languagesList.append(qMakePair(locale, displayName));
    }

    std::sort(languagesList.begin(), languagesList.end(),
              [](const QPair<QString, QString>& a, const QPair<QString, QString>& b) {
                  return a.second < b.second;
              });

    int idx = 0;
    for (const auto& pair : languagesList) {
        const QString& locale = pair.first;
        const QString& displayName = pair.second;

        ui->emulatorLanguageComboBox->addItem(displayName, locale);
        languages[locale.toStdString()] = idx;
        idx++;
    }

    connect(ui->emulatorLanguageComboBox, &QComboBox::currentIndexChanged, this,
            &SettingsDialog::OnLanguageChanged);
}

void SettingsDialog::OnLanguageChanged(int index) {
    if (index == -1)
        return;

    ui->retranslateUi(this);

    emit LanguageChanged(ui->emulatorLanguageComboBox->itemData(index).toString());
}

void SettingsDialog::OnCursorStateChanged(s16 index) {
    if (index == -1)
        return;
    if (index == HideCursorState::Idle) {
        ui->idleTimeoutGroupBox->show();
    } else {
        if (!ui->idleTimeoutGroupBox->isHidden()) {
            ui->idleTimeoutGroupBox->hide();
        }
    }
}

void SettingsDialog::VolumeSliderChange(int value) {
    ui->volumeText->setText(QString::number(ui->horizontalVolumeSlider->sliderPosition()) + "%");
}

int SettingsDialog::exec() {
    return QDialog::exec();
}

SettingsDialog::~SettingsDialog() {
    EmulatorSettings.Load();
}

void SettingsDialog::updateNoteTextEdit(const QString& elementName) {
    QString text;

    // clang-format off
    // General
    if (elementName == "consoleLanguageGroupBox") {
        text = tr("Console Language:\\nLanguage the game runs in. Pick one the game's region supports.");
    } else if (elementName == "emulatorLanguageGroupBox") {
        text = tr("Launcher Language:\\nLanguage of this interface.");
    } else if (elementName == "showSplashCheckBox") {
        text = tr("Splash Screen:\\nShow the game's splash image while it starts.");
    } else if (elementName == "discordRPCCheckbox") {
        text = tr("Discord Rich Presence:\\nShow the game you are playing on your Discord profile.");
    #ifdef ENABLE_UPDATER
    } else if (elementName == "updaterGroupBox") {
        text = tr("Launcher Updates:\\nRelease: monthly, tested.\\nNightly: newest features, less stable.\\nThe emulator itself is updated from Version Manager.");
#endif
    } else if (elementName == "GUIBackgroundImageGroupBox") {
        text = tr("Background Image:\\nOpacity of the selected game's background art.");
    } else if (elementName == "GUIMusicGroupBox") {
        text = tr("Title Music:\\nPlay the game's theme when it is selected.");
    } else if (elementName == "enableHDRCheckBox") {
        text = tr("HDR:\\nHDR output for games that support it. Needs an HDR10 (BT.2020 PQ) display.");
    } else if (elementName == "disableTrophycheckBox") {
        text = tr("Hide Trophy Pop-ups:\\nTrophies still unlock; view them in Trophy Viewer.");
    } else if (elementName == "enableCompatibilityCheckBox") {
        text = tr("Compatibility:\\nShow each game's compatibility status in the list.");
    } else if (elementName == "checkCompatibilityOnStartupCheckBox") {
        text = tr("Update on Startup:\\nRefresh the compatibility database when the launcher opens.");
    } else if (elementName == "updateCompatibilityButton") {
        text = tr("Update Now:\\nRefresh the compatibility database.");
    }

    // Log
    if (elementName == "logAppendCheckBox") {
        text = tr("Append:\\nAdd to the existing log instead of replacing it.");
    } else if (elementName == "logEnableCheckBox") {
        text = tr("Logging:\\nWrite a log file. Keep it on when reporting a problem.");
    } else if (elementName == "logFilter") {
        text = tr("Log Filter:\\nClass:level pairs, e.g. \"*:info Render.Vulkan:debug\".\\nLevels: trace, debug, info, warning, error, critical, off. Use the table or presets to build it.");
    } else if (elementName == "logMaxSkipDurationLineEdit") {
        text = tr("Skip Window:\\nHow long (ms) repeated lines are dropped when Skip Duplicates is on."); //TODO grey out if disabled
    } else if (elementName == "logOpenLocationButton") {
        text = tr("Open Log Folder:\\nOpen the folder that holds the log files.");
    } else if (elementName == "logSeparateCheckBox") {
        text = tr("One Log per Game:\\nWrite each game to its own log file.");
    } else if (elementName == "logSizeLimitLineEdit") {
        text = tr("Size Limit:\\nLargest a log file may grow, in bytes.");
    } else if (elementName == "logSkipDuplicateCheckBox") {
        text = tr("Skip Duplicates:\\nDrop lines identical to the previous one.");
    } else if (elementName == "logSyncCheckBox") {
        text = tr("Synchronous:\\nWrite lines in strict order. Off is faster.");
    } else if (elementName == "logTypeGroupBox") {
        text = tr("Log Type:\\nwincolor: normal console output.\\nmsvc: send output to the debugger.");
    }

    //User
    if (elementName == "OpenCustomTrophyLocationButton") {
        text = tr("Custom Trophies:\\nPut trophy.wav or trophy.mp3 and bronze/silver/gold/platinum.png in this folder to replace the pop-up art and sound.");
    }

    // Input
    if (elementName == "hideCursorGroupBox") {
        text = tr("Hide Cursor:\\nNever, after being idle, or always.");
    } else if (elementName == "idleTimeoutGroupBox") {
        text = tr("Idle Timeout:\\nSeconds before an idle cursor hides.");
    } else if (elementName == "backgroundControllerCheckBox") {
        text = tr("Background Input:\\nRead the controller while the game window is not focused.");
    }

    // Graphics
    if (elementName == "graphicsAdapterGroupBox") {
        text = tr("Graphics Device:\\nGPU used by the emulator. Auto picks the best one.");
    } else if (elementName == "presentModeGroupBox") {
        text = tr("Present Mode:\\nConfigures how video output will be presented to your screen.\\n\\n"
                  "Mailbox: Frames synchronize with your screen's refresh rate. New frames will replace any pending frames. Reduces latency but may skip frames if running behind.\\n"
                  "Fifo: Frames synchronize with your screen's refresh rate. New frames will be queued behind pending frames. Ensures all frames are presented but may increase latency.\\n"
                  "Immediate: Frames immediately present to your screen when ready. May result in tearing.");
    } else if (elementName == "windowSizeGroupBox") {
        text = tr("Window Size:\\nWindow size at launch. Not the game's render resolution.");
    } else if (elementName == "antiAliasingCheckBox") {
        text = tr("FXAA:\\nSmooths jagged edges at almost no cost.");
    } else if (elementName == "resolutionScaleComboBox") {
        text = tr("Resolution Scaling:\\nScales the resolution reported to the game. Only affects games that render at that size. Applies on next launch.");
    } else if (elementName == "brightnessSlider") {
        text = tr("Brightness:\\n100% is unchanged. Lifts or lowers mid-tones; blacks and whites stay. Also in the game window's title bar menu.");
    } else if (elementName == "contrastSlider") {
        text = tr("Contrast:\\n100% is unchanged. Higher is punchier, lower is flatter. Also in the game window's title bar menu.");
    } else if (elementName == "anisotropicComboBox") {
        text = tr("Anisotropic Filtering:\\nSharper textures on angled surfaces. Off keeps the game's setting.");
    } else if (elementName == "frameGenBackendComboBox") {
        text = tr("Frame Generation Method:\\nFSR 3.1: AMD frame generation.\\nMulti FG: works on any GPU, supports 2x-6x. Fast small objects may smear.");
    } else if (elementName == "lemonFgHighQualityCheckBox") {
        text = tr("High Quality:\\nFiner motion search, less smearing, more GPU time.");
    } else if (elementName == "lemonFgHudProtectionCheckBox") {
        text = tr("HUD Protection:\\nKeeps static HUD in place. Turn off if parts of the scene look frozen.");
    } else if (elementName == "lemonFgMultiplierComboBox") {
        text = tr("Multiplier:\\nFrames shown per game frame. Match it to your refresh rate; higher costs more GPU and VRAM.");
    } else if (elementName == "frameGenCheckBox") {
        text = tr("Frame Generation:\\nInserts generated frames for smoother motion. Best above 30 FPS. Toggle key is set in Hotkeys (default 1).");
    } else if (elementName == "heightDivider") {
        text = tr("Vblank Frequency:\\nEmulated display refresh. Keep 60 Hz; other values can speed up or break games.");
    } else if (elementName == "dumpShadersCheckBox") {
        text = tr("Dump Shaders:\\nSave the game's shaders to disk.");
    }

    // Path
    if (elementName == "gameFoldersGroupBox" || elementName == "gameFoldersListWidget") {
        text = tr("Game Folders:\\nFolders scanned for installed games.");
    } else if (elementName == "addFolderButton") {
        text = tr("Add:\\nAdd a game folder.");
    } else if (elementName == "removeFolderButton") {
        text = tr("Remove:\\nRemove the selected folder.");
    } else if (elementName == "PortableUserFolderGroupBox") {
        text = tr("Portable User Folder:\\nKeep settings and data next to this build. Restart after creating it.");
    } else if (elementName == "sysmodulesGroupBox") {
        text = tr("Sysmodules:\\nFolder PS4 system modules are loaded from.");
    } else if (elementName == "browse_sysmodules") {
        text = tr("Browse:\\nChoose the sysmodules folder.");
    } else if (elementName == "fontsGroupBox" || elementName == "fontsPath") {
        text = tr("Fonts:\\nFolder PS4 system fonts are loaded from.");
    } else if (elementName == "browse_fonts") {
        text = tr("Browse:\\nChoose the fonts folder.");
    }

    // DLC Folder
    if (elementName == "dlcFolderGroupBox" || elementName == "currentDLCFolder") {
        text = tr("DLC:\\nFolder game DLC is loaded from.");
    } else if (elementName == "folderButton") {
        text = tr("Browse:\\nChoose the DLC folder.");
    }

    // Save Data
    if (elementName == "homeFolderGroupBox" || elementName == "homeFolderPath") {
        text = tr("Home Folder:\\nSaves, trophies and input configs for all users.");
    } else if (elementName == "homeFolderButton") {
        text = tr("Browse:\\nChoose the home folder.");
    }

    // Debug
    if (elementName == "debugDump") {
        text = tr("Dump Executable Info:\\nSave the running program's symbols and headers.");
    } else if (elementName == "vkValidationCheckBox") {
        text = tr("Vulkan Validation:\\nCheck renderer calls and log problems. Slow; needs the Vulkan SDK.");
    } else if (elementName == "vkSyncValidationCheckBox") {
        text = tr("Sync Validation:\\nCheck GPU synchronization. Slow; needs the Vulkan SDK.");
    } else if (elementName == "vkCoreValidationCheckBox") {
        text = tr("Core Validation:\\nCheck API usage. Slow; needs the Vulkan SDK.");
    } else if (elementName == "vkGpuValidationCheckBox") {
        text = tr("GPU-Assisted Validation:\\nCheck shaders while they run. Very slow; needs the Vulkan SDK.");
    } else if (elementName == "rdocCheckBox") {
        text = tr("RenderDoc:\\nAllow RenderDoc to capture frames.");
    } else if (elementName == "crashDiagnosticsCheckBox") {
        text = tr("Crash Diagnostics:\\nWrite a .yaml report on 'Device lost'. Use with both debug markers and Vulkan validation.");
    } else if (elementName == "guestMarkersCheckBox") {
        text = tr("Guest Markers:\\nPass the game's own debug markers to Vulkan.");
    } else if (elementName == "hostMarkersCheckBox") {
        text = tr("Host Markers:\\nAdd emulator markers and resource names for debuggers.");
    } else if (elementName == "copyGPUBuffersCheckBox") {
        text = tr("Copy GPU Buffers:\\nCopy command buffers before submit. May help with PM4 type 0 crashes.");
    } else if (elementName == "collectShaderCheckBox") {
        text = tr("Collect Shaders:\\nNeeded to edit shaders in the debug menu (Ctrl+F10).");
    } else if (elementName == "micComboBox") {
        text = tr("Microphone:\\nNone, the system default, or a specific device.");
    } else if (elementName == "volumeSliderElement") {
        text = tr("Volume:\\nGlobal game volume, 0-500%. Default 100%.");
    } else if (elementName == "chooseHomeTabGroupBox") {
        text = tr("Default Tab:\\nTab shown when settings open.");
    } else if (elementName == "motionControlsCheckBox") {
        text = tr("Motion Controls:\\nUse the controller's gyro when available.");
    } else if (elementName == "usbComboBox") {
        text = tr("USB Device:\\nA real USB device, or an emulated Skylanders Portal, Infinity Base or Dimensions Toypad.");
    }

    // Experimental
    if (elementName == "dmaCheckBox") {
        text = tr("Direct Memory Access:\\nLet the GPU read any CPU memory. Some games need it.");
    } else if (elementName == "neoCheckBox") {
        text = tr("PS4 Pro Mode:\\nEmulate a PS4 Pro. Unstable in many games.");
    } else if (elementName == "devkitCheckBox") {
        text = tr("Devkit Mode:\\nEmulate devkit memory size.");
    } else if (elementName == "networkConnectedCheckBox") {
        text = tr("Report Network Connected:\\nGames see a network connection. Online play is not supported.");
    } else if (elementName == "shaderCaheCheckBox") {
        text = tr("Shader Cache:\\nKeep compiled shaders on disk to reduce stutter.");
    } else if (elementName == "shaderCacheArchiveCheckBox") {
        text = tr("Compress Cache:\\nStore the shader cache in one zip file.");
    } else if (elementName == "shadnetCheckBox") {
        text = tr("shadNet:\\nExperimental online service. Register at https://www.shadps4.net/shadnet/register/.");
    } else if (elementName == "readbacksGroupBox") {
        text = tr("Readbacks:\\nCopy GPU writes back to memory. Some games need it; can cost performance.");
    } else if (elementName == "readbackLinearImagesCheckBox") {
        text = tr("Linear Image Readback:\\nDownload GPU-written linear images. Fixes some games.");
    } else if (elementName == "dmemGroupBox") {
        text = tr("Extra DMem:\\nAllocate additional direct memory. Can break some games.");
    }

    // clang-format on
    ui->descriptionText->setText(text.replace("\\n", "\n"));
}

bool SettingsDialog::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::Enter || event->type() == QEvent::Leave) {
        if (qobject_cast<QWidget*>(obj)) {
            bool hovered = (event->type() == QEvent::Enter);
            QString elementName = obj->objectName();
            if (hovered) {
                updateNoteTextEdit(elementName);
            } else {
                ui->descriptionText->setText(defaultTextEdit);
            }
            return true;
        }
    }
    return QDialog::eventFilter(obj, event);
}

void SettingsDialog::UpdateSettings(bool is_specific) {
    ////// Entries with game-specific settings

    // General tab
    EmulatorSettings.SetConsoleLanguage(
        languageIndexes[ui->consoleLanguageComboBox->currentIndex()], is_specific);
    EmulatorSettings.SetRedZonePatchingEnabled(ui->redZoneCheckBox->isChecked(), is_specific);
    EmulatorSettings.SetShowSplash(ui->showSplashCheckBox->isChecked(), is_specific);
    EmulatorSettings.SetVolumeSlider(ui->horizontalVolumeSlider->value(), is_specific);

    const std::string backend = ui->audioBackendComboBox->currentText().toStdString();
    EmulatorSettings.SetAudioBackend(ui->audioBackendComboBox->currentIndex(), is_specific);
    if (backend == "SDL") {
        EmulatorSettings.SetSDLMainOutputDevice(
            ui->GenAudioComboBox->currentData().toString().toStdString(), is_specific);
        EmulatorSettings.SetSDLPadSpkOutputDevice(
            ui->DsAudioComboBox->currentData().toString().toStdString(), is_specific);
        EmulatorSettings.SetSDLMicDevice(ui->micComboBox->currentData().toString().toStdString(),
                                         is_specific);
    } else if (backend == "OpenAL") {
        EmulatorSettings.SetOpenALMainOutputDevice(
            ui->GenAudioComboBox->currentData().toString().toStdString(), is_specific);
        EmulatorSettings.SetOpenALPadSpkOutputDevice(
            ui->DsAudioComboBox->currentData().toString().toStdString(), is_specific);
        EmulatorSettings.SetOpenALMicDevice(ui->micComboBox->currentData().toString().toStdString(),
                                            is_specific);
    }

    // Graphics tab
    EmulatorSettings.SetGpuId(ui->graphicsAdapterBox->currentIndex() - 1, is_specific);
    EmulatorSettings.SetNullGPU(false, is_specific);

    EmulatorSettings.SetFullScreen(
        screenModeMap.value(ui->displayModeComboBox->currentText()) != "Windowed", is_specific);
    EmulatorSettings.SetFullScreenMode(
        screenModeMap.value(ui->displayModeComboBox->currentText()).toStdString(), is_specific);
    EmulatorSettings.SetPresentMode(
        presentModeMap.value(ui->presentModeComboBox->currentText()).toStdString(), is_specific);

    EmulatorSettings.SetWindowWidth(ui->widthSpinBox->value(), is_specific);
    EmulatorSettings.SetWindowHeight(ui->heightSpinBox->value(), is_specific);
    EmulatorSettings.SetHdrAllowed(ui->enableHDRCheckBox->isChecked(), is_specific);
    EmulatorSettings.SetFsrEnabled(ui->FSRCheckBox->isChecked(), is_specific);
    EmulatorSettings.SetFrameGenerationEnabled(frameGenCheckBox->isChecked(), is_specific);
    EmulatorSettings.SetFrameGenerationBackend(
        frameGenBackendComboBox->currentData().toString().toStdString(), is_specific);
    EmulatorSettings.SetLemonFgHighQuality(lemonFgHighQualityCheckBox->isChecked(), is_specific);
    EmulatorSettings.SetLemonFgHudProtection(lemonFgHudProtectionCheckBox->isChecked(),
                                             is_specific);
    EmulatorSettings.SetLemonFgMultiplier(lemonFgMultiplierComboBox->currentData().toUInt(),
                                          is_specific);
    SaveDlss5Settings();
    EmulatorSettings.SetAntiAliasing(antiAliasingCheckBox->isChecked(), is_specific);
    EmulatorSettings.SetResolutionScale(resolutionScaleComboBox->currentData().toUInt(),
                                        is_specific);
    EmulatorSettings.SetAnisotropicFiltering(anisotropicComboBox->currentData().toUInt(),
                                             is_specific);
    EmulatorSettings.SetBrightness(brightnessSlider->value(), is_specific);
    EmulatorSettings.SetContrast(contrastSlider->value(), is_specific);
    EmulatorSettings.SetRcasEnabled(ui->RCASCheckBox->isChecked(), is_specific);
    EmulatorSettings.SetRcasAttenuation(ui->RCASSlider->value(), is_specific);

    // User tab
    EmulatorSettings.SetTrophyPopupDisabled(ui->disableTrophycheckBox->isChecked(), is_specific);
    EmulatorSettings.SetTrophyNotificationDuration(ui->popUpDurationSpinBox->value(), is_specific);

    if (ui->radioButton_Top->isChecked()) {
        EmulatorSettings.SetTrophyNotificationSide("top", is_specific);
    } else if (ui->radioButton_Left->isChecked()) {
        EmulatorSettings.SetTrophyNotificationSide("left", is_specific);
    } else if (ui->radioButton_Right->isChecked()) {
        EmulatorSettings.SetTrophyNotificationSide("right", is_specific);
    } else if (ui->radioButton_Bottom->isChecked()) {
        EmulatorSettings.SetTrophyNotificationSide("bottom", is_specific);
    }

    // Input tab
    EmulatorSettings.SetMotionControlsEnabled(ui->motionControlsCheckBox->isChecked(), is_specific);
    EmulatorSettings.SetBackgroundControllerInput(ui->backgroundControllerCheckBox->isChecked(),
                                                  is_specific);

    EmulatorSettings.SetCursorState(ui->hideCursorComboBox->currentIndex(), is_specific);
    EmulatorSettings.SetCursorHideTimeout(ui->idleTimeoutSpinBox->value(), is_specific);
    EmulatorSettings.SetUsbDeviceBackend(ui->usbComboBox->currentIndex(), is_specific);
    EmulatorSettings.SetCameraId(ui->cameraComboBox->currentIndex() - 1, is_specific);

    // Network tab
    EmulatorSettings.SetConnectedToNetwork(ui->networkConnectedCheckBox->isChecked(), is_specific);
    EmulatorSettings.SetShadNetEnabled(ui->shadnetCheckBox->isChecked(), is_specific);
    EmulatorSettings.SetUPnPEnabled(ui->upnpCheckBox->isChecked(), is_specific);
    EmulatorSettings.SetForcedHttpsDisabled(ui->disableHttpsCheckBox->isChecked(), is_specific);

    EmulatorSettings.SetShadNetServer(ui->serverLineEdit->text().toStdString(), is_specific);
    EmulatorSettings.SetSignalingInfo(ui->signalingInfoLineEdit->text().toStdString(), is_specific);
    EmulatorSettings.SetP2PPort(ui->p2pPortSpinBox->value(), is_specific);
    EmulatorSettings.SetShadNetWebApiServer(ui->servWebApiLineEdit->text().toStdString(),
                                            is_specific);

    // Log tab
    EmulatorSettings.SetLogAppend(ui->logAppendCheckBox->isChecked(), is_specific);
    EmulatorSettings.SetLogEnable(ui->logEnableCheckBox->isChecked(), is_specific);
    EmulatorSettings.SetLogFilter(ui->logFilterLineEdit->text().toStdString(), is_specific);
    EmulatorSettings.SetLogFlushLevel(logFlushLevelBox->currentData().toString().toStdString(),
                                      is_specific);
    EmulatorSettings.SetLogMaxSkipDuration(ui->logMaxSkipDurationLineEdit->value(), is_specific);
    EmulatorSettings.SetLogSeparate(ui->logSeparateCheckBox->isChecked(), is_specific);
    EmulatorSettings.SetLogSizeLimit(ui->logSizeLimitLineEdit->value(), is_specific);
    EmulatorSettings.SetLogSkipDuplicate(ui->logSkipDuplicateCheckBox->isChecked(), is_specific);
    EmulatorSettings.SetLogSync(ui->logSyncCheckBox->isChecked(), is_specific);
#ifdef _WIN32
    EmulatorSettings.SetLogType(logTypeMap.value(ui->logTypeComboBox->currentText()).toStdString(),
                                is_specific);
#endif

    // Debug tab
    EmulatorSettings.SetDumpShaders(ui->dumpShadersCheckBox->isChecked(), is_specific);
    EmulatorSettings.SetDebugDump(ui->debugDump->isChecked(), is_specific);
    EmulatorSettings.SetRenderdocEnabled(ui->rdocCheckBox->isChecked(), is_specific);
    EmulatorSettings.SetCopyGpuBuffers(ui->copyGPUBuffersCheckBox->isChecked(), is_specific);

    EmulatorSettings.SetVkValidationEnabled(ui->vkValidationCheckBox->isChecked(), is_specific);
    EmulatorSettings.SetVkValidationSyncEnabled(ui->vkSyncValidationCheckBox->isChecked(),
                                                is_specific);
    EmulatorSettings.SetVkValidationCoreEnabled(ui->vkCoreValidationCheckBox->isChecked(),
                                                is_specific);
    EmulatorSettings.SetVkValidationGpuEnabled(ui->vkGpuValidationCheckBox->isChecked(),
                                               is_specific);

    EmulatorSettings.SetShaderCollect(ui->collectShaderCheckBox->isChecked(), is_specific);
    EmulatorSettings.SetVkCrashDiagnosticEnabled(ui->crashDiagnosticsCheckBox->isChecked(),
                                                 is_specific);
    EmulatorSettings.SetVkHostMarkersEnabled(ui->hostMarkersCheckBox->isChecked(), is_specific);
    EmulatorSettings.SetVkGuestMarkersEnabled(ui->guestMarkersCheckBox->isChecked(), is_specific);

    // Experimental tab
    EmulatorSettings.SetReadbacksMode(ui->readbacksModeComboBox->currentIndex(), is_specific);
    EmulatorSettings.SetReadbackLinearImagesEnabled(ui->readbackLinearImagesCheckBox->isChecked(),
                                                    is_specific);
    EmulatorSettings.SetDirectMemoryAccessEnabled(ui->dmaCheckBox->isChecked(), is_specific);
    EmulatorSettings.SetDevKit(ui->devkitCheckBox->isChecked(), is_specific);
    EmulatorSettings.SetNeo(ui->neoCheckBox->isChecked(), is_specific);
    EmulatorSettings.SetPipelineCacheEnabled(ui->shaderCacheCheckBox->isChecked(), is_specific);
    EmulatorSettings.SetPipelineCacheArchived(ui->shaderCacheArchiveCheckBox->isChecked(),
                                              is_specific);

    EmulatorSettings.SetVblankFrequency(ui->vblankSpinBox->value(), is_specific);
    EmulatorSettings.SetExtraDmemInMBytes(ui->dmemSpinBox->value(), is_specific);

    ////// Entries with no game-specific settings
    if (!is_game_specific) {
        // Frontend tab
        EmulatorSettings.SetDiscordRPCEnabled(ui->discordRPCCheckbox->isChecked());
        m_gui_settings->SetValue(gui::glc_showCompatibility,
                                 ui->enableCompatibilityCheckBox->isChecked());
        m_gui_settings->SetValue(gui::gen_checkCompatibilityAtStartup,
                                 ui->checkCompatibilityOnStartupCheckBox->isChecked());
        m_gui_settings->SetValue(gui::gl_playBackgroundMusic, ui->playBGMCheckBox->isChecked());
        m_gui_settings->SetValue(gui::gl_backgroundMusicVolume, ui->BGMVolumeSlider->value());
        m_gui_settings->SetValue(gui::gen_checkForUpdates, ui->updateCheckBox->isChecked());
        m_gui_settings->SetValue(gui::gen_showChangeLog, ui->changelogCheckBox->isChecked());
        m_gui_settings->SetValue(gui::gl_showBackgroundImage,
                                 ui->showBackgroundImageCheckBox->isChecked());
        m_gui_settings->SetValue(gui::gl_backgroundImageOpacity,
                                 std::clamp(ui->backgroundImageOpacitySlider->value(), 0, 100));
        emit BackgroundOpacityChanged(ui->backgroundImageOpacitySlider->value());
        m_gui_settings->SetValue(gui::gen_homeTab,
                                 chooseHomeTabMap.value(ui->chooseHomeTabComboBox->currentText()));

        BackgroundMusicPlayer::getInstance().setVolume(ui->BGMVolumeSlider->value());

        // Paths tab
        std::vector<GameInstallDir> dirs_with_states;
        for (int i = 0; i < ui->gameFoldersListWidget->count(); i++) {
            QListWidgetItem* item = ui->gameFoldersListWidget->item(i);
            QString path_string = item->text();
            auto path = Common::FS::PathFromQString(path_string);
            bool enabled = (item->checkState() == Qt::Checked);

            dirs_with_states.push_back({path, enabled});
        }
        EmulatorSettings.SetAllGameInstallDirs(dirs_with_states);
    }
}

void SettingsDialog::SyncRealTimeWidgetstoConfig() {
    std::filesystem::path userdir = Common::FS::GetUserPath(Common::FS::PathType::UserDir);

    if (!is_game_specific) {
        ui->gameFoldersListWidget->clear();
        std::vector<GameInstallDir> install_dir_array = EmulatorSettings.GetAllGameInstallDirs();
        std::vector<bool> install_dirs_enabled = EmulatorSettings.GetGameInstallDirsEnabled();

        if (install_dirs_enabled.size() < install_dir_array.size()) {
            install_dirs_enabled.resize(install_dir_array.size(), true);
        }

        std::vector<GameInstallDir> settings_install_dirs_config;
        for (size_t i = 0; i < install_dir_array.size(); i++) {
            GameInstallDir dir = install_dir_array[i];
            bool enabled = install_dirs_enabled[i];

            settings_install_dirs_config.push_back(dir);

            QString path_string;
            Common::FS::PathToQString(path_string, dir.path);

            QListWidgetItem* item = new QListWidgetItem(path_string);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
            ui->gameFoldersListWidget->addItem(item);
        }

        EmulatorSettings.SetAllGameInstallDirs(settings_install_dirs_config);
    }

    ui->horizontalVolumeSlider->setValue(EmulatorSettings.GetVolumeSlider());

    if (EmulatorState::GetInstance()->IsGameRunning()) {
        m_ipc_client->setFsr(EmulatorSettings.IsFsrEnabled());
        m_ipc_client->setRcas(EmulatorSettings.IsRcasEnabled());
        m_ipc_client->setRcasAttenuation(EmulatorSettings.GetRcasAttenuation());
    }
}

void SettingsDialog::SetDefaultValues() {
    if (!is_game_specific) {
        m_gui_settings->SetValue(gui::gl_showBackgroundImage, true);
        m_gui_settings->SetValue(gui::gl_backgroundImageOpacity, 50);
        m_gui_settings->SetValue(gui::gl_playBackgroundMusic, false);
        m_gui_settings->SetValue(gui::gl_backgroundMusicVolume, 50);
        m_gui_settings->SetValue(gui::gen_checkForUpdates, gui::gen_checkForUpdates.def);
        m_gui_settings->SetValue(gui::gen_showChangeLog, false);
        m_gui_settings->SetValue(gui::gen_guiLanguage, "en_US");
        m_gui_settings->SetValue(gui::glc_showLoadGameSizeEnabled, true);
        m_gui_settings->SetValue(gui::glc_showCompatibility, false);
        m_gui_settings->SetValue(gui::gen_checkCompatibilityAtStartup, false);
        m_gui_settings->SetValue(gui::gen_homeTab, "General");
    }
}

void SettingsDialog::SaveSettings() {
    if (is_game_specific) {
        EmulatorSettings.Save(gs_serial);
    } else {
        EmulatorSettings.Save();
    }
};

void SettingsDialog::PollSDLevents() {
    SDL_Event event;
    while (SdlEventWrapper::Wrapper::wrapperActive) {

        if (!SDL_WaitEvent(&event)) {
            return;
        }

        if (event.type == SDL_EVENT_QUIT) {
            return;
        }

        if (event.type == SDL_EVENT_AUDIO_DEVICE_ADDED ||
            event.type == SDL_EVENT_AUDIO_DEVICE_REMOVED) {
            RefreshAudioDevices();
        }
    }
}

void SettingsDialog::RefreshAudioDevices() {
    // prevent device list from refreshing too fast
    QThread::msleep(100);

    ui->GenAudioComboBox->clear();
    ui->DsAudioComboBox->clear();
    ui->micComboBox->clear();

    const QString backend = ui->audioBackendComboBox->currentText();
    int deviceCount;
    QStringList playbackDeviceList;
    QStringList recordingDeviceList;

    ui->micComboBox->addItem(micMap.key("None"), "None");
    ui->micComboBox->addItem(micMap.key("Default Device"), "Default Device");
    ui->GenAudioComboBox->addItem(tr("Default Device"), "Default Device");
    ui->DsAudioComboBox->addItem(tr("Default Device"), "Default Device");

    if (backend == "SDL") {
        // Playback
        SDL_AudioDeviceID* playbackDevices = SDL_GetAudioPlaybackDevices(&deviceCount);

        if (!playbackDevices) {
            LOG_ERROR(Core_Devices, "Unable to retrieve audio device list {}", SDL_GetError());
            return;
        }

        for (int i = 0; i < deviceCount; ++i) {
            const char* name = SDL_GetAudioDeviceName(playbackDevices[i]);
            std::string name_string = std::string(name);
            playbackDeviceList.append(QString::fromStdString(name_string));
        }

        SDL_free(playbackDevices);

        // Mics
        int count = 0;
        SDL_AudioDeviceID* recordingDevices = SDL_GetAudioRecordingDevices(&count);
        if (recordingDevices) {
            for (int i = 0; i < count; ++i) {
                SDL_AudioDeviceID devId = recordingDevices[i];
                const char* name = SDL_GetAudioDeviceName(devId);
                if (name) {
                    QString qname = QString::fromUtf8(name);
                    ui->micComboBox->addItem(qname, QString::number(devId));
                }
            }
            SDL_free(recordingDevices);
        } else {
            qDebug() << "Error SDL_GetAudioRecordingDevices:" << SDL_GetError();
        }
    } else if (backend == "OpenAL") {
        // Playback
        const ALCchar* playbackdevices = alcGetString(NULL, ALC_ALL_DEVICES_SPECIFIER);
        if (playbackdevices) {
            while (*playbackdevices != '\0') {
                playbackDeviceList.append(QString::fromUtf8(playbackdevices));
                playbackdevices += strlen(playbackdevices) + 1;
            }
        }

        // Mics
        if (alcIsExtensionPresent(NULL, "ALC_ENUMERATION_EXT") == AL_TRUE) {
            const ALCchar* recordingDevices = alcGetString(NULL, ALC_CAPTURE_DEVICE_SPECIFIER);

            if (recordingDevices) {
                while (*recordingDevices != '\0') {
                    recordingDeviceList.append(QString::fromUtf8(recordingDevices));
                    recordingDevices += strlen(recordingDevices) + 1;
                }
            }
        }
    }

    for (const QString& name : playbackDeviceList) {
        ui->DsAudioComboBox->addItem(name, name);
        ui->GenAudioComboBox->addItem(name, name);
    }
    for (const QString& name : recordingDeviceList) {
        ui->micComboBox->addItem(name, name);
    }

    if (backend == "SDL") {
        ui->GenAudioComboBox->setCurrentText(
            QString::fromStdString(EmulatorSettings.GetSDLMainOutputDevice()));
        ui->DsAudioComboBox->setCurrentText(
            QString::fromStdString(EmulatorSettings.GetSDLPadSpkOutputDevice()));
        ui->micComboBox->setCurrentText(QString::fromStdString(EmulatorSettings.GetSDLMicDevice()));
    } else if (backend == "OpenAL") {
        ui->GenAudioComboBox->setCurrentText(
            QString::fromStdString(EmulatorSettings.GetOpenALMainOutputDevice()));
        ui->DsAudioComboBox->setCurrentText(
            QString::fromStdString(EmulatorSettings.GetOpenALPadSpkOutputDevice()));
        ui->micComboBox->setCurrentText(
            QString::fromStdString(EmulatorSettings.GetOpenALMicDevice()));
    }
}

void SettingsDialog::GetPhysicalDevices() {
#ifndef __APPLE__
    if (volkInitialize() != VK_SUCCESS) {
        qWarning() << "Failed to initialize Volk.";
        return;
    }

    // Create Vulkan instance
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "shadPS4QtLauncher";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instInfo{};
    instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instInfo.pApplicationInfo = &appInfo;

    VkInstance instance;
    if (vkCreateInstance(&instInfo, nullptr, &instance) != VK_SUCCESS) {
        qWarning() << "Failed to create Vulkan instance.";
        return;
    }

    // Load instance-based function pointers
    volkLoadInstance(instance);

    // Enumerate devices
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        qWarning() << "No Vulkan physical devices found.";
        return;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    m_physical_devices.clear();
    for (uint32_t i = 0; i < deviceCount; ++i) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[i], &props);
        QString name = QString::fromUtf8(props.deviceName, -1);
        m_physical_devices.push_back(name);
    }

    vkDestroyInstance(instance, nullptr);
#else
    // Apple devices have a single GPU, query its name.
    std::string gpuName = GetAppleGpuName();
    m_physical_devices.push_back(QString::fromStdString(gpuName));
#endif
}

namespace {
constexpr std::array<const char*, 116> LogClassNames{
    "Log", "Common", "Common.Filesystem", "Common.Memory", "KeyManager", "Core", "Core.Linker",
    "Core.Devices", "Config", "Debug", "Kernel", "Kernel.Pthread", "Kernel.Fs", "Kernel.Vmm",
    "Kernel.Event", "Kernel.Sce", "Lib", "Lib.LibcInternal", "Lib.Kernel", "Lib.Pad",
    "Lib.SystemGesture", "Lib.GnmDriver", "Lib.SystemService", "Lib.UserService", "Lib.VideoOut",
    "Lib.CommonDlg", "Lib.MsgDlg", "Lib.AudioOut", "Lib.AudioIn", "Lib.Move", "Lib.Net",
    "Lib.NetCtl", "Lib.SaveData", "Lib.SaveDataDialog", "Lib.Ssl", "Lib.Ssl2", "Lib.Http",
    "Lib.Http2", "Lib.SysModule", "Lib.NpCommon", "Lib.NpCommerce", "Lib.NpAuth", "Lib.NpManager",
    "Lib.NpMatching2", "Lib.NpSignaling", "Lib.NpScore", "Lib.NpTrophy", "Lib.NpTus",
    "Lib.NpUtility", "Lib.NpWebApi", "Lib.NpWebApi2", "Lib.NpProfileDialog",
    "Lib.NpSnsFacebookDialog", "Lib.Screenshot", "Lib.AppContent", "Lib.Rtc", "Lib.Rudp",
    "Lib.DiscMap", "Lib.Png", "Lib.Jpeg", "Lib.PlayGo", "Lib.PlayGoDialog", "Lib.Random",
    "Lib.Usbd", "Lib.Ajm", "Lib.ErrorDialog", "Lib.ImeDialog", "Lib.InvitationDialog",
    "Lib.AvPlayer", "Lib.Ngs2", "Lib.Audio3d", "Lib.Ime", "Lib.GameLiveStreaming",
    "Lib.Remoteplay", "Lib.SharePlay", "Lib.Fiber", "Lib.Vdec2", "Lib.Vdecsw", "Lib.Videodec",
    "Lib.VideoRecording", "Lib.Voice", "Lib.RazorCpu", "Lib.Mouse", "Lib.Keyboard",
    "Lib.WebBrowserDialog", "Lib.NpParty", "Lib.NpPartner", "Lib.Zlib", "Lib.Hmd",
    "Lib.HmdSetupDialog", "Lib.SigninDialog", "Lib.Camera", "Lib.CompanionHttpd",
    "Lib.CompanionUtil", "Lib.ContentExport", "Lib.VrTracker", "Lib.Font", "Lib.FontFt",
    "Frontend", "NpHandler", "Render", "Render.Vulkan", "Render.Recompiler", "ShadNet", "ImGui",
    "Loader", "Input", "Tty",
};
constexpr std::array<const char*, 7> LogLevelNames{"trace", "debug",    "info", "warning",
                                                   "error", "critical", "off"};

QComboBox* MakeLogLevelBox(QWidget* parent, const QString& emptyName) {
    auto* box = new QComboBox(parent);
    if (!emptyName.isEmpty()) {
        box->addItem(emptyName, QString());
    }
    for (const char* level : LogLevelNames) {
        box->addItem(level, QString(level));
    }
    return box;
}
} // namespace

void SettingsDialog::SetupNavigation() {
    struct Page {
        const char* icon;
        QString subtitle;
    };
    const std::array<Page, 10> pages{{
        {":/images/settings_icon.png", tr("Console, audio and CPU")},
        {":/images/themes_icon.png", tr("Launcher look, updates and compatibility")},
        {":/images/fullscreen_icon.png", tr("Display, upscaling, frame generation and DLSS5")},
        {":/images/users.png", tr("Profiles and trophies")},
        {":/images/controller_icon.png", tr("Controllers, cursor, camera and USB devices")},
        {":/images/website.png", tr("Online services and connection")},
        {":/images/folder_icon.png", tr("Games, DLC, saves and system files")},
        {":/images/list_icon.png", tr("Log output and per-class log levels")},
        {":/images/utils_icon.png", tr("Validation, markers and dumps for troubleshooting")},
        {":/images/dump_icon.png", tr("Memory, readbacks and shader cache")},
    }};

    auto* tabs = ui->tabWidgetSettings;
    tabs->tabBar()->hide();
    tabs->setDocumentMode(true);

    auto* nav = new QListWidget(this);
    nav->setObjectName("settingsNav");
    nav->setIconSize(QSize(20, 20));
    nav->setFixedWidth(210);
    nav->setFrameShape(QFrame::NoFrame);
    nav->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    for (int i = 0; i < tabs->count(); ++i) {
        QPixmap icon(pages[i].icon);
        QPainter painter(&icon);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(icon.rect(), palette().color(QPalette::WindowText));
        painter.end();
        auto* item = new QListWidgetItem(QIcon(icon), tabs->tabText(i), nav);
        item->setSizeHint(QSize(0, 40));
        item->setHidden(!tabs->isTabVisible(i));
    }

    auto* search = new QLineEdit(this);
    search->setObjectName("settingsSearch");
    search->setPlaceholderText(tr("Search settings"));
    search->setClearButtonEnabled(true);

    auto* sidebar = new QVBoxLayout;
    sidebar->setSpacing(10);
    sidebar->addWidget(search);
    sidebar->addWidget(nav, 1);

    auto* title = new QLabel(this);
    title->setObjectName("settingsPageTitle");
    auto* subtitle = new QLabel(this);
    subtitle->setObjectName("settingsPageSubtitle");

    auto* root = static_cast<QVBoxLayout*>(layout());
    root->removeWidget(tabs);
    root->removeWidget(ui->buttonBox);
    root->removeWidget(ui->descriptionText);

    ui->descriptionText->setFixedHeight(84);
    ui->descriptionText->setFrameShape(QFrame::NoFrame);
    ui->descriptionText->setObjectName("descriptionText");

    auto* content = new QVBoxLayout;
    content->setSpacing(6);
    content->addWidget(title);
    content->addWidget(subtitle);
    content->addSpacing(6);
    content->addWidget(tabs, 1);
    content->addWidget(ui->descriptionText);

    auto* body = new QHBoxLayout;
    body->setSpacing(18);
    body->addLayout(sidebar);
    body->addLayout(content, 1);
    root->addLayout(body, 1);
    root->addWidget(ui->buttonBox);
    root->setContentsMargins(16, 16, 16, 12);

    const auto showPage = [=](int index) {
        if (index < 0) {
            return;
        }
        title->setText(tabs->tabText(index));
        subtitle->setText(pages[index].subtitle);
    };
    connect(nav, &QListWidget::currentRowChanged, tabs, &QTabWidget::setCurrentIndex);
    connect(tabs, &QTabWidget::currentChanged, this, [=](int index) {
        nav->setCurrentRow(index);
        showPage(index);
    });
    nav->setCurrentRow(tabs->currentIndex());
    showPage(tabs->currentIndex());

    auto* searchTimer = new QTimer(this);
    searchTimer->setSingleShot(true);
    searchTimer->setInterval(250);
    connect(search, &QLineEdit::textChanged, searchTimer, qOverload<>(&QTimer::start));
    connect(searchTimer, &QTimer::timeout, this, [=] {
        const QString query = search->text().trimmed();
        int firstMatch = -1;
        for (int i = 0; i < tabs->count(); ++i) {
            bool pageMatches = query.isEmpty();
            for (QWidget* widget : tabs->widget(i)->findChildren<QWidget*>()) {
                QString label;
                if (auto* button = qobject_cast<QAbstractButton*>(widget)) {
                    label = button->text();
                } else if (auto* group = qobject_cast<QGroupBox*>(widget)) {
                    label = group->title();
                } else if (auto* textLabel = qobject_cast<QLabel*>(widget)) {
                    label = textLabel->text();
                } else {
                    continue;
                }
                const bool matches = !query.isEmpty() && label.contains(query, Qt::CaseInsensitive);
                if (widget->property("searchMatch").toBool() != matches) {
                    widget->setProperty("searchMatch", matches);
                    widget->style()->unpolish(widget);
                    widget->style()->polish(widget);
                }
                pageMatches |= matches;
            }
            pageMatches |= tabs->tabText(i).contains(query, Qt::CaseInsensitive);
            nav->item(i)->setHidden(!tabs->isTabVisible(i) || !pageMatches);
            if (pageMatches && tabs->isTabVisible(i) && firstMatch < 0) {
                firstMatch = i;
            }
        }
        if (firstMatch >= 0 && nav->item(tabs->currentIndex())->isHidden()) {
            tabs->setCurrentIndex(firstMatch);
        }
    });

    ui->buttonBox->button(QDialogButtonBox::Save)->setObjectName("primaryButton");
    setStyleSheet(R"(
        #settingsNav { background: transparent; outline: 0; }
        #settingsNav::item { border-radius: 8px; padding: 4px 10px; margin: 1px 0; }
        #settingsNav::item:hover { background: palette(midlight); }
        #settingsNav::item:selected { background: palette(highlight); color: palette(highlighted-text); }
        #settingsSearch { border: 1px solid palette(mid); border-radius: 8px; padding: 6px 10px; }
        #settingsSearch:focus { border-color: palette(highlight); }
        #settingsPageTitle { font-size: 20pt; font-weight: 600; }
        #settingsPageSubtitle { color: palette(placeholder-text); }
        QTabWidget::pane { border: none; }
        QScrollArea { background: transparent; border: none; }
        QGroupBox { border: 1px solid palette(mid); border-radius: 10px; margin-top: 14px;
                    padding: 10px 10px 8px 10px; font-weight: 600; }
        QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; }
        QGroupBox > QWidget, QGroupBox QCheckBox, QGroupBox QComboBox, QGroupBox QLabel,
        QGroupBox QLineEdit, QGroupBox QPushButton, QGroupBox QSpinBox { font-weight: normal; }
        [searchMatch="true"] { color: palette(highlight); font-weight: 700; }
        #descriptionText { border: 1px solid palette(mid); border-radius: 10px; padding: 6px 10px;
                           background: palette(alternate-base); }
        QDialogButtonBox QPushButton { min-width: 96px; padding: 6px 14px; border-radius: 8px;
                                       border: 1px solid palette(mid); background: palette(button); }
        QDialogButtonBox QPushButton:hover { border-color: palette(highlight); }
        #primaryButton { background: palette(highlight); color: palette(highlighted-text);
                         border: none; }
    )");
    resize(std::max(width(), 1120), std::max(height(), 800));
}

void SettingsDialog::SetupLogLevels() {
    auto* group = new QGroupBox(tr("Log Levels"), ui->logFilter->parentWidget());
    group->setObjectName("logLevelsGroupBox");
    auto* layout = new QVBoxLayout(group);

    auto* levelsRow = new QHBoxLayout;
    levelsRow->addWidget(new QLabel(tr("All classes:"), group));
    logDefaultLevelBox = MakeLogLevelBox(group, {});
    levelsRow->addWidget(logDefaultLevelBox);
    levelsRow->addWidget(new QLabel(tr("Flush at:"), group));
    logFlushLevelBox = MakeLogLevelBox(group, tr("default"));
    levelsRow->addWidget(logFlushLevelBox);
    layout->addLayout(levelsRow);

    auto* searchEdit = new QLineEdit(group);
    searchEdit->setPlaceholderText(tr("Search classes..."));
    searchEdit->setClearButtonEnabled(true);
    layout->addWidget(searchEdit);

    logClassTable = new QTableWidget(static_cast<int>(LogClassNames.size()), 2, group);
    logClassTable->setHorizontalHeaderLabels({tr("Class"), tr("Level")});
    logClassTable->verticalHeader()->setVisible(false);
    logClassTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    logClassTable->setSelectionMode(QAbstractItemView::NoSelection);
    logClassTable->setMinimumHeight(240);
    for (int row = 0; row < static_cast<int>(LogClassNames.size()); ++row) {
        auto* nameItem = new QTableWidgetItem(LogClassNames[row]);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        logClassTable->setItem(row, 0, nameItem);
        auto* levelBox = MakeLogLevelBox(logClassTable, tr("default"));
        connect(levelBox, &QComboBox::currentIndexChanged, this,
                [this] { WriteFilterFromLogLevels(); });
        logClassTable->setCellWidget(row, 1, levelBox);
    }
    layout->addWidget(logClassTable);

    connect(searchEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        for (int row = 0; row < logClassTable->rowCount(); ++row) {
            logClassTable->setRowHidden(
                row, !logClassTable->item(row, 0)->text().contains(text, Qt::CaseInsensitive));
        }
    });
    connect(logDefaultLevelBox, &QComboBox::currentIndexChanged, this,
            [this] { WriteFilterFromLogLevels(); });
    connect(ui->logFilterLineEdit, &QLineEdit::textChanged, this,
            [this] { SyncLogLevelsFromFilter(); });

    ui->verticalLayout_15->insertWidget(1, group);
}

void SettingsDialog::SyncLogLevelsFromFilter() {
    if (syncingLogLevels) {
        return;
    }
    syncingLogLevels = true;
    QString defaultLevel = "info";
    QMap<QString, QString> classLevels;
    extraLogFilter.clear();
    for (const QString& entry : ui->logFilterLineEdit->text().split(' ', Qt::SkipEmptyParts)) {
        const QStringList pair = entry.split(':');
        const bool knownClass =
            pair.size() == 2 &&
            std::ranges::any_of(LogClassNames, [&](const char* name) { return pair[0] == name; });
        if (pair.size() == 2 && pair[0] == "*") {
            defaultLevel = pair[1];
        } else if (knownClass) {
            classLevels[pair[0]] = pair[1];
        } else {
            extraLogFilter.push_back(entry);
        }
    }
    logDefaultLevelBox->setCurrentIndex(std::max(0, logDefaultLevelBox->findData(defaultLevel)));
    for (int row = 0; row < logClassTable->rowCount(); ++row) {
        auto* levelBox = static_cast<QComboBox*>(logClassTable->cellWidget(row, 1));
        const QString level = classLevels.value(logClassTable->item(row, 0)->text());
        levelBox->setCurrentIndex(std::max(0, levelBox->findData(level)));
    }
    syncingLogLevels = false;
}

void SettingsDialog::WriteFilterFromLogLevels() {
    if (syncingLogLevels) {
        return;
    }
    QStringList entries{"*:" + logDefaultLevelBox->currentData().toString()};
    for (int row = 0; row < logClassTable->rowCount(); ++row) {
        const QString level =
            static_cast<QComboBox*>(logClassTable->cellWidget(row, 1))->currentData().toString();
        if (!level.isEmpty()) {
            entries.push_back(logClassTable->item(row, 0)->text() + ":" + level);
        }
    }
    entries.append(extraLogFilter);
    syncingLogLevels = true;
    ui->logFilterLineEdit->setText(entries.join(' '));
    syncingLogLevels = false;
}

void SettingsDialog::SetupGraphicsExtras() {
    // Window size: a list of common resolutions instead of typing the width and height. The hidden
    // spin boxes keep holding the values that are loaded and saved.
    ui->widthLabel->setVisible(false);
    ui->widthSpinBox->setVisible(false);
    ui->heightLabel->setVisible(false);
    ui->heightSpinBox->setVisible(false);

    resolutionComboBox = new QComboBox(ui->windowSizeGroupBox);
    resolutionComboBox->setObjectName("resolutionComboBox");
    static constexpr std::array<std::pair<int, int>, 10> resolutions{{
        {1280, 720},
        {1366, 768},
        {1600, 900},
        {1920, 1080},
        {2560, 1080},
        {2560, 1440},
        {3440, 1440},
        {3200, 1800},
        {3840, 2160},
        {5120, 2880},
    }};
    for (const auto& [width, height] : resolutions) {
        QString name = QStringLiteral("%1 x %2").arg(width).arg(height);
        if (height == 720) {
            name += " (HD)";
        } else if (height == 1080 && width == 1920) {
            name += " (Full HD)";
        } else if (height == 1440 && width == 2560) {
            name += " (2K)";
        } else if (height == 2160) {
            name += " (4K)";
        }
        resolutionComboBox->addItem(name, QSize(width, height));
    }
    ui->resLayout->addWidget(resolutionComboBox);

    connect(resolutionComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                const QSize size = resolutionComboBox->itemData(index).toSize();
                if (size.isValid()) {
                    ui->widthSpinBox->setValue(size.width());
                    ui->heightSpinBox->setValue(size.height());
                }
            });

    // Frame generation; its in-game toggle key is set in the Hotkeys dialog.
    frameGenCheckBox = new QCheckBox(tr("Frame Generation"), ui->FSRGroupBox);
    frameGenCheckBox->setObjectName("frameGenCheckBox");
    ui->verticalLayout_5->insertWidget(0, frameGenCheckBox);

    frameGenBackendComboBox = new QComboBox(ui->FSRGroupBox);
    frameGenBackendComboBox->setObjectName("frameGenBackendComboBox");
    frameGenBackendComboBox->addItem(tr("FSR 3.1"), QStringLiteral("FSR"));
    frameGenBackendComboBox->addItem(tr("Multi FG"), QStringLiteral("Lemon"));
    ui->verticalLayout_5->insertWidget(1, frameGenBackendComboBox);

    lemonFgHighQualityCheckBox = new QCheckBox(tr("Multi FG: High Quality"), ui->FSRGroupBox);
    lemonFgHighQualityCheckBox->setObjectName("lemonFgHighQualityCheckBox");
    ui->verticalLayout_5->insertWidget(2, lemonFgHighQualityCheckBox);

    lemonFgHudProtectionCheckBox = new QCheckBox(tr("Multi FG: HUD Protection"), ui->FSRGroupBox);
    lemonFgHudProtectionCheckBox->setObjectName("lemonFgHudProtectionCheckBox");
    ui->verticalLayout_5->insertWidget(3, lemonFgHudProtectionCheckBox);

    lemonFgMultiplierComboBox = new QComboBox(ui->FSRGroupBox);
    lemonFgMultiplierComboBox->setObjectName("lemonFgMultiplierComboBox");
    for (const uint multiplier : {2u, 3u, 4u, 6u}) {
        lemonFgMultiplierComboBox->addItem(tr("Multi FG: %1x").arg(multiplier), multiplier);
    }
    ui->verticalLayout_5->insertWidget(4, lemonFgMultiplierComboBox);

    const auto updateLemonFgOptions = [this] {
        const bool lemon = frameGenBackendComboBox->currentData().toString() == "Lemon";
        lemonFgHighQualityCheckBox->setEnabled(lemon);
        lemonFgHudProtectionCheckBox->setEnabled(lemon);
        lemonFgMultiplierComboBox->setEnabled(lemon);
    };
    connect(frameGenBackendComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            updateLemonFgOptions);
    updateLemonFgOptions();

    imageQualityGroupBox = new QGroupBox(tr("Image Quality"), this);
    imageQualityGroupBox->setObjectName("imageQualityGroupBox");
    auto* imageQualityLayout = new QVBoxLayout(imageQualityGroupBox);

    antiAliasingCheckBox = new QCheckBox(tr("FXAA Anti-Aliasing"), imageQualityGroupBox);
    antiAliasingCheckBox->setObjectName("antiAliasingCheckBox");
    imageQualityLayout->addWidget(antiAliasingCheckBox);

    auto* scaleLayout = new QHBoxLayout();
    scaleLayout->addWidget(new QLabel(tr("Resolution Scaling:"), imageQualityGroupBox));
    resolutionScaleComboBox = new QComboBox(imageQualityGroupBox);
    resolutionScaleComboBox->setObjectName("resolutionScaleComboBox");
    for (const int percent : {50, 75, 100, 125, 150, 200, 300, 400}) {
        resolutionScaleComboBox->addItem(QStringLiteral("%1%").arg(percent), percent);
    }
    scaleLayout->addWidget(resolutionScaleComboBox, 1);
    imageQualityLayout->addLayout(scaleLayout);

    auto* anisotropicLayout = new QHBoxLayout();
    anisotropicLayout->addWidget(new QLabel(tr("Anisotropic Filtering:"), imageQualityGroupBox));
    anisotropicComboBox = new QComboBox(imageQualityGroupBox);
    anisotropicComboBox->setObjectName("anisotropicComboBox");
    anisotropicComboBox->addItem(tr("Off"), 0);
    for (const int level : {2, 4, 8, 16}) {
        anisotropicComboBox->addItem(QStringLiteral("%1x").arg(level), level);
    }
    anisotropicLayout->addWidget(anisotropicComboBox, 1);
    imageQualityLayout->addLayout(anisotropicLayout);

    auto* brightnessLayout = new QHBoxLayout();
    brightnessLayout->addWidget(new QLabel(tr("Brightness:"), imageQualityGroupBox));
    brightnessSlider = new QSlider(Qt::Horizontal, imageQualityGroupBox);
    brightnessSlider->setObjectName("brightnessSlider");
    brightnessSlider->setRange(25, 300);
    brightnessSlider->setSingleStep(5);
    brightnessSlider->setPageStep(25);
    auto* brightnessValueLabel = new QLabel(imageQualityGroupBox);
    brightnessValueLabel->setMinimumWidth(40);
    connect(brightnessSlider, &QSlider::valueChanged, brightnessValueLabel,
            [brightnessValueLabel](int value) {
                brightnessValueLabel->setText(QStringLiteral("%1%").arg(value));
            });
    brightnessSlider->setValue(100);
    brightnessValueLabel->setText(QStringLiteral("100%"));
    brightnessLayout->addWidget(brightnessSlider, 1);
    brightnessLayout->addWidget(brightnessValueLabel);
    imageQualityLayout->addLayout(brightnessLayout);

    auto* contrastLayout = new QHBoxLayout();
    contrastLayout->addWidget(new QLabel(tr("Contrast:"), imageQualityGroupBox));
    contrastSlider = new QSlider(Qt::Horizontal, imageQualityGroupBox);
    contrastSlider->setObjectName("contrastSlider");
    contrastSlider->setRange(25, 300);
    contrastSlider->setSingleStep(5);
    contrastSlider->setPageStep(25);
    auto* contrastValueLabel = new QLabel(imageQualityGroupBox);
    contrastValueLabel->setMinimumWidth(40);
    connect(contrastSlider, &QSlider::valueChanged, contrastValueLabel,
            [contrastValueLabel](int value) {
                contrastValueLabel->setText(QStringLiteral("%1%").arg(value));
            });
    contrastSlider->setValue(100);
    contrastValueLabel->setText(QStringLiteral("100%"));
    contrastLayout->addWidget(contrastSlider, 1);
    contrastLayout->addWidget(contrastValueLabel);
    imageQualityLayout->addLayout(contrastLayout);

    ui->graphicsTabLayoutRight->insertWidget(
        ui->graphicsTabLayoutRight->indexOf(ui->FSRGroupBox) + 1, imageQualityGroupBox);

    // DLSS5, which the emulator's renderer runs on the game picture; the options live in the
    // emulator's user/dlss5.json and are applied when a game starts.
    dlss5GroupBox = new QGroupBox(tr("DLSS5"), this);
    dlss5GroupBox->setObjectName("dlss5GroupBox");
    auto* dlss5Layout = new QVBoxLayout(dlss5GroupBox);

    dlss5CheckBox = new QCheckBox(tr("DLSS5 Neural Rendering"), dlss5GroupBox);
    dlss5CheckBox->setObjectName("dlss5CheckBox");
    dlss5CheckBox->setToolTip(
        tr("Runs NVIDIA's DLSS5 neural renderer on the game picture inside the emulator, in a "
           "window or full screen. It needs an NVIDIA RTX card and nvngx_dlssnr.dll in "
           "qtplugins\\DLSS5. The key that turns it on and off is set in Hotkeys (default: 2)."));
    dlss5Layout->addWidget(dlss5CheckBox);

    const auto addDlss5Row = [this, dlss5Layout](QWidget* label, QWidget* control) {
        auto* row = new QHBoxLayout();
        row->addWidget(label, 0);
        row->addWidget(control, 1);
        dlss5Layout->addLayout(row);
    };

    const auto addDlss5SliderRow = [this](QVBoxLayout* target, const QString& label,
                                          QSlider*& slider, int min, int max, int initial) {
        auto* row = new QHBoxLayout();
        row->addWidget(new QLabel(label, dlss5GroupBox), 0);
        slider = new QSlider(Qt::Horizontal, dlss5GroupBox);
        slider->setRange(min, max);
        auto* valueLabel = new QLabel(dlss5GroupBox);
        valueLabel->setMinimumWidth(40);
        QObject::connect(slider, &QSlider::valueChanged, valueLabel, [valueLabel](int value) {
            valueLabel->setText(QString::number(value * 0.01, 'f', 2));
        });
        slider->setValue(initial);
        valueLabel->setText(QString::number(initial * 0.01, 'f', 2));
        row->addWidget(slider, 1);
        row->addWidget(valueLabel);
        target->addLayout(row);
    };
    dlss5NetworkComboBox = new QComboBox(dlss5GroupBox);
    dlss5NetworkComboBox->setObjectName("dlss5NetworkComboBox");
    dlss5NetworkComboBox->addItem(tr("Default"), 0);
    dlss5NetworkComboBox->addItem(tr("Full Quality"), 1);
    dlss5NetworkComboBox->addItem(tr("Fast"), 2);
    dlss5NetworkComboBox->setToolTip(
        tr("Neural network DLSS5 runs. Full Quality looks best; Fast uses a smaller network that "
           "costs less GPU time."));
    addDlss5Row(new QLabel(tr("Network:"), dlss5GroupBox), dlss5NetworkComboBox);
    addDlss5SliderRow(dlss5Layout, tr("DLSS5 Intensity:"), dlss5IntensitySlider, 0, 100, 100);

    // Fine tuning: the NGX style and the three strengths.
    auto* fineTuneBox = new QGroupBox(tr("DLSS5 Fine Tuning"), dlss5GroupBox);
    fineTuneBox->setObjectName("dlss5FineTuneGroupBox");
    auto* fineTuneLayout = new QVBoxLayout(fineTuneBox);

    auto* styleRow = new QHBoxLayout();
    styleRow->addWidget(new QLabel(tr("Style:"), fineTuneBox), 0);
    dlss5StyleComboBox = new QComboBox(fineTuneBox);
    dlss5StyleComboBox->setObjectName("dlss5StyleComboBox");
    const QString styles[] = {tr("Standard"), tr("Natural"), tr("Cinematic")};
    for (int index = 0; index < 3; ++index) {
        dlss5StyleComboBox->addItem(styles[index], index);
    }
    styleRow->addWidget(dlss5StyleComboBox, 1);
    fineTuneLayout->addLayout(styleRow);

    addDlss5SliderRow(fineTuneLayout, tr("Global Tone:"), dlss5GlobalToneSlider, 0, 200, 100);
    addDlss5SliderRow(fineTuneLayout, tr("Local Tone:"), dlss5LocalToneSlider, 0, 200, 0);
    addDlss5SliderRow(fineTuneLayout, tr("Local Structure:"), dlss5LocalStructureSlider, 0, 200,
                      100);
    addDlss5SliderRow(fineTuneLayout, tr("Skin Structure:"), dlss5SkinStructureSlider, -100, 250,
                      100);
    dlss5Layout->addWidget(fineTuneBox);

    ui->graphicsTabLayoutLeft->insertWidget(
        ui->graphicsTabLayoutLeft->indexOf(ui->graphicsAdapterGroupBox) + 1, dlss5GroupBox);
}

void SettingsDialog::LoadDlss5Settings() {
    QJsonObject j;
    const std::filesystem::path path =
        Common::FS::GetUserPath(Common::FS::PathType::UserDir) / "dlss5.json";
    if (QFile file(QString::fromStdString(path.string())); file.open(QIODevice::ReadOnly)) {
        j = QJsonDocument::fromJson(file.readAll()).object();
    }

    const auto selectData = [](QComboBox* box, const QVariant& data) {
        QSignalBlocker blocker(box);
        const int index = box->findData(data);
        box->setCurrentIndex(index >= 0 ? index : 0);
    };
    const auto setSlider = [](QSlider* slider, double value, double min, double max) {
        slider->setValue(qRound(qBound(min, value, max) * 100.0));
    };

    dlss5CheckBox->setChecked(j.value("Enabled").toBool(false));
    selectData(dlss5StyleComboBox, qBound(0, j.value("Style").toInt(1), 2));
    selectData(dlss5NetworkComboBox, qBound(0, j.value("Preset").toInt(1), 2));
    setSlider(dlss5IntensitySlider, j.value("Intensity").toDouble(1.0), 0.0, 1.0);
    setSlider(dlss5GlobalToneSlider, j.value("GlobalTone").toDouble(1.0), 0.0, 2.0);
    setSlider(dlss5LocalToneSlider, j.value("LocalTone").toDouble(0.0), 0.0, 2.0);
    setSlider(dlss5LocalStructureSlider, j.value("LocalStructure").toDouble(1.0), 0.0, 2.0);
    setSlider(dlss5SkinStructureSlider, j.value("SkinStructure").toDouble(1.0), -1.0, 2.5);
}

void SettingsDialog::SaveDlss5Settings() {
    QJsonObject j;
    j["Enabled"] = dlss5CheckBox->isChecked();
    j["Style"] = dlss5StyleComboBox->currentData().toInt();
    j["Preset"] = dlss5NetworkComboBox->currentData().toInt();
    j["Intensity"] = dlss5IntensitySlider->value() * 0.01;
    j["GlobalTone"] = dlss5GlobalToneSlider->value() * 0.01;
    j["LocalTone"] = dlss5LocalToneSlider->value() * 0.01;
    j["LocalStructure"] = dlss5LocalStructureSlider->value() * 0.01;
    j["SkinStructure"] = dlss5SkinStructureSlider->value() * 0.01;

    const std::filesystem::path dir = Common::FS::GetUserPath(Common::FS::PathType::UserDir);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path path = dir / "dlss5.json";
    QFile file(QString::fromStdString(path.string()));
    if (!file.open(QIODevice::WriteOnly)) {
        LOG_ERROR(Config, "Could not write {}", path.string());
        return;
    }
    file.write(QJsonDocument(j).toJson(QJsonDocument::Indented));
}

void SettingsDialog::SyncImageQualityComboBoxes() {
    const auto select = [](QComboBox* box, u32 value, const QString& custom) {
        int index = box->findData(value);
        if (index < 0) {
            box->addItem(custom, value);
            index = box->count() - 1;
        }
        QSignalBlocker blocker(box);
        box->setCurrentIndex(index);
    };
    const u32 scale = EmulatorSettings.GetResolutionScale();
    select(resolutionScaleComboBox, scale, QStringLiteral("%1%").arg(scale));
    const u32 aniso = EmulatorSettings.GetAnisotropicFiltering();
    select(anisotropicComboBox, aniso, QStringLiteral("%1x").arg(aniso));
}

void SettingsDialog::SyncResolutionComboBox() {
    const QSize current(ui->widthSpinBox->value(), ui->heightSpinBox->value());
    int index = resolutionComboBox->findData(current);
    if (index < 0) {
        // A size from an older config that is not in the list stays selectable.
        resolutionComboBox->addItem(
            tr("Custom (%1 x %2)").arg(current.width()).arg(current.height()), current);
        index = resolutionComboBox->count() - 1;
    }
    QSignalBlocker blocker(resolutionComboBox);
    resolutionComboBox->setCurrentIndex(index);
}
