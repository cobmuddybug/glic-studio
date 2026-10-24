#include "settings_panel.hpp"

#include "model/params.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace studio {

namespace {

// Channel names per colour space, for the column headers.
const char* channelName(glic::ColorSpace cs, int ch) {
    static const char* names[16][3] = {
        {"I1", "I2", "I3"}, {"R", "G", "B"}, {"C", "M", "Y"}, {"H", "S", "B"},
        {"X", "Y", "Z"}, {"Y", "x", "y"}, {"H", "C", "L"}, {"L", "u", "v"},
        {"L", "a", "b"}, {"H", "W", "B"}, {"R-G", "G", "B-G"}, {"Y", "Pb", "Pr"},
        {"Y", "Cb", "Cr"}, {"Y", "Db", "Dr"}, {"Grey", "Grey", "Grey"}, {"Y", "U", "V"},
    };
    int i = static_cast<int>(cs);
    return (i >= 0 && i < 16) ? names[i][ch] : "?";
}

bool choiceCombo(const char* id, int& value, const std::vector<Choice>& choices) {
    const char* preview = "?";
    for (const auto& c : choices)
        if (c.value == value) preview = c.label.c_str();
    bool changed = false;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo(id, preview, ImGuiComboFlags_HeightLarge)) {
        for (const auto& c : choices) {
            bool selected = c.value == value;
            if (ImGui::Selectable(c.label.c_str(), selected)) {
                value = c.value;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

// Power-of-two value edited as its exponent, labelled with the value.
bool pow2Slider(const char* id, int& value, int expMin, int expMax) {
    int e = std::clamp(log2Floor(std::max(value, 1)), expMin, expMax);
    char label[32];
    std::snprintf(label, sizeof label, "%d", 1 << e);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderInt(id, &e, expMin, expMax, label, ImGuiSliderFlags_AlwaysClamp)) {
        value = 1 << e;
        return true;
    }
    return false;
}

bool scaleSlider(const char* id, int& value) {
    int e = std::clamp(log2Floor(std::max(value, 1)), kScaleExpMin, kScaleExpMax);
    char label[32];
    std::snprintf(label, sizeof label, "2^%d", e);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderInt(id, &e, kScaleExpMin, kScaleExpMax, label, ImGuiSliderFlags_AlwaysClamp)) {
        value = 1 << e;
        return true;
    }
    return false;
}

// Draws one control for one channel. Returns true if the value changed.
bool channelControl(glic::ChannelSettings& c, ChannelParam p, const char* id) {
    int v;
    switch (p) {
    case ChannelParam::MinBlock: return pow2Slider(id, c.minBlockSize, kBlockExpMin, kBlockExpMax);
    case ChannelParam::MaxBlock: return pow2Slider(id, c.maxBlockSize, kBlockExpMin, kBlockExpMax);
    case ChannelParam::Threshold:
        ImGui::SetNextItemWidth(-FLT_MIN);
        return ImGui::SliderFloat(id, &c.threshold, 5, 250, "%.0f");
    case ChannelParam::Prediction:
        v = static_cast<int>(c.prediction);
        if (choiceCombo(id, v, predictionChoices())) { c.prediction = static_cast<glic::Prediction>(v); return true; }
        return false;
    case ChannelParam::Quantization:
        ImGui::SetNextItemWidth(-FLT_MIN);
        return ImGui::SliderInt(id, &c.quantization, 0, 255, "%d", ImGuiSliderFlags_AlwaysClamp);
    case ChannelParam::Clamp: {
        bool mod = c.clamp == glic::ClampMethod::MOD256;
        if (ImGui::Checkbox(id, &mod)) { c.clamp = mod ? glic::ClampMethod::MOD256 : glic::ClampMethod::NONE; return true; }
        return false;
    }
    case ChannelParam::Wavelet: return choiceCombo(id, c.wavelet, waveletChoices());
    case ChannelParam::TransformType:
        v = static_cast<int>(c.transformType);
        if (choiceCombo(id, v, transformTypeChoices())) { c.transformType = static_cast<glic::TransformType>(v); return true; }
        return false;
    case ChannelParam::Compress:
        ImGui::SetNextItemWidth(-FLT_MIN);
        return ImGui::SliderFloat(id, &c.compress, 0, 255, "%.0f");
    case ChannelParam::Scale: return scaleSlider(id, c.scale);
    case ChannelParam::Encoding:
        v = static_cast<int>(c.encoding);
        if (choiceCombo(id, v, encodingChoices())) { c.encoding = static_cast<glic::Encoding>(v); return true; }
        return false;
    }
    return false;
}

struct Row {
    ChannelParam param;
    const char* label;
    const char* help;
};

const Row kRows[] = {
    {ChannelParam::MinBlock, "Min block", "Smallest quadtree block"},
    {ChannelParam::MaxBlock, "Max block", "Largest quadtree block"},
    {ChannelParam::Threshold, "Threshold", "Split a block when its std-dev exceeds this (lower = finer)"},
    {ChannelParam::Prediction, "Prediction", "How each block is predicted from its neighbours"},
    {ChannelParam::Quantization, "Quantization", "Residual quantization (0 = off)"},
    {ChannelParam::Clamp, "Mod 256", "Wrap residuals modulo 256 instead of clamping"},
    {ChannelParam::Wavelet, "Wavelet", "Wavelet applied to each block's residual"},
    {ChannelParam::TransformType, "Transform", "Fast wavelet or wavelet packet transform"},
    {ChannelParam::Compress, "Compression", "Zero wavelet coefficients below a magnitude threshold"},
    {ChannelParam::Scale, "Scale", "Wavelet coefficient scale (low values destroy detail)"},
    {ChannelParam::Encoding, "Encoding", "How values are written: raw, packed bits, or run-length"},
};

} // namespace

void drawSettingsPanel(App& app) {
    glic::EncodeSettings& s = app.editSettings();
    bool& linked = app.linkChannels();
    bool changed = false;

    if (!ImGui::BeginTabBar("##settings_tabs")) return;
    if (ImGui::BeginTabItem("Encode")) {
        if (ImGui::BeginTable("##image_params", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("##l", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 6.5f);
            ImGui::TableSetupColumn("##v", ImGuiTableColumnFlags_WidthStretch);
            auto label = [](const char* text, const char* help) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(text);
                ImGui::SetItemTooltip("%s", help);
                ImGui::TableSetColumnIndex(1);
            };

            label("Colour space", "The colour space GLIC works in; each channel is coded separately");
            int cs = static_cast<int>(s.colorSpace);
            if (choiceCombo("##colorspace", cs, colorSpaceChoices())) {
                s.colorSpace = static_cast<glic::ColorSpace>(cs);
                changed = true;
            }

            label("Border", "Colour of everything outside the image, which predictors see at the edges");
            float border[3] = {s.border[0] / 255.0f, s.border[1] / 255.0f, s.border[2] / 255.0f};
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::ColorEdit3("##border", border, ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_Uint8)) {
                for (int i = 0; i < 3; i++) s.border[i] = static_cast<uint8_t>(std::lround(border[i] * 255.0f));
                changed = true;
            }

            label("Seed", "Seeds GLIC's randomness (block sampling, REF, RANDOM)");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputScalar("##seed", ImGuiDataType_S64, &s.seed, nullptr, nullptr, "%lld")) changed = true;
            ImGui::EndTable();
        }

