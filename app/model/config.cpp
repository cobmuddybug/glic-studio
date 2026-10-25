#include "config.hpp"

#include <glic/image_io.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace studio {

namespace fs = std::filesystem;
using nlohmann::json;

std::string configDir() {
    if (const char* d = std::getenv("GLIC_STUDIO_CONFIG_DIR"); d && *d) return d;
    if (const char* x = std::getenv("XDG_CONFIG_HOME"); x && *x) return (fs::path(x) / "glic-studio").string();
    if (const char* h = std::getenv("HOME"); h && *h) return (fs::path(h) / ".config" / "glic-studio").string();
    return "glic-studio";
}

StudioConfig loadConfig(const std::string& path, std::string* error) {
    StudioConfig c;
    auto bytes = glic::readFile(path);
    if (!bytes) return c;  // first run
    json doc = json::parse(bytes->begin(), bytes->end(), nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        if (error) *error = path + " is not valid JSON; using defaults";
        return c;
    }
    try {
        c.document = doc;
        if (doc.contains("recent_files"))
            for (const auto& f : doc["recent_files"])
                if (f.is_string() && c.recentFiles.size() < StudioConfig::kMaxRecent) c.recentFiles.push_back(f.get<std::string>());
        if (doc.contains("last_preset")) {
            std::string err;
            c.lastPreset = glic::preset::parse(doc["last_preset"].dump(), &err);
            if (!c.lastPreset && error) *error = "last preset unreadable: " + err;
        }
        if (doc.contains("window")) {
            c.windowWidth = doc["window"].value("width", 0);
            c.windowHeight = doc["window"].value("height", 0);
        }
        c.proxyEdge = std::clamp(doc.value("proxy_edge", c.proxyEdge), 128, 2048);
        c.autoArrange = doc.value("auto_arrange", c.autoArrange);
        c.restoreLastPreset = doc.value("restore_last_preset", c.restoreLastPreset);
    } catch (const json::exception& e) {
        if (error) *error = std::string("config: ") + e.what();
        StudioConfig fresh;
        fresh.document = doc;
        return fresh;
    }
    return c;
}

bool saveConfig(const std::string& path, const StudioConfig& c) {
    json doc = c.document.is_object() ? c.document : json::object();
    doc["recent_files"] = c.recentFiles;
    if (c.lastPreset) doc["last_preset"] = json::parse(glic::preset::serialize(*c.lastPreset));
    else doc.erase("last_preset");
    doc["window"] = {{"width", c.windowWidth}, {"height", c.windowHeight}};
    doc["proxy_edge"] = c.proxyEdge;
    doc["auto_arrange"] = c.autoArrange;
    doc["restore_last_preset"] = c.restoreLastPreset;

    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << doc.dump(2) << "\n";
        if (!out) return false;
    }
    fs::rename(tmp, path, ec);
    return !ec;
}

void addRecent(StudioConfig& c, const std::string& path) {
    std::error_code ec;
    std::string abs = fs::absolute(path, ec).lexically_normal().string();
    if (ec) abs = path;
    c.recentFiles.erase(std::remove(c.recentFiles.begin(), c.recentFiles.end(), abs), c.recentFiles.end());
    c.recentFiles.insert(c.recentFiles.begin(), abs);
    if (c.recentFiles.size() > StudioConfig::kMaxRecent) c.recentFiles.resize(StudioConfig::kMaxRecent);
}

} // namespace studio
