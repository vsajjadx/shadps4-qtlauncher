// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once
#include <core/emulator_settings.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <system_error>
#include <string_view>
#include <utility>
#include <vector>

#include <QFutureWatcher>
#include <QtConcurrent>

#include "core/file_format/psf.h"
#include "core/file_format/pkg.h"
#include "core/file_sys/game_backend.h"
#include "game_list_utils.h"
#include "gui_settings.h"

class GameInfoClass : public QObject {
    Q_OBJECT
public:
    struct PackageMetadata {
        std::string title_id;
        std::string category;
        std::string version;
    };

    GameInfoClass();
    ~GameInfoClass();
    void GetGameInfo(QWidget* parent = nullptr, bool force_size_refresh = false,
                     const std::string& force_size_serial = {});
    QVector<GameInfo> m_games;
    QVector<GameInfo> m_games_backup;
    std::shared_ptr<gui_settings> m_gui_settings;

    static void SceUpdateChecker(const std::string sceItem, std::filesystem::path& gameItem,
                                 std::filesystem::path& update_folder,
                                 std::filesystem::path& patch_folder,
                                 std::filesystem::path& game_folder) {
        const std::string rel_path = "sce_sys/" + sceItem;

        for (const auto& candidate : {update_folder, patch_folder, game_folder}) {
            if (const auto root = Core::FileSys::ResolveGameRoot(candidate)) {
                if (const auto resolved = Core::FileSys::ResolveGameFilePath(*root, rel_path)) {
                    gameItem = *resolved;
                    return;
                }
            }
        }
        gameItem = game_folder / "sce_sys" / sceItem;
    }

    static bool CompareStrings(GameInfo& a, GameInfo& b) {
        std::string name_a = a.name, name_b = b.name;
        std::transform(name_a.begin(), name_a.end(), name_a.begin(), ::tolower);
        std::transform(name_b.begin(), name_b.end(), name_b.begin(), ::tolower);
        return name_a < name_b;
    }

    static std::optional<PackageMetadata> GetPackageMetadata(const PSF& psf) {
        const auto category = psf.GetString("CATEGORY");
        if (!category.has_value() || category->empty()) {
            return std::nullopt;
        }

        std::string title_id{psf.GetString("TITLE_ID").value_or("")};
        if (const auto content_id = psf.GetString("CONTENT_ID");
            content_id.has_value() && content_id->size() >= 16) {
            const std::string content_title_id{content_id->substr(7, 9)};
            if (!title_id.empty() && title_id != content_title_id) {
                return std::nullopt;
            }
            if (title_id.empty()) {
                title_id = content_title_id;
            }
        }
        if (title_id.empty()) {
            return std::nullopt;
        }

        return PackageMetadata{std::move(title_id), std::string{*category},
                               std::string{psf.GetString("APP_VER").value_or("")}};
    }

    static std::optional<PackageMetadata> ReadPackageMetadata(
        const std::filesystem::path& file_path) {
        PKG pkg;
        std::string failreason;
        PSF psf;
        if (!pkg.Open(file_path, failreason, false) || pkg.sfo.empty() || !psf.Open(pkg.sfo)) {
            return std::nullopt;
        }
        return GetPackageMetadata(psf);
    }

    static std::optional<std::vector<std::uint64_t>> ParsePackageVersion(
        std::string_view version) {
        if (version.empty()) {
            return std::nullopt;
        }
        std::vector<std::uint64_t> components;
        size_t start = 0;
        while (start < version.size()) {
            const size_t end = version.find('.', start);
            const auto component = version.substr(
                start, end == std::string_view::npos ? version.size() - start : end - start);
            if (component.empty()) {
                return std::nullopt;
            }
            std::uint64_t value{};
            const auto [parsed_end, error] =
                std::from_chars(component.data(), component.data() + component.size(), value);
            if (error != std::errc{} || parsed_end != component.data() + component.size()) {
                return std::nullopt;
            }
            components.push_back(value);
            if (end == std::string_view::npos) {
                break;
            }
            start = end + 1;
        }
        if (version.back() == '.') {
            return std::nullopt;
        }
        return components;
    }

