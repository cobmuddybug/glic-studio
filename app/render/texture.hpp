#pragma once

#include <glic/glic.hpp>

#include <cstdint>

namespace studio {

// An RGBA OpenGL texture holding an ARGB image. Main thread only.
class Texture {
public:
    Texture() = default;
    ~Texture();
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    void upload(const glic::Image& img);
    void clear();
    // Nearest filtering when zoomed in (to see pixels), linear when zoomed out.
    void setFiltering(bool nearest);

    bool valid() const { return id_ != 0; }
    uint64_t imguiId() const { return id_; }
    int width() const { return w_; }
    int height() const { return h_; }

private:
    unsigned id_ = 0;
    int w_ = 0, h_ = 0;
    bool nearest_ = false;
};

} // namespace studio
