#pragma once

#include "app.hpp"
#include "ui/canvas.hpp"

namespace studio {

// The codec overlays panel (O), floating in the canvas's top-right corner:
// overlay toggles, channel, legends, and what is under the cursor.
// Call with the cursor at the canvas's top-left.
void drawOverlayPanel(App& app, const CanvasOutput& canvas);

} // namespace studio
