// Host harness for the image module: decodes the fixtures in tests/models, encodes PNGs and
// checks the round trip. Built by the host CMake branch, not part of the APK.

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "texture/g3d_image.hpp"

#ifndef G3D_TEST_MODEL_DIR
#define G3D_TEST_MODEL_DIR "."
#endif

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const std::string &label) {
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("FAIL  %s\n", label.c_str());
    } else {
        std::printf("ok    %s\n", label.c_str());
    }
}

std::vector<char> readAll(const std::string &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    return std::vector<char>((std::istreambuf_iterator<char>(stream)),
                             std::istreambuf_iterator<char>());
}

std::string modelPath(const std::string &name) {
    return std::string(G3D_TEST_MODEL_DIR) + "/" + name;
}

// A gradient with a different alpha in every column, so a lossy round trip is visible in the
// channel that carries no colour.
g3d::Image makeSource(int width, int height) {
    g3d::Image image;
    image.width = width;
    image.height = height;
    image.pixels.assign(static_cast<size_t>(width) * height * 4, 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint8_t *pixel = image.rgba(x, y);
            pixel[0] = static_cast<uint8_t>(x * 7 + y * 3);
            pixel[1] = static_cast<uint8_t>(255 - y * 5);
            pixel[2] = static_cast<uint8_t>(x * 3 + y);
            pixel[3] = static_cast<uint8_t>(x * 255 / (width - 1));
        }
    }
    image.name = "fonte";
    return image;
}

bool samePixels(const g3d::Image &a, const g3d::Image &b) {
    if (a.width != b.width || a.height != b.height || a.pixels != b.pixels) {
        return false;
    }
    return true;
}

double meanChannel(const g3d::Image &image, int channel, int x0, int x1, int y0, int y1) {
    double sum = 0.0;
    int count = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            if (const uint8_t *pixel = image.rgba(x, y)) {
                sum += pixel[channel];
                ++count;
            }
        }
    }
    return count == 0 ? -1.0 : sum / count;
}

void testPngRoundTrip() {
    const g3d::Image source = makeSource(24, 16);
    std::vector<uint8_t> encoded;
    std::string error;
    check(g3d::encodePng(source, encoded, error), "uma imagem com alpha vira PNG");
    check(!encoded.empty(), "o PNG tem bytes");

    g3d::Image decoded;
    check(g3d::decodeImage(encoded.data(), encoded.size(), "redondo.png", decoded, error),
          "o PNG codificado volta a decodificar");
    check(decoded.width == 24 && decoded.height == 16, "o PNG preserva as dimensões");
    check(samePixels(source, decoded), "o PNG é sem perdas: cada canal volta igual");
    check(decoded.name == "redondo.png", "o nome da imagem é guardado na camada");

    const std::string path = "host_image_preview.png";
    if (g3d::writePngFile(path, source, error)) {
        const std::vector<char> file = readAll(path);
        g3d::Image fromDisk;
        check(!file.empty() && g3d::decodeImage(file.data(), file.size(), path, fromDisk, error),
              "o PNG escrito no disco decodifica");
        check(!file.empty() && samePixels(source, fromDisk),
              "o PNG do disco é igual ao da memória");
        std::remove(path.c_str());
    } else {
        check(false, "o PNG pode ser escrito no disco");
    }
}

void testJpegFixture() {
    const std::vector<char> bytes = readAll(modelPath("gradient.jpg"));
    check(!bytes.empty(), "a fixture JPEG existe");
    if (bytes.empty()) {
        return;
    }
    g3d::Image image;
    std::string error;
    check(g3d::decodeImage(bytes.data(), bytes.size(), "gradient.jpg", image, error),
          "JPEG decodifica");
    if (!image.valid()) {
        check(false, "o JPEG tem pixels");
        return;
    }
    check(image.width == 64 && image.height == 64, "o JPEG tem as dimensões do arquivo");
    check(meanChannel(image, 3, 0, 64, 0, 64) == 255.0, "JPEG sem alpha chega opaco");
    // The fixture is red on the left, blue on the right, bright green on top and dark green at
    // the bottom, with a black square in the middle. JPEG is lossy, so the tones are compared as
    // means over bands that avoid the square.
    check(meanChannel(image, 0, 0, 8, 0, 16) < meanChannel(image, 0, 56, 64, 0, 16),
          "o JPEG não inverte as colunas");
    check(meanChannel(image, 1, 0, 8, 0, 8) > meanChannel(image, 1, 0, 8, 56, 64),
          "o JPEG não inverte as linhas");
    check(meanChannel(image, 0, 20, 44, 20, 44) < 40.0, "o quadrado preto continua escuro");
}

