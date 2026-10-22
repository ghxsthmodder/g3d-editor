#include "texture/texture_studio.hpp"

#include <cstdio>
#include <sstream>

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

int TextureStudio::addLayerAt(const Image &image, const TextureLayer &at, std::string &error) {
    std::lock_guard<std::mutex> guard(mutex_);
    const int id = composition_.addLayerAt(image, at, error);
    if (id > 0) {
        touch();
    }
    return id;
}

std::vector<TextureLayer> TextureStudio::layers() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return composition_.layers();
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

std::string TextureStudio::describe() const {
    std::lock_guard<std::mutex> guard(mutex_);
    std::ostringstream stream;
    if (composition_.canvasWidth() <= 0 || composition_.canvasHeight() <= 0) {
        stream << "tela não definida";
    } else {
        stream << "tela=" << composition_.canvasWidth() << "x" << composition_.canvasHeight();
    }
    uint8_t rgb[3];
    composition_.background(rgb);
    char colour[16];
    std::snprintf(colour, sizeof(colour), "#%02X%02X%02X", rgb[0], rgb[1], rgb[2]);
    stream << " camadas=" << composition_.layers().size() << " fundo=" << colour
           << " mem=" << (static_cast<double>(composition_.memoryBytes()) / (1024.0 * 1024.0))
           << "MB";
    return stream.str();
}

std::string TextureStudio::layerRows() const {
    std::lock_guard<std::mutex> guard(mutex_);
    const std::vector<TextureLayer> &layers = composition_.layers();
    std::string text;
    for (size_t position = layers.size(); position-- > 0;) {
        const TextureLayer &layer = layers[position];
        char line[256];
        std::snprintf(line, sizeof(line),
                      "%d | %s | op=%d%% · %.0f,%.0f · %d%% · %.0f°%s\n",
                      layer.id, layer.name.c_str(),
                      static_cast<int>(layer.opacity * 100.0f + 0.5f), layer.centreX,
                      layer.centreY,
                      static_cast<int>(layer.scaleX * 100.0f + 0.5f), layer.rotationDegrees,
                      layer.visible ? "" : " · oculta");
        text += line;
    }
    return text;
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
