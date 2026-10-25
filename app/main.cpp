// glic-studio: a GLIC editor (SDL3 + OpenGL 3.3 + Dear ImGui).

#include "app.hpp"
#include "platform/platform.hpp"

#include <glic/image_io.hpp>

#include "preset.hpp"
#include "spec.hpp"

#include <SDL3/SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

struct Options {
    std::string open;
    std::string spec;
    std::string decode;  // unlink decoding and apply these decode-side values
    std::string view;
    std::string tab;
    std::string capture;  // write a screenshot once a full render is shown, then exit
    std::string preset;
    std::string exportPng;  // export the full render (with preset) once shown, then exit
    int width = 0, height = 0;
};

void usage() {
    std::puts(
        "glic-studio [image]\n"
        "  --open FILE        open an image at start\n"
        "  --preset FILE      load a preset (.json, or a PNG exported by glic-studio)\n"
        "  --spec \"k=v ...\"   initial encode settings (same keys as `glic encode --spec`)\n"
        "  --decode \"k=v ...\" unlink decoding and set these decode-side values\n"
        "  --view MODE        glitched | original | split | difference\n"
        "  --tab NAME         settings tab to show: encode | decode\n"
        "  --size WxH         initial window size in points\n"
        "  --capture OUT.png  screenshot the window once a full render is on screen, then exit\n"
        "  --export OUT.png   export the full render (preset embedded) once it is on screen, then exit");
}

bool parse(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&](std::string& dst) {
            if (i + 1 >= argc) return false;
            dst = argv[++i];
            return true;
        };
        std::string v;
        if (a == "--open") { if (!next(o.open)) return false; }
        else if (a == "--spec") { if (!next(o.spec)) return false; }
        else if (a == "--decode") { if (!next(o.decode)) return false; }
        else if (a == "--view") { if (!next(o.view)) return false; }
        else if (a == "--tab") { if (!next(o.tab)) return false; }
        else if (a == "--capture") { if (!next(o.capture)) return false; }
        else if (a == "--preset") { if (!next(o.preset)) return false; }
        else if (a == "--export") { if (!next(o.exportPng)) return false; }
        else if (a == "--size") {
            if (!next(v) || std::sscanf(v.c_str(), "%dx%d", &o.width, &o.height) != 2) return false;
        } else if (a == "-h" || a == "--help") { return false; }
        else if (!a.empty() && a[0] != '-') { o.open = a; }
        else return false;
    }
    return true;
}

// Where bundled presets live: $GLIC_STUDIO_PRESETS, next to an installed
// binary (../share/glic-studio/presets), or the source tree for dev builds.
std::vector<std::string> presetDirs(const char* argv0) {
    std::vector<std::string> dirs;
    if (const char* env = std::getenv("GLIC_STUDIO_PRESETS")) dirs.push_back(env);
    std::error_code ec;
    auto exe = std::filesystem::canonical("/proc/self/exe", ec);
    if (ec) exe = std::filesystem::absolute(argv0, ec);
    dirs.push_back((exe.parent_path().parent_path() / "share/glic-studio/presets").string());
    dirs.push_back(GLIC_STUDIO_SOURCE_DIR "/presets");
    return dirs;
}

void applyStyle(float scale) {
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    ImGui::StyleColorsDark();
    // Restrained: default dark style, square-ish, no decorative chrome.
    style.WindowRounding = 0;
    style.FrameRounding = 2;
    style.GrabRounding = 2;
    style.TabRounding = 2;
    style.WindowBorderSize = 1;
    style.FrameBorderSize = 0;
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
}

