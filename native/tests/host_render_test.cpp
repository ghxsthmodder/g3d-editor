// Headless check of the renderer: builds an EGL context through Mesa, compiles the real shaders,
// uploads an imported model, draws into a pbuffer and reads the pixels back. It also exercises tap
// picking, the 2D atlas layer and its own pan/zoom, which use the same maths as on the device.
// Skipped (exit 0) when EGL is unavailable.

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
#include "uv/uv_editor.hpp"

#ifndef G3D_TEST_MODEL_DIR
#define G3D_TEST_MODEL_DIR "."
#endif

namespace {

constexpr int kSize = 64;
constexpr float kBackground[3] = {0.094f, 0.106f, 0.125f};

int failures = 0;
int checks = 0;

// One tap mapped into atlas coordinates.
struct AtlasSample {
    bool hit;
    float u;
    float v;
};

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

// Counts pixels of one colour inside a column range, so the atlas can be inspected separately
// from the 3D preview. Rows are read bottom-up, which does not matter for a full-height band.
int countColorIn(const std::vector<unsigned char> &pixels, int fromX, int toX, float red,
                 float green, float blue, float tolerance = 3.0f) {
    int count = 0;
    for (int y = 0; y < kSize; ++y) {
        for (int x = fromX; x < toX; ++x) {
            if (matches(pixels, static_cast<size_t>(y) * kSize + x, red, green, blue, tolerance)) {
                ++count;
            }
        }
    }
    return count;
}

int countOtherThanIn(const std::vector<unsigned char> &pixels, int fromX, int toX, float red,
                     float green, float blue) {
    return (toX - fromX) * kSize - countColorIn(pixels, fromX, toX, red, green, blue);
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

    // --- the atlas layer -----------------------------------------------------------------
    // The split puts 55% of the surface on the 3D preview, so the atlas owns columns 35..63.
    constexpr int kAtlasLeft = static_cast<int>(static_cast<float>(kSize) * 0.55f);
    constexpr float kGrid[3] = {0.239f, 0.263f, 0.310f};
    constexpr float kBorder[3] = {0.478f, 0.518f, 0.584f};
    constexpr float kOverlap[3] = {0.898f, 0.267f, 0.239f};

    g3d::UvEditor &editor = g3d::UvEditor::instance();
    std::string editError;
    check(editor.begin(0, editError), "sessao de edicao UV abre a malha da cena: " + editError);
    renderer.setViewMode(g3d::Renderer::kViewSplit);
    renderer.draw();
    check(glGetError() == GL_NO_ERROR, "nenhum erro de GL com o atlas visivel");
    const std::vector<unsigned char> split = readPixels();
    maybePreview(std::getenv("G3D_RENDER_PREVIEW"), "atlas", split);

    const int atlasPixels = countOtherThanIn(split, kAtlasLeft, kSize, kBackground[0],
                                            kBackground[1], kBackground[2]);
    // The atlas square is sized by the short side of its panel, so it cannot fill the whole band.
    check(atlasPixels > 500,
          "o painel do atlas mostra o quadrado, pixels=" + std::to_string(atlasPixels));
    const int modelPixels = countOtherThanIn(split, 0, kAtlasLeft, kBackground[0], kBackground[1],
                                             kBackground[2]);
    check(modelPixels > 10, "a vista 3D continua ao lado do atlas, pixels=" +
                                 std::to_string(modelPixels));

    // Islands that claim the same UV space must be impossible to miss in the atlas.
    std::printf("      atlas: %s\n", editor.report().c_str());
    const int overlapPixels =
        countColorIn(split, kAtlasLeft, kSize, kOverlap[0], kOverlap[1], kOverlap[2], 8.0f);
    check(overlapPixels > 0,
          "ilhas sobrepostas aparecem em vermelho, pixels=" + std::to_string(overlapPixels));

    // --- the atlas maps taps back to UV coordinates --------------------------------------
    // Sampled before the message is built: reading the output array inside check() would let the
    // compiler evaluate the text first and print the previous sample.
    const auto sample = [&renderer](float x, float y) {
        float point[2] = {-1.0f, -1.0f};
        const bool hit = renderer.uvAt(x, y, point);
        return AtlasSample{hit, point[0], point[1]};
    };
    const float centreX = static_cast<float>(kAtlasLeft) + static_cast<float>(kSize - kAtlasLeft) / 2.0f;
    const float centreY = static_cast<float>(kSize) / 2.0f;
    const AtlasSample atlasCentre = sample(centreX, centreY);
    check(atlasCentre.hit && std::fabs(atlasCentre.u - 0.5f) < 2e-3f && std::fabs(atlasCentre.v - 0.5f) < 2e-3f,
          "o centro do painel e o centro do atlas, u=" + std::to_string(atlasCentre.u) +
              " v=" + std::to_string(atlasCentre.v));
    check(!sample(1.0f, centreY).hit, "um toque na vista 3D nao cai no atlas");
    if (atlasCentre.hit) {
        const AtlasSample right = sample(centreX + 8.0f, centreY);
        const float rightOffset = right.u - atlasCentre.u;
        check(right.hit && rightOffset > 0.05f,
              "mover para a direita aumenta U, dU=" + std::to_string(rightOffset));
        const AtlasSample above = sample(centreX, centreY - 8.0f);
        check(above.hit && above.v > atlasCentre.v,
              "subir na tela aumenta V, v=" + std::to_string(above.v));
        const AtlasSample below = sample(centreX, centreY + 8.0f);
        check(below.hit && below.v < atlasCentre.v,
              "descer na tela diminui V, v=" + std::to_string(below.v));

        renderer.uvPan(8.0f, -8.0f);
        const AtlasSample dragged = sample(centreX + 8.0f, centreY - 8.0f);
        check(dragged.hit && std::fabs(dragged.u - atlasCentre.u) < 0.02f &&
                  std::fabs(dragged.v - atlasCentre.v) < 0.02f,
              "arrastar move o atlas junto com o dedo, u=" + std::to_string(dragged.u) +
                  " v=" + std::to_string(dragged.v));

        renderer.uvFrame();
        renderer.uvZoom(2.0f);
        const AtlasSample zoomed = sample(centreX + 8.0f, centreY);
        check(zoomed.hit && std::fabs(zoomed.u - atlasCentre.u) < rightOffset * 0.7f,
              "zoom aproxima o ponto do centro, dU=" + std::to_string(zoomed.u - atlasCentre.u));
        renderer.uvFrame();
        const AtlasSample framed = sample(centreX + 8.0f, centreY);
        check(framed.hit && std::fabs(framed.u - atlasCentre.u) > rightOffset * 0.9f,
              "reenquadrar devolve o zoom, dU=" + std::to_string(framed.u - atlasCentre.u));
    }

    // --- selecting an island repaints it --------------------------------------------------
    const int amberBefore = countColorIn(split, kAtlasLeft, kSize, 0.98f, 0.72f, 0.22f, 8.0f);
    check(editor.select(0, false), "selecionar a primeira ilha funciona");
    renderer.draw();
    const std::vector<unsigned char> selectedAtlas = readPixels();
    maybePreview(std::getenv("G3D_RENDER_PREVIEW"), "ilha", selectedAtlas);
    const int amberAfter =
        countColorIn(selectedAtlas, kAtlasLeft, kSize, 0.98f, 0.72f, 0.22f, 8.0f);
    check(amberAfter > amberBefore,
          "a ilha selecionada aparece em ambar, antes=" + std::to_string(amberBefore) +
              " depois=" + std::to_string(amberAfter));

    // --- moving an island moves its pixels ------------------------------------------------
    editor.translate(0.2f, 0.0f);
    renderer.draw();
    const std::vector<unsigned char> shifted = readPixels();
    int shiftedChannels = 0;
    for (size_t i = 0; i < selectedAtlas.size(); ++i) {
        if (std::abs(static_cast<int>(selectedAtlas[i]) - static_cast<int>(shifted[i])) > 4) {
            ++shiftedChannels;
        }
    }
    check(shiftedChannels > 100,
          "arrastar uma ilha muda o atlas, canais=" + std::to_string(shiftedChannels));

    // --- repacking separates the islands -----------------------------------------------
    // Once the islands no longer share UV space the warning colour has to leave the atlas: the
    // only way it comes back is a genuine overlap.
    check(editor.repack(4), "reagrupar as ilhas no atlas funciona");
    renderer.draw();
    const std::vector<unsigned char> repacked = readPixels();
    maybePreview(std::getenv("G3D_RENDER_PREVIEW"), "repack", repacked);
    const int overlapAfterRepack =
        countColorIn(repacked, kAtlasLeft, kSize, kOverlap[0], kOverlap[1], kOverlap[2], 8.0f);
    check(overlapAfterRepack == 0,
          "reagrupadas, nenhuma ilha aparece em vermelho, pixels=" +
              std::to_string(overlapAfterRepack));
    const int amberRepacked =
        countColorIn(repacked, kAtlasLeft, kSize, 0.98f, 0.72f, 0.22f, 8.0f);
    check(amberRepacked > 0,
          "a selecao continua visivel depois de reagrupar, pixels=" +
              std::to_string(amberRepacked));
    int repackChannels = 0;
    for (size_t i = 0; i < shifted.size(); ++i) {
        if (std::abs(static_cast<int>(shifted[i]) - static_cast<int>(repacked[i])) > 4) {
            ++repackChannels;
        }
    }
    check(repackChannels > 100,
          "reagrupar muda o desenho do atlas, canais=" + std::to_string(repackChannels));

    // --- the other view modes -------------------------------------------------------------
    renderer.setViewMode(g3d::Renderer::kViewAtlasOnly);
    renderer.draw();
    const std::vector<unsigned char> onlyAtlas = readPixels();
    maybePreview(std::getenv("G3D_RENDER_PREVIEW"), "so_atlas", onlyAtlas);
    check(renderer.pickAt(centreY, centreY) == -1, "na vista do atlas um toque nao seleciona malha");
    const int leftIsAtlas = countOtherThanIn(onlyAtlas, 0, kAtlasLeft, kBackground[0],
                                             kBackground[1], kBackground[2]);
    check(leftIsAtlas > 1000,
          "o atlas ocupa a superficie inteira, pixels=" + std::to_string(leftIsAtlas));

    // An atlas with no session open still shows its frame, and with nothing painted over it this is
    // where the grid and the border are unambiguously visible.
    editor.cancel();
    renderer.draw();
    const std::vector<unsigned char> atlasEmpty = readPixels();
    maybePreview(std::getenv("G3D_RENDER_PREVIEW"), "vazio", atlasEmpty);
    check(countColorIn(atlasEmpty, 0, kSize, kGrid[0], kGrid[1], kGrid[2]) > 0,
          "a grade do atlas aparece sem edicao aberta");
    check(countColorIn(atlasEmpty, 0, kSize, kBorder[0], kBorder[1], kBorder[2]) > 0,
          "a borda do atlas aparece sem edicao aberta");
    check(countColorIn(atlasEmpty, 0, kSize, kOverlap[0], kOverlap[1], kOverlap[2], 8.0f) == 0,
          "sem edicao aberta nao ha aviso de sobreposicao");
    // The whole surface is the atlas now, so its centre moved to the centre of the surface.
    const AtlasSample emptyCentre = sample(centreY, centreY);
    check(emptyCentre.hit && std::fabs(emptyCentre.u - 0.5f) < 2e-3f,
          "o atlas vazio continua mapeando toques, u=" + std::to_string(emptyCentre.u));

    renderer.setViewMode(g3d::Renderer::kViewThreeDimensional);
    renderer.draw();
    check(glGetError() == GL_NO_ERROR, "fechar a sessao nao deixa erro de GL");
    check(renderer.pickAt(centreY, centreY) >= 0, "a vista 3D volta a aceitar toques");

    std::printf("\n%d verificacoes, %d falhas\n", checks, failures);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
    return failures == 0 ? 0 : 1;
}
