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
#include "texture/g3d_image.hpp"
#include "texture/texture_studio.hpp"
#include "uv/uv_editor.hpp"

#ifndef G3D_TEST_MODEL_DIR
#define G3D_TEST_MODEL_DIR "."
#endif

namespace {

constexpr int kSize = 64;
// Read from the renderer instead of being written again here: an assertion against a copy of a
// colour would pass while the screen changed.
using g3d::kAtlasBorder;
using g3d::kAtlasGrid;
using g3d::kAtlasOverlap;
using g3d::kBackground;
using g3d::kOutlineColor;

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

// The offset that lines one colour up with itself, in surface pixels: the column and the row counted
// from the top, the way a finger moves. A dragged island is a rigid shape, so the best alignment is
// the drag, and an outline clipped by the edge of the panel does not skew it the way it skews the
// centre of the shape. Returns how many pixels line up.
int colourShift(const std::vector<unsigned char> &before, const std::vector<unsigned char> &after,
                 int fromX, int toX, float red, float green, float blue, int out[2]) {
    int bestScore = 0;
    out[0] = 0;
    out[1] = 0;
    for (int offsetY = -6; offsetY <= 6; ++offsetY) {
        for (int offsetX = -6; offsetX <= 6; ++offsetX) {
            int score = 0;
            for (int row = 0; row < kSize; ++row) {
                // glReadPixels returns the bottom row first, so a surface row grows downwards from
                // the offset above by reading the other image further up.
                const int sourceRow = row + offsetY;
                if (sourceRow < 0 || sourceRow >= kSize) {
                    continue;
                }
                for (int column = fromX; column < toX; ++column) {
                    const int sourceColumn = column - offsetX;
                    if (sourceColumn < fromX || sourceColumn >= toX) {
                        continue;
                    }
                    if (matches(before, static_cast<size_t>(sourceRow) * kSize + sourceColumn, red,
                                green, blue, 8.0f) &&
                        matches(after, static_cast<size_t>(row) * kSize + column, red, green, blue,
                                8.0f)) {
                        ++score;
                    }
                }
            }
            if (score > bestScore) {
                bestScore = score;
                out[0] = offsetX;
                out[1] = offsetY;
            }
        }
    }
    return bestScore;
}

// Pixels where the layer green beats the canvas blue, whatever the wireframe left on top of it.
int greenishOnTop(const std::vector<unsigned char> &pixels) {
    int count = 0;
    for (size_t pixel = 0; pixel < pixels.size() / 4; ++pixel) {
        const size_t offset = pixel * 4;
        if (pixels[offset + 1] > 120 && pixels[offset + 1] > pixels[offset + 2] + 30) {
            ++count;
        }
    }
    return count;
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

    // The ceiling this driver gives is what the compositor refuses above, so the phone's own limit is
    // read here instead of only being trusted to work on a device.
    GLint maxSide = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSide);
    check(maxSide > 0, "o driver diz o maior lado de textura: " + std::to_string(maxSide));
    const int published = g3d::TextureStudio::instance().deviceLimit();
    check(published == static_cast<int>(maxSide),
          "o estúdio recebe o limite da GPU: " + std::to_string(published));
    std::string canvasError;
    const bool refused = !g3d::TextureStudio::instance().setCanvas(1024 * 1024, 1024 * 1024,
                                                                  canvasError);
    check(refused, "uma tela acima do limite do driver é recusada antes de alocar");
    g3d::TextureStudio::instance().reset();

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
    const int outlinePixels =
        countClose(outlined, kOutlineColor[0], kOutlineColor[1], kOutlineColor[2]);
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
    // Where the atlas starts is whatever the renderer says, so the assertion below reads the number
    // the surface was actually divided with instead of a copy of it.
    const int kAtlasLeft = static_cast<int>(static_cast<float>(kSize) * renderer.splitRatio());

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
        countColorIn(split, kAtlasLeft, kSize, kAtlasOverlap[0], kAtlasOverlap[1],
                                         kAtlasOverlap[2], 8.0f);
    check(overlapPixels > 0,
          "ilhas sobrepostas aparecem em vermelho, pixels=" + std::to_string(overlapPixels));

