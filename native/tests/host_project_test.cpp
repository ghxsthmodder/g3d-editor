// Host harness for the project file: writes the container, breaks it on purpose, and saves and
// reopens the editor through the real singletons. Built by the host CMake branch only.

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "core/document.hpp"
#include "project/project_container.hpp"
#include "project/project_io.hpp"
#include "texture/g3d_image.hpp"
#include "texture/texture_studio.hpp"
#include "uv/uv_editor.hpp"

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
    return std::vector<char>((std::istreambuf_iterator<char>(stream)),
                             std::istreambuf_iterator<char>());
}

g3d::Image solid(int width, int height, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha,
                 const std::string &name) {
    g3d::Image image;
    const uint8_t fill[4] = {red, green, blue, alpha};
    image.allocate(width, height, fill);
    image.name = name;
    return image;
}

// --- the container alone, without a scene behind it ------------------------------------------
void testContainer() {
    std::string error;
    const std::string json = "{\"format\":1,\"oi\":1}";
    const std::vector<uint8_t> bin = {1, 2, 3, 4, 5, 6};
    std::vector<uint8_t> file;
    const bool written = g3d::writeProjectContainer(json, bin, file, error);
    check(written, "o container é escrito: " + error);
    check(file.size() >= g3d::kProjectHeaderBytes + json.size(), "o arquivo guarda a descrição");
    check(file.size() % 4u == 0u, "o arquivo inteiro vem em múltiplos de quatro");

    g3d::ProjectChunks chunks;
    const bool reread = g3d::readProjectContainer(file.data(), file.size(), chunks, error);
    check(reread, "o container é relido: " + error);
    check(chunks.version == g3d::kProjectVersion, "a versão lida é a escrita");
    check(chunks.json == json, "a descrição volta byte a byte");
    check(chunks.bin.size() == 8u &&
              std::equal(chunks.bin.begin(),
                         chunks.bin.begin() + static_cast<ptrdiff_t>(bin.size()), bin.begin()),
          "o bloco binário volta com os zeros de preenchimento no fim");

    // What the reader has to refuse: the container is the only thing standing between a stray
    // file and a half-opened project.
    g3d::ProjectChunks rejected;
    check(!g3d::readProjectContainer(file.data(), 8, rejected, error),
          "um cabeçalho cortado é recusado");
    check(!g3d::readProjectContainer(file.data(), file.size() - 1, rejected, error),
          "um arquivo sem o fim do bloco é recusado");
    std::vector<uint8_t> wrongMagic = file;
    wrongMagic[0] = 'X';
    check(!g3d::readProjectContainer(wrongMagic.data(), wrongMagic.size(), rejected, error),
          "uma mágica errada é recusada");
    check(error.find("G3DP") != std::string::npos, "a recusa diz qual mágica esperava");
    std::vector<uint8_t> future = file;
    future[4] = 99;
    check(!g3d::readProjectContainer(future.data(), future.size(), rejected, error),
          "uma versão futura é recusada");
    std::vector<uint8_t> unaligned = file;
    unaligned[8] = static_cast<uint8_t>(unaligned[8] + 1u);
    check(!g3d::readProjectContainer(unaligned.data(), unaligned.size(), rejected, error),
          "um comprimento desalinhado é recusado");
    const std::string empty = "";
    check(!g3d::writeProjectContainer(empty, bin, file, error),
          "uma descrição vazia não vira projeto");
    check(!g3d::writeProjectContainer("oi", bin, file, error), "a descrição precisa ser JSON");
    check(!g3d::readProjectContainer("", 0, rejected, error), "zero bytes não são um projeto");
}

