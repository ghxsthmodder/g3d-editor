// Host harness for the compositor: builds the layer stack in memory and checks the pixels it
// paints. Built by the host CMake branch, not part of the APK.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "texture/g3d_image.hpp"
#include "texture/texture_layer.hpp"
#include "texture/texture_studio.hpp"

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

g3d::Image solid(int width, int height, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha,
                 const std::string &name) {
    g3d::Image image;
    const uint8_t fill[4] = {red, green, blue, alpha};
    image.allocate(width, height, fill);
    image.name = name;
    return image;
}

// A picture whose right half is transparent: the compositor has to let the background through.
g3d::Image halfTransparent(int width, int height, const std::string &name) {
    g3d::Image image;
    image.allocate(width, height, nullptr);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint8_t *pixel = image.rgba(x, y);
            pixel[0] = 255;
            pixel[3] = x < width / 2 ? 255 : 0;
        }
    }
    image.name = name;
    return image;
}

bool equals(const g3d::Image &image, int x, int y, int red, int green, int blue, int alpha,
            int tolerance = 0) {
    const uint8_t *pixel = image.rgba(x, y);
    if (pixel == nullptr) {
        return false;
    }
    return std::abs(static_cast<int>(pixel[0]) - red) <= tolerance &&
           std::abs(static_cast<int>(pixel[1]) - green) <= tolerance &&
           std::abs(static_cast<int>(pixel[2]) - blue) <= tolerance &&
           std::abs(static_cast<int>(pixel[3]) - alpha) <= tolerance;
}

bool sameImage(const g3d::Image &a, const g3d::Image &b) {
    return a.width == b.width && a.height == b.height && a.pixels == b.pixels;
}

g3d::TextureComposition makeComposition(int side, const uint8_t background[3], std::string &error) {
    g3d::TextureComposition composition;
    composition.setCanvas(side, side, error);
    composition.setBackground(background);
    return composition;
}

void testCanvasAndBackground() {
    std::string error;
    g3d::TextureComposition composition;
    check(!composition.setCanvas(0, 0, error), "uma tela de zero pixels é recusada");
    check(!composition.setCanvas(9000, 64, error), "uma tela acima do limite é recusada");
    check(error.find("8192") != std::string::npos, "a recusa diz o limite por lado");

    const uint8_t background[3] = {10, 200, 30};
    composition = makeComposition(32, background, error);
    check(composition.canvasWidth() == 32 && composition.canvasHeight() == 32,
          "a tela aceita tem as medidas pedidas");

    g3d::Image out;
    check(composition.compose(out, error), "compor sem nenhuma camada funciona");
    check(equals(out, 0, 0, 10, 200, 30, 255) && equals(out, 31, 31, 10, 200, 30, 255),
          "sem camadas a tela inteira é a cor de fundo");
    uint8_t read[3];
    composition.background(read);
    check(read[0] == 10 && read[1] == 200 && read[2] == 30, "a cor de fundo pode ser lida");

    g3d::Image empty;
    g3d::TextureComposition noCanvas;
    check(!noCanvas.compose(empty, error), "compor sem definir a tela é recusado");
}

void testPlaceAndSize() {
    std::string error;
    const uint8_t background[3] = {0, 0, 0};
    g3d::TextureComposition composition = makeComposition(32, background, error);
    const int red = composition.addLayer(solid(8, 8, 255, 0, 0, 255, "vermelho.png"), error);
    check(red > 0, "a camada criada tem id");
    check(composition.addLayer(solid(0, 0, 1, 1, 1, 255, "vazia"), error) < 0,
          "uma imagem sem pixels não vira camada");

    g3d::Image out;
    check(composition.compose(out, error), "compor uma camada opaca");
    // 8 px wide centred on a 32 px canvas: the pixel centres 12..19 land on source pixels 0..7.
    check(equals(out, 12, 16, 255, 0, 0, 255), "a camada cobre o centro esperado");
    check(equals(out, 19, 19, 255, 0, 0, 255), "a camada cobre o canto oposto esperado");
    check(equals(out, 11, 16, 0, 0, 0, 255), "fora da camada fica o fundo");
    check(composition.layer(red)->name == "vermelho.png", "o nome do arquivo chega na camada");

    check(composition.translate(red, 4.0f, -4.0f), "mover a camada");
    composition.compose(out, error);
    check(equals(out, 16, 8, 255, 0, 0, 255) && equals(out, 11, 16, 0, 0, 0, 255),
          "a camada seguiu o deslocamento");

    check(!composition.translate(9999, 1.0f, 0.0f), "mover uma camada inexistente falha");
    check(!composition.setOpacity(red, 2.0f), "uma opacidade acima de 1 é recusada");
    check(!composition.scaleBy(red, 0.0f), "escalar por zero é recusado");
}

