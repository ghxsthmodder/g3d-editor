#include "texture/texture_layer.hpp"

#include <algorithm>
#include <cmath>

namespace g3d {
namespace {

constexpr int kMaxCanvasSide = 8192;
constexpr size_t kMaxLayers = 24;
// The stack lives in RAM next to the mesh and the atlas, so a pile of full-size pictures is
// refused before the process is killed for it.
constexpr size_t kMaxLayerBytes = 64u * 1024u * 1024u;

uint8_t toByte(float fraction) {
    return static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, fraction)) * 255.0f + 0.5f);
}

// Bilinear sample in pixel-centre coordinates. False when the point falls outside the picture,
// which is where the layer stops covering the canvas.
bool sampleBilinear(const Image &image, float sx, float sy, float out[4]) {
    const float fx = sx - 0.5f;
    const float fy = sy - 0.5f;
    if (fx < -1.0f || fy < -1.0f || fx > static_cast<float>(image.width) ||
        fy > static_cast<float>(image.height)) {
        return false;
    }
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const float wx = fx - static_cast<float>(x0);
    const float wy = fy - static_cast<float>(y0);
    for (int channel = 0; channel < 4; ++channel) {
        out[channel] = 0.0f;
    }
    for (int offsetY = 0; offsetY < 2; ++offsetY) {
        for (int offsetX = 0; offsetX < 2; ++offsetX) {
            const int x = std::min(std::max(x0 + offsetX, 0), image.width - 1);
            const int y = std::min(std::max(y0 + offsetY, 0), image.height - 1);
            const float weight = (offsetX ? wx : 1.0f - wx) * (offsetY ? wy : 1.0f - wy);
            const uint8_t *pixel = image.rgba(x, y);
            for (int channel = 0; channel < 4; ++channel) {
                out[channel] += weight * static_cast<float>(pixel[channel]);
            }
        }
    }
    return true;
}

}  // namespace

bool TextureComposition::setCanvas(int width, int height, std::string &error) {
    if (width <= 0 || height <= 0 || width > kMaxCanvasSide || height > kMaxCanvasSide ||
        static_cast<long long>(width) * height > kMaxImagePixels) {
        error = "a tela do compositor precisa ficar entre 1 e " + std::to_string(kMaxCanvasSide) +
                " px por lado";
        return false;
    }
    canvasWidth_ = width;
    canvasHeight_ = height;
    return true;
}

void TextureComposition::setBackground(const uint8_t rgb[3]) {
    background_[0] = rgb[0];
    background_[1] = rgb[1];
    background_[2] = rgb[2];
}

void TextureComposition::background(uint8_t out[3]) const {
    out[0] = background_[0];
    out[1] = background_[1];
    out[2] = background_[2];
}

TextureLayer *TextureComposition::find(int id) {
    for (TextureLayer &layer : layers_) {
        if (layer.id == id) {
            return &layer;
        }
    }
    return nullptr;
}

const TextureLayer *TextureComposition::find(int id) const {
    return const_cast<TextureComposition *>(this)->find(id);
}

const TextureLayer *TextureComposition::layer(int id) const {
    return find(id);
}

size_t TextureComposition::memoryBytes() const {
    size_t bytes = 0;
    for (const TextureLayer &layer : layers_) {
        bytes += layer.image.pixels.size();
    }
    return bytes;
}

int TextureComposition::addLayer(const Image &image, std::string &error) {
    if (!image.valid()) {
        error = "a imagem da camada não tem pixels";
        return -1;
    }
    if (canvasWidth_ <= 0 || canvasHeight_ <= 0) {
        error = "defina a resolução da textura antes de criar camadas";
        return -1;
    }
    if (layers_.size() >= kMaxLayers) {
        error = "o compositor não aceita mais que " + std::to_string(kMaxLayers) + " camadas";
        return -1;
    }
    if (memoryBytes() + image.pixels.size() > kMaxLayerBytes) {
        error = "as camadas passariam de " + std::to_string(kMaxLayerBytes / (1024 * 1024)) +
                " MB";
        return -1;
    }
    TextureLayer layer;
    layer.id = nextId_++;
    layer.name = image.name.empty() ? "camada " + std::to_string(layer.id) : image.name;
    layer.image = image;
    layer.centreX = canvasWidth_ * 0.5f;
    layer.centreY = canvasHeight_ * 0.5f;
    layers_.push_back(std::move(layer));
    return layers_.back().id;
}