// --- the whole editor, saved and reopened ----------------------------------------------------
void testSaveAndOpen() {
    std::string error;
    g3d::Document &document = g3d::Document::instance();
    g3d::TextureStudio &studio = g3d::TextureStudio::instance();
    g3d::UvEditor &editor = g3d::UvEditor::instance();
    editor.cancel();
    studio.reset();

    // Nothing to save before a model exists, and the reason is said out loud.
    std::vector<uint8_t> none;
    g3d::ProjectReport emptyReport;
    error.clear();
    check(!g3d::saveProject(none, emptyReport, error), "sem modelo não há projeto");
    check(error.find("modelo") != std::string::npos, "e a recusa diz que falta o modelo");

    const std::string scenePath = std::string(G3D_TEST_MODEL_DIR) + "/two-objects.obj";
    const std::vector<char> sceneBytes = readAll(scenePath);
    const bool imported =
        document.importModel(sceneBytes.data(), sceneBytes.size(), scenePath, error);
    check(imported, "importa two-objects.obj: " + error);
    check(document.hasSource(), "o documento guarda o arquivo de onde veio");
    check(document.sourceName() == scenePath, "e guarda o nome dele");

    // Edited UVs on the second mesh, applied so the scene carries them.
    const bool openedSession = editor.begin(1, error);
    check(openedSession, "abre a malha para editar: " + error);
    check(editor.select(0, false) && editor.translate(0.07f, -0.03f), "move uma ilha");
    const bool applied = editor.apply(error);
    check(applied, "aplica os UVs na cena: " + error);
    std::vector<float> uvsApplied;
    document.withScene([&uvsApplied](g3d::Scene &scene) { uvsApplied = scene.meshAt(1)->uvs; });
    check(!uvsApplied.empty(), "a cena tem UVs para guardar");

    // A layer stack with two pictures placed differently, and a canvas with a colour of its own.
    const uint8_t tint[3] = {12, 200, 60};
    const bool canvas = studio.setCanvas(256, 128, error);
    check(canvas, "define a tela do compositor: " + error);
    studio.setBackground(tint);
    const int lower = studio.addLayer(solid(32, 32, 255, 0, 0, 255, "vermelho.png"), error);
    const int upper = studio.addLayer(solid(16, 8, 0, 255, 0, 255, "faixa.png"), error);
    check(lower > 0 && upper > 0, "duas camadas entram");
    studio.translate(upper, 20.0f, -10.0f);
    studio.rotate(upper, 30.0f);
    studio.scaleBy(upper, 1.5f);
    studio.setOpacity(upper, 0.4f);
    studio.setVisible(lower, false);

    std::vector<uint8_t> project;
    g3d::ProjectReport saved;
    const bool savedFile = g3d::saveProject(project, saved, error);
    check(savedFile, "salvar o projeto: " + error);
    std::printf("      %s\n", saved.describe().c_str());
    check(saved.modelBytes > 0, "o modelo inteiro vai no arquivo");
    check(saved.layers == 2, "as duas camadas vão no arquivo");
    check(saved.meshes >= 1, "os UVs editados vão no arquivo");
    check(project.size() % 4u == 0u, "o projeto vem em múltiplos de quatro");
    check(saved.warnings.empty(), "com os UVs aplicados não há nada a avisar");

    // An edit still on the atlas and not in the scene is said before the file is written, because
    // what the project carries is the scene.
    check(editor.select(0, false) && editor.translate(0.04f, 0.0f), "move uma ilha sem aplicar");
    std::vector<uint8_t> uncommitted;
    g3d::ProjectReport warned;
    const bool warnedSaved = g3d::saveProject(uncommitted, warned, error);
    check(warnedSaved, "salvar com a edição pendente ainda salva");
    check(warned.warnings.size() == 1u &&
              warned.warnings[0].find("nao esta aplicada") != std::string::npos,
          "e avisa que a edição pendente fica de fora");
    editor.cancel();

    // Wipe everything the project has to bring back, then open it from the bytes alone.
    editor.cancel();
    studio.reset();
    const std::vector<uint8_t> copy = project;
    g3d::ProjectReport opened;
    const bool reopened =
        g3d::openProject(copy.data(), copy.size(), "recuperado.g3dp", opened, error);
    check(reopened, "reabrir o projeto: " + error);
    std::printf("      %s\n", opened.describe().c_str());
    check(opened.meshes == saved.meshes, "a reabertura traz as mesmas malhas");
    check(opened.layers == saved.layers, "a reabertura traz as mesmas camadas");

    std::vector<float> uvsRestored;
    document.withScene([&uvsRestored](g3d::Scene &scene) { uvsRestored = scene.meshAt(1)->uvs; });
    check(uvsRestored == uvsApplied, "os UVs editados voltam exatamente");

    check(studio.canvasWidth() == 256 && studio.canvasHeight() == 128, "a tela volta do tamanho");
    uint8_t readTint[3] = {0};
    studio.background(readTint);
    check(readTint[0] == 12 && readTint[1] == 200 && readTint[2] == 60, "o fundo volta na cor");
    const std::vector<g3d::TextureLayer> layers = studio.layers();
    check(layers.size() == 2, "a pilha tem duas camadas outra vez");
    if (layers.size() == 2) {
        check(layers[1].name == "faixa.png" && layers[0].name == "vermelho.png",
              "a ordem da pilha volta como era");
        check(std::fabs(layers[1].centreX - (128.0f + 20.0f)) < 0.01f &&
                  std::fabs(layers[1].centreY - (64.0f - 10.0f)) < 0.01f,
              "o deslocamento da camada volta");
        check(std::fabs(layers[1].rotationDegrees - 30.0f) < 0.01f, "o giro da camada volta");
        check(std::fabs(layers[1].scaleX - 1.5f) < 0.01f, "a escala da camada volta");
        check(std::fabs(layers[1].opacity - 0.4f) < 0.01f, "a opacidade da camada volta");
        check(!layers[0].visible, "a camada escondida volta escondida");
        check(layers[0].image && layers[0].image->width == 32 &&
                  layers[0].image->pixels == solid(32, 32, 255, 0, 0, 255, "vermelho.png").pixels,
              "a imagem da camada volta byte a byte");
    }

    // The reopened document is editable: the project is not a read-only archive.
    const bool editable = editor.begin(1, error);
    check(editable, "a malha reaberta pode ser editada de novo: " + error);
    check(editor.select(0, false) && editor.translate(0.01f, 0.0f), "e aceita uma edição");
    editor.cancel();

    // A project that claims to be a model, or is a model that claims to be a project.
    g3d::ProjectReport confused;
    const bool refused =
        g3d::openProject(sceneBytes.data(), sceneBytes.size(), "two-objects.obj", confused, error);
    check(refused == false, "um OBJ aberto como projeto é recusado");
    check(!error.empty(), "e a recusa diz por quê");
    std::vector<uint8_t> truncated = copy;
    truncated.resize(copy.size() / 2);
    error.clear();
    check(!g3d::openProject(truncated.data(), truncated.size(), "cortado.g3dp", confused, error),
          "um projeto cortado é recusado");
}