        if (ImGui::Checkbox("Link channels", &linked) && linked) {
            linkChannels(s);
            changed = true;
        }

        const int columns = linked ? 1 : 3;
        const ImGuiTableFlags tflags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX;
        if (ImGui::BeginTable("##channels", columns + 1, tflags)) {
            ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 6.5f);
            for (int c = 0; c < columns; c++) {
                ImGui::TableSetupColumn(linked ? "All channels" : channelName(s.colorSpace, c), ImGuiTableColumnFlags_WidthStretch);
            }
            ImGui::TableHeadersRow();

            for (const Row& row : kRows) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(row.label);
                ImGui::SetItemTooltip("%s", row.help);
                for (int c = 0; c < columns; c++) {
                    ImGui::TableSetColumnIndex(c + 1);
                    ImGui::PushID(static_cast<int>(row.param) * 8 + c);
                    if (channelControl(s.channels[c], row.param, "##v")) {
                        if (linked) linkChannels(s);
                        changed = true;
                    }
                    if (ImGui::BeginPopupContextItem("##ctx")) {
                        if (ImGui::MenuItem("Reset to default")) {
                            resetParam(s, c, row.param);
                            if (linked) linkChannels(s);
                            changed = true;
                        }
                        if (ImGui::MenuItem("Copy value to all channels", nullptr, false, !linked)) {
                            copyToAllChannels(s, c, row.param);
                            changed = true;
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();

    if (changed) app.settingsChanged();
}

} // namespace studio
