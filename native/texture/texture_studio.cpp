#include "texture/texture_studio.hpp"

namespace g3d {

TextureStudio &TextureStudio::instance() {
    static TextureStudio studio;
    return studio;
}

void TextureStudio::touch() {
    ++revision_;
    dirty_ = true;
}

bool TextureStudio::setCanvas(int width, int height, std::string &error) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!composition_.setCanvas(width, height, error)) {
        return false;
    }
    touch();
    return true;
}

int TextureStudio::canvasWidth() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return composition_.canvasWidth();
}

int TextureStudio::canvasHeight() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return composition_.canvasHeight();
}

void TextureStudio::setBackground(const uint8_t rgb[3]) {
    std::lock_guard<std::mutex> guard(mutex_);
    composition_.setBackground(rgb);
    touch();
}

void TextureStudio::background(uint8_t out[3]) const {
    std::lock_guard<std::mutex> guard(mutex_);
    composition_.background(out);
}

int TextureStudio::addLayer(const Image &image, std::string &error) {
    std::lock_guard<std::mutex> guard(mutex_);
    const int id = composition_.addLayer(image, error);
    if (id > 0) {
        touch();
    }
    return id;
}

bool TextureStudio::removeLayer(int id) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!composition_.removeLayer(id)) {
        return false;
    }
    touch();
    return true;
}

bool TextureStudio::duplicateLayer(int id, int &newId, std::string &error) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!composition_.duplicateLayer(id, newId, error)) {
        return false;
    }
    touch();
    return true;
}

bool TextureStudio::moveUp(int id) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!composition_.moveUp(id)) {
        return false;
    }
    touch();
    return true;
}

bool TextureStudio::moveDown(int id) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!composition_.moveDown(id)) {
        return false;
    }
    touch();
    return true;
}

bool TextureStudio::setVisible(int id, bool visible) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!composition_.setVisible(id, visible)) {
        return false;
    }
    touch();
    return true;
}

bool TextureStudio::setOpacity(int id, float opacity) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!composition_.setOpacity(id, opacity)) {
        return false;
    }
    touch();
    return true;
}

bool TextureStudio::translate(int id, float dxPixels, float dyPixels) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!composition_.translate(id, dxPixels, dyPixels)) {
        return false;
    }
    touch();
    return true;
}

bool TextureStudio::rotate(int id, float degreesDelta) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!composition_.rotate(id, degreesDelta)) {
        return false;
    }
    touch();
    return true;
}

bool TextureStudio::scaleBy(int id, float factor) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!composition_.scaleBy(id, factor)) {
        return false;
    }
    touch();
    return true;
}

size_t TextureStudio::layerCount() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return composition_.layers().size();
}

size_t TextureStudio::memoryBytes() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return composition_.memoryBytes();
}

bool TextureStudio::renderLocked(Image &out, std::string &error) {
    Image fresh;
    if (!composition_.compose(fresh, error)) {
        composed_.clear();
        dirty_ = true;
        return false;
    }
    composed_ = std::move(fresh);
    dirty_ = false;
    out = composed_;
    return true;
}

bool TextureStudio::texture(Image &out) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!dirty_ && composed_.valid()) {
        out = composed_;
        return true;
    }
    if (!dirty_ && !composed_.valid()) {
        return false;
    }
    std::string ignored;
    return renderLocked(out, ignored);
}

bool TextureStudio::render(Image &out, std::string &error) {
    std::lock_guard<std::mutex> guard(mutex_);
    return renderLocked(out, error);
}

void TextureStudio::reset() {
    std::lock_guard<std::mutex> guard(mutex_);
    composition_ = TextureComposition();
    composed_.clear();
    dirty_ = true;
    ++revision_;
}

uint64_t TextureStudio::revision() {
    std::lock_guard<std::mutex> guard(mutex_);
    return revision_;
}

}  // namespace g3d