    // --- the atlas can take the bigger part of the surface ---------------------------------
    renderer.setSplitRatio(g3d::kAtlasDominantSplitRatio);
    const int narrowLeft = static_cast<int>(static_cast<float>(kSize) * renderer.splitRatio());
    renderer.draw();
    const std::vector<unsigned char> dominant = readPixels();
    maybePreview(std::getenv("G3D_RENDER_PREVIEW"), "atlas_dominante", dominant);
    const int dominantPixels = countOtherThanIn(dominant, narrowLeft, kSize, kBackground[0],
                                                kBackground[1], kBackground[2]);
    check(dominantPixels > atlasPixels,
          "o atlas cresce quando e o assunto da tela, antes=" + std::to_string(atlasPixels) +
              " depois=" + std::to_string(dominantPixels));
    const int sliverPixels = countOtherThanIn(dominant, 0, narrowLeft, kBackground[0],
                                              kBackground[1], kBackground[2]);
    check(sliverPixels > 0 && narrowLeft < kAtlasLeft,
          "a vista 3D encolhe mas continua mostrando o modelo, pixels=" +
              std::to_string(sliverPixels));
    // A ratio that would erase one of the panels is brought back to one that works.
    renderer.setSplitRatio(4.0f);
    check(renderer.splitRatio() <= 0.9f && renderer.splitRatio() >= 0.1f,
          "uma razao absurda e limitada, razao=" + std::to_string(renderer.splitRatio()));
    renderer.setSplitRatio(g3d::kDefaultSplitRatio);

    // --- a tall surface is divided along its height --------------------------------------
    // The app is locked to landscape, but the surface is whatever the window hands over: a tablet
    // held up or a split-screen window comes out taller than wide, and two panels side by side
    // there would each be measured by half of the short side. Stacked, both keep every pixel
    // across. The bound below is 92% of the width because the square keeps a margin of its own, and
    // that margin is the border's, not a tax on the panel.
    renderer.onSurfaceChanged(360, 640);
    renderer.setSplitRatio(g3d::kAtlasDominantSplitRatio);
    int atlasFirst = -1;
    int atlasLast = -1;
    for (int x = 0; x < 360; ++x) {
        float point[2] = {-1.0f, -1.0f};
        if (!renderer.uvAt(static_cast<float>(x) + 0.5f, 400.0f, point)) {
            continue;
        }
        if (atlasFirst < 0) {
            atlasFirst = x;
        }
        atlasLast = x;
    }
    const int atlasSpan = atlasLast - atlasFirst + 1;
    check(atlasSpan * 100 >= 360 * 92,
          "em pe o atlas e medido pela largura quase toda, alcance=" + std::to_string(atlasSpan));
    check(atlasFirst > 0 && atlasFirst + atlasLast == 359,
          "o quadrado do atlas fica centrado na tela, primeiro=" + std::to_string(atlasFirst) +
              " ultimo=" + std::to_string(atlasLast));
    float stripUv[2] = {-1.0f, -1.0f};
    check(!renderer.uvAt(180.0f, 30.0f, stripUv),
          "a faixa de cima e do modelo: ali o toque nao cai no atlas");

