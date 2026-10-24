#pragma once

#include "model/preview_scheduler.hpp"
#include "platform/platform.hpp"
#include "render/texture.hpp"
#include "render/worker.hpp"

#include <glic/glic.hpp>

#include <future>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace studio {

enum class ViewMode { Glitched, Original, Split, Difference };
const char* viewModeName(ViewMode m);

struct CanvasView {
    ViewMode mode = ViewMode::Glitched;
    bool fit = true;
    float zoom = 1.0f;          // screen points per image pixel
    float panX = 0, panY = 0;   // offset of the image centre from the canvas centre, in points
    float split = 0.5f;         // split position as a fraction of image width
};

// What is currently shown as "glitched": the newest accepted render.
struct ShownRender {
    bool valid = false;
    bool proxy = false;
    int encodedWidth = 0, encodedHeight = 0;  // size of the image that was encoded
    double encodeMs = 0, decodeMs = 0;
    bool encodeReused = false;
    glic::Image image;                         // decoded (GLIC pads sides below 64)
    std::shared_ptr<const glic::GlicData> data;
};

class App {
public:
    explicit App(Platform& platform);
    ~App();

    void handle(const AppEvent& e);
    void frame();  // builds the whole UI for one frame
    // How long the main loop may sleep: 0 = render again now, -1 = until an event.
    int idleTimeoutMs();
    bool quitRequested() const { return quit_; }

    void openImage(const std::string& path);
    void setSettings(const glic::EncodeSettings& s);
    const glic::EncodeSettings& settings() const { return settings_; }

    // For --capture: true once a full-resolution render of the current
    // settings is on screen.
    bool fullRenderShown() const { return scheduler_.fullIsCurrent() && shown_.valid && !shown_.proxy; }
    CanvasView& view() { return view_; }
    void setStatus(std::string msg, bool error = false);

    // Settings panel access.
    glic::EncodeSettings& editSettings() { return settings_; }
    void settingsChanged();
    bool& linkChannels() { return linkChannels_; }

    // Decode tab (design §6.4). Linked = decode with the header.
    bool decodeLinked() const { return decodeLinked_; }
    void setDecodeLinked(bool linked);
    glic::EncodeSettings& editDecode() { return decodeSettings_; }
    bool& decodeChannelsLinked() { return decodeChannelsLinked_; }
    // The decode-side values the encoder writes for the current settings.
    glic::EncodeSettings headerSettings() const;

    // Select a settings tab on the next frame ("Encode", "Decode").
    void selectTab(std::string tab) { selectTab_ = std::move(tab); }
    std::string takeTabSelection() { return std::exchange(selectTab_, {}); }

private:
    void pumpRender();
    void acceptResult(RenderResult r);
    void buildLayout(bool force);
    void menuBar();
    void statusBar(float height);
    void runAction(int action);
    void requestExport();
    void finishExport();
    void copyToClipboard();
    glic::Image croppedShown() const;
    double nowMs() const;

    Platform& platform_;
    RenderWorker worker_;
    PreviewScheduler scheduler_;

    glic::EncodeSettings settings_;
    bool linkChannels_ = true;
    bool decodeLinked_ = true;
    glic::EncodeSettings decodeSettings_;  // decode-side values only (used when unlinked)
    bool decodeChannelsLinked_ = true;
    int proxyEdge_ = 512;

    std::shared_ptr<const glic::Image> source_;
    std::string sourcePath_;
    std::future<std::optional<glic::Image>> loading_;
    std::string loadingPath_;

    ShownRender shown_;
    Texture originalTex_, glitchedTex_, diffTex_;
    bool diffDirty_ = true;
    CanvasView view_;

    std::string exportPath_;  // set while an export waits for its full render
    std::string status_;
    bool statusError_ = false;
    double statusAt_ = 0;

    bool layoutBuilt_ = false;
    bool layoutNarrow_ = false;
    bool autoArrange_ = true;
    int framesToRender_ = 3;
    bool quit_ = false;
    std::string selectTab_;
};

} // namespace studio
