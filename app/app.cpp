#include "app.hpp"

#include "model/params.hpp"
#include "spec.hpp"
#include "render/resize.hpp"
#include "ui/canvas.hpp"
#include "ui/corrupt_tab.hpp"
#include "ui/animate_tab.hpp"
#include "ui/explore_ui.hpp"
#include "ui/overlay_panel.hpp"
#include "ui/keybindings.hpp"
#include "ui/settings_panel.hpp"

#include <glic/image_io.hpp>

#include "png_meta.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <filesystem>

namespace studio {

namespace {

constexpr const char* kCanvasWindow = "Canvas";
constexpr const char* kSettingsWindow = "Settings";
constexpr const char* kHistoryWindow = "History";
constexpr int kThumbnailEdge = 160;

} // namespace

const char* viewModeName(ViewMode m) {
    switch (m) {
    case ViewMode::Glitched: return "Glitched";
    case ViewMode::Original: return "Original";
    case ViewMode::Split: return "Split";
    case ViewMode::Difference: return "Difference";
    }
    return "";
}

App::App(Platform& platform)
    : platform_(platform),
      worker_([&platform] { platform.postEvent(AppEventKind::RenderDone); }),
      batch_(0, [&platform] { platform.postEvent(AppEventKind::RenderDone); }),
      animPreview_(1, [&platform] { platform.postEvent(AppEventKind::RenderDone); }) {
    anim_.ffmpeg = findFfmpeg();
    if (!anim_.ffmpeg) anim_.format = AnimFormat::PngFrames;
    linkChannels_ = channelsIdentical(settings_);
}

App::~App() = default;

double App::nowMs() const {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

void App::setStatus(std::string msg, bool error) {
    status_ = std::move(msg);
    statusError_ = error;
    statusAt_ = nowMs();
}

void App::handle(const AppEvent& e) {
    framesToRender_ = std::max(framesToRender_, 2);
    switch (e.kind) {
    case AppEventKind::Quit: quit_ = true; break;
    case AppEventKind::OpenFile:
    case AppEventKind::DropFile:
        if (!e.path.empty()) openPath(e.path);
        break;
    case AppEventKind::SavePreset:
        if (!e.path.empty()) savePreset(e.path);
        break;
    case AppEventKind::SaveAnimation:
        if (!e.path.empty()) startAnimationExport(e.path);
        break;
    case AppEventKind::SaveFile:
        if (e.path.empty()) break;
        exportPath_ = e.path;
        if (std::filesystem::path(exportPath_).extension().empty()) exportPath_ += ".png";
        if (fullRenderShown()) finishExport();
        else setStatus("Rendering full resolution for export...");
        break;
    default: break;
    }
}

void App::openImage(const std::string& path) {
    loadingPath_ = path;
    loading_ = std::async(std::launch::async, [path] { return glic::loadImage(path); });
    setStatus("Loading " + std::filesystem::path(path).filename().string() + "...");
}

void App::openPath(const std::string& path) {
    namespace fs = std::filesystem;
    std::string ext = fs::path(path).extension().string();
    for (auto& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (ext == ".json") {
        loadPresetFile(path);
        return;
    }
    if (ext == ".png") {
        if (auto bytes = glic::readFile(path); bytes && glic::png::readText(*bytes, glic::preset::kPngKeyword)) {
            std::string err;
            if (auto p = glic::preset::fromPng(*bytes, &err)) {
                pendingEmbedded_ = std::move(*p);
                pendingEmbeddedPath_ = path;
                return;  // the canvas shows a bar asking what to do
            }
            setStatus("Embedded preset unreadable (" + err + "); opening as an image", true);
        }
    }
    openImage(path);
}

glic::preset::Preset App::currentPreset() const {
    glic::preset::Preset p;
    p.name = presetName_;
    p.document = presetDocument_;
    p.encode = settings_;
    p.decodeLinked = decodeLinked_;
    if (decodeLinked_) {
        p.decode = settings_;
        for (auto& ch : p.decode.channels)
            if (static_cast<int>(ch.prediction) < 0) ch.prediction = glic::Prediction::NONE;
    } else {
        p.decode = decodeSettings_;
    }
    p.decode.seed = settings_.seed;
    p.corruption = corruption_;
    return p;
}

void App::applyPreset(const glic::preset::Preset& p, const std::string& origin) {
    presetName_ = p.name;
    presetDocument_ = p.document;
    settings_ = p.encode;
    linkChannels_ = channelsIdentical(settings_);
    decodeLinked_ = p.decodeLinked;
    decodeSettings_ = p.decode;
    decodeChannelsLinked_ = channelsIdentical(decodeSettings_);
    corruption_ = p.corruption;
    setStatus("Preset " + (p.name.empty() ? origin : "\"" + p.name + "\"") + " loaded");
    settingsChanged();
}

bool App::loadPresetFile(const std::string& path) {
    std::string err;
    auto p = glic::preset::load(path, &err);
    if (!p) {
        setStatus("Could not load preset: " + err, true);
        return false;
    }
    if (p->name.empty()) p->name = std::filesystem::path(path).stem().string();
    applyPreset(*p, std::filesystem::path(path).filename().string());
    return true;
}

void App::savePreset(const std::string& path) {
    std::string target = path;
    if (std::filesystem::path(target).extension().empty()) target += ".json";
    glic::preset::Preset p = currentPreset();
    if (p.name.empty()) p.name = std::filesystem::path(target).stem().string();
    if (glic::preset::save(target, p)) {
        presetName_ = p.name;
        setStatus("Saved preset " + std::filesystem::path(target).filename().string());
    } else {
        setStatus("Could not write " + target, true);
    }
}

void App::setBundledPresetDirs(std::vector<std::string> dirs) {
    namespace fs = std::filesystem;
    bundled_.clear();
    for (const auto& dir : dirs) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        std::vector<std::pair<std::string, std::vector<BundledPreset>>> groups;
        for (const auto& sub : fs::directory_iterator(dir, ec)) {
            if (!sub.is_directory()) continue;
            std::vector<BundledPreset> items;
            for (const auto& f : fs::directory_iterator(sub.path(), ec))
                if (f.path().extension() == ".json") items.push_back({f.path().stem().string(), f.path().string()});
            std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
            if (!items.empty()) groups.emplace_back(sub.path().filename().string(), std::move(items));
        }
        std::sort(groups.begin(), groups.end(), [](const auto& a, const auto& b) {
            // Hand-made examples first, then GLIC's originals.
            auto rank = [](const std::string& g) { return g == "examples" ? 0 : g == "glic" ? 1 : 2; };
            return rank(a.first) != rank(b.first) ? rank(a.first) < rank(b.first) : a.first < b.first;
        });
        if (!groups.empty()) {
            bundled_ = std::move(groups);
            return;  // first directory that has presets wins
        }
    }
}

void App::presetsMenu() {
    if (!ImGui::BeginMenu("Presets")) return;
    if (ImGui::MenuItem("Open preset...", shortcutLabel(Action::OpenImage))) platform_.showOpenImageDialog();
    if (ImGui::MenuItem("Save preset...", shortcutLabel(Action::SavePreset))) runAction(static_cast<int>(Action::SavePreset));
    if (!bundled_.empty()) ImGui::Separator();
    for (const auto& [group, items] : bundled_) {
        const char* label = group == "examples" ? "Examples" : group == "glic" ? "GLIC originals" : group.c_str();
        if (ImGui::BeginMenu(label)) {
            for (const auto& item : items)
                if (ImGui::MenuItem(item.name.c_str())) loadPresetFile(item.path);
            ImGui::EndMenu();
        }
    }
    if (bundled_.empty()) ImGui::TextDisabled("(bundled presets not found)");
    ImGui::Separator();
    ImGui::MenuItem("Restore last settings on launch", nullptr, &restoreLastPreset_);
    ImGui::SetItemTooltip("Off: every launch starts from the default settings");
    ImGui::EndMenu();
}

void App::embeddedPresetBar() {
    if (!pendingEmbedded_) return;
    const std::string file = std::filesystem::path(pendingEmbeddedPath_).filename().string();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    ImGui::BeginChild("##embedded", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s contains a glic-studio preset%s%s%s.", file.c_str(), pendingEmbedded_->name.empty() ? "" : " (\"",
                pendingEmbedded_->name.c_str(), pendingEmbedded_->name.empty() ? "" : "\")");
    if (ImGui::Button("Load settings")) {
        applyPreset(*pendingEmbedded_, file);
        pendingEmbedded_.reset();
    }
    ImGui::SameLine();
    if (ImGui::Button("Load as source image")) {
        openImage(pendingEmbeddedPath_);
        pendingEmbedded_.reset();
    }
    ImGui::SameLine();
    if (ImGui::Button("Dismiss")) pendingEmbedded_.reset();
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

bool App::exportTo(const std::string& path) {
    if (!fullRenderShown()) return false;
    exportPath_ = path;
    return finishExport();
}

void App::setSettings(const glic::EncodeSettings& s) {
    settings_ = s;
    linkChannels_ = channelsIdentical(settings_);
    settingsChanged();
}

void App::settingsChanged() {
    framesToRender_ = std::max(framesToRender_, 2);
    if (!source_) return;
    const bool fits = std::max(source_->width, source_->height) <= proxyEdge_;
    scheduler_.changed(nowMs(), fits);
}

glic::EncodeSettings App::headerSettings() const {
    // What decodeParamsFor() would write, except that random choices are only
    // known after encoding: take those from the latest encoded header.
    glic::DecodeParams params = glic::decodeParamsFor(settings_);
    bool random = false;
    for (const auto& ch : settings_.channels)
        random |= ch.wavelet == glic::kWaveletRandom || ch.transformType == glic::TransformType::RANDOM;
    if (random && shown_.data) params = shown_.data->layout().params;
    glic::EncodeSettings s = glic::settingsFromHeader(params);
    s.seed = settings_.seed;
    return s;
}

void App::setDecodeLinked(bool linked) {
    if (linked == decodeLinked_) return;
    decodeLinked_ = linked;
    if (!linked) {
        // Start from exactly what linked decoding uses, so unlinking alone changes nothing.
        decodeSettings_ = headerSettings();
        decodeChannelsLinked_ = channelsIdentical(decodeSettings_);
        setStatus("Decode unlinked: mismatched decoding");
    } else {
        setStatus("Decode linked to encode");
    }
    settingsChanged();
}

// ------------------------------------------------------------ exploration

void App::restore(const glic::preset::Preset& p, std::string label) {
    presetName_ = p.name;
    presetDocument_ = p.document;
    settings_ = p.encode;
    linkChannels_ = channelsIdentical(settings_);
    decodeLinked_ = p.decodeLinked;
    decodeSettings_ = p.decode;
    decodeChannelsLinked_ = channelsIdentical(decodeSettings_);
    corruption_ = p.corruption;
    historyLabel_ = std::move(label);
    settingsChanged();
}

void App::recordHistory() {
    const bool added = history_.record(currentPreset(), historyLabel_);
    historyLabel_ = "edit";
    const int cur = history_.current();
    if (cur < 0 || (!added && !history_.node(cur).thumbnail.empty())) return;
    glic::Image thumb = downscaleToFit(croppedShown(), kThumbnailEdge);
    auto& tex = historyTex_[cur];
    if (!tex) tex = std::make_unique<Texture>();
    tex->upload(thumb);
    history_.setThumbnail(cur, std::move(thumb));
}

const Texture* App::historyTexture(int node) const {
    auto it = historyTex_.find(node);
    return it != historyTex_.end() && it->second && it->second->valid() ? it->second.get() : nullptr;
}

void App::undo() {
    if (history_.empty()) return;
    // Unsettled edits since the last recorded state: go back to that state first.
    const auto& here = history_.node(history_.current()).preset;
    if (!here.sameSettings(currentPreset())) {
        restore(here, "undo");
        setStatus("Undo");
        return;
    }
    if (const auto* p = history_.undo()) {
        restore(*p, "undo");
        setStatus("Undo");
    } else {
        setStatus("Nothing to undo");
    }
}

void App::redo() {
    if (const auto* p = history_.redo()) {
        restore(*p, "redo");
        setStatus("Redo");
    } else {
        setStatus("Nothing to redo");
    }
}

void App::jumpHistory(int node) {
    if (const auto* p = history_.jumpTo(node)) restore(*p, "history");
}

void App::randomizeAll() {
    glic::preset::Preset p = currentPreset();
    randomize(p, exploreRng_);
    p.name.clear();
    restore(p, "randomize");
    setStatus("Randomized");
}

void App::openMutateGrid(bool open) {
    if (open && !source_) {
        setStatus("Open an image first", true);
        return;
    }
    grid_.open = open;
    framesToRender_ = std::max(framesToRender_, 2);
    if (!open) {
        batch_.cancel();
        return;
    }
    grid_.image = std::make_shared<const glic::Image>(downscaleToFit(*source_, MutateGrid::kCellEdge));
    grid_.cellWidth = grid_.image->width;
    grid_.cellHeight = grid_.image->height;
    regenerateMutations();
}

void App::regenerateMutations() {
    if (!grid_.open || !grid_.image) return;
    const glic::preset::Preset base = currentPreset();
    grid_.presets.clear();
    for (int i = 0; i < MutateGrid::kCells; i++) {
        grid_.presets.push_back(mutate(base, grid_.strength, exploreRng_));
        grid_.presets.back().name.clear();
        grid_.state[static_cast<size_t>(i)] = 0;
        grid_.errors[static_cast<size_t>(i)].clear();
    }
    batch_.submit(++grid_.batch, grid_.image, grid_.presets);
}

void App::adoptMutation(int cell) {
    if (cell < 0 || cell >= static_cast<int>(grid_.presets.size())) return;
    restore(grid_.presets[static_cast<size_t>(cell)], "mutate");
    setStatus("Adopted mutation " + std::to_string(cell + 1));
    regenerateMutations();  // keep exploring from the adopted preset
}

// ------------------------------------------------------------ animation

void App::requestAnimationPreview() {
    if (!source_) return;
    auto& a = anim_;
    a.previewFrame = std::clamp(a.previewFrame, 0, std::max(0, a.spec.frames - 1));
    auto img = std::make_shared<const glic::Image>(downscaleToFit(*source_, MutateGrid::kCellEdge));
    a.previewWidth = img->width;
    a.previewHeight = img->height;
    a.previewReady = false;
    a.previewError.clear();
    animPreview_.submit(++a.previewBatch, img, {framePreset(currentPreset(), a.spec, a.previewFrame)});
}

void App::requestAnimationExport() {
    if (!source_) {
        setStatus("Open an image first", true);
        return;
    }
    if (exporter_.running()) {
        setStatus("An animation export is already running", true);
        return;
    }
    const std::string stem = std::filesystem::path(sourcePath_).stem().string() + "-glic";
    switch (anim_.format) {
    case AnimFormat::Mp4: platform_.showSaveAnimationDialog(stem + ".mp4", false, false); break;
    case AnimFormat::Gif: platform_.showSaveAnimationDialog(stem + ".gif", true, false); break;
    case AnimFormat::PngFrames: platform_.showSaveAnimationDialog({}, false, true); break;
    }
}

void App::startAnimationExport(const std::string& path) {
    if (!source_) return;
    AnimationJob job;
    job.source = source_;
    job.base = currentPreset();
    job.spec = anim_.spec;
    job.maxEdge = anim_.maxEdge;
    job.fps = anim_.fps;
    job.format = anim_.format;
    job.output = path;
    if (anim_.format != AnimFormat::PngFrames) {
        const char* ext = anim_.format == AnimFormat::Gif ? ".gif" : ".mp4";
        if (std::filesystem::path(job.output).extension().empty()) job.output += ext;
        job.ffmpeg = anim_.ffmpeg.value_or("ffmpeg");
    }
    if (exporter_.start(std::move(job))) setStatus("Exporting animation...");
    else setStatus("An animation export is already running", true);
    framesToRender_ = std::max(framesToRender_, 2);
}

void App::pumpAnimation() {
    for (auto& r : animPreview_.poll()) {
        if (r.batch != anim_.previewBatch) continue;
        if (r.ok) anim_.previewTex.upload(r.image);
        else anim_.previewError = r.error;
        anim_.previewReady = r.ok;
    }
    const AnimationProgress p = exporter_.progress();
    if (p.done && !anim_.lastProgress.done) setStatus(p.message, !p.ok && !p.cancelled);
    anim_.lastProgress = p;
}

// ------------------------------------------------------------ persistence

void App::useConfig(StudioConfig config, bool restorePreset) {
    config_ = std::move(config);
    proxyEdge_ = config_.proxyEdge;
    autoArrange_ = config_.autoArrange;
    restoreLastPreset_ = config_.restoreLastPreset;
    if (restorePreset && restoreLastPreset_ && config_.lastPreset) restore(*config_.lastPreset, "open");
}

StudioConfig App::config() const {
    StudioConfig c = config_;
    c.lastPreset = currentPreset();
    c.proxyEdge = proxyEdge_;
    c.autoArrange = autoArrange_;
    c.restoreLastPreset = restoreLastPreset_;
    return c;
}

bool App::runNamedAction(const std::string& name) {
    if (name == "randomize") randomizeAll();
    else if (name == "undo") undo();
    else if (name == "redo") redo();
    else if (name == "mutate") openMutateGrid(!grid_.open);
    else if (name.rfind("adopt:", 0) == 0) adoptMutation(std::atoi(name.c_str() + 6) - 1);
    else if (name.rfind("export-anim:", 0) == 0) {
        const std::string path = name.substr(12);
        const std::string ext = std::filesystem::path(path).extension().string();
        anim_.format = ext == ".gif" ? AnimFormat::Gif : ext == ".mp4" ? AnimFormat::Mp4 : AnimFormat::PngFrames;
        startAnimationExport(path);
    }
    else return false;
    return true;
}

bool App::scriptIdle() const {
    if (!fullRenderShown() || anim_.lastProgress.running) return false;
    if (grid_.open)
        for (int s : grid_.state)
            if (s == 0) return false;
    return true;
}

void App::pumpBatch() {
    for (auto& r : batch_.poll()) {
        if (r.batch != grid_.batch || r.index < 0 || r.index >= MutateGrid::kCells) continue;
        const auto i = static_cast<size_t>(r.index);
        if (r.ok) {
            grid_.textures[i].upload(r.image);
            grid_.state[i] = 1;
        } else {
            grid_.state[i] = 2;
            grid_.errors[i] = r.error;
        }
    }
}

void App::setCorruptionEnabled(bool on) {
    if (on == corruption_.enabled) return;
    corruption_.enabled = on;
    setStatus(on ? "Corruption on" : "Corruption off");
    settingsChanged();
}

void App::rerollCorruptionSeed() {
    // splitmix64 of the current seed: a fresh-looking seed, still reproducible.
    uint64_t z = static_cast<uint64_t>(corruption_.seed) + 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    corruption_.seed = static_cast<int64_t>(z >> 1);  // keep it positive for readable JSON
    setStatus(corruption_.enabled ? "New corruption seed" : "New corruption seed (corruption is off: C)");
    settingsChanged();
}

void App::overlaysChanged() {
    overlayDirty_ = true;
    framesToRender_ = std::max(framesToRender_, 2);
    // Data is only captured while an overlay needs it: render again (decode
    // only, the encode is cached) if the shown render lacks it.
    const glic::Capture* c = shown_.capture.get();
    const bool missing = (overlays_.needsSegments() && !(c && c->segments)) ||
                         (overlays_.needsValues() && !(c && c->values));
    if (missing) settingsChanged();
}

glic::ColorSpace App::decodedColorSpace() const {
    if (shown_.data) return static_cast<glic::ColorSpace>(shown_.data->layout().params.colorSpace);
    return settings_.colorSpace;
}

void App::refreshOverlayTextures() {
    if (!overlayDirty_) return;
    overlayDirty_ = false;
    const glic::Capture* c = shown_.capture.get();
    const int ch = overlays_.channel;
    if (c && overlays_.needsSegments() && c->segments) predictorShares_ = studio::predictorShares(*c, ch);
    if (c && overlays_.panel && overlays_.predictors && c->segments)
        heatmapTex_.upload(predictorHeatmap(*c, ch, overlays_.heatmapOpacity));
    else
        heatmapTex_.clear();
    if (c && overlays_.needsValues() && c->values)
        coefficientTex_.upload(coefficientImage(*c, ch, &coefficientPeak_));
    else
        coefficientTex_.clear();
}

void App::pumpRender() {
    for (auto& r : worker_.poll()) {
        const bool show = scheduler_.finished(r.generation, r.cancelled);
        if (!show) continue;
        if (r.ok) {
            acceptResult(std::move(r));
        } else {
            // Keep the last good render on screen (design §M5).
            std::string why = r.error;
            if (why.rfind("Decoding failed: ", 0) == 0) why = why.substr(17);
            setStatus((r.decodeFailed ? "Decode failed: " : "Render failed: ") + why + " (showing the last good render)", true);
        }
    }
    if (!source_) return;
    while (auto job = scheduler_.next(nowMs())) {
        RenderRequest req;
        req.generation = job->ticket;
        req.source = source_;
        req.encode = settings_;
        req.decode = glic::preset::decodeSettings(currentPreset());
        req.proxy = job->proxy;
        req.proxyEdge = proxyEdge_;
        req.captureSegments = overlays_.needsSegments();
        req.captureValues = overlays_.needsValues();
        req.corruption = corruption_;
        worker_.submit(std::move(req));
    }
}

void App::acceptResult(RenderResult r) {
    shown_.valid = true;
    shown_.proxy = r.proxy;
    shown_.encodedWidth = r.sourceWidth;
    shown_.encodedHeight = r.sourceHeight;
    shown_.encodeMs = r.encodeMs;
    shown_.decodeMs = r.decodeMs;
    shown_.encodeReused = r.encodeReused;
    shown_.image = std::move(r.image);
    shown_.data = std::move(r.data);
    shown_.capture = std::move(r.capture);
    shown_.corrupted = r.corruption.fileSize > 0;  // stats exist only for corrupted renders
    shown_.corruption = std::move(r.corruption);
    if (shown_.data) shownLayout_ = shown_.data->layout();
    glitchedTex_.upload(shown_.image);
    diffDirty_ = true;
    overlayDirty_ = true;
    if (statusError_) setStatus("");
    if (!shown_.proxy && scheduler_.fullIsCurrent()) recordHistory();
    if (!shown_.proxy && !exportPath_.empty() && scheduler_.fullIsCurrent()) finishExport();
}

glic::Image App::croppedShown() const { return cropTo(shown_.image, shown_.encodedWidth, shown_.encodedHeight); }

void App::requestExport() {
    if (!source_) {
        setStatus("Nothing to export: open an image first", true);
        return;
    }
    std::string stem = std::filesystem::path(sourcePath_).stem().string();
    platform_.showSavePngDialog(stem + "-glic.png");
}

bool App::finishExport() {
    // Exports what the canvas shows: the full-resolution render, cropped to the
    // source size (GLIC's decoder pads sides below 64 px).
    const bool ok = glic::writeFile(exportPath_, glic::preset::exportPng(croppedShown(), currentPreset()));
    if (ok)
        setStatus("Exported " + std::filesystem::path(exportPath_).filename().string());
    else
        setStatus("Export failed: could not write " + exportPath_, true);
    exportPath_.clear();
    return ok;
}

void App::copyToClipboard() {
    if (!shown_.valid) {
        setStatus("Nothing to copy yet", true);
        return;
    }
    if (platform_.copyPngToClipboard(glic::encodePng(croppedShown())))
        setStatus(shown_.proxy ? "Copied (preview resolution)" : "Copied to clipboard");
    else
        setStatus("Copy failed", true);
}

void App::runAction(int a) {
    const float fb = ImGui::GetIO().DisplayFramebufferScale.x;
    switch (static_cast<Action>(a)) {
    case Action::OpenImage: platform_.showOpenImageDialog(); break;
    case Action::ExportPng: requestExport(); break;
    case Action::CopyImage: copyToClipboard(); break;
    case Action::CycleView: view_.mode = static_cast<ViewMode>((static_cast<int>(view_.mode) + 1) % 4); break;
    case Action::ZoomIn: zoomBy(view_, 1.25f); break;
    case Action::ZoomOut: zoomBy(view_, 0.8f); break;
    case Action::ZoomFit: view_.fit = true; break;
    case Action::Zoom100: zoomTo100(view_, fb); break;
    case Action::ToggleDecodeLink: setDecodeLinked(!decodeLinked_); break;
    case Action::Undo: undo(); break;
    case Action::Redo: redo(); break;
    case Action::Randomize: randomizeAll(); break;
    case Action::MutateGrid: openMutateGrid(!grid_.open); break;
    case Action::ToggleCorruption: setCorruptionEnabled(!corruption_.enabled); break;
    case Action::RerollSeed: rerollCorruptionSeed(); break;
    case Action::ToggleOverlays:
        overlays_.panel = !overlays_.panel;
        overlaysChanged();
        break;
    case Action::SavePreset: {
        std::string stem = presetName_.empty() ? std::filesystem::path(sourcePath_).stem().string() : presetName_;
        platform_.showSavePresetDialog((stem.empty() ? "preset" : stem) + ".json");
        break;
    }
    }
}

int App::idleTimeoutMs() {
    if (framesToRender_ > 0) return 0;
    if (loading_.valid()) return 30;
    const double wake = scheduler_.wakeIn(nowMs());
    if (wake >= 0) return std::max(1, static_cast<int>(std::ceil(wake)));
    if (anim_.lastProgress.running) return 100;  // export progress
    if (ImGui::GetIO().WantTextInput) return 500;  // caret blink
    return -1;
}

void App::buildLayout(bool force) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float scale = ImGui::GetStyle().FontScaleDpi;
    const bool narrow = vp->WorkSize.x < 820 * scale || vp->WorkSize.x < vp->WorkSize.y * 1.05f;
    if (!force && layoutBuilt_ && (!autoArrange_ || narrow == layoutNarrow_)) return;

    const ImGuiID dock = ImGui::GetID("##dockspace");
    if (!force && !layoutBuilt_) {
        // A layout saved by a previous session (layout.ini): keep it.
        if (ImGuiDockNode* node = ImGui::DockBuilderGetNode(dock); node && node->IsSplitNode()) {
            layoutBuilt_ = true;
            layoutNarrow_ = narrow;
            return;
        }
    }
    ImGui::DockBuilderRemoveNode(dock);
    ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dock, vp->WorkSize);
    ImGuiID canvas = dock, settings = 0;
    if (narrow) {
        settings = ImGui::DockBuilderSplitNode(dock, ImGuiDir_Down, 0.45f, nullptr, &canvas);
    } else {
        const float ratio = std::clamp(380 * scale / vp->WorkSize.x, 0.25f, 0.45f);
        settings = ImGui::DockBuilderSplitNode(dock, ImGuiDir_Right, ratio, nullptr, &canvas);
    }
    const float historyHeight = (kThumbnailEdge * 0.55f + 40) * scale;
    const float canvasHeight = narrow ? vp->WorkSize.y * 0.55f : vp->WorkSize.y;
    const ImGuiID history = ImGui::DockBuilderSplitNode(canvas, ImGuiDir_Down,
                                                        std::clamp(historyHeight / canvasHeight, 0.08f, 0.3f), nullptr, &canvas);
    ImGui::DockBuilderDockWindow(kCanvasWindow, canvas);
    ImGui::DockBuilderDockWindow(kHistoryWindow, history);
    ImGui::DockBuilderDockWindow(kSettingsWindow, settings);
    ImGui::DockBuilderFinish(dock);
    layoutBuilt_ = true;
    layoutNarrow_ = narrow;
}

void App::menuBar() {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open image...", shortcutLabel(Action::OpenImage))) runAction(static_cast<int>(Action::OpenImage));
        if (ImGui::BeginMenu("Open recent", !config_.recentFiles.empty())) {
            std::string open;
            for (const auto& f : config_.recentFiles)
                if (ImGui::MenuItem(std::filesystem::path(f).filename().string().c_str())) open = f;
                else ImGui::SetItemTooltip("%s", f.c_str());
            ImGui::Separator();
            if (ImGui::MenuItem("Clear list")) config_.recentFiles.clear();
            ImGui::EndMenu();
            if (!open.empty()) openPath(open);
        }
        if (ImGui::MenuItem("Export PNG...", shortcutLabel(Action::ExportPng), false, source_ != nullptr))
            runAction(static_cast<int>(Action::ExportPng));
        if (ImGui::MenuItem("Copy image", shortcutLabel(Action::CopyImage), false, shown_.valid))
            runAction(static_cast<int>(Action::CopyImage));
        ImGui::Separator();
        if (ImGui::MenuItem("Save preset...", shortcutLabel(Action::SavePreset))) runAction(static_cast<int>(Action::SavePreset));
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) quit_ = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        for (ViewMode m : {ViewMode::Glitched, ViewMode::Original, ViewMode::Split, ViewMode::Difference})
            if (ImGui::MenuItem(viewModeName(m), nullptr, view_.mode == m)) view_.mode = m;
        ImGui::TextDisabled("Tab cycles; hold Space for the original");
        ImGui::Separator();
        if (ImGui::MenuItem("Zoom to fit", shortcutLabel(Action::ZoomFit))) runAction(static_cast<int>(Action::ZoomFit));
        if (ImGui::MenuItem("Zoom 100%", shortcutLabel(Action::Zoom100))) runAction(static_cast<int>(Action::Zoom100));
        if (ImGui::MenuItem("Zoom in", shortcutLabel(Action::ZoomIn))) runAction(static_cast<int>(Action::ZoomIn));
        if (ImGui::MenuItem("Zoom out", shortcutLabel(Action::ZoomOut))) runAction(static_cast<int>(Action::ZoomOut));
        ImGui::Separator();
        if (ImGui::MenuItem("Codec overlays", shortcutLabel(Action::ToggleOverlays), overlays_.panel))
            runAction(static_cast<int>(Action::ToggleOverlays));
        ImGui::Separator();
        if (ImGui::MenuItem("Reset layout")) buildLayout(true);
        ImGui::MenuItem("Rearrange for window shape", nullptr, &autoArrange_);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", shortcutLabel(Action::Undo), false, !history_.empty())) undo();
        if (ImGui::MenuItem("Redo", shortcutLabel(Action::Redo), false, history_.canRedo())) redo();
        ImGui::Separator();
        if (ImGui::MenuItem("Randomize", shortcutLabel(Action::Randomize))) randomizeAll();
        if (ImGui::MenuItem("Mutate grid", shortcutLabel(Action::MutateGrid), grid_.open)) openMutateGrid(!grid_.open);
        ImGui::EndMenu();
    }
    presetsMenu();
    if (ImGui::BeginMenu("Decode")) {
        bool linked = decodeLinked_;
        if (ImGui::MenuItem("Linked to encode", shortcutLabel(Action::ToggleDecodeLink), &linked)) setDecodeLinked(linked);
        ImGui::TextDisabled("Unlinked = mismatched decoding (Decode tab)");
        ImGui::Separator();
        bool corrupt = corruption_.enabled;
        if (ImGui::MenuItem("Corrupt encoded data", shortcutLabel(Action::ToggleCorruption), &corrupt)) setCorruptionEnabled(corrupt);
        if (ImGui::MenuItem("New corruption seed", shortcutLabel(Action::RerollSeed))) rerollCorruptionSeed();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Preview")) {
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
        if (ImGui::SliderInt("Proxy size", &proxyEdge_, 128, 2048, "%d px")) settingsChanged();
        ImGui::SetItemTooltip("Longest edge of the fast preview shown while settings change");
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void App::statusBar(float height) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - height));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, height));
    ImGui::SetNextWindowViewport(vp->ID);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##status", nullptr, flags);
    std::string text;
    if (source_) {
        char buf[256];
        if (shown_.valid) {
            if (shown_.encodeReused)
                std::snprintf(buf, sizeof buf, "%.0f ms (decode only)  \xC2\xB7  ", shown_.decodeMs);
            else
                std::snprintf(buf, sizeof buf, "%.0f ms (enc %.0f + dec %.0f)  \xC2\xB7  ",
                              shown_.encodeMs + shown_.decodeMs, shown_.encodeMs, shown_.decodeMs);
            text += buf;
        }
        std::snprintf(buf, sizeof buf, "%d\xC3\x97%d", source_->width, source_->height);
        text += buf;
        if (shown_.valid && shown_.proxy) {
            std::snprintf(buf, sizeof buf, " (preview %d\xC3\x97%d)", shown_.encodedWidth, shown_.encodedHeight);
            text += buf;
        }
        std::snprintf(buf, sizeof buf, "  \xC2\xB7  seed %lld  \xC2\xB7  ", static_cast<long long>(settings_.seed));
        text += buf;
        if (anim_.lastProgress.running) {
            std::snprintf(buf, sizeof buf, "%s %d/%d  \xC2\xB7  ", anim_.lastProgress.encoding ? "encoding video" : "exporting",
                          anim_.lastProgress.framesDone, anim_.lastProgress.frames);
            text += buf;
        }
        if (!scheduler_.idle()) text += "rendering...";
        else text += shown_.valid ? (shown_.proxy ? "proxy" : "full") : "-";
    } else {
        text = "No image";
    }
    if (corruption_.enabled) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1), "corrupt");
        if (shown_.corrupted)
            ImGui::SetItemTooltip("%zu bytes changed (seed %lld). C turns corruption off, N rerolls",
                                  shown_.corruption.bytesChanged, static_cast<long long>(corruption_.seed));
        ImGui::SameLine();
        ImGui::TextDisabled("\xC2\xB7");
        ImGui::SameLine();
    }
    if (!decodeLinked_) {
        ImGui::TextColored(ImVec4(0.95f, 0.7f, 0.3f, 1), "mismatch");
        ImGui::SetItemTooltip("Decode is unlinked from encode (L to relink)");
        ImGui::SameLine();
        ImGui::TextDisabled("\xC2\xB7");
        ImGui::SameLine();
    }
    ImGui::TextUnformatted(text.c_str());
    if (!status_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("\xC2\xB7");
        ImGui::SameLine();
        if (statusError_) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1), "%s", status_.c_str());
        else ImGui::TextUnformatted(status_.c_str());
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void App::frame() {
    if (framesToRender_ > 0) framesToRender_--;

    // Finished image load?
    if (loading_.valid() && loading_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto img = loading_.get();
        if (img && !img->empty()) {
            source_ = std::make_shared<const glic::Image>(std::move(*img));
            sourcePath_ = loadingPath_;
            originalTex_.upload(*source_);
            glitchedTex_.clear();
            diffTex_.clear();
            shown_ = ShownRender{};
            history_.clear();
            historyTex_.clear();
            historyLabel_ = "open";
            if (grid_.open) openMutateGrid(true);  // re-made for the new image
            view_.fit = true;
            scheduler_ = PreviewScheduler{};
            setStatus("Opened " + std::filesystem::path(sourcePath_).filename().string());
            addRecent(config_, sourcePath_);
            settingsChanged();
        } else {
            setStatus("Could not open " + loadingPath_, true);
            auto& recent = config_.recentFiles;
            recent.erase(std::remove(recent.begin(), recent.end(), loadingPath_), recent.end());
        }
        framesToRender_ = std::max(framesToRender_, 2);
    }
    pumpRender();
    pumpBatch();
    pumpAnimation();

    for (Action a : pressedActions()) runAction(static_cast<int>(a));
    const bool holdOriginal = ImGui::IsKeyDown(kHoldOriginalKey) && !ImGui::GetIO().WantTextInput;

    menuBar();

    const float statusHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, vp->WorkSize.y - statusHeight));
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##host", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(3);
    buildLayout(false);
    ImGui::DockSpace(ImGui::GetID("##dockspace"), ImVec2(0, 0), ImGuiDockNodeFlags_None);
    ImGui::End();

    // Difference image, computed lazily against the same-size original.
    if (view_.mode == ViewMode::Difference && diffDirty_ && shown_.valid && source_) {
        glic::Image orig = shown_.proxy ? downscaleToFit(*source_, std::max(shown_.encodedWidth, shown_.encodedHeight))
                                        : *source_;
        diffTex_.upload(difference(orig, croppedShown()));
        diffDirty_ = false;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::Begin(kCanvasWindow, nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        CanvasImages ci;
        ci.original = &originalTex_;
        ci.glitched = &glitchedTex_;
        ci.difference = &diffTex_;
        ci.loading = loading_.valid();
        if (source_) {
            ci.imageWidth = source_->width;
            ci.imageHeight = source_->height;
        }
        if (shown_.valid && shown_.image.width > 0) {
            ci.glitchedU = static_cast<float>(shown_.encodedWidth) / shown_.image.width;
            ci.glitchedV = static_cast<float>(shown_.encodedHeight) / shown_.image.height;
        }
        ci.framebufferScale = ImGui::GetIO().DisplayFramebufferScale.x;
        if (overlays_.panel && shown_.valid) {
            refreshOverlayTextures();
            ci.heatmap = &heatmapTex_;
            ci.coefficients = &coefficientTex_;
            ci.capture = shown_.capture.get();
            ci.overlayChannel = overlays_.channel;
            ci.blocks = overlays_.blocks && ci.capture && ci.capture->segments;
            ci.overlayWidth = shown_.encodedWidth;
            ci.overlayHeight = shown_.encodedHeight;
        }
        embeddedPresetBar();
        if (grid_.open) {
            drawMutateGrid(*this);
        } else {
            const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
            const CanvasOutput co = drawCanvas(view_, ci, holdOriginal);
            if (overlays_.panel) {
                ImGui::SetCursorScreenPos(canvasPos);
                drawOverlayPanel(*this, co);
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();

    if (ImGui::Begin(kHistoryWindow, nullptr, ImGuiWindowFlags_HorizontalScrollbar)) drawHistoryStrip(*this);
    ImGui::End();

    if (ImGui::Begin(kSettingsWindow)) drawSettingsPanel(*this);
    ImGui::End();

    statusBar(statusHeight);
}

} // namespace studio
