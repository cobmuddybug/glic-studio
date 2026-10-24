#pragma once

// SDL3 window, OpenGL context, file dialogs (via xdg-desktop-portal on
// Wayland), drag-and-drop and clipboard. Everything asynchronous arrives back
// on the main thread as an AppEvent through pollEvents().

#include <SDL3/SDL.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace studio {

enum class AppEventKind { Quit, OpenFile, SaveFile, SavePreset, DropFile, RenderDone, Wake };

struct AppEvent {
    AppEventKind kind;
    std::string path;  // OpenFile/SaveFile/DropFile; empty if the dialog was cancelled
};

class Platform {
public:
    bool init(const char* title);
    void shutdown();

    SDL_Window* window() const { return window_; }
    SDL_GLContext glContext() const { return gl_; }
    const char* glslVersion() const { return "#version 330 core"; }

    // Content scale of the display the window is on (1.0 = 100%).
    float contentScale() const;
    std::string videoDriver() const;

    // Asynchronous dialogs; the result comes back as an AppEvent.
    void showOpenImageDialog();
    void showSavePngDialog(const std::string& suggestedName);

    // Places a PNG on the clipboard (image/png).
    bool copyPngToClipboard(std::vector<uint8_t> png);

    // Thread-safe: wakes the main loop with an event (e.g. from the render worker).
    void postEvent(AppEventKind kind, std::string path = {});

    // Waits up to timeoutMs (0 = poll, -1 = forever) for events. SDL events are
    // passed to `onSdl` (for ImGui); app-level events are returned.
    std::vector<AppEvent> waitEvents(int timeoutMs, const std::function<void(const SDL_Event&)>& onSdl);

private:
    SDL_Window* window_ = nullptr;
    SDL_GLContext gl_ = nullptr;
    uint32_t userEvent_ = 0;
};

} // namespace studio
