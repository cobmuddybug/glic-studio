#pragma once

#include "app.hpp"

namespace studio {

// The mutate grid (M), drawn in the canvas window in place of the canvas:
// nine perturbations of the current preset; click one to adopt it.
void drawMutateGrid(App& app);

// The branching history strip: the current line of history as thumbnails.
// Click to jump; nodes with several branches carry a count, and right-click
// lists their branches.
void drawHistoryStrip(App& app);

} // namespace studio