    static bool IsPackageVersionNewer(std::string_view candidate, std::string_view current) {
        const auto candidate_parts = ParsePackageVersion(candidate);
        const auto current_parts = ParsePackageVersion(current);
        if (!candidate_parts || !current_parts) {
            return false;
        }
        const size_t count = std::max(candidate_parts->size(), current_parts->size());
        for (size_t i = 0; i < count; ++i) {
            const auto candidate_part =
                i < candidate_parts->size() ? (*candidate_parts)[i] : std::uint64_t{};
            const auto current_part =
                i < current_parts->size() ? (*current_parts)[i] : std::uint64_t{};
            if (candidate_part != current_part) {
                return candidate_part > current_part;
            }
        }
        return false;
    }

    static void FindAdjacentPackages(GameInfo& game, const PackageMetadata& base_package) {
        const auto package_directory =
            game.path.parent_path().empty() ? std::filesystem::current_path()
                                            : game.path.parent_path();
        std::error_code scan_error;
        std::vector<std::filesystem::path> package_paths;
        for (std::filesystem::directory_iterator it{
                 package_directory, std::filesystem::directory_options::skip_permission_denied,
                 scan_error},
             end;
             !scan_error && it != end; it.increment(scan_error)) {
            std::error_code type_error;
            if (it->is_regular_file(type_error) && !type_error &&
                GameListUtils::IsPackagePath(it->path()) &&
                it->path().lexically_normal() != game.path.lexically_normal()) {
                package_paths.push_back(it->path());
            }
        }
        std::ranges::sort(package_paths);

        for (const auto& package_path : package_paths) {
            const auto candidate = ReadPackageMetadata(package_path);
            if (!candidate || candidate->title_id != base_package.title_id) {
                continue;
            }
            if (candidate->category == "gp") {
                const bool valid_first_version =
                    game.version == "Unknown" &&
                    ParsePackageVersion(candidate->version).has_value();
                if (!candidate->version.empty() &&
                    (valid_first_version ||
                     IsPackageVersionNewer(candidate->version, game.version))) {
                    game.package_update_path = package_path;
                    game.version = candidate->version;
                }
            } else if (candidate->category == "ac") {
                game.package_dlc_paths.push_back(package_path);
            }
        }
    }