void testScaleAndRotation() {
    std::string error;
    const uint8_t background[3] = {0, 0, 0};
    g3d::TextureComposition composition = makeComposition(32, background, error);
    const int red = composition.addLayer(solid(8, 8, 255, 0, 0, 255, "quadrado"), error);
    check(composition.scaleBy(red, 2.0f), "escalar a camada");
    g3d::Image out;
    composition.compose(out, error);
    // Doubled, the 8 px picture spans 15 px of canvas: the column two in from its edge is red.
    check(equals(out, 10, 16, 255, 0, 0, 255), "dobrada, a camada alcança mais longe");
    check(equals(out, 16, 16, 255, 0, 0, 255), "o centro continua coberto");

    // A wide strip turned by a quarter turn becomes a tall one. Centred on the canvas, the 16x4
    // picture covers columns 8..23 and rows 14..17; a quarter turn swaps the two.
    const int green = composition.addLayer(solid(16, 4, 0, 255, 0, 255, "faixa"), error);
    composition.compose(out, error);
    check(equals(out, 10, 16, 0, 255, 0, 255), "a faixa horizontal cobre a linha");
    check(!equals(out, 16, 10, 0, 255, 0, 255, 2), "a coluna fica livre antes de girar");
    check(composition.rotate(green, 90.0f), "girar a camada");
    composition.compose(out, error);
    check(equals(out, 16, 10, 0, 255, 0, 255, 2), "girada, a faixa cobre a coluna");
    check(!equals(out, 10, 16, 0, 255, 0, 255, 2), "e deixa de cobrir a linha");
    check(composition.rotate(green, -90.0f) &&
              std::abs(composition.layer(green)->rotationDegrees) < 0.001f,
          "o ângulo volta a zero");
}

void testOpacityAndAlpha() {
    std::string error;
    const uint8_t blue[3] = {0, 0, 255};
    g3d::TextureComposition composition = makeComposition(32, blue, error);
    const int red = composition.addLayer(solid(8, 8, 255, 0, 0, 255, "vermelho"), error);
    g3d::Image out;

    check(composition.setOpacity(red, 0.5f), "definir a opacidade");
    composition.compose(out, error);
    check(equals(out, 16, 16, 128, 0, 128, 255, 1), "meia opacidade mistura com o fundo");

    check(composition.setVisible(red, false), "esconder a camada");
    composition.compose(out, error);
    check(equals(out, 16, 16, 0, 0, 255, 255), "a camada escondida não aparece");
    check(composition.setVisible(red, true), "mostrar a camada de novo");
    composition.compose(out, error);
    check(!equals(out, 16, 16, 0, 0, 255, 255, 1), "mostra de novo cobre o fundo");

    // A layer whose right half is transparent lets the background through there.
    const int hole = composition.addLayer(halfTransparent(16, 8, "metade"), error);
    composition.setOpacity(hole, 1.0f);
    composition.compose(out, error);
    check(equals(out, 9, 16, 255, 0, 0, 255), "a metade opaca da camada cobre");
    check(equals(out, 23, 16, 0, 0, 255, 255), "a metade transparente deixa o fundo passar");
}

