#pragma once

// All keyboard shortcuts in one table (design §6.5), so they are easy to change.
// Mnemonic letters, no F-keys. Later milestones add their actions here.

#include <imgui.h>

#include <vector>

namespace studio {

enum class Action {
    OpenImage,
    ExportPng,
    CopyImage,
    CycleView,
    ZoomIn,
    ZoomOut,
    ZoomFit,
    Zoom100,
    ToggleDecodeLink,
    SavePreset,
};

struct KeyBinding {
    Action action;
    ImGuiKeyChord chord;
    const char* label;  // shown in menus
    bool global;        // fires even while a text field has focus (Ctrl chords)
};

const std::vector<KeyBinding>& keyBindings();
const char* shortcutLabel(Action a);

// Actions triggered this frame. Bare-key actions are suppressed while typing.
std::vector<Action> pressedActions();

// "Hold Space to show the original" (design §6.5).
constexpr ImGuiKey kHoldOriginalKey = ImGuiKey_Space;

} // namespace studio
