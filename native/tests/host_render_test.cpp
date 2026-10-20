// Headless check of the renderer: builds an EGL context through Mesa, compiles the real shaders,
// uploads an imported model, draws into a pbuffer and reads the pixels back. It also exercises tap
// picking, which uses the same matrices as the device. Skipped (exit 0) when EGL is unavailable.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include "core/document.hpp"
#include "importers/assimp_importer.hpp"
#include "renderer/g3d_renderer.hpp"

#ifndef G3D_TEST_MODEL_DIR
#define G3D_TEST_MODEL_DIR "."
#endif

namespace {

constexpr int kSize = 64;
constexpr float kBackground[3] = {0.094f, 0.106f, 0.125f};

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

std::vector<unsigned char> readPixels() {
    std::vector<unsigned char> pixels(static_cast<size_t>(kSize) * kSize * 4, 0);
    glReadPixels(0, 0, kSize, kSize, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    return pixels;
}

bool matches(const std::vector<unsigned char> &pixels, size_t pixel, float red, float green,
             float blue, float tolerance = 6.0f) {
    const size_t offset = pixel * 4;
    const float scale = 255.0f;
    return std::abs(pixels[offset + 0] - red * scale) <= tolerance &&
           std::abs(pixels[offset + 1] - green * scale) <= tolerance &&
           std::abs(pixels[offset + 2] - blue * scale) <= tolerance;
}

int countForeground(const std::vector<unsigned char> &pixels) {
    int count = 0;
    for (size_t pixel = 0; pixel < pixels.size() / 4; ++pixel) {
        if (!matches(pixels, pixel, kBackground[0], kBackground[1], kBackground[2], 3.0f)) {
            ++count;
        }
    }
    return count;
}

int countClose(const std::vector<unsigned char> &pixels, float red, float green, float blue) {
    int count = 0;
    for (size_t pixel = 0; pixel < pixels.size() / 4; ++pixel) {
        if (matches(pixels, pixel, red, green, blue, 8.0f)) {
            ++count;
        }
    }
    return count;
}

std::vector<char> readAll(const std::string &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    return std::vector<char>((std::istreambuf_iterator<char>(stream)),
                             std::istreambuf_iterator<char>());
}

// Optional visual check: G3D_RENDER_PREVIEW=/tmp/look writes PPM images that can be opened
// directly, which is how the renderer was reviewed without a phone attached.
void writePreview(const std::string &prefix, const std::string &suffix,
                  const std::vector<unsigned char> &pixels) {
    std::ofstream out(prefix + "." + suffix + ".ppm", std::ios::binary);
    if (!out) {
        return;
    }
    out << "P6\n" << kSize << " " << kSize << "\n255\n";
    std::vector<unsigned char> row(static_cast<size_t>(kSize) * 3);
    for (int y = kSize - 1; y >= 0; --y) {  // GL reads bottom-up
        const size_t base = static_cast<size_t>(y) * kSize * 4;
        for (int x = 0; x < kSize; ++x) {
            row[static_cast<size_t>(x) * 3 + 0] = pixels[base + static_cast<size_t>(x) * 4 + 0];
            row[static_cast<size_t>(x) * 3 + 1] = pixels[base + static_cast<size_t>(x) * 4 + 1];
            row[static_cast<size_t>(x) * 3 + 2] = pixels[base + static_cast<size_t>(x) * 4 + 2];
        }
        out.write(reinterpret_cast<const char *>(row.data()),
                  static_cast<std::streamsize>(row.size()));
    }
    std::printf("      preview escrita em %s.%s.ppm\n", prefix.c_str(), suffix.c_str());
}

void maybePreview(const char *prefix, const std::string &suffix,
                  const std::vector<unsigned char> &pixels) {
    if (prefix != nullptr) {
        writePreview(prefix, suffix, pixels);
    }
}

}  // namespace

int main() {
    // A headless run has no X11 or DRM master, so prefer Mesa's surfaceless platform when the
    // loader advertises it and fall back to the default display otherwise.
    const char *clientExtensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    EGLDisplay display = EGL_NO_DISPLAY;
#ifdef EGL_PLATFORM_SURFACELESS_MESA
    if (clientExtensions != nullptr && std::strstr(clientExtensions, "EGL_MESA_platform_surfaceless") != nullptr) {
        display = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    }
#endif
    if (display == EGL_NO_DISPLAY) {
        display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    }

    EGLint major = 0;
    EGLint minor = 0;
    if (display == EGL_NO_DISPLAY || eglInitialize(display, &major, &minor) != EGL_TRUE) {
        std::printf("SKIP  no EGL display available for the headless renderer test\n");
        return 0;
    }

    const EGLint configAttribs[] = {EGL_SURFACE_TYPE,
                                    EGL_PBUFFER_BIT,
                                    EGL_RENDERABLE_TYPE,
                                    EGL_OPENGL_ES3_BIT_KHR,
                                    EGL_RED_SIZE,
                                    8,
                                    EGL_GREEN_SIZE,
                                    8,
                                    EGL_BLUE_SIZE,
                                    8,
                                    EGL_DEPTH_SIZE,
                                    16,
                                    EGL_NONE};
    EGLConfig config = nullptr;
    EGLint configCount = 0;
    if (eglChooseConfig(display, configAttribs, &config, 1, &configCount) != EGL_TRUE ||
        configCount < 1) {
        std::printf("SKIP  no ES3-capable EGL config (choose error 0x%x)\n", eglGetError());
        return 0;
    }

    const EGLint surfaceAttribs[] = {EGL_WIDTH, kSize, EGL_HEIGHT, kSize, EGL_NONE};
    const EGLSurface surface = eglCreatePbufferSurface(display, config, surfaceAttribs);
    const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    const EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
        eglMakeCurrent(display, surface, surface, context) != EGL_TRUE) {
        std::printf("SKIP  could not make an ES3 context current (error 0x%x)\n", eglGetError());
        return 0;
    }

