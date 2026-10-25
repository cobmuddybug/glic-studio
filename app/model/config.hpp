#pragma once

// Settings persistence (design §M8): recent files, the last preset, window
// size and a few preferences, as JSON in the config directory. ImGui keeps
// the dock layout next to it (layout.ini). Unknown fields survive a save.

#include "preset.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace studio {

struct StudioConfig {
    static constexpr size_t kMaxRecent = 10;
    std::vector<std::string> recentFiles;          // newest first
    std::optional<glic::preset::Preset> lastPreset;
    int windowWidth = 0, windowHeight = 0;         // points; 0 = default
    int proxyEdge = 512;
    bool autoArrange = true;
    nlohmann::json document = nlohmann::json::object();
};

// $GLIC_STUDIO_CONFIG_DIR, else $XDG_CONFIG_HOME/glic-studio, else ~/.config/glic-studio.
std::string configDir();

// A missing file gives the defaults. An unreadable one also gives the
// defaults, with `error` set, so a bad file never stops the app starting.
StudioConfig loadConfig(const std::string& path, std::string* error = nullptr);
// Creates the directory; writes to a temporary file and renames it into place.
bool saveConfig(const std::string& path, const StudioConfig& config);

// Moves `path` to the front of the recent list (absolute, no duplicates, capped).
void addRecent(StudioConfig& config, const std::string& path);

} // namespace studio
