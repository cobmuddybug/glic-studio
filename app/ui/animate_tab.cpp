#include "animate_tab.hpp"

#include "model/params.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace studio {

namespace {

void label(const char* text) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    ImGui::TableSetColumnIndex(1);
}

// A start/end value with a "current" button that copies the current setting.
bool valueField(const char* id, double& v, const AnimTargetInfo& info, double current) {
    bool changed = false;
    const float button = ImGui::CalcTextSize("Current").x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SetNextItemWidth(-(button + ImGui::GetStyle().ItemSpacing.x));
    const double speed = info.integer ? (info.max - info.min > 1000 ? 1.0 : 0.1) : (info.max - info.min) / 400;
    const char* fmt = info.integer ? "%.0f" : info.max <= 1 ? "%.5f" : "%.2f";
    changed |= ImGui::DragScalar(id, ImGuiDataType_Double, &v, static_cast<float>(speed), &info.min, &info.max, fmt,
                                 ImGuiSliderFlags_AlwaysClamp);
    ImGui::SameLine();
    ImGui::PushID(id);
    if (ImGui::Button("Current")) {
        v = current;
        changed = true;
    }
    ImGui::SetItemTooltip("Use the current setting (%g)", current);
    ImGui::PopID();
    return changed;
}

void hint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

} // namespace

