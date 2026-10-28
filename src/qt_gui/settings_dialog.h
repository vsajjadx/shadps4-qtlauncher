// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <span>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QGroupBox>
#include <QPushButton>
#include <QSlider>

#include "common/path_util.h"
#include "core/emulator_settings.h"
#include "gui_settings.h"
#include "ipc/ipc_client.h"
#include "qt_gui/compatibility_info.h"

namespace Ui {
class SettingsDialog;
}

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(std::shared_ptr<gui_settings> gui_settings,
                            std::shared_ptr<CompatibilityInfoClass> m_compat_info,
                            std::shared_ptr<IpcClient> ipc_client, QWidget* parent = nullptr,
                            bool is_game_running = false, bool is_game_specific = false,
                            std::string gsc_serial = "");
    ~SettingsDialog();

    bool eventFilter(QObject* obj, QEvent* event) override;
    void updateNoteTextEdit(const QString& groupName);

    int exec() override;

signals:
    void LanguageChanged(const QString& locale);
    void CompatibilityChanged();
    void BackgroundOpacityChanged(int opacity);

private:
    void LoadValuesFromConfig();
    void UpdateSettings(bool is_specific = false);
    void SyncRealTimeWidgetstoConfig();
    void InitializeEmulatorLanguages();
    void OnLanguageChanged(int index);
    void OnCursorStateChanged(s16 index);
    void SetDefaultValues();
    void VolumeSliderChange(int value);
    void PollSDLevents();
    void GetPhysicalDevices();
    void SaveSettings();
    void RefreshAudioDevices();
    void SetupGraphicsExtras();
    void SyncResolutionComboBox();
    void SyncImageQualityComboBoxes();

    void closeEvent(QCloseEvent* event) override;

    std::map<std::string, int> languages;
    QString defaultTextEdit;
    int initialHeight;
    std::string gs_serial;
    QFuture<void> Polling;

    bool is_game_running = false;
    bool is_game_specific = false;
    bool is_game_saving = false;

    QComboBox* resolutionComboBox{};
    QCheckBox* frameGenCheckBox{};
    QCheckBox* antiAliasingCheckBox{};
    QComboBox* resolutionScaleComboBox{};
    QComboBox* anisotropicComboBox{};
    QSlider* brightnessSlider{};
    QSlider* contrastSlider{};
    QGroupBox* imageQualityGroupBox{};

    // DLSS5 (NeuralScreen) group; options are stored in the emulator's
    // user/neural_screen.json so they survive emulator updates.
    QGroupBox* dlss5GroupBox{};
    QCheckBox* dlss5CheckBox{};
    QCheckBox* dlss5BoostCheckBox{};
    QCheckBox* dlss5AutoQualityCheckBox{};
    QComboBox* dlss5ResolutionComboBox{};
    QComboBox* dlss5PassesComboBox{};
    QComboBox* dlss5ProfileComboBox{};
    QComboBox* dlss5ModelComboBox{};
    QSlider* dlss5IntensitySlider{};
    QSlider* dlss5LocalToneSlider{};
    QSlider* dlss5LocalStructureSlider{};
    QSlider* dlss5SkinStructureSlider{};
    void LoadDlss5Settings();
    void SaveDlss5Settings();

    std::unique_ptr<Ui::SettingsDialog> ui;
    std::shared_ptr<gui_settings> m_gui_settings;
    std::shared_ptr<IpcClient> m_ipc_client;
};