void testOrderDuplicateRemove() {
    std::string error;
    const uint8_t background[3] = {0, 0, 0};
    g3d::TextureComposition composition = makeComposition(32, background, error);
    const int red = composition.addLayer(solid(16, 16, 255, 0, 0, 255, "vermelho"), error);
    const int green = composition.addLayer(solid(16, 16, 0, 255, 0, 255, "verde"), error);
    g3d::Image out;
    composition.compose(out, error);
    check(equals(out, 16, 16, 0, 255, 0, 255), "a última camada criada fica por cima");

    check(composition.moveUp(red), "mover a camada de baixo para cima");
    check(!composition.moveDown(green), "a camada que ficou embaixo não desce mais");
    composition.compose(out, error);
    check(equals(out, 16, 16, 255, 0, 0, 255), "a ordem trocou qual cor aparece");
    check(composition.moveDown(red), "descer de novo");

    int copy = -1;
    check(composition.duplicateLayer(green, copy, error) && copy > 0, "duplicar a camada");
    check(composition.layer(copy)->name == "verde (cópia)", "a cópia é nomeada como cópia");
    check(composition.layers().size() == 3, "a pilha ganhou uma camada");
    composition.compose(out, error);
    check(equals(out, 16, 16, 0, 255, 0, 255), "a cópia do topo esconde as de baixo");

    check(composition.removeLayer(green), "remover a camada original");
    check(!composition.removeLayer(green), "remover duas vezes falha");
    check(composition.layers().size() == 2, "a pilha perdeu uma camada");
    check(composition.memoryBytes() == 2u * 16 * 16 * 4, "as bytes da pilha acompanham a remoção");
}

void testDeterminismAndPng() {
    std::string error;
    const uint8_t background[3] = {20, 20, 20};
    g3d::TextureComposition composition = makeComposition(24, background, error);
    composition.addLayer(solid(8, 8, 255, 0, 0, 255, "a"), error);
    composition.addLayer(halfTransparent(16, 8, "b"), error);
    composition.rotate(composition.layers()[1].id, 30.0f);

    g3d::Image first;
    g3d::Image second;
    check(composition.compose(first, error) && composition.compose(second, error),
          "compor duas vezes");
    check(sameImage(first, second), "a mesma pilha compose a mesma imagem");

    std::vector<uint8_t> png;
    check(g3d::encodePng(first, png, error), "a textura composta vira PNG");
    g3d::Image readBack;
    check(g3d::decodeImage(png.data(), png.size(), "composto.png", readBack, error) &&
              sameImage(first, readBack),
          "o PNG exportado volta igual ao que foi composto");

    // A file on disk is what the user picks up: same bytes as the memory version.
    const std::string path = "host_compose_preview.png";
    if (g3d::writePngFile(path, first, error)) {
        std::ifstream stream(path, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(stream)),
                                std::istreambuf_iterator<char>());
        g3d::Image fromDisk;
        check(!bytes.empty() &&
                  g3d::decodeImage(bytes.data(), bytes.size(), path, fromDisk, error) &&
                  sameImage(first, fromDisk),
              "o PNG gravado no disco é a textura composta");
        std::remove(path.c_str());
    } else {
        check(false, "o PNG composto pode ser gravado");
    }
}

void testLayerBudget() {
    std::string error;
    const uint8_t background[3] = {0, 0, 0};
    g3d::TextureComposition composition = makeComposition(64, background, error);
    int added = 0;
    for (int index = 0; index < 40; ++index) {
        const int id = composition.addLayer(solid(4, 4, 0, 0, 0, 255, "picotada"), error);
        if (id < 0) {
            break;
        }
        ++added;
    }
    check(added == 24, "a pilha para em 24 camadas");
    check(error.find("camadas") != std::string::npos, "a recusa fala das camadas");

    // Sixteen megapixels times four bytes is the whole budget in two pictures.
    g3d::TextureComposition big = makeComposition(64, background, error);
    g3d::Image heavy;
    heavy.width = 4096;
    heavy.height = 4096;
    heavy.pixels.assign(static_cast<size_t>(4096) * 4096 * 4, 7);
    check(big.addLayer(heavy, error) > 0, "uma camada do tamanho do limite entra");
    check(big.addLayer(heavy, error) < 0, "a segunda imagem de 16 MP estoura o orçamento");
    check(big.memoryBytes() == heavy.pixels.size(), "a camada recusada não fica na pilha");
}