// --- a project that carries the geometry a generation produced --------------------------------
// Moving an island keeps the mesh as the file describes it, so its UVs alone are enough. Generating
// is different: xatlas welds the mesh and reopens it along the seams, and the result is another
// mesh. A project storing only its UVs would hand the model back the coordinates it never lost.
void testGeneratedGeometry() {
    std::string error;
    g3d::Document &document = g3d::Document::instance();
    g3d::TextureStudio &studio = g3d::TextureStudio::instance();
    g3d::UvEditor &editor = g3d::UvEditor::instance();
    editor.cancel();
    studio.reset();

    const std::string scenePath = std::string(G3D_TEST_MODEL_DIR) + "/two-objects.obj";
    const std::vector<char> sceneBytes = readAll(scenePath);
    check(document.importModel(sceneBytes.data(), sceneBytes.size(), scenePath, error),
          "importa o modelo outra vez: " + error);

    std::vector<float> filePositions;
    uint32_t fileVertices = 0;
    document.withScene([&](g3d::Scene &scene) {
        filePositions = scene.meshAt(0)->positions;
        fileVertices = scene.meshAt(0)->vertexCount();
    });

    g3d::UvOptions options;
    options.padding = 2;
    check(editor.generate(0, options, error), "gera os UVs da primeira malha: " + error);
    check(editor.apply(error), "e aplica a malha gerada na cena: " + error);

    std::vector<float> positionsGenerated;
    std::vector<float> uvsGenerated;
    uint32_t generatedVertices = 0;
    document.withScene([&](g3d::Scene &scene) {
        positionsGenerated = scene.meshAt(0)->positions;
        uvsGenerated = scene.meshAt(0)->uvs;
        generatedVertices = scene.meshAt(0)->vertexCount();
    });
    check(uvsGenerated.size() == generatedVertices * 2u, "cada vértice recebeu um par de UVs");
    // The vertex count is not what gives the generation away: this cube comes back with the same
    // twenty-four vertices in another order, so UVs saved on their own would have fitted and landed
    // on the wrong corners, with nothing to report.
    check(positionsGenerated != filePositions,
          "a malha gerada não é a do arquivo, com seus " + std::to_string(fileVertices) +
              " vértices");

    std::vector<uint8_t> project;
    g3d::ProjectReport saved;
    check(g3d::saveProject(project, saved, error), "salvar o projeto gerado: " + error);
    check(saved.warnings.empty(),
          "nada ficou de fora: " + (saved.warnings.empty() ? std::string() : saved.warnings[0]));
    check(saved.meshes >= 1, "a malha gerada vai no arquivo");

    studio.reset();
    editor.cancel();
    g3d::ProjectReport opened;
    const std::vector<uint8_t> copy = project;
    check(g3d::openProject(copy.data(), copy.size(), "gerado.g3dp", opened, error),
          "reabrir o projeto gerado: " + error);
    check(opened.meshes == saved.meshes, "a reabertura traz as mesmas malhas");
    check(opened.warnings.empty(),
          "e não teve de recusar nenhum UV: " +
              (opened.warnings.empty() ? std::string() : opened.warnings[0]));

    std::vector<float> positionsRestored;
    std::vector<float> uvsRestored;
    document.withScene([&](g3d::Scene &scene) {
        positionsRestored = scene.meshAt(0)->positions;
        uvsRestored = scene.meshAt(0)->uvs;
    });
    check(positionsRestored == positionsGenerated, "a malha volta com a geometria da geração");
    check(uvsRestored == uvsGenerated, "e com os UVs daquela geração, exatamente");
    check(positionsRestored != filePositions, "não com a ordem que o arquivo descreve");

    // The reopened scene still does not match the embedded file, so saving it again carries the
    // geometry once more rather than quietly falling back to the UVs that will not fit.
    std::vector<uint8_t> again;
    g3d::ProjectReport resaved;
    check(g3d::saveProject(again, resaved, error), "salvar depois de reabrir: " + error);
    check(resaved.warnings.empty(), "e o segundo salvar não perde nada");
    check(again.size() == copy.size(), "o projeto regravado tem o mesmo tamanho");
    g3d::ProjectReport second;
    check(g3d::openProject(again.data(), again.size(), "gerado2.g3dp", second, error),
          "reabrir o projeto regravado: " + error);
    std::vector<float> uvsTwice;
    document.withScene([&uvsTwice](g3d::Scene &scene) { uvsTwice = scene.meshAt(0)->uvs; });
    check(uvsTwice == uvsGenerated, "os UVs sobrevivem a duas idas e voltas");

    editor.cancel();
    studio.reset();
}

}  // namespace

int main() {
    std::printf("== arquivo de projeto ==\n");
    testContainer();
    testSaveAndOpen();
    testGeneratedGeometry();
    std::printf("\n%d verificacoes, %d falhas\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