    // --- a wide surface hands the atlas only what a square can use -------------------------------
    // The ratio is a fraction of the width, but the square is measured by the height: wider than
    // that, the extra columns are letterbox no island is ever drawn in. The model draws whatever
    // rectangle it is handed, so the surplus goes to the 3D view instead of sitting empty beside
    // the atlas. The panel bounds are private, so what is read here is the square: centred in its
    // panel it leaves one margin on each side, and the model begins one further margin left.
    renderer.onSurfaceChanged(640, 360);
    int wideFirst = -1;
    int wideLast = -1;
    for (int x = 0; x < 640; ++x) {
        float point[2] = {-1.0f, -1.0f};
        if (!renderer.uvAt(static_cast<float>(x) + 0.5f, 180.0f, point)) {
            continue;
        }
        if (wideFirst < 0) {
            wideFirst = x;
        }
        wideLast = x;
    }
    const int wideSpan = wideLast - wideFirst + 1;
    check(wideSpan * 100 >= 360 * 95 && wideSpan <= 360,
          "em deitado o quadrado do atlas e medido pela altura, alcance=" +
              std::to_string(wideSpan));
    const int atlasMargin = 639 - wideLast;
    check(atlasMargin * 100 <= 360 * 5,
          "a direita do quadrado e a margem do painel e nao letterbox, folga=" +
              std::to_string(atlasMargin));
    check(wideFirst - atlasMargin >= 200,
          "o modelo recebe a largura que o quadrado nao usa, colunas=" +
              std::to_string(wideFirst - atlasMargin));
    // To the pixel: past the surface height the square cannot grow, so the columns beyond it belong
    // to the model. The cap the renderer applies is the height itself, not the height divided by
    // the atlas's own margin, which hands the panel a band no island is ever drawn in.
    check(wideFirst - atlasMargin >= 640 - 360,
          "o painel do atlas nao passa da altura, o resto e do modelo, colunas=" +
              std::to_string(wideFirst - atlasMargin));
    float modelUv[2] = {-1.0f, -1.0f};
    check(!renderer.uvAt(50.0f, 180.0f, modelUv), "a esquerda continua sendo o modelo");
    renderer.onSurfaceChanged(kSize, kSize);
    renderer.setSplitRatio(g3d::kDefaultSplitRatio);

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
    const int amberBefore = countColorIn(split, kAtlasLeft, kSize, kOutlineColor[0],
                                         kOutlineColor[1], kOutlineColor[2], 8.0f);
    check(editor.select(0, false), "selecionar a primeira ilha funciona");
    renderer.draw();
    const std::vector<unsigned char> selectedAtlas = readPixels();
    maybePreview(std::getenv("G3D_RENDER_PREVIEW"), "ilha", selectedAtlas);
    const int amberAfter = countColorIn(selectedAtlas, kAtlasLeft, kSize, kOutlineColor[0],
                                        kOutlineColor[1], kOutlineColor[2], 8.0f);
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

    // --- dragging with a finger moves the island by the same number of pixels -------------
    // The translation above left the islands half outside the atlas square, so they are fitted back
    // in before measuring, and the grab starts from an empty selection to see what it selects.
    check(editor.fit(), "reajustar as ilhas antes de medir o arrasto funciona");
    editor.clearSelection();

    // Which island lands on which pixel is the packer's choice, so the point the grab is asked about
    // is looked for in the atlas instead of assumed from where the islands were expected to be.
    float islandPixel[2] = {0.0f, 0.0f};
    bool foundIsland = false;
    for (int row = 0; row < kSize && !foundIsland; ++row) {
        for (int column = kAtlasLeft; column < kSize && !foundIsland; ++column) {
            float uv[2];
            if (!renderer.uvAt(static_cast<float>(column), static_cast<float>(row), uv)) {
                continue;
            }
            if (editor.pick(uv[0], uv[1]) != 0) {
                continue;
            }
            islandPixel[0] = static_cast<float>(column);
            islandPixel[1] = static_cast<float>(row);
            foundIsland = true;
        }
    }
    check(foundIsland, "a primeira ilha tem um pixel para agarrar, coluna=" +
                           std::to_string(islandPixel[0]) + " linha=" +
                           std::to_string(islandPixel[1]));
    check(!renderer.uvGrab(static_cast<float>(kAtlasLeft) - 10.0f, 10.0f, false),
          "agarrar fora do atlas nao pega ilha nenhuma");
    check(!editor.isSelected(0u), "sem agarrar, a primeira ilha continua fora da selecao");
    check(renderer.uvGrab(islandPixel[0], islandPixel[1], false),
          "agarrar uma ilha sem selecao funciona");
    check(editor.isSelected(0u), "o arrasto seleciona a ilha que foi agarrada");

