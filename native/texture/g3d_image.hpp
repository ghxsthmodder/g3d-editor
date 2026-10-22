#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace g3d {

// An 8-bit RGBA image in memory: a decoded file, a layer source or the composited texture.
// Rows are stored top-down, the way PNG and JPEG save them, so a file round trip never flips.
struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;  // width * height * 4, always four channels
    std::string name;             // file name shown in the layer list

    bool valid() const;
    void clear();

    // Four bytes of the pixel at x,y. Null when the coordinate is outside, which keeps the
    // sampling code from reading past the buffer.
    const uint8_t *rgba(int x, int y) const;
    uint8_t *rgba(int x, int y);

    // Reserves width x height filled with the given colour. False on a size that would not fit.
    bool allocate(int width, int height, const uint8_t fill[4]);

    // The pixels themselves, which is what the panel adds up for the memory line.
    size_t memoryBytes() const { return pixels.size(); }
};

// Importing a 90 MB texture into an app that also holds a mesh and an atlas would only crash, so
// anything above this pixel count is refused before the buffer is allocated.
constexpr long long kMaxImagePixels = 4096LL * 4096LL;

// Decodes PNG or JPEG from memory into RGBA. The name is kept for the layer list.
bool decodeImage(const void *data, size_t length, const std::string &name, Image &out,
                 std::string &error);

// Encodes the image as a PNG in memory: the exported texture and the file-system write share it.
bool encodePng(const Image &image, std::vector<uint8_t> &out, std::string &error);

// Writes the PNG to a path. Used by the host tests to leave a viewable artefact behind.
bool writePngFile(const std::string &path, const Image &image, std::string &error);

// Reference texture: two tones in tiles of tileSize pixels, so a user can read the distortion and
// the seams of the UVs from the surface of the model.
void fillCheckerboard(Image &image, int tileSize, const uint8_t toneA[3], const uint8_t toneB[3]);

}  // namespace g3d