    std::printf("      renderer=%s version=%s\n", reinterpret_cast<const char *>(glGetString(GL_RENDERER)),
                reinterpret_cast<const char *>(glGetString(GL_VERSION)));

    // --- geometry reaches the GPU -------------------------------------------------------
    const std::string path = std::string(G3D_TEST_MODEL_DIR) + "/cube.obj";
    const std::vector<char> bytes = readAll(path);
    std::string error;
    const bool imported =
        g3d::Document::instance().importModel(bytes.data(), bytes.size(), path, error);
    check(imported, "documento recebe o modelo importado: " + error);

    g3d::Renderer &renderer = g3d::Renderer::instance();
    check(renderer.onContextCreated(), "contexto GL cria shaders e geometria");
    renderer.onSurfaceChanged(kSize, kSize);
    check(glGetError() == GL_NO_ERROR, "nenhum erro de GL apos a configuracao");

    renderer.draw();
    check(glGetError() == GL_NO_ERROR, "nenhum erro de GL durante o desenho");
    const std::vector<unsigned char> solid = readPixels();
    maybePreview(std::getenv("G3D_RENDER_PREVIEW"), "solido", solid);
    const int covered = countForeground(solid);
    check(covered > kSize, "o modelo cobre parte da imagem, pixels=" + std::to_string(covered));

    // --- shading modes really change the output -----------------------------------------
    renderer.setShadingMode(1);
    renderer.draw();
    const std::vector<unsigned char> normals = readPixels();
    int different = 0;
    for (size_t i = 0; i < solid.size(); ++i) {
        if (std::abs(static_cast<int>(solid[i]) - static_cast<int>(normals[i])) > 4) {
            ++different;
        }
    }
    maybePreview(std::getenv("G3D_RENDER_PREVIEW"), "normais", normals);
    check(different > 100, "modo de normais produz outra imagem, canais=" + std::to_string(different));
    renderer.setShadingMode(0);

    // --- tap picking uses the same matrices as the draw ---------------------------------
    const int centre = renderer.pickAt(static_cast<float>(kSize) / 2.0f, static_cast<float>(kSize) / 2.0f);
    check(centre == 0, "toque no centro seleciona a malha, indice=" + std::to_string(centre));
    const int corner = renderer.pickAt(0.5f, 0.5f);
    check(corner == -1, "toque no canto nao seleciona nada, indice=" + std::to_string(corner));

    // --- the selection outline is drawn --------------------------------------------------
    g3d::Document::instance().select(centre);
    renderer.draw();
    const std::vector<unsigned char> outlined = readPixels();
    maybePreview(std::getenv("G3D_RENDER_PREVIEW"), "selecao", outlined);
    const int outlinePixels = countClose(outlined, 0.98f, 0.72f, 0.22f);
    check(outlinePixels > 0, "contorno da malha selecionada aparece, pixels=" +
                                 std::to_string(outlinePixels));

    // --- gestures move the camera --------------------------------------------------------
    const std::vector<unsigned char> before = outlined;
    renderer.orbit(30.0f, 12.0f);
    renderer.draw();
    const std::vector<unsigned char> after = readPixels();
    int moved = 0;
    for (size_t i = 0; i < before.size(); ++i) {
        if (std::abs(static_cast<int>(before[i]) - static_cast<int>(after[i])) > 4) {
            ++moved;
        }
    }
    check(moved > 100, "orbitar muda a imagem, canais=" + std::to_string(moved));

    std::printf("\n%d verificacoes, %d falhas\n", checks, failures);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
    return failures == 0 ? 0 : 1;
}
