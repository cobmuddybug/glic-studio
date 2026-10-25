#include "platform.hpp"

#include <cstdio>
#include <cstring>
#include <memory>

namespace studio {

namespace {

struct PendingPath {
    AppEventKind kind;
    std::string path;
};

struct DialogContext {
    Platform* platform;
    AppEventKind kind;
};

void SDLCALL dialogCallback(void* userdata, const char* const* filelist, int) {
    std::unique_ptr<DialogContext> ctx(static_cast<DialogContext*>(userdata));
    std::string path = (filelist && filelist[0]) ? filelist[0] : "";
    ctx->platform->postEvent(ctx->kind, path);
}

const SDL_DialogFileFilter kImageFilters[] = {
    {"Images and presets", "png;jpg;jpeg;bmp;tga;gif;psd;pnm;ppm;pgm;hdr;json"},
    {"Images", "png;jpg;jpeg;bmp;tga;gif;psd;pnm;ppm;pgm;hdr"},
    {"glic-studio presets", "json"},
    {"All files", "*"},
};
const SDL_DialogFileFilter kPresetFilter[] = {{"glic-studio preset", "json"}};
const SDL_DialogFileFilter kPngFilter[] = {{"PNG image", "png"}};

struct ClipboardPng {
    std::vector<uint8_t> bytes;
};

const void* SDLCALL clipboardData(void* userdata, const char* mime, size_t* size) {
    auto* data = static_cast<ClipboardPng*>(userdata);
    if (std::strcmp(mime, "image/png") != 0) {
        *size = 0;
        return nullptr;
    }
    *size = data->bytes.size();
    return data->bytes.data();
}

void SDLCALL clipboardCleanup(void* userdata) { delete static_cast<ClipboardPng*>(userdata); }

} // namespace

bool Platform::init(const char* title) {
    // Must be set before SDL_Init so the compositor (niri window rules) sees it.
    SDL_SetHint(SDL_HINT_APP_ID, "glic-studio");
    SDL_SetAppMetadata("glic-studio", "0.1.0", "glic-studio");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    userEvent_ = SDL_RegisterEvents(1);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    const float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    const SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
    window_ = SDL_CreateWindow(title, static_cast<int>(1280 * scale), static_cast<int>(800 * scale), flags);
    if (!window_) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }
    // No minimum above ~640x400 (design §2): niri sizes columns freely.
    SDL_SetWindowMinimumSize(window_, 320, 240);
    gl_ = SDL_GL_CreateContext(window_);
    if (!gl_) {
        std::fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError());
        return false;
    }
    SDL_GL_MakeCurrent(window_, gl_);
    SDL_GL_SetSwapInterval(1);
    SDL_ShowWindow(window_);
    return true;
}

void Platform::shutdown() {
    if (gl_) SDL_GL_DestroyContext(gl_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

float Platform::contentScale() const {
    float s = SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(window_));
    return s > 0 ? s : 1.0f;
}

std::string Platform::videoDriver() const {
    const char* d = SDL_GetCurrentVideoDriver();
    return d ? d : "?";
}

void Platform::showOpenImageDialog() {
    SDL_ShowOpenFileDialog(dialogCallback, new DialogContext{this, AppEventKind::OpenFile}, window_, kImageFilters,
                           4, nullptr, false);
}

void Platform::showSavePngDialog(const std::string& suggestedName) {
    SDL_ShowSaveFileDialog(dialogCallback, new DialogContext{this, AppEventKind::SaveFile}, window_, kPngFilter, 1,
                           suggestedName.empty() ? nullptr : suggestedName.c_str());
}

void Platform::showSavePresetDialog(const std::string& suggestedName) {
    SDL_ShowSaveFileDialog(dialogCallback, new DialogContext{this, AppEventKind::SavePreset}, window_, kPresetFilter, 1,
                           suggestedName.empty() ? nullptr : suggestedName.c_str());
}

bool Platform::copyPngToClipboard(std::vector<uint8_t> png) {
    static const char* mimes[] = {"image/png"};
    auto* data = new ClipboardPng{std::move(png)};
    return SDL_SetClipboardData(clipboardData, clipboardCleanup, data, mimes, 1);
}

void Platform::postEvent(AppEventKind kind, std::string path) {
    SDL_Event e{};
    e.type = userEvent_;
    e.user.code = static_cast<int>(kind);
    e.user.data1 = new std::string(std::move(path));
    SDL_PushEvent(&e);
}

std::vector<AppEvent> Platform::waitEvents(int timeoutMs, const std::function<void(const SDL_Event&)>& onSdl) {
    std::vector<AppEvent> out;
    SDL_Event e;
    bool got = timeoutMs == 0 ? SDL_PollEvent(&e) : SDL_WaitEventTimeout(&e, timeoutMs);
    while (got) {
        if (e.type == userEvent_) {
            std::unique_ptr<std::string> path(static_cast<std::string*>(e.user.data1));
            out.push_back({static_cast<AppEventKind>(e.user.code), *path});
        } else {
            onSdl(e);
            if (e.type == SDL_EVENT_QUIT ||
                (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(window_)))
                out.push_back({AppEventKind::Quit, {}});
            else if (e.type == SDL_EVENT_DROP_FILE && e.drop.data)
                out.push_back({AppEventKind::DropFile, e.drop.data});
        }
        got = SDL_PollEvent(&e);
    }
    return out;
}

} // namespace studio
