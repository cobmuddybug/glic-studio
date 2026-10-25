#include <doctest/doctest.h>

#include "model/config.hpp"

#include <glic/image_io.hpp>

#include <cstdlib>
#include <filesystem>
#include <unistd.h>

namespace fs = std::filesystem;

TEST_CASE("config: defaults, round trip, unknown fields, recent list") {
    const fs::path dir = fs::temp_directory_path() / ("glic-studio-config-" + std::to_string(::getpid()));
    fs::remove_all(dir);
    const std::string path = (dir / "config.json").string();

    std::string err;
    studio::StudioConfig c = studio::loadConfig(path, &err);  // missing: defaults, no error
    CHECK(err.empty());
    CHECK(c.recentFiles.empty());
    CHECK_FALSE(c.lastPreset);
    CHECK(c.proxyEdge == 512);
    CHECK_FALSE(c.restoreLastPreset);

    for (int i = 0; i < 14; i++) studio::addRecent(c, "/img/" + std::to_string(i) + ".png");
    studio::addRecent(c, "/img/5.png");  // moves to the front, no duplicate
    CHECK(c.recentFiles.size() == studio::StudioConfig::kMaxRecent);
    CHECK(c.recentFiles.front() == "/img/5.png");
    CHECK(std::count(c.recentFiles.begin(), c.recentFiles.end(), "/img/5.png") == 1);

    glic::preset::Preset p;
    p.encode.channels[2].quantization = 77;
    p.corruption.enabled = true;
    c.lastPreset = p;
    c.windowWidth = 1000;
    c.windowHeight = 700;
    c.proxyEdge = 768;
    c.autoArrange = false;
    c.restoreLastPreset = true;
    c.document["from_the_future"] = 42;
    REQUIRE(studio::saveConfig(path, c));  // creates the directory

    studio::StudioConfig back = studio::loadConfig(path, &err);
    CHECK(err.empty());
    CHECK(back.recentFiles == c.recentFiles);
    REQUIRE(back.lastPreset);
    CHECK(back.lastPreset->sameSettings(p));
    CHECK(back.windowWidth == 1000);
    CHECK(back.proxyEdge == 768);
    CHECK_FALSE(back.autoArrange);
    CHECK(back.restoreLastPreset);
    REQUIRE(studio::saveConfig(path, back));
    CHECK(studio::loadConfig(path).document.value("from_the_future", 0) == 42);

    // A broken file never stops the app: defaults plus a message.
    REQUIRE(glic::writeFile(path, {'{', 'o', 'o', 'p', 's'}));
    studio::StudioConfig broken = studio::loadConfig(path, &err);
    CHECK_FALSE(err.empty());
    CHECK(broken.recentFiles.empty());
    fs::remove_all(dir);
}

TEST_CASE("config: directory follows GLIC_STUDIO_CONFIG_DIR, then XDG_CONFIG_HOME") {
    const char* oldDir = std::getenv("GLIC_STUDIO_CONFIG_DIR");
    const std::string saved = oldDir ? oldDir : "";
    const char* oldXdg = std::getenv("XDG_CONFIG_HOME");
    const std::string savedXdg = oldXdg ? oldXdg : "";
    ::setenv("GLIC_STUDIO_CONFIG_DIR", "/x/y", 1);
    CHECK(studio::configDir() == "/x/y");
    ::unsetenv("GLIC_STUDIO_CONFIG_DIR");
    ::setenv("XDG_CONFIG_HOME", "/cfg", 1);
    CHECK(studio::configDir() == "/cfg/glic-studio");
    if (!saved.empty()) ::setenv("GLIC_STUDIO_CONFIG_DIR", saved.c_str(), 1);
    if (savedXdg.empty()) ::unsetenv("XDG_CONFIG_HOME");
    else ::setenv("XDG_CONFIG_HOME", savedXdg.c_str(), 1);
}
