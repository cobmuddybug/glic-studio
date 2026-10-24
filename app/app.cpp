#include "app.hpp"

#include "model/params.hpp"
#include "render/resize.hpp"
#include "ui/canvas.hpp"
#include "ui/keybindings.hpp"
#include "ui/settings_panel.hpp"

#include <glic/image_io.hpp>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>

namespace studio {

namespace {

constexpr const char* kCanvasWindow = "Canvas";
constexpr const char* kSettingsWindow = "Settings";

glic::Image crop(const glic::Image& img, int w, int h) {
    if (img.width == w && img.height == h) return img;
    glic::Image out;
    out.width = std::min(w, img.width);
    out.height = std::min(h, img.height);
    out.pixels.resize(static_cast<size_t>(out.width) * out.height);
    for (int y = 0; y < out.height; y++)
        std::copy_n(img.pixels.begin() + static_cast<size_t>(y) * img.width, out.width,
                    out.pixels.begin() + static_cast<size_t>(y) * out.width);
    return out;
}

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
    : platform_(platform), worker_([&platform] { platform.postEvent(AppEventKind::RenderDone); }) {
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
        if (!e.path.empty()) openImage(e.path);
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

void App::pumpRender() {
    for (auto& r : worker_.poll()) {
        const bool show = scheduler_.finished(r.generation, r.cancelled);
        if (!show) continue;
        if (r.ok) {
            acceptResult(std::move(r));
        } else {
            // Keep the last good render on screen (design §M5).
            setStatus("Render failed: " + r.error, true);
        }
    }
    if (!source_) return;
    while (auto job = scheduler_.next(nowMs())) {
        RenderRequest req;
        req.generation = job->ticket;
        req.source = source_;
        req.encode = settings_;
        req.proxy = job->proxy;
        req.proxyEdge = proxyEdge_;
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
    shown_.image = std::move(r.image);
    shown_.data = std::move(r.data);
    glitchedTex_.upload(shown_.image);
    diffDirty_ = true;
    if (statusError_) setStatus("");
    if (!shown_.proxy && !exportPath_.empty() && scheduler_.fullIsCurrent()) finishExport();
}

glic::Image App::croppedShown() const { return crop(shown_.image, shown_.encodedWidth, shown_.encodedHeight); }

void App::requestExport() {
    if (!source_) {
        setStatus("Nothing to export: open an image first", true);
        return;
    }
    std::string stem = std::filesystem::path(sourcePath_).stem().string();
    platform_.showSavePngDialog(stem + "-glic.png");
}

void App::finishExport() {
    // Exports what the canvas shows: the full-resolution render, cropped to the
    // source size (GLIC's decoder pads sides below 64 px).
    if (glic::saveImage(exportPath_, croppedShown()))
        setStatus("Exported " + std::filesystem::path(exportPath_).filename().string());
    else
        setStatus("Export failed: could not write " + exportPath_, true);
    exportPath_.clear();
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
    }
}

int App::idleTimeoutMs() {
    if (framesToRender_ > 0) return 0;
    if (loading_.valid()) return 30;
    const double wake = scheduler_.wakeIn(nowMs());
    if (wake >= 0) return std::max(1, static_cast<int>(std::ceil(wake)));
    if (ImGui::GetIO().WantTextInput) return 500;  // caret blink
    return -1;
}

void App::buildLayout(bool force) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float scale = ImGui::GetStyle().FontScaleDpi;
    const bool narrow = vp->WorkSize.x < 820 * scale || vp->WorkSize.x < vp->WorkSize.y * 1.05f;
    if (!force && layoutBuilt_ && (!autoArrange_ || narrow == layoutNarrow_)) return;

    const ImGuiID dock = ImGui::GetID("##dockspace");
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
    ImGui::DockBuilderDockWindow(kCanvasWindow, canvas);
    ImGui::DockBuilderDockWindow(kSettingsWindow, settings);
    ImGui::DockBuilderFinish(dock);
    layoutBuilt_ = true;
    layoutNarrow_ = narrow;
}

void App::menuBar() {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open image...", shortcutLabel(Action::OpenImage))) runAction(static_cast<int>(Action::OpenImage));
        if (ImGui::MenuItem("Export PNG...", shortcutLabel(Action::ExportPng), false, source_ != nullptr))
            runAction(static_cast<int>(Action::ExportPng));
        if (ImGui::MenuItem("Copy image", shortcutLabel(Action::CopyImage), false, shown_.valid))
            runAction(static_cast<int>(Action::CopyImage));
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
        if (ImGui::MenuItem("Reset layout")) buildLayout(true);
        ImGui::MenuItem("Rearrange for window shape", nullptr, &autoArrange_);
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
            std::snprintf(buf, sizeof buf, "%.0f ms (enc %.0f + dec %.0f)  \xC2\xB7  ", shown_.encodeMs + shown_.decodeMs,
                          shown_.encodeMs, shown_.decodeMs);
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
        if (!scheduler_.idle()) text += "rendering...";
        else text += shown_.valid ? (shown_.proxy ? "proxy" : "full") : "-";
    } else {
        text = "No image";
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
            view_.fit = true;
            scheduler_ = PreviewScheduler{};
            setStatus("Opened " + std::filesystem::path(sourcePath_).filename().string());
            settingsChanged();
        } else {
            setStatus("Could not open " + loadingPath_, true);
        }
        framesToRender_ = std::max(framesToRender_, 2);
    }
    pumpRender();

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
        drawCanvas(view_, ci, holdOriginal);
    }
    ImGui::End();
    ImGui::PopStyleVar();

    if (ImGui::Begin(kSettingsWindow)) drawSettingsPanel(*this);
    ImGui::End();

    statusBar(statusHeight);
}

} // namespace studio
