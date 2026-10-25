#pragma once

#include <glic/glic.hpp>

namespace studio {

// Box-filter downscale so the longest edge is at most `maxEdge`. Returns the
// image unchanged (copied) when it already fits. Deterministic.
glic::Image downscaleToFit(const glic::Image& src, int maxEdge);

// The top-left w x h of `img` (GLIC's decoder pads sides below 64 px).
glic::Image cropTo(const glic::Image& img, int w, int h);

// Per-channel |a - b|, opaque. Sizes must match.
glic::Image difference(const glic::Image& a, const glic::Image& b);

} // namespace studio