bool TextureComposition::removeLayer(int id) {
    for (size_t index = 0; index < layers_.size(); ++index) {
        if (layers_[index].id == id) {
            layers_.erase(layers_.begin() + static_cast<long>(index));
            return true;
        }
    }
    return false;
}

bool TextureComposition::duplicateLayer(int id, int &newId, std::string &error) {
    const TextureLayer *source = find(id);
    if (source == nullptr) {
        return false;
    }
    // addLayer may move the whole vector, so the placement is read out of it first.
    const std::string name = source->name;
    const float centreX = source->centreX;
    const float centreY = source->centreY;
    const float rotationDegrees = source->rotationDegrees;
    const float scaleX = source->scaleX;
    const float scaleY = source->scaleY;
    const float opacity = source->opacity;
    const bool visible = source->visible;

    newId = addLayer(source->image, error);
    if (newId < 0) {
        return false;
    }
    TextureLayer *copy = find(newId);
    copy->name = name + " (cópia)";
    copy->centreX = centreX;
    copy->centreY = centreY;
    copy->rotationDegrees = rotationDegrees;
    copy->scaleX = scaleX;
    copy->scaleY = scaleY;
    copy->opacity = opacity;
    copy->visible = visible;
    return true;
}

bool TextureComposition::moveUp(int id) {
    for (size_t index = 0; index + 1 < layers_.size(); ++index) {
        if (layers_[index].id == id) {
            std::swap(layers_[index], layers_[index + 1]);
            return true;
        }
    }
    return false;
}

bool TextureComposition::moveDown(int id) {
    for (size_t index = 1; index < layers_.size(); ++index) {
        if (layers_[index].id == id) {
            std::swap(layers_[index], layers_[index - 1]);
            return true;
        }
    }
    return false;
}

bool TextureComposition::setVisible(int id, bool visible) {
    TextureLayer *layer = find(id);
    if (layer == nullptr) {
        return false;
    }
    layer->visible = visible;
    return true;
}

bool TextureComposition::setOpacity(int id, float opacity) {
    TextureLayer *layer = find(id);
    if (layer == nullptr || !(opacity >= 0.0f) || opacity > 1.0f) {
        return false;
    }
    layer->opacity = opacity;
    return true;
}

bool TextureComposition::translate(int id, float dxPixels, float dyPixels) {
    TextureLayer *layer = find(id);
    if (layer == nullptr) {
        return false;
    }
    layer->centreX += dxPixels;
    layer->centreY += dyPixels;
    return true;
}

bool TextureComposition::rotate(int id, float degreesDelta) {
    TextureLayer *layer = find(id);
    if (layer == nullptr) {
        return false;
    }
    // Kept inside one turn so the status line shows a readable angle after many clicks.
    float angle = std::fmod(layer->rotationDegrees + degreesDelta, 360.0f);
    if (angle < 0.0f) {
        angle += 360.0f;
    }
    layer->rotationDegrees = angle;
    return true;
}

bool TextureComposition::scaleBy(int id, float factor) {
    TextureLayer *layer = find(id);
    if (layer == nullptr || !(factor > 0.0f) || factor > 100.0f) {
        return false;
    }
    layer->scaleX *= factor;
    layer->scaleY *= factor;
    return true;
}

