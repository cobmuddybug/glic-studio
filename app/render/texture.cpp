#include "texture.hpp"

#include <SDL3/SDL_opengl.h>

namespace studio {

Texture::~Texture() { clear(); }

void Texture::clear() {
    if (id_) {
        GLuint id = id_;
        glDeleteTextures(1, &id);
    }
    id_ = 0;
    w_ = h_ = 0;
}

void Texture::upload(const glic::Image& img) {
    if (img.empty()) {
        clear();
        return;
    }
    if (!id_) {
        GLuint id;
        glGenTextures(1, &id);
        id_ = id;
        nearest_ = !nearest_;
        setFiltering(!nearest_);
    }
    glBindTexture(GL_TEXTURE_2D, id_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    // 0xAARRGGBB in little-endian memory is B,G,R,A.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width, img.height, 0, GL_BGRA, GL_UNSIGNED_BYTE, img.pixels.data());
    w_ = img.width;
    h_ = img.height;
}

void Texture::setFiltering(bool nearest) {
    if (!id_ || nearest == nearest_) return;
    nearest_ = nearest;
    glBindTexture(GL_TEXTURE_2D, id_);
    GLint f = nearest ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
}

} // namespace studio