    renderer.draw();
    const std::vector<unsigned char> beforeDrag = readPixels();
    const int amberDragged = countColorIn(beforeDrag, kAtlasLeft, kSize, kOutlineColor[0],
                                          kOutlineColor[1], kOutlineColor[2], 8.0f);
    check(amberDragged > 20,
          "a ilha agarrada aparece em ambar, pixels=" + std::to_string(amberDragged));
    renderer.uvDragPixels(-2.0f, 2.0f);
    renderer.draw();
    const std::vector<unsigned char> afterDrag = readPixels();
    maybePreview(std::getenv("G3D_RENDER_PREVIEW"), "arrastar_ilha", afterDrag);
    int shift[2] = {0, 0};
    const int aligned = colourShift(beforeDrag, afterDrag, kAtlasLeft, kSize, kOutlineColor[0],
                                    kOutlineColor[1], kOutlineColor[2], shift);
    check(shift[0] == -2 && shift[1] == 2 && aligned * 2 > amberDragged,
          "arrastar 2 pixels move a ilha 2 pixels, coluna=" + std::to_string(shift[0]) +
              " linha=" + std::to_string(shift[1]) + " alinhados=" + std::to_string(aligned) +
              " de " + std::to_string(amberDragged));

    // Grabbing an island that is already in the group leaves the group alone. Handing it to select()
    // again would toggle it off, and a selection of two islands would shrink without anyone asking.
    check(editor.select(1u, true), "somar uma segunda ilha a selecao funciona");
    check(renderer.uvGrab(islandPixel[0], islandPixel[1], false),
          "agarrar uma ilha ja selecionada funciona");
    check(editor.isSelected(0u) && editor.isSelected(1u),
          "agarrar o grupo nao desfaz nenhuma ilha dele");

    // --- repacking separates the islands -----------------------------------------------
    // Once the islands no longer share UV space the warning colour has to leave the atlas: the
    // only way it comes back is a genuine overlap.
    check(editor.repack(4), "reagrupar as ilhas no atlas funciona");
    renderer.draw();
    const std::vector<unsigned char> repacked = readPixels();
    maybePreview(std::getenv("G3D_RENDER_PREVIEW"), "repack", repacked);
    const int overlapAfterRepack =
        countColorIn(repacked, kAtlasLeft, kSize, kAtlasOverlap[0], kAtlasOverlap[1],
                     kAtlasOverlap[2], 8.0f);
    check(overlapAfterRepack == 0,
          "reagrupadas, nenhuma ilha aparece em vermelho, pixels=" +
              std::to_string(overlapAfterRepack));
    const int amberRepacked = countColorIn(repacked, kAtlasLeft, kSize, kOutlineColor[0],
                                           kOutlineColor[1], kOutlineColor[2], 8.0f);
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

