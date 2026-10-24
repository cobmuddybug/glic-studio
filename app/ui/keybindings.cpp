#include "keybindings.hpp"

namespace studio {

const std::vector<KeyBinding>& keyBindings() {
    static const std::vector<KeyBinding> table = {
        {Action::OpenImage, ImGuiMod_Ctrl | ImGuiKey_O, "Ctrl+O", true},
        {Action::ExportPng, ImGuiMod_Ctrl | ImGuiKey_S, "Ctrl+S", true},
        {Action::CopyImage, ImGuiMod_Ctrl | ImGuiKey_C, "Ctrl+C", false},
        {Action::CycleView, ImGuiKey_Tab, "Tab", false},
        {Action::ZoomIn, ImGuiKey_Equal, "+", false},
        {Action::ZoomIn, ImGuiKey_KeypadAdd, "+", false},
        {Action::ZoomOut, ImGuiKey_Minus, "-", false},
        {Action::ZoomOut, ImGuiKey_KeypadSubtract, "-", false},
        {Action::ZoomFit, ImGuiKey_0, "0", false},
        {Action::Zoom100, ImGuiKey_1, "1", false},
        {Action::ToggleDecodeLink, ImGuiKey_L, "L", false},
    };
    return table;
}

const char* shortcutLabel(Action a) {
    for (const auto& b : keyBindings())
        if (b.action == a) return b.label;
    return "";
}

std::vector<Action> pressedActions() {
    std::vector<Action> out;
    const bool typing = ImGui::GetIO().WantTextInput;
    for (const auto& b : keyBindings()) {
        if (typing && !b.global) continue;
        if (ImGui::IsKeyChordPressed(b.chord)) out.push_back(b.action);
    }
    return out;
}

} // namespace studio
