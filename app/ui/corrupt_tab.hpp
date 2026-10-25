#pragma once

#include "app.hpp"

namespace studio {

// The Corrupt tab (design §M5): random-bytes corruption with seed and rate,
// header protection, and the byte map with per-region rates.
void drawCorruptTab(App& app);

} // namespace studio