// The canvas is the texture the model samples, so the ceiling of the driver is a rule of the canvas
// and the budget of pixels is another. They refuse apart, and each says which one refused.
void testCanvasCeilings() {
    const uint8_t background[3] = {0, 0, 0};
    std::string error;
    g3d::TextureComposition composition = makeComposition(64, background, error);

    check(composition.deviceLimit() == 0, "sem GL ninguém conhece o limite da GPU");
    composition.setDeviceLimit(2048);
    check(!composition.setCanvas(4096, 4096, error), "a tela acima do limite da GPU é recusada");
    const bool namesThePhone = error.find("2048") != std::string::npos;
    check(namesThePhone, "a recusa cita o número que o telefone deu");
    check(error.find("GPU") != std::string::npos, "a recusa diz de onde veio o limite");
    check(composition.setCanvas(2048, 2048, error), "o lado exato do limite entra");

    // 8192 x 4096 cabe em todo limite de lado e estoura o orçamento: as duas regras
    // falham apartadas.
    composition.setDeviceLimit(0);
    check(!composition.setCanvas(8192, 4096, error), "o orçamento de pixels barra a tela grande");
    check(error.find("MP") != std::string::npos, "a recusa do orçamento fala em megapixels");
    check(error.find("por lado") == std::string::npos, "e não culpa o limite de lado");
}

void testMemoryAccounting() {
    g3d::TextureStudio &studio = g3d::TextureStudio::instance();
    std::string error;
    studio.reset();
    check(studio.memoryBytes() == 0, "um estúdio vazio não retém nada");
    check(studio.setCanvas(64, 64, error), "a tela do estúdio entra");
    const int layer = studio.addLayer(solid(8, 8, 200, 30, 30, 255, "quadrado.png"), error);
    check(layer > 0, "uma camada entra");
    check(studio.memoryBytes() == 8 * 8 * 4, "a pilha conta a camada");
    g3d::Image picture;
    check(studio.texture(picture), "a composição é pintada");
    check(studio.memoryBytes() == 8u * 8 * 4 + 64u * 64 * 4,
          "a imagem composta entra na conta, que é a que o painel mostra");
    studio.reset();
    check(studio.memoryBytes() == 0, "o reset esvazia a conta");

    // A reset comes whenever another model is imported, and the ceiling of the device does not
    // change with it.
    studio.setDeviceLimit(1024);
    check(studio.deviceLimit() == 1024, "o estúdio guarda o limite que a GPU deu");
    studio.reset();
    check(studio.deviceLimit() == 1024, "o reset não esquece o limite do telefone");
    const bool refused = !studio.setCanvas(2048, 2048, error);
    check(refused, "a tela acima do limite segue recusada depois do reset");
    studio.setDeviceLimit(0);
}

