#pragma once

namespace glic {

// GLIC's colour space conversions on Java-style ARGB ints. Unknown ids
// (including RGB = 1) pass the colour through unchanged.
int toColorspace(int c, int cs);
int fromColorspace(int c, int cs);

} // namespace glic
