#pragma once

#include "model/config.hpp"
#include "model/explore.hpp"
#include "model/history.hpp"
#include "model/preview_scheduler.hpp"
#include "render/overlay.hpp"
#include "platform/platform.hpp"
#include "render/texture.hpp"
#include "render/animation_export.hpp"
#include "render/batch.hpp"
#include "render/worker.hpp"

#include "preset.hpp"

#include <glic/glic.hpp>

#include <future>
#include <map>
#include <vector>
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
    std::shared_ptr<const glic::Capture> capture;  // overlay data, if it was asked for
    bool corrupted = false;                        // decoded from corrupted bytes
    glic::CorruptionStats corruption;
};

// The mutate grid (design §M6): nine small renders, each a perturbation of
// the preset it was made from.
struct MutateGrid {
    static constexpr int kCells = 9;
    static constexpr int kCellEdge = 256;  // longest edge of the cell renders
    bool open = false;
    float strength = 0.25f;
    uint64_t batch = 0;
    std::shared_ptr<const glic::Image> image;  // source downscaled for the cells
    std::vector<glic::preset::Preset> presets;
    std::array<Texture, kCells> textures;
    std::array<int, kCells> state{};           // 0 rendering, 1 ready, 2 failed
    std::array<std::string, kCells> errors;
    int cellWidth = 0, cellHeight = 0;         // useful size inside the (padded) renders
};

// The Animate tab (design §M7).
struct AnimationState {
    AnimationSpec spec;
    AnimFormat format = AnimFormat::Mp4;
    int fps = 24;
    int maxEdge = 0;                     // 0 = full size
    std::optional<std::string> ffmpeg;   // found on PATH at startup
    // Preview of one frame, rendered off to the side (never touches the settings).
    int previewFrame = 0;
    uint64_t previewBatch = 0;
    bool previewReady = false;
    std::string previewError;
    Texture previewTex;
    int previewWidth = 0, previewHeight = 0;
    AnimationProgress lastProgress;
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
    // Opens a path: a preset (.json), a PNG with an embedded preset (asks), or an image.
    void openPath(const std::string& path);

    // Presets (design §5). The current state as a preset, and back.
    glic::preset::Preset currentPreset() const;
    void applyPreset(const glic::preset::Preset& p, const std::string& origin);
    bool loadPresetFile(const std::string& path);
    void setBundledPresetDirs(std::vector<std::string> dirs);
    // Writes the full render with the preset embedded; for --export.
    bool exportTo(const std::string& path);
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

    // Corrupt tab (design §M5). Call settingsChanged() after editing.
    glic::CorruptionSettings& editCorruption() { return corruption_; }
    // A new seed derived from the current one (N): deterministic, no clock.
    void rerollCorruptionSeed();
    void setCorruptionEnabled(bool on);
    // Layout of the shown (clean) encoded data, for the byte map.
    const glic::GlicLayout* shownLayout() const { return shown_.data ? &shownLayout_ : nullptr; }

    // Overlays (design §M4). Call overlaysChanged() after editing them.
    OverlaySettings& overlays() { return overlays_; }
    void overlaysChanged();
    const ShownRender& shown() const { return shown_; }
    // The colour space the shown image was decoded with (for channel names).
    glic::ColorSpace decodedColorSpace() const;
    int64_t coefficientPeak() const { return coefficientPeak_; }
    const std::array<double, glic::kPredictionCount>& predictorShares() const { return predictorShares_; }

    // Exploration (design §M6).
    // Applies a preset quietly (undo, history, mutate); `label` names the
    // history node the next settled render records.
    void restore(const glic::preset::Preset& p, std::string label);
    void undo();
    void redo();
    void jumpHistory(int node);
    void randomizeAll();
    const History& history() const { return history_; }
    const Texture* historyTexture(int node) const;
    MutateGrid& mutateGrid() { return grid_; }
    void openMutateGrid(bool open);
    void regenerateMutations();
    void adoptMutation(int cell);
    bool hasSource() const { return source_ != nullptr; }

    // Persistence (design §M8). useConfig() applies a loaded config (and the
    // last preset if `restorePreset`); config() is the state to save on exit.
    void useConfig(StudioConfig config, bool restorePreset);
    StudioConfig config() const;

    AnimationState& animation() { return anim_; }
    void requestAnimationPreview();       // renders anim_.previewFrame
    void requestAnimationExport();        // asks where to save, then exports
    void cancelAnimationExport() { exporter_.cancel(); }
    bool exporting() { return exporter_.running(); }
    // For --actions: runs "randomize", "undo", "redo", "mutate", "adopt:N" or
    // "export-anim:PATH" (the Animate tab's settings; .mp4, .gif, else a folder);
    // false for an unknown name. scriptIdle(): the full render and any grid
    // cells are done, so the next action may run.
    bool runNamedAction(const std::string& name);
    bool scriptIdle() const;

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
    bool finishExport();
    void savePreset(const std::string& path);
    void presetsMenu();
    void embeddedPresetBar();
    void copyToClipboard();
    void refreshOverlayTextures();
    void recordHistory();
    void pumpBatch();
    void pumpAnimation();
    void startAnimationExport(const std::string& path);
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
    glic::CorruptionSettings corruption_;
    glic::GlicLayout shownLayout_;

    std::shared_ptr<const glic::Image> source_;
    std::string sourcePath_;
    std::future<std::optional<glic::Image>> loading_;
    std::string loadingPath_;

    ShownRender shown_;
    Texture originalTex_, glitchedTex_, diffTex_;
    bool diffDirty_ = true;
    CanvasView view_;

    OverlaySettings overlays_;
    Texture heatmapTex_, coefficientTex_;
    bool overlayDirty_ = true;
    int64_t coefficientPeak_ = 0;
    std::array<double, glic::kPredictionCount> predictorShares_{};

    History history_;
    std::map<int, std::unique_ptr<Texture>> historyTex_;
    std::string historyLabel_ = "open";
    ExploreRng exploreRng_;
    MutateGrid grid_;
    BatchRenderer batch_;
    AnimationState anim_;
    StudioConfig config_;
    BatchRenderer animPreview_;
    AnimationExporter exporter_;

    std::string exportPath_;  // set while an export waits for its full render

    // Preset identity: name and the loaded document (unknown fields survive a save).
    std::string presetName_;
    nlohmann::json presetDocument_ = nlohmann::json::object();
    struct BundledPreset {
        std::string name, path;
    };
    std::vector<std::pair<std::string, std::vector<BundledPreset>>> bundled_;  // (group, presets)
    // A dropped PNG that carries a preset: ask what to do with it.
    std::optional<glic::preset::Preset> pendingEmbedded_;
    std::string pendingEmbeddedPath_;
    std::string status_;
    bool statusError_ = false;
    double statusAt_ = 0;

    bool layoutBuilt_ = false;
    bool layoutNarrow_ = false;
    bool autoArrange_ = true;
    bool restoreLastPreset_ = false;
    int framesToRender_ = 3;
    bool quit_ = false;
    std::string selectTab_;
};

} // namespace studio