void testPalettePng() {
    const std::vector<char> bytes = readAll(modelPath("palette.png"));
    check(!bytes.empty(), "a fixture PNG de paleta existe");
    if (bytes.empty()) {
        return;
    }
    g3d::Image image;
    std::string error;
    check(g3d::decodeImage(bytes.data(), bytes.size(), "palette.png", image, error),
          "PNG de paleta decodifica");
    check(image.width == 32 && image.height == 32, "o PNG de paleta tem as dimensões do arquivo");
    if (!image.valid()) {
        return;
    }
    // Index 1 is the transparent one: the band of columns 8..15 in the top rows.
    const uint8_t *transparent = image.rgba(12, 4);
    const uint8_t *opaque = image.rgba(28, 28);
    check(transparent != nullptr && transparent[3] == 0, "a transparência da paleta vira alpha");
    check(opaque != nullptr && opaque[3] == 255, "o resto da paleta fica opaco");
    check(opaque != nullptr && opaque[0] == 0 && opaque[1] == 0 && opaque[2] == 255,
          "a cor da paleta chega em RGBA");
}

void testLimits() {
    std::string error;
    g3d::Image image;

    const std::vector<char> huge = readAll(modelPath("oversized.png"));
    check(!huge.empty(), "a fixture que declara 9000x9000 existe");
    if (!huge.empty()) {
        check(!g3d::decodeImage(huge.data(), huge.size(), "oversized.png", image, error),
              "uma imagem acima do orçamento é recusada");
        check(error.find("limite") != std::string::npos, "a recusa diz o motivo: o limite");
        check(!image.valid(), "a imagem recusada não deixa pixels alocados");
    }

    const std::string garbage = "isso nao e uma imagem";
    check(!g3d::decodeImage(garbage.data(), garbage.size(), "lixo", image, error),
          "bytes que não são imagem são recusados");
    check(!error.empty(), "a recusa de bytes estranhos tem mensagem");

    check(!g3d::decodeImage(nullptr, 0, "nada", image, error), "sem bytes não há decodificação");

    const uint8_t fill[4] = {10, 20, 30, 255};
    g3d::Image small;
    check(!small.allocate(0, 8, fill), "uma largura zero não aloca");
    check(!small.allocate(5000, 5000, fill), "um tamanho acima do orçamento não aloca");
    check(small.allocate(8, 8, fill), "um tamanho normal aloca");
    check(small.valid() && small.rgba(3, 3)[1] == 20, "o preenchimento chega em cada pixel");
    check(small.rgba(8, 0) == nullptr, "fora da imagem não há pixel");
    check(small.rgba(-1, 0) == nullptr, "coordenada negativa não lê memória");
}

void testCheckerboard() {
    g3d::Image image;
    image.width = 16;
    image.height = 16;
    image.pixels.assign(16 * 16 * 4, 0);
    const uint8_t light[3] = {220, 220, 220};
    const uint8_t dark[3] = {40, 40, 40};
    g3d::fillCheckerboard(image, 8, light, dark);
    check(image.rgba(0, 0)[0] == light[0], "o canto claro do quadriculado usa o tom A");
    check(image.rgba(8, 0)[0] == dark[0], "o tile ao lado usa o tom B");
    check(image.rgba(0, 8)[0] == dark[0], "o tile abaixo usa o tom B");
    check(image.rgba(9, 9)[3] == 255, "o quadriculado de referência é opaco");
}

}  // namespace

int main() {
    std::printf("== modulo de imagem ==\n");
    testPngRoundTrip();
    testJpegFixture();
    testPalettePng();
    testLimits();
    testCheckerboard();
    std::printf("%d checagens, %d falhas\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