bool writeScreenshot(SDL_Window* window, const std::string& path) {
    int w, h;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    glic::Image img;
    img.width = w;
    img.height = h;
    img.pixels.resize(static_cast<size_t>(w) * h);
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    for (int y = 0; y < h; y++) {
        const uint8_t* row = rgba.data() + static_cast<size_t>(h - 1 - y) * w * 4;
        for (int x = 0; x < w; x++)
            img.pixels[static_cast<size_t>(y) * w + x] = glic::makeColor(row[x * 4], row[x * 4 + 1], row[x * 4 + 2]);
    }
    return glic::saveImage(path, img);
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parse(argc, argv, opt)) {
        usage();
        return 1;
    }

    studio::Platform platform;
    if (!platform.init("glic-studio")) return 1;
    if (opt.width > 0) SDL_SetWindowSize(platform.window(), opt.width, opt.height);
    std::fprintf(stderr, "glic-studio: video driver %s, content scale %.2f\n", platform.videoDriver().c_str(),
                 platform.contentScale());

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;  // layout persistence arrives in M8 (~/.config/glic-studio)
    io.Fonts->AddFontDefaultVector();
    float scale = platform.contentScale();
    applyStyle(scale);
    ImGui_ImplSDL3_InitForOpenGL(platform.window(), platform.glContext());
    ImGui_ImplOpenGL3_Init(platform.glslVersion());

    int exitCode = 0;
    {
        studio::App app(platform);
        app.setBundledPresetDirs(presetDirs(argv[0]));
        if (!opt.preset.empty()) {
            if (std::filesystem::path(opt.preset).extension() == ".json") {
                if (!app.loadPresetFile(opt.preset)) return 1;
            } else {
                auto bytes = glic::readFile(opt.preset);
                std::string err;
                auto p = bytes ? glic::preset::fromPng(*bytes, &err) : std::nullopt;
                if (!p) {
                    std::fprintf(stderr, "--preset: %s\n", err.c_str());
                    return 1;
                }
                app.applyPreset(*p, opt.preset);
            }
        }
        if (!opt.spec.empty()) {
            glic::EncodeSettings s;
            if (auto err = glic::applySpec(s, opt.spec); !err.empty()) {
                std::fprintf(stderr, "--spec: %s\n", err.c_str());
                return 1;
            }
            app.setSettings(s);
        }
        if (!opt.decode.empty()) {
            app.setDecodeLinked(false);
            if (auto err = glic::applySpec(app.editDecode(), opt.decode); !err.empty()) {
                std::fprintf(stderr, "--decode: %s\n", err.c_str());
                return 1;
            }
            app.decodeChannelsLinked() = false;
            app.settingsChanged();
        }
        if (opt.tab == "decode") app.selectTab("Decode");
        if (opt.view == "original") app.view().mode = studio::ViewMode::Original;
        else if (opt.view == "split") app.view().mode = studio::ViewMode::Split;
        else if (opt.view == "difference") app.view().mode = studio::ViewMode::Difference;
        if (!opt.open.empty()) app.openImage(opt.open);

        const auto started = std::chrono::steady_clock::now();
        int captureCountdown = -1;

        while (!app.quitRequested()) {
            int timeout = app.idleTimeoutMs();
            // Capture mode must keep drawing without input events.
            if ((!opt.capture.empty() || !opt.exportPng.empty()) && (timeout < 0 || timeout > 20)) timeout = 20;
            auto events = platform.waitEvents(timeout, [&](const SDL_Event& e) {
                ImGui_ImplSDL3_ProcessEvent(&e);
                if (e.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
                    scale = platform.contentScale();
                    applyStyle(scale);
                }
            });
            for (const auto& e : events) app.handle(e);

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            app.frame();
            ImGui::Render();
            glViewport(0, 0, static_cast<int>(io.DisplaySize.x * io.DisplayFramebufferScale.x),
                       static_cast<int>(io.DisplaySize.y * io.DisplayFramebufferScale.y));
            glClearColor(0.09f, 0.09f, 0.09f, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            if (!opt.exportPng.empty() && app.fullRenderShown()) {
                bool ok = app.exportTo(opt.exportPng);
                std::fprintf(stderr, "glic-studio: %s %s\n", ok ? "exported" : "FAILED to export", opt.exportPng.c_str());
                exitCode = ok ? 0 : 2;
                break;
            }
            if (!opt.capture.empty()) {
                if (captureCountdown < 0 && app.fullRenderShown()) captureCountdown = 3;  // let layout settle
                if (captureCountdown > 0 && --captureCountdown == 0) {
                    bool ok = writeScreenshot(platform.window(), opt.capture);
                    std::fprintf(stderr, "glic-studio: %s %s\n", ok ? "captured" : "FAILED to capture", opt.capture.c_str());
                    exitCode = ok ? 0 : 2;
                    break;
                }
                if (std::chrono::steady_clock::now() - started > std::chrono::seconds(60)) {
                    std::fprintf(stderr, "glic-studio: capture timed out\n");
                    exitCode = 3;
                    break;
                }
            }
            SDL_GL_SwapWindow(platform.window());
        }
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    platform.shutdown();
    return exitCode;
}
