#include "texture/texture_studio.hpp"

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace g3d {
namespace {

// The undo window of the compositor. A step is the canvas, the colour behind it and the placements,
// so the number of steps is cheap and what really costs is the picture a dropped step was the last
// to hold. The bytes are half of what the stack itself may reach: a phone carries the model, the
// atlas and this window at the same time, and the newest step is never the one dropped.
constexpr size_t kMaxHistorySteps = 64;
constexpr size_t kMaxHistoryBytes = 32u * 1024u * 1024u;

// What forgetting both stacks would give back in pixels. A picture the stack on the canvas still
// shows is not freed by dropping a step, and one several steps share is only freed by the last of
// them, so every buffer counts once and only when nothing live reaches it. The handful of numbers a
// step itself is costs nothing worth naming.
size_t historyCost(const std::vector<TextureState> &undoStack,
                   const std::vector<TextureState> &redoStack,
                   const std::vector<TextureLayer> &live) {
    std::vector<const Image *> held;
    held.reserve(live.size());
    for (const TextureLayer &layer : live) {
        if (layer.image) {
            held.push_back(layer.image.get());
        }
    }
    std::vector<const Image *> counted;
    size_t bytes = 0;
    for (const std::vector<TextureState> *stack : {&undoStack, &redoStack}) {
        for (const TextureState &step : *stack) {
            for (const TextureLayer &layer : step.layers) {
                if (!layer.image) {
                    continue;
                }
                const Image *picture = layer.image.get();
                if (std::find(held.begin(), held.end(), picture) != held.end()) {
                    continue;
                }
                if (std::find(counted.begin(), counted.end(), picture) != counted.end()) {
                    continue;
                }
                counted.push_back(picture);
                bytes += picture->memoryBytes();
            }
        }
    }
    return bytes;
}

}  // namespace

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
    const TextureState before = composition_.state();
    if (!composition_.setCanvas(width, height, error)) {
        return false;
    }
    remember(before);
    touch();
    return true;
}

void TextureStudio::setDeviceLimit(int side) {
    std::lock_guard<std::mutex> guard(mutex_);
    composition_.setDeviceLimit(side);
}

int TextureStudio::deviceLimit() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return composition_.deviceLimit();
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
    const TextureState before = composition_.state();
    composition_.setBackground(rgb);
    remember(before);
    touch();
}

void TextureStudio::background(uint8_t out[3]) const {
    std::lock_guard<std::mutex> guard(mutex_);
    composition_.background(out);
}

int TextureStudio::addLayer(const Image &image, std::string &error) {
    std::lock_guard<std::mutex> guard(mutex_);
    const TextureState before = composition_.state();
    const int id = composition_.addLayer(image, error);
    if (id > 0) {
        remember(before);
        touch();
    }
    return id;
}

