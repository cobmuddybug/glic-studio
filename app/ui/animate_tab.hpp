#pragma once

#include "app.hpp"

namespace studio {

// The Animate tab (design §M7): one parameter (or the corruption seed) from a
// start to an end value over N frames with easing; a one-frame preview; and
// export to MP4/GIF through ffmpeg, or to PNG frames without it.
void drawAnimateTab(App& app);

} // namespace studio