// The undo of the compositor is the whole document: canvas, background and stack. A step shares the
// pictures instead of copying them, so removing a layer and undoing it costs no pixels, and the
// history only holds bytes the canvas no longer shows.
void testUndoRedo() {
    g3d::TextureStudio &studio = g3d::TextureStudio::instance();
    std::string error;
    studio.reset();
    check(!studio.canUndo() && !studio.canRedo(), "um estúdio novo não tem o que desfazer");
    check(!studio.undo() && !studio.redo(), "desfazer sem história não faz nada");

    check(studio.setCanvas(64, 64, error), "a tela entra");
    check(studio.undo(), "a resolução da tela é um passo");
    check(studio.canvasWidth() == 0, "desfazer leva a tela embora");
    check(studio.redo(), "e o refazer a traz de volta");
    check(studio.canvasWidth() == 64, "a tela volta com o refazer");

    const int first = studio.addLayer(solid(8, 8, 200, 30, 30, 255, "vermelho.png"), error);
    const int second = studio.addLayer(solid(8, 8, 30, 200, 30, 255, "verde.png"), error);
    const int third = studio.addLayer(solid(8, 8, 30, 30, 200, 255, "azul.png"), error);
    check(first > 0 && second > 0 && third > 0, "três camadas entram");
    const uint8_t tint[3] = {0x12, 0x34, 0x56};
    studio.setBackground(tint);
    studio.translate(second, 8.0f, -4.0f);
    check(studio.layerCount() == 3, "a pilha tem três camadas");
    uint8_t back[3] = {0, 0, 0};
    studio.background(back);
    check(back[0] == 0x12 && back[2] == 0x56, "o fundo mudou");

    studio.removeLayer(third);
    check(studio.layerCount() == 2, "remover deixou duas camadas");
    check(studio.undo(), "desfazer a remoção");
    check(studio.layerCount() == 3, "a camada removida volta");
    check(studio.canRedo(), "e o passo tem refazer");
    check(studio.undo(), "desfazer o arrasto");
    check(studio.undo(), "desfazer o fundo");
    check(studio.undo(), "desfazer a camada azul");
    check(studio.layerCount() == 2, "quatro passos atrás ficam a vermelha e a verde");
    check(std::fabs(studio.layers()[1].centreX - 32.0f) < 0.01f, "a do topo volta ao centro");
    studio.background(back);
    check(back[0] != 0x12, "e o fundo volta ao que era");
    check(studio.undo(), "desfazer a verde");
    check(studio.layerCount() == 1, "sobrou a primeira camada");
    check(studio.undo(), "desfazer a vermelha");
    check(studio.layerCount() == 0, "e a pilha fica vazia, com a tela ainda de pé");
    for (int step = 0; step < 6; ++step) {
        check(studio.redo(), "refazer os seis passos anda pra frente");
    }
    check(studio.layerCount() == 2, "o documento volta ao que estava antes do primeiro desfazer");
    check(std::fabs(studio.layers()[1].centreX - 40.0f) < 0.01f, "o arrasto volta com o refazer");
    check(!studio.canRedo(), "refazer até o fim não deixa nada à frente");

    // The history is a window, not a trail: a new mutation drops the redo branch.
    check(studio.undo(), "um passo para trás");
    check(studio.canRedo(), "que deixou refazer");
    studio.setOpacity(first, 0.5f);
    check(!studio.canRedo(), "editar depois de desfazer fecha o refazer");

    // A picture the canvas still shows is not a cost of the history; a layer removed and undone is
    // the same buffer all the way through.
    studio.reset();
    check(studio.setCanvas(64, 64, error), "a tela do histórico");
    const int kept = studio.addLayer(solid(8, 8, 200, 30, 30, 255, "queda.png"), error);
    check(studio.historyBytes() == 0, "uma camada na tela não é bytes de histórico");
    studio.removeLayer(kept);
    check(studio.historyBytes() == 8u * 8 * 4, "a camada que saiu da tela fica no histórico");
    studio.undo();
    check(studio.historyBytes() == 0, "de volta à tela, o histórico não paga por ela");
    check(studio.memoryBytes() == 8u * 8 * 4, "e a pilha continua com uma imagem de 8x8");

    // Duplicating is the same buffer painted twice: neither the stack nor the history pays again.
    int copy = 0;
    check(studio.duplicateLayer(kept, copy, error) && copy > 0, "a camada é duplicada");
    check(studio.memoryBytes() == 8u * 8 * 4, "a cópia não soma bytes à pilha");
    check(studio.historyBytes() == 0, "a cópia não soma bytes ao histórico");
    check(studio.layers()[0].image.get() == studio.layers()[1].image.get(),
          "as duas camadas apontam para a mesma imagem");
    check(studio.undo() && studio.layerCount() == 1, "desfazer tira a cópia");
    check(studio.canRedo(), "a cópia pode ser refeita");
    studio.clearHistory();
    check(!studio.canUndo() && !studio.canRedo(), "esquecer a história fecha o documento");
    studio.reset();
}