void drawAnimateTab(App& app) {
    AnimationState& a = app.animation();
    AnimationSpec& s = a.spec;
    const glic::preset::Preset base = app.currentPreset();
    const AnimTargetInfo& info = animTargetInfo(s.target);
    const bool running = app.exporting();
    bool changed = false;

    ImGui::BeginDisabled(running);
    if (ImGui::BeginTable("##anim", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("##l", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 6.5f);
        ImGui::TableSetupColumn("##v", ImGuiTableColumnFlags_WidthStretch);

        label("Parameter");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##target", info.name)) {
            for (const auto& t : animTargets()) {
                if (ImGui::Selectable(t.name, t.target == s.target) && t.target != s.target) {
                    s.target = t.target;
                    // Start from where the image is now, and sweep towards the far end of the range.
                    s.start = currentValue(base, s);
                    s.end = (s.start - t.min) < (t.max - s.start) ? std::min(t.max, s.start + (t.max - t.min) / 2) : t.min;
                    if (t.target == AnimTarget::EncodeSeed || t.target == AnimTarget::CorruptionSeed) s.end = s.start + s.frames - 1;
                    if (t.target == AnimTarget::CorruptionRate) s.end = std::max(0.01, s.start * 20);
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        const AnimTargetInfo& ti = animTargetInfo(s.target);

        if (ti.decodable) {
            label("Side");
            const bool unlinked = !app.decodeLinked();
            int side = s.decodeSide && unlinked ? 1 : 0;
            changed |= ImGui::RadioButton("Encode", &side, 0);
            ImGui::SameLine();
            ImGui::BeginDisabled(!unlinked);
            changed |= ImGui::RadioButton("Decode", &side, 1);
            ImGui::EndDisabled();
            if (!unlinked) ImGui::SetItemTooltip("Unlink decoding (L) to animate a mismatched decode");
            s.decodeSide = side == 1;
        }
        if (ti.perChannel) {
            label("Channel");
            const glic::ColorSpace cs = s.decodeSide ? app.editDecode().colorSpace : app.settings().colorSpace;
            int choice = s.channel + 1;
            for (int c = 0; c < 4; c++) {
                if (c) ImGui::SameLine();
                char lbl[24];
                std::snprintf(lbl, sizeof lbl, "%s##c%d", c == 0 ? "All" : channelName(cs, c - 1), c);
                changed |= ImGui::RadioButton(lbl, &choice, c);
            }
            s.channel = choice - 1;
        }

        label("Start");
        changed |= valueField("##start", s.start, ti, currentValue(base, s));
        label("End");
        changed |= valueField("##end", s.end, ti, currentValue(base, s));

        label("Frames");
        ImGui::SetNextItemWidth(-FLT_MIN);
        changed |= ImGui::SliderInt("##frames", &s.frames, 2, 600, "%d", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);

        label("Easing");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##easing", easingName(s.easing))) {
            for (auto e : {Easing::Linear, Easing::EaseIn, Easing::EaseOut, Easing::EaseInOut})
                if (ImGui::Selectable(easingName(e), e == s.easing)) {
                    s.easing = e;
                    changed = true;
                }
            ImGui::EndCombo();
        }

        label("Size");
        static const int edges[] = {0, 2048, 1024, 512, 256};
        char sizeLabel[32];
        auto sizeName = [&](int e) {
            if (e == 0) std::snprintf(sizeLabel, sizeof sizeLabel, "Full size");
            else std::snprintf(sizeLabel, sizeof sizeLabel, "Longest edge %d px", e);
            return sizeLabel;
        };
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##size", sizeName(a.maxEdge))) {
            for (int e : edges)
                if (ImGui::Selectable(sizeName(e), e == a.maxEdge)) a.maxEdge = e;
            ImGui::EndCombo();
        }

        label("Format");
        int format = static_cast<int>(a.format);
        ImGui::BeginDisabled(!a.ffmpeg);
        ImGui::RadioButton("MP4", &format, static_cast<int>(AnimFormat::Mp4));
        ImGui::SameLine();
        ImGui::RadioButton("GIF", &format, static_cast<int>(AnimFormat::Gif));
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::RadioButton("PNG frames", &format, static_cast<int>(AnimFormat::PngFrames));
        a.format = static_cast<AnimFormat>(format);

        if (a.format != AnimFormat::PngFrames) {
            label("FPS");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SliderInt("##fps", &a.fps, 1, 60, "%d", ImGuiSliderFlags_AlwaysClamp);
        }
        ImGui::EndTable();
    }
    if (!a.ffmpeg)
        hint("ffmpeg is not on PATH, so MP4 and GIF are unavailable: the export writes PNG frames.");
    else if (a.format != AnimFormat::PngFrames)
        ImGui::TextDisabled("%d frames at %d fps = %.1f s", s.frames, a.fps, static_cast<double>(s.frames) / a.fps);
    else
        hint("Each frame PNG embeds its own preset.");
    ImGui::EndDisabled();

    // Preview: one frame, rendered small, without touching the settings.
    ImGui::SeparatorText("Preview");
    int shownFrame = a.previewFrame + 1;
    ImGui::SetNextItemWidth(-FLT_MIN);
    char fmt[64];
    std::snprintf(fmt, sizeof fmt, "frame %%d  (%s %g)", animTargetInfo(s.target).name, frameValue(s, a.previewFrame));
    if (ImGui::SliderInt("##pframe", &shownFrame, 1, s.frames, fmt, ImGuiSliderFlags_AlwaysClamp)) {
        a.previewFrame = shownFrame - 1;
        changed = true;
    }
    if (changed || a.previewBatch == 0 || ImGui::IsWindowAppearing()) app.requestAnimationPreview();
    if (a.previewTex.valid() && a.previewWidth > 0) {
        const float w = std::min(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 22);
        const float h = w * a.previewHeight / a.previewWidth;
        const ImVec2 uv(static_cast<float>(a.previewWidth) / a.previewTex.width(),
                        static_cast<float>(a.previewHeight) / a.previewTex.height());
        ImGui::ImageWithBg(ImTextureRef(a.previewTex.imguiId()), ImVec2(w, h), ImVec2(0, 0), uv, ImVec4(0, 0, 0, 0),
                           ImVec4(1, 1, 1, a.previewReady ? 1.0f : 0.5f));
    }
    if (!a.previewError.empty()) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1), "%s", a.previewError.c_str());

    ImGui::Spacing();
    if (running) {
        const AnimationProgress& p = a.lastProgress;
        char overlay[64];
        std::snprintf(overlay, sizeof overlay, p.encoding ? "encoding video..." : "frame %d / %d", p.framesDone, p.frames);
        ImGui::ProgressBar(p.frames ? static_cast<float>(p.framesDone) / p.frames : 0.0f, ImVec2(-FLT_MIN, 0), overlay);
        if (ImGui::Button("Cancel export")) app.cancelAnimationExport();
    } else {
        char button[48];
        std::snprintf(button, sizeof button, "Export %s...", animFormatName(a.format));
        ImGui::BeginDisabled(!app.hasSource());
        if (ImGui::Button(button, ImVec2(-FLT_MIN, ImGui::GetFrameHeight() * 1.4f))) app.requestAnimationExport();
        ImGui::EndDisabled();
        if (a.lastProgress.done && !a.lastProgress.message.empty())
            ImGui::TextWrapped("%s", a.lastProgress.message.c_str());
    }
}

} // namespace studio
