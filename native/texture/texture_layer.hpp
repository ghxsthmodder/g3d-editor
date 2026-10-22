#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "texture/g3d_image.hpp"

namespace g3d {

// One imported image placed on the atlas. The canvas is measured in pixels with the y axis
// pointing down, the same way a PNG stores its rows, so the exported file and the on-screen
// composition agree.
struct TextureLayer {
    int id = 0;
    std::string name;
    Image image;
    float centreX = 0.0f;          // pixels from the left edge
    float centreY = 0.0f;          // pixels from the top edge
    float rotationDegrees = 0.0f;  // clockwise as the image is seen
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float opacity = 1.0f;  // 0..1, multiplied by the alpha of each pixel
    bool visible = true;
};

// The layer stack of the compositor: the order of the vector is the order of painting, first
// element at the bottom. Everything here is CPU code with no GL and no file access, which is what
// makes the exported PNG and the previewed texture the same image.
class TextureComposition {
public:
    // The canvas is the atlas resolution: it has to fit inside the texture the model samples.
    bool setCanvas(int width, int height, std::string &error);
    int canvasWidth() const { return canvasWidth_; }
    int canvasHeight() const { return canvasHeight_; }

    // The biggest texture this GPU can be given, read from the driver once the context
    // exists. Zero means nobody has said, which is the state a CPU-only build or a host test
    // starts in. The canvas is the texture the model samples, so a side above it is refused
    // before anything is allocated.
    void setDeviceLimit(int side) { deviceLimit_ = side > 0 ? side : 0; }
    int deviceLimit() const { return deviceLimit_; }

    void setBackground(const uint8_t rgb[3]);
    void background(uint8_t out[3]) const;

    // Adds a layer with the image already decoded by the caller. The layer starts centred on the
    // canvas, at its own pixel size. Returns the new id, or -1 with error set.
    int addLayer(const Image &image, std::string &error);
    // The same layer, but placed as a saved project had it: a restored stack has to come back where
    // it was, not centred. Only the placement is read from `at`; the picture is the one given.
    int addLayerAt(const Image &image, const TextureLayer &at, std::string &error);
    bool removeLayer(int id);
    bool duplicateLayer(int id, int &newId, std::string &error);

    bool moveUp(int id);
    bool moveDown(int id);

    bool setVisible(int id, bool visible);
    bool setOpacity(int id, float opacity);
    // Pixels of displacement; the axes agree with the y-down canvas.
    bool translate(int id, float dxPixels, float dyPixels);
    bool rotate(int id, float degreesDelta);
    // Multiplies both axes, so the picture does not change shape.
    bool scaleBy(int id, float factor);

    const std::vector<TextureLayer> &layers() const { return layers_; }
    const TextureLayer *layer(int id) const;
    bool hasLayers() const { return !layers_.empty(); }
    size_t memoryBytes() const;

    // Paints the background and every visible layer into out. The result is always the same for
    // the same stack, so a test can compare pixels and a preview cannot drift from the export.
    bool compose(Image &out, std::string &error) const;

private:
    const TextureLayer *find(int id) const;
    TextureLayer *find(int id);

    int canvasWidth_ = 0;
    int canvasHeight_ = 0;
    int deviceLimit_ = 0;
    uint8_t background_[3] = {46, 50, 60};
    std::vector<TextureLayer> layers_;
    int nextId_ = 1;
};

}  // namespace g3d
