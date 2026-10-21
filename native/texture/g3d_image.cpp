#include "texture/g3d_image.hpp"

#include <cstdlib>
#include <limits>

#include "stb/stb_image.h"
#include "stb/stb_image_write.h"

namespace g3d {
namespace {

void appendToVector(void *context, void *data, int size) {
    auto *out = static_cast<std::vector<uint8_t> *>(context);
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    out->insert(out->end(), bytes, bytes + size);
}

}  // namespace

bool Image::valid() const {
    return width > 0 && height > 0 && pixels.size() == static_cast<size_t>(width) * height * 4;
}

void Image::clear() {
    width = 0;
    height = 0;
    pixels.clear();
    name.clear();
}

const uint8_t *Image::rgba(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return nullptr;
    }
    return pixels.data() + (static_cast<size_t>(y) * width + x) * 4;
}

uint8_t *Image::rgba(int x, int y) {
    return const_cast<uint8_t *>(const_cast<const Image *>(this)->rgba(x, y));
}

bool Image::allocate(int newWidth, int newHeight, const uint8_t fill[4]) {
    if (newWidth <= 0 || newHeight <= 0 ||
        static_cast<long long>(newWidth) * newHeight > kMaxImagePixels) {
        return false;
    }
    width = newWidth;
    height = newHeight;
    pixels.assign(static_cast<size_t>(newWidth) * newHeight * 4, 0);
    if (fill != nullptr) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                uint8_t *pixel = rgba(x, y);
                for (uint32_t channel = 0; channel < 4; ++channel) {
                    pixel[channel] = fill[channel];
                }
            }
        }
    }
    return true;
}

bool decodeImage(const void *data, size_t length, const std::string &name, Image &out,
                 std::string &error) {
    out.clear();
    if (data == nullptr || length == 0) {
        error = "não há bytes para decodificar";
        return false;
    }
    if (length > static_cast<size_t>(std::numeric_limits<int>::max())) {
        error = "arquivo de imagem grande demais";
        return false;
    }
    const int bytes = static_cast<int>(length);
    int width = 0;
    int height = 0;
    int channels = 0;
    // The header is read first: the budget has to refuse an oversized texture before its pixels
    // are allocated, not after.
    if (stbi_info_from_memory(static_cast<const uint8_t *>(data), bytes, &width, &height,
                             &channels) == 0) {
        error = std::string("formato de imagem não reconhecido: ") + stbi_failure_reason();
        return false;
    }
    if (static_cast<long long>(width) * height > kMaxImagePixels) {
        error = "imagem de " + std::to_string(width) + "x" + std::to_string(height) +
                " px acima do limite de " + std::to_string(kMaxImagePixels / (1024 * 1024)) +
                " MP";
        return false;
    }
    // The flip flag is global state in stb; decoding must not depend on what another caller left.
    stbi_set_flip_vertically_on_load(0);
    uint8_t *pixels =
        stbi_load_from_memory(static_cast<const uint8_t *>(data), bytes, &width, &height,
                              &channels, 4);
    if (pixels == nullptr) {
        error = std::string("falha ao decodificar a imagem: ") + stbi_failure_reason();
        return false;
    }
    out.name = name;
    out.width = width;
    out.height = height;
    out.pixels.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
    std::free(pixels);
    return true;
}

bool encodePng(const Image &image, std::vector<uint8_t> &out, std::string &error) {
    out.clear();
    if (!image.valid()) {
        error = "a imagem não tem pixels para codificar";
        return false;
    }
    if (stbi_write_png_to_func(&appendToVector, &out, image.width, image.height, 4,
                               image.pixels.data(), image.width * 4) == 0) {
        out.clear();
        error = "falha ao codificar o PNG";
        return false;
    }
    return true;
}

bool writePngFile(const std::string &path, const Image &image, std::string &error) {
    if (!image.valid()) {
        error = "a imagem não tem pixels para gravar";
        return false;
    }
    if (stbi_write_png(path.c_str(), image.width, image.height, 4, image.pixels.data(),
                       image.width * 4) == 0) {
        error = "falha ao gravar " + path;
        return false;
    }
    return true;
}

void fillCheckerboard(Image &image, int tileSize, const uint8_t toneA[3], const uint8_t toneB[3]) {
    if (!image.valid() || tileSize <= 0) {
        return;
    }
    const uint8_t opaque = 255;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const bool even = ((x / tileSize) + (y / tileSize)) % 2 == 0;
            const uint8_t *tone = even ? toneA : toneB;
            uint8_t *pixel = image.rgba(x, y);
            pixel[0] = tone[0];
            pixel[1] = tone[1];
            pixel[2] = tone[2];
            pixel[3] = opaque;
        }
    }
}

}  // namespace g3d