    // --- a pin colours the islands it holds ------------------------------------------------
    // The outline stays amber because the ring is what says which islands a tool reaches, and the
    // pin reads through the fill: on the screen the two channels stay as apart as they are in the
    // code, and an island in the group and pinned at once says both.
    check(editor.lockSelection(), "bloquear a selecao pelo renderizador");
    // A drag over a group of pins would move nothing, so the gesture is refused and the touch
    // belongs to the camera: what is highlighted has to be what follows the finger.
    check(!renderer.uvGrab(islandPixel[0], islandPixel[1], false),
          "um grupo de pinos nao e agarrado");
    renderer.draw();
    const std::vector<unsigned char> pinned = readPixels();
    maybePreview(std::getenv("G3D_RENDER_PREVIEW"), "ilhas_bloqueadas", pinned);
    int pinChannels = 0;
    for (size_t i = 0; i < repacked.size(); ++i) {
        if (std::abs(static_cast<int>(pinned[i]) - static_cast<int>(repacked[i])) > 4) {
            ++pinChannels;
        }
    }
    check(pinChannels > 30,
          "o pino muda o preenchimento das ilhas, canais=" + std::to_string(pinChannels));
    const int amberPinned = countColorIn(pinned, kAtlasLeft, kSize, kOutlineColor[0],
                                         kOutlineColor[1], kOutlineColor[2], 8.0f);
    check(amberPinned > 0 && amberPinned <= amberRepacked + 8,
          "o contorno ambar continua dizendo o que a ferramenta alcanca, pixels=" +
              std::to_string(amberPinned));
    check(editor.unlockSelection(), "destravar a selecao");
    renderer.draw();
    const std::vector<unsigned char> unpinned = readPixels();
    int backChannels = 0;
    for (size_t i = 0; i < repacked.size(); ++i) {
        if (unpinned[i] != repacked[i]) {
            ++backChannels;
        }
    }
    check(backChannels == 0,
          "solto o pino o atlas volta a ser o mesmo, canais=" + std::to_string(backChannels));

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
    check(countColorIn(atlasEmpty, 0, kSize, kAtlasGrid[0], kAtlasGrid[1], kAtlasGrid[2]) > 0,
          "a grade do atlas aparece sem edicao aberta");
    check(countColorIn(atlasEmpty, 0, kSize, kAtlasBorder[0], kAtlasBorder[1], kAtlasBorder[2]) > 0,
          "a borda do atlas aparece sem edicao aberta");
    check(countColorIn(atlasEmpty, 0, kSize, kAtlasOverlap[0], kAtlasOverlap[1], kAtlasOverlap[2], 8.0f) == 0,
          "sem edicao aberta nao ha aviso de sobreposicao");
    // The whole surface is the atlas now, so its centre moved to the centre of the surface.
    const AtlasSample emptyCentre = sample(centreY, centreY);
    check(emptyCentre.hit && std::fabs(emptyCentre.u - 0.5f) < 2e-3f,
          "o atlas vazio continua mapeando toques, u=" + std::to_string(emptyCentre.u));

    // --- the composed texture reaches both views ---------------------------------------
    // One layer of pure green over a blue canvas: a colour the solid shading cannot produce, so
    // seeing it proves the picture really went through the compositor and into GL.
    g3d::TextureStudio &studio = g3d::TextureStudio::instance();
    std::string composeError;
    check(studio.setCanvas(64, 64, composeError), "o compositor recebe a tela do atlas");
    const uint8_t canvasBackground[3] = {0, 0, 255};
    studio.setBackground(canvasBackground);
    g3d::Image dot;
    const uint8_t green[4] = {0, 255, 0, 255};
    dot.allocate(16, 16, green);
    dot.name = "ponto.png";
    const int dotLayer = studio.addLayer(dot, composeError);
    check(dotLayer > 0, "a camada importada entra na pilha");

    renderer.setViewMode(g3d::Renderer::kViewThreeDimensional);
    renderer.setShadingMode(g3d::Renderer::kShadeTextured);
    renderer.draw();
    check(glGetError() == GL_NO_ERROR, "nenhum erro de GL no modo texturizado");
    const std::vector<unsigned char> textured = readPixels();
    maybePreview(std::getenv("G3D_RENDER_PREVIEW"), "textura", textured);
    int greenish = 0;
    for (size_t pixel = 0; pixel < textured.size() / 4; ++pixel) {
        const size_t offset = pixel * 4;
        if (textured[offset + 1] > 60 && textured[offset + 1] > textured[offset + 0] + 30 &&
            textured[offset + 1] > textured[offset + 2] + 30) {
            ++greenish;
        }
    }
    check(greenish > 50, "o modelo amostra a camada verde da textura, pixels=" +
                             std::to_string(greenish));
    int texturedChanged = 0;
    for (size_t i = 0; i < solid.size(); ++i) {
        if (std::abs(static_cast<int>(solid[i]) - static_cast<int>(textured[i])) > 8) {
            ++texturedChanged;
        }
    }
    check(texturedChanged > 400, "o modo texturizado produz outra imagem, canais=" +
                                     std::to_string(texturedChanged));