int TextureStudio::addLayerAt(const Image &image, const TextureLayer &at, std::string &error) {
    std::lock_guard<std::mutex> guard(mutex_);
    const TextureState before = composition_.state();
    const int id = composition_.addLayerAt(image, at, error);
    if (id > 0) {
        remember(before);
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
    const TextureState before = composition_.state();
    if (!composition_.removeLayer(id)) {
        return false;
    }
    remember(before);
    touch();
    return true;
}

bool TextureStudio::duplicateLayer(int id, int &newId, std::string &error) {
    std::lock_guard<std::mutex> guard(mutex_);
    const TextureState before = composition_.state();
    if (!composition_.duplicateLayer(id, newId, error)) {
        return false;
    }
    remember(before);
    touch();
    return true;
}

bool TextureStudio::moveUp(int id) {
    std::lock_guard<std::mutex> guard(mutex_);
    const TextureState before = composition_.state();
    if (!composition_.moveUp(id)) {
        return false;
    }
    remember(before);
    touch();
    return true;
}

bool TextureStudio::moveDown(int id) {
    std::lock_guard<std::mutex> guard(mutex_);
    const TextureState before = composition_.state();
    if (!composition_.moveDown(id)) {
        return false;
    }
    remember(before);
    touch();
    return true;
}

bool TextureStudio::setVisible(int id, bool visible) {
    std::lock_guard<std::mutex> guard(mutex_);
    const TextureState before = composition_.state();
    if (!composition_.setVisible(id, visible)) {
        return false;
    }
    remember(before);
    touch();
    return true;
}

bool TextureStudio::setOpacity(int id, float opacity) {
    std::lock_guard<std::mutex> guard(mutex_);
    const TextureState before = composition_.state();
    if (!composition_.setOpacity(id, opacity)) {
        return false;
    }
    remember(before);
    touch();
    return true;
}

bool TextureStudio::translate(int id, float dxPixels, float dyPixels) {
    std::lock_guard<std::mutex> guard(mutex_);
    const TextureState before = composition_.state();
    if (!composition_.translate(id, dxPixels, dyPixels)) {
        return false;
    }
    remember(before);
    touch();
    return true;
}

bool TextureStudio::rotate(int id, float degreesDelta) {
    std::lock_guard<std::mutex> guard(mutex_);
    const TextureState before = composition_.state();
    if (!composition_.rotate(id, degreesDelta)) {
        return false;
    }
    remember(before);
    touch();
    return true;
}

bool TextureStudio::scaleBy(int id, float factor) {
    std::lock_guard<std::mutex> guard(mutex_);
    const TextureState before = composition_.state();
    if (!composition_.scaleBy(id, factor)) {
        return false;
    }
    remember(before);
    touch();
    return true;
}

size_t TextureStudio::layerCount() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return composition_.layers().size();
}

size_t TextureStudio::memoryBytes() const {
    std::lock_guard<std::mutex> guard(mutex_);
    // The composed picture is the one buffer nobody sees in the layer list, and it is canvas sized.
    return composition_.memoryBytes() + composed_.memoryBytes();
}

void TextureStudio::remember(const TextureState &before) {
    undo_.push_back(before);
    trim(undo_);
    redo_.clear();
}

void TextureStudio::trim(std::vector<TextureState> &stack) const {
    // The oldest step is always kept, even when one picture is bigger than the window: undoing the
    // last click is worth more than an empty history, and dropping the newest would come to the
    // same thing.
    while (stack.size() > 1 &&
           (stack.size() > kMaxHistorySteps ||
            historyCost(undo_, redo_, composition_.layers()) > kMaxHistoryBytes)) {
        stack.erase(stack.begin());
    }
}

bool TextureStudio::undo() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (undo_.empty()) {
        return false;
    }
    TextureState back = std::move(undo_.back());
    undo_.pop_back();
    redo_.push_back(composition_.state());
    trim(redo_);
    composition_.setState(back);
    touch();
    return true;
}

bool TextureStudio::redo() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (redo_.empty()) {
        return false;
    }
    TextureState forward = std::move(redo_.back());
    redo_.pop_back();
    undo_.push_back(composition_.state());
    trim(undo_);
    composition_.setState(forward);
    touch();
    return true;
}

void TextureStudio::clearHistory() {
    std::lock_guard<std::mutex> guard(mutex_);
    undo_.clear();
    redo_.clear();
}

size_t TextureStudio::historyBytes() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return historyCost(undo_, redo_, composition_.layers());
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
           << " mem="
           << (static_cast<double>(composition_.memoryBytes() + composed_.memoryBytes()) /
               (1024.0 * 1024.0))
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
    // The driver's ceiling is a fact about the device, not about the stack: an import or a project
    // opening throws the pictures away, and the next canvas still has to fit this GPU.
    const int limit = composition_.deviceLimit();
    composition_ = TextureComposition();
    composition_.setDeviceLimit(limit);
    composed_.clear();
    dirty_ = true;
    ++revision_;
    // Another document is not the previous one's before: the pictures of the model that just closed
    // would be held by nothing but this history.
    undo_.clear();
    redo_.clear();
}

uint64_t TextureStudio::revision() {
    std::lock_guard<std::mutex> guard(mutex_);
    return revision_;
}

}  // namespace g3d
