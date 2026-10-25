#include "keybindings.hpp"

namespace studio {

const std::vector<KeyBinding>& keyBindings() {
    static const std::vector<KeyBinding> table = {
        {Action::OpenImage, ImGuiMod_Ctrl | ImGuiKey_O, "Ctrl+O", true},
        {Action::ExportPng, ImGuiMod_Ctrl | ImGuiKey_S, "Ctrl+S", true},
        {Action::SavePreset, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, "Ctrl+Shift+S", true},
        {Action::CopyImage, ImGuiMod_Ctrl | ImGuiKey_C, "Ctrl+C", false},
        {Action::CycleView, ImGuiKey_Tab, "Tab", false},
        {Action::ZoomIn, ImGuiKey_Equal, "+", false},
        {Action::ZoomIn, ImGuiKey_KeypadAdd, "+", false},
        {Action::ZoomOut, ImGuiKey_Minus, "-", false},
        {Action::ZoomOut, ImGuiKey_KeypadSubtract, "-", false},
        {Action::ZoomFit, ImGuiKey_0, "0", false},
        {Action::Zoom100, ImGuiKey_1, "1", false},
        {Action::ToggleDecodeLink, ImGuiKey_L, "L", false},
        {Action::ToggleOverlays, ImGuiKey_O, "O", false},
        {Action::ToggleCorruption, ImGuiKey_C, "C", false},
        {Action::RerollSeed, ImGuiKey_N, "N", false},
        {Action::Undo, ImGuiMod_Ctrl | ImGuiKey_Z, "Ctrl+Z", false},
        {Action::Redo, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, "Ctrl+Shift+Z", false},
        {Action::Redo, ImGuiMod_Ctrl | ImGuiKey_Y, "Ctrl+Y", false},
        {Action::Randomize, ImGuiKey_R, "R", false},
        {Action::MutateGrid, ImGuiKey_M, "M", false},
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
