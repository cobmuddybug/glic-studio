#pragma once

#include <glic/glic.hpp>

namespace studio {

// Box-filter downscale so the longest edge is at most `maxEdge`. Returns the
// image unchanged (copied) when it already fits. Deterministic.
glic::Image downscaleToFit(const glic::Image& src, int maxEdge);

// Per-channel |a - b|, opaque. Sizes must match.
glic::Image difference(const glic::Image& a, const glic::Image& b);

} // namespace studio