    static GameInfo readGameInfo(const std::filesystem::path& filePath) {
        GameInfo game;
        game.path = filePath;
        const QString extension = QString::fromStdString(filePath.extension().string());
        if (extension.compare(QStringLiteral(".pkg"), Qt::CaseInsensitive) == 0) {
            PKG pkg;
            std::string failreason;
            PSF psf;
            if (!pkg.Open(filePath, failreason) || pkg.sfo.empty() || !psf.Open(pkg.sfo)) {
                return game;
            }
            const auto package_metadata = GetPackageMetadata(psf);
            if (!package_metadata || package_metadata->category != "gd") {
                return game;
            }

            if (!pkg.icon0_png.empty()) {
                game.icon = QImage::fromData(reinterpret_cast<const uchar*>(pkg.icon0_png.data()),
                                             static_cast<qsizetype>(pkg.icon0_png.size()));
                game.icon_path = GameListUtils::CachePackageAsset(
                    filePath, QStringLiteral("icon0.png"), pkg.icon0_png);
            }
            game.pic_path = GameListUtils::CachePackageAsset(
                filePath, QStringLiteral("pic1.png"), pkg.pic1_png);
            game.snd0_path = GameListUtils::CachePackageAsset(
                filePath, QStringLiteral("snd0.at9"), pkg.snd0_at9);

            if (const auto localized_title =
                    psf.GetString(fmt::format("TITLE_{:02}", EmulatorSettings.GetConsoleLanguage()));
                localized_title.has_value()) {
                game.name = *localized_title;
            } else if (const auto title = psf.GetString("TITLE"); title.has_value()) {
                game.name = *title;
            }
            game.serial = package_metadata->title_id;
            if (const auto content_id = psf.GetString("CONTENT_ID");
                content_id.has_value() && !content_id->empty()) {
                game.region = GameListUtils::GetRegion(content_id->at(0)).toStdString();
            }
            if (const auto fw_int_opt = psf.GetInteger("SYSTEM_VER"); fw_int_opt.has_value()) {
                const u32 fw_int = *fw_int_opt;
                if (fw_int == 0) {
                    game.fw = "0.00";
                } else {
                    const u8 major_bcd = (fw_int >> 24) & 0xFF;
                    const u8 minor_bcd = (fw_int >> 16) & 0xFF;
                    const int major = ((major_bcd >> 4) * 10) + (major_bcd & 0xF);
                    const int minor = ((minor_bcd >> 4) * 10) + (minor_bcd & 0xF);
                    game.fw = QString("%1.%2")
                                  .arg(major)
                                  .arg(minor, 2, 10, QChar('0'))
                                  .toStdString();
                }
            }
            if (!package_metadata->version.empty()) {
                game.version = package_metadata->version;
            }
            if (const auto play_time = psf.GetString("PLAY_TIME"); play_time.has_value()) {
                game.play_time = *play_time;
            }
            if (const auto save_dir = psf.GetString("INSTALL_DIR_SAVEDATA"); save_dir.has_value()) {
                game.save_dir = *save_dir;
            } else {
                game.save_dir = game.serial;
            }
            FindAdjacentPackages(game, *package_metadata);
            return game;
        }
        std::filesystem::path param_sfo_path;
        const std::filesystem::path stem_path = Core::FileSys::StripZArchiveExtension(filePath);
        std::filesystem::path game_update_path = stem_path;
        game_update_path += "-UPDATE";
        std::filesystem::path zar_update_path = game_update_path;
        zar_update_path += ".zar";

        std::filesystem::path game_patch_path = stem_path;
        game_patch_path += "-patch";
        std::filesystem::path zar_patch_path = game_patch_path;
        zar_patch_path += ".zar";
        SceUpdateChecker("param.sfo", param_sfo_path, game_update_path, game_patch_path, game.path);

        if (std::filesystem::exists(game_update_path)) {
            game.update_path = game_update_path;
        } else if (std::filesystem::exists(game_patch_path)) {
            game.update_path = game_patch_path;
        } else if (std::filesystem::exists(zar_update_path)) {
            game.update_path = zar_update_path;
        } else if (std::filesystem::exists(zar_patch_path)) {
            game.update_path = zar_patch_path;
        } else {
            game.update_path = "";
        }

        PSF psf;
        if (psf.Open(param_sfo_path)) {
            SceUpdateChecker("icon0.png", game.icon_path, game_update_path, game_patch_path,
                             game.path);
            QString iconpath;
            Common::FS::PathToQString(iconpath, game.icon_path);
            game.icon = QImage(iconpath);
            SceUpdateChecker("pic1.png", game.pic_path, game_update_path, game_patch_path,
                             game.path);
            SceUpdateChecker("snd0.at9", game.snd0_path, game_update_path, game_patch_path,
                             game.path);

            if (const auto localized_title = psf.GetString(
                    fmt::format("TITLE_{:02}", EmulatorSettings.GetConsoleLanguage()));
                localized_title.has_value()) {
                game.name = *localized_title;
            } else if (const auto title = psf.GetString("TITLE"); title.has_value()) {
                game.name = *title;
            }
            if (const auto title_id = psf.GetString("TITLE_ID"); title_id.has_value()) {
                game.serial = *title_id;
            }
            if (const auto content_id = psf.GetString("CONTENT_ID");
                content_id.has_value() && !content_id->empty()) {
                game.region = GameListUtils::GetRegion(content_id->at(0)).toStdString();
            }
            if (const auto fw_int_opt = psf.GetInteger("SYSTEM_VER"); fw_int_opt.has_value()) {
                uint32_t fw_int = *fw_int_opt;
                if (fw_int == 0) {
                    game.fw = "0.00";
                } else {
                    u8 major_bcd = (fw_int >> 24) & 0xFF;
                    u8 minor_bcd = (fw_int >> 16) & 0xFF;

                    int major = ((major_bcd >> 4) * 10) + (major_bcd & 0xF);
                    int minor = ((minor_bcd >> 4) * 10) + (minor_bcd & 0xF);

                    QString fw = QString("%1.%2").arg(major).arg(minor, 2, 10, QChar('0'));
                    game.fw = fw.toStdString();
                }
            }

            if (auto app_ver = psf.GetString("APP_VER"); app_ver.has_value()) {
                game.version = *app_ver;
            }
            if (const auto play_time = psf.GetString("PLAY_TIME"); play_time.has_value()) {
                game.play_time = *play_time;
            }
            if (const auto save_dir = psf.GetString("INSTALL_DIR_SAVEDATA"); save_dir.has_value()) {
                game.save_dir = *save_dir;
            } else {
                game.save_dir = game.serial;
            }
        }
        return game;
    }
};