bool TextureComposition::compose(Image &out, std::string &error) const {
    if (canvasWidth_ <= 0 || canvasHeight_ <= 0) {
        error = "defina a resolução da textura antes de compor";
        return false;
    }
    if (!out.allocate(canvasWidth_, canvasHeight_, nullptr)) {
        error = "não foi possível reservar a textura";
        return false;
    }
    for (size_t pixel = 0; pixel < out.pixels.size(); pixel += 4) {
        out.pixels[pixel] = background_[0];
        out.pixels[pixel + 1] = background_[1];
        out.pixels[pixel + 2] = background_[2];
        out.pixels[pixel + 3] = 255;
    }

    for (const TextureLayer &layer : layers_) {
        if (!layer.visible || layer.opacity <= 0.0f || !layer.image.valid() ||
            layer.scaleX <= 0.0f || layer.scaleY <= 0.0f) {
            continue;
        }

        const float radians = layer.rotationDegrees * 0.017453292519943295f;
        const float cosine = std::cos(radians);
        const float sine = std::sin(radians);
        const float halfWidth = (layer.image.width - 1) * 0.5f * layer.scaleX;
        const float halfHeight = (layer.image.height - 1) * 0.5f * layer.scaleY;

        // The rotated corners bound the pixels the layer can touch; a small decal does not pay a
        // pass over the whole canvas.
        float minimumX = layer.centreX, maximumX = layer.centreX;
        float minimumY = layer.centreY, maximumY = layer.centreY;
        const float corners[4][2] = {
            {-halfWidth, -halfHeight}, {halfWidth, -halfHeight},
            {halfWidth, halfHeight},   {-halfWidth, halfHeight},
        };
        for (const auto &corner : corners) {
            const float x = layer.centreX + corner[0] * cosine - corner[1] * sine;
            const float y = layer.centreY + corner[0] * sine + corner[1] * cosine;
            minimumX = std::min(minimumX, x);
            maximumX = std::max(maximumX, x);
            minimumY = std::min(minimumY, y);
            maximumY = std::max(maximumY, y);
        }
        const int firstX = std::max(0, static_cast<int>(std::floor(minimumX)));
        const int lastX = std::min(out.width - 1, static_cast<int>(std::ceil(maximumX)));
        const int firstY = std::max(0, static_cast<int>(std::floor(minimumY)));
        const int lastY = std::min(out.height - 1, static_cast<int>(std::ceil(maximumY)));

        const float centreSourceX = (layer.image.width - 1) * 0.5f;
        const float centreSourceY = (layer.image.height - 1) * 0.5f;
        const float opacity = layer.opacity;

        for (int y = firstY; y <= lastY; ++y) {
            for (int x = firstX; x <= lastX; ++x) {
                // Undo the placement: centre, rotation, scale, then into the picture's pixels.
                const float ux = static_cast<float>(x) + 0.5f - layer.centreX;
                const float uy = static_cast<float>(y) + 0.5f - layer.centreY;
                const float rotatedX = ux * cosine + uy * sine;
                const float rotatedY = -ux * sine + uy * cosine;
                const float sourceX = centreSourceX + rotatedX / layer.scaleX;
                const float sourceY = centreSourceY + rotatedY / layer.scaleY;

                float source[4];
                if (!sampleBilinear(layer.image, sourceX, sourceY, source)) {
                    continue;
                }
                const float alpha = source[3] / 255.0f * opacity;
                if (alpha <= 0.0f) {
                    continue;
                }

                uint8_t *destination = out.rgba(x, y);
                const float belowAlpha = destination[3] / 255.0f;
                const float covered = 1.0f - alpha;
                const float resultAlpha = alpha + belowAlpha * covered;
                for (int channel = 0; channel < 3; ++channel) {
                    // Straight alpha, source over: the PNG that is written has to stay editable
                    // as a layer file instead of carrying premultiplied colours.
                    const float below = destination[channel] / 255.0f * belowAlpha * covered;
                    destination[channel] =
                        toByte((source[channel] / 255.0f * alpha + below) / resultAlpha);
                }
                destination[3] = toByte(resultAlpha);
            }
        }
    }
    return true;
}

}  // namespace g3d