    renderer.setShadingMode(g3d::Renderer::kShadeChecker);
    renderer.draw();
    const std::vector<unsigned char> checker = readPixels();
    maybePreview(std::getenv("G3D_RENDER_PREVIEW"), "grade", checker);
    int light = 0;
    int dark = 0;
    for (size_t pixel = 0; pixel < checker.size() / 4; ++pixel) {
        if (!matches(checker, pixel, kBackground[0], kBackground[1], kBackground[2], 3.0f)) {
            if (checker[pixel * 4] > 150) {
                ++light;
            } else if (checker[pixel * 4] < 120) {
                ++dark;
            }
        }
    }
    check(light > 50 && dark > 50, "a grade de referencia mostra os dois tons, claros=" +
                                       std::to_string(light) + " escuros=" +
                                       std::to_string(dark));

    // Back to the atlas: the picture is the background of the square, the wireframe lies on it.
    renderer.setShadingMode(g3d::Renderer::kShadeSolid);
    renderer.setViewMode(g3d::Renderer::kViewAtlasOnly);
    renderer.draw();
    const std::vector<unsigned char> atlasTextured = readPixels();
    maybePreview(std::getenv("G3D_RENDER_PREVIEW"), "atlas_textura", atlasTextured);
    check(greenishOnTop(atlasTextured) > 30,
          "o atlas mostra a camada como fundo, pixels=" + std::to_string(greenishOnTop(atlasTextured)));

    // Hiding the wireframe is a view decision: the picture underneath has to show unblended.
    renderer.setUvOverlayVisible(false);
    renderer.draw();
    const std::vector<unsigned char> pictureOnly = readPixels();
    maybePreview(std::getenv("G3D_RENDER_PREVIEW"), "atlas_sem_linhas", pictureOnly);
    check(matches(pictureOnly, (kSize / 2) * kSize + kSize / 2, 0.0f, 1.0f, 0.0f, 8.0f),
          "sem as linhas UV o centro do atlas e a cor da camada");
    check(matches(pictureOnly, (kSize / 2) * kSize + kSize / 2 + 22, 0.0f, 0.0f, 1.0f, 8.0f),
          "e ao lado dele o fundo do atlas");
    renderer.setUvOverlayVisible(true);

    // The exported PNG is the composition, so the overlay it never showed stays out of it.
    g3d::Image exported;
    check(studio.render(exported, composeError), "o compositor entrega a textura para exportar");
    if (exported.valid()) {
        const uint8_t *centre = exported.rgba(32, 32);
        const uint8_t *edge = exported.rgba(60, 32);
        check(centre != nullptr && centre[1] == 255 && centre[0] == 0 && centre[2] == 0,
              "o PNG exportado tem a camada no centro");
        check(edge != nullptr && edge[2] == 255 && edge[0] == 0 && edge[1] == 0,
              "e a cor de fundo ao lado, sem linhas UV");
    } else {
        check(false, "o PNG exportado tem pixels");
    }

    // Moving a layer moves the revision, and the renderer has to notice without a re-upload call.
    // The lines are hidden for this one too: the assertion is about the picture, not the ruler.
    check(studio.translate(dotLayer, 22.0f, 0.0f), "mover a camada no compositor");
    renderer.setUvOverlayVisible(false);
    renderer.draw();
    const std::vector<unsigned char> afterMove = readPixels();
    maybePreview(std::getenv("G3D_RENDER_PREVIEW"), "atlas_mudanca", afterMove);
    check(matches(afterMove, (kSize / 2) * kSize + kSize / 2, 0.0f, 0.0f, 1.0f, 8.0f),
          "o atlas acompanha a camada que saiu do centro");
    renderer.setUvOverlayVisible(true);

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