// A long session on a phone cannot be allowed to keep every picture it touched. The window has a
// ceiling, and what goes over it is the oldest step: the user is undoing what they just did.
void testHistoryWindow() {
    g3d::TextureStudio &studio = g3d::TextureStudio::instance();
    std::string error;
    studio.reset();
    check(studio.setCanvas(64, 64, error), "a tela da janela");
    const int layer = studio.addLayer(solid(8, 8, 200, 30, 30, 255, "janela.png"), error);
    int steps = 0;
    for (int nudge = 0; nudge < 200; ++nudge) {
        if (!studio.translate(layer, 0.1f, 0.0f)) {
            break;
        }
        ++steps;
    }
    check(steps == 200, "duzentos arrastos passam: " + std::to_string(steps));
    int undone = 0;
    while (undone < 300 && studio.undo()) {
        ++undone;
    }
    check(undone <= 64, "o histórico tem teto de 64 passos, desfeitos=" + std::to_string(undone));
    check(undone > 1, "e ainda desfaz o que foi feito por último");
    check(studio.canvasWidth() == 64, "o passo sacrificado é o mais antigo, não a tela");
    check(studio.layerCount() == 1, "a camada continua onde o primeiro passo restante a deixou");
    studio.reset();
}

// The panel of the phone parses the two strings below, so their shape is part of the contract
// between the native stack and the Kotlin side and not a free-form debug print.
void testStudioText() {
    g3d::TextureStudio &studio = g3d::TextureStudio::instance();
    std::string error;
    studio.reset();
    check(studio.describe().find("tela não definida") != std::string::npos,
          "sem tela o resumo diz que falta tela");
    check(studio.layerRows().empty(), "sem camadas não há linhas de pilha");
    check(studio.setCanvas(64, 64, error), "a tela do estúdio é aceita");
    check(studio.describe().find("tela=64x64") != std::string::npos, "o resumo traz a tela");
    const uint8_t tint[3] = {0x12, 0x34, 0x56};
    studio.setBackground(tint);
    check(studio.describe().find("fundo=#123456") != std::string::npos, "o resumo traz o fundo");

    const int lower = studio.addLayer(solid(8, 8, 200, 30, 30, 255, "vermelho.png"), error);
    const int upper = studio.addLayer(solid(8, 8, 30, 200, 30, 255, "verde.png"), error);
    check(lower > 0 && upper > 0, "duas camadas entram no estúdio");
    studio.setOpacity(lower, 0.5f);
    studio.setVisible(upper, false);
    studio.translate(upper, 8.0f, -4.0f);

    const std::string rows = studio.layerRows();
    const size_t endOfFirst = rows.find('\n');
    const std::string firstLine = rows.substr(0, endOfFirst);
    check(firstLine.rfind(std::to_string(upper) + " | verde.png |", 0) == 0,
          "a camada do topo da pilha vem na primeira linha");
    check(firstLine.find("oculta") != std::string::npos, "a camada invisível é marcada como oculta");
    check(rows.find("op=50%") != std::string::npos, "a opacidade aparece na linha");
    check(rows.find("40,28") != std::string::npos, "o centro da camada aparece na linha");
    check(rows.find("32,32") != std::string::npos, "a camada quieta está no centro da tela");
    check(std::count(rows.begin(), rows.end(), '\n') == 2, "uma linha por camada");

    studio.removeLayer(upper);
    check(studio.layerRows().find("vermelho.png") != std::string::npos &&
              studio.layerRows().find("verde.png") == std::string::npos,
          "remover some com a linha da camada");
    studio.reset();
    check(studio.layerRows().empty(), "o reset esvazia a pilha do estúdio");
}

}  // namespace

int main() {
    std::printf("== compositor de texturas ==\n");
    testCanvasAndBackground();
    testPlaceAndSize();
    testScaleAndRotation();
    testOpacityAndAlpha();
    testOrderDuplicateRemove();
    testDeterminismAndPng();
    testLayerBudget();
    testCanvasCeilings();
    testMemoryAccounting();
    testUndoRedo();
    testHistoryWindow();
    testStudioText();
    std::printf("%d checagens, %d falhas\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
