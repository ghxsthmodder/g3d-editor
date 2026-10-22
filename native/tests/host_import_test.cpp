// Host harness for the importer: runs Assimp against the fixtures in tests/models and checks the
// conversion into the internal representation. Built by the host CMake branch, not part of the APK.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "core/document.hpp"
#include "core/primitives.hpp"
#include "importers/assimp_importer.hpp"
#include "importers/import_progress.hpp"
#include "uv/uv_service.hpp"

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

g3d::ImportResult importFile(const std::string &name, std::string &error) {
    const std::string path = modelPath(name);
    const std::vector<char> bytes = readAll(path);
    g3d::ImportResult result;
    if (!g3d::importModelFromMemory(bytes.data(), bytes.size(), path, result, error)) {
        result.scene.clear();
    }
    return result;
}

bool hasWarning(const g3d::ImportResult &result, const std::string &fragment) {
    return std::any_of(result.warnings.begin(), result.warnings.end(),
                       [&](const std::string &warning) {
                           return warning.find(fragment) != std::string::npos;
                       });
}

// Written on its own, so the check does not pass simply by agreeing with the importer's code: a
// string the phone can turn into Java, meaning valid UTF-8 with no overlong form, no surrogate
// written as bytes, no character above the basic plane and no control byte in it.
bool isSafeForJava(const std::string &text) {
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char byte = static_cast<unsigned char>(text[i]);
        if (byte < 0x20u || byte == 0x7Fu) {
            return false;
        }
        if (byte < 0x80u) {
            continue;
        }
        size_t rest = 0;
        unsigned char minimum = 0x80u;
        unsigned char maximum = 0xBFu;
        if (byte >= 0xC2u && byte <= 0xDFu) {
            rest = 1;
        } else if (byte == 0xE0u) {
            rest = 2;
            minimum = 0xA0u;
        } else if (byte == 0xEDu) {
            rest = 2;
            maximum = 0x9Fu;
        } else if (byte >= 0xE1u && byte <= 0xEFu) {
            rest = 2;
        } else {
            return false;
        }
        for (size_t follow = 1; follow <= rest; ++follow) {
            if (i + follow >= text.size()) {
                return false;
            }
            const unsigned char next = static_cast<unsigned char>(text[i + follow]);
            const unsigned char low = follow == 1 ? minimum : 0x80u;
            const unsigned char high = follow == 1 ? maximum : 0xBFu;
            if (next < low || next > high) {
                return false;
            }
        }
        i += rest;
    }
    return true;
}

}  // namespace

int main() {
    std::string error;

    // --- Wavefront OBJ with per-face normals and UVs -------------------------------------
    g3d::ImportResult cube = importFile("cube.obj", error);
    check(error.empty() && cube.scene.meshCount() == 1, "cube.obj importa uma malha: " + error);
    if (cube.scene.meshCount() == 1) {
        const g3d::Mesh *mesh = cube.scene.meshAt(0);
        check(mesh->name == "cube", "nome da malha vem do arquivo: " + mesh->name);
        check(mesh->vertexCount() == 24,
              "cube.obj tem 24 vertices, obteve " + std::to_string(mesh->vertexCount()));
        check(mesh->triangleCount() == 12, "cube.obj tem 12 triangulos");
        check(mesh->hasUvs(), "UVs do arquivo sao preservados");
        check(mesh->hasNormals(), "normais do arquivo sao preservados");
        std::string meshError;
        check(mesh->validate(meshError), "malha convertida passa na validacao: " + meshError);
        float minimum[3];
        float maximum[3];
        check(cube.scene.bounds(minimum, maximum) && std::abs(maximum[0] - 0.5f) < 1e-4f &&
                  std::abs(minimum[1] + 0.5f) < 1e-4f,
              "bounds do cubo corretos");
    }

    // --- OBJ with two objects: must stay two selectable meshes ---------------------------
    error.clear();
    g3d::ImportResult twoObjects = importFile("two-objects.obj", error);
    check(twoObjects.scene.meshCount() == 2, "two-objects.obj importa duas malhas: " + error);
    if (twoObjects.scene.meshCount() == 2) {
        const std::string first = twoObjects.scene.meshAt(0)->name;
        const std::string second = twoObjects.scene.meshAt(1)->name;
        check(first != second, "as malhas tem nomes distintos");
        float leftMin[3];
        float leftMax[3];
        float rightMin[3];
        float rightMax[3];
        g3d::Scene &scene = twoObjects.scene;
        check(g3d::computeBounds(*scene.meshAt(0), leftMin, leftMax) &&
                  g3d::computeBounds(*scene.meshAt(1), rightMin, rightMax),
              "as duas malhas tem limites calculaveis");
        // The file order is not guaranteed to survive the import, so compare by position.
        const bool separated = leftMax[0] < rightMin[0] || rightMax[0] < leftMin[0];
        check(separated, "as duas malhas ocupam regioes separadas no eixo X (mesh0 x " +
                             std::to_string(leftMin[0]) + ".." + std::to_string(leftMax[0]) +
                             ", mesh1 x " + std::to_string(rightMin[0]) + ".." +
                             std::to_string(rightMax[0]) + ")");
    }

    // --- selection behaviour ------------------------------------------------------------
    twoObjects.scene.select(1);
    check(twoObjects.scene.selected() == 1, "selecao por indice funciona");
    twoObjects.scene.select(99);
    check(twoObjects.scene.selected() == -1, "indice fora do intervalo seleciona nada");
    check(twoObjects.scene.meshAt(99) == nullptr, "indice fora do intervalo devolve nulo");

    // --- GLB real do Khronos -------------------------------------------------------------
    error.clear();
    g3d::ImportResult box = importFile("Box.glb", error);
    check(error.empty() && box.scene.meshCount() >= 1, "Box.glb importa: " + error);
    if (box.scene.meshCount() >= 1) {
        const g3d::Mesh *mesh = box.scene.meshAt(0);
        std::string meshError;
        check(mesh->validate(meshError), "Box.glb converte para malha valida: " + meshError);
        check(mesh->triangleCount() == 12, "Box.glb tem 12 triangulos");
        check(mesh->hasNormals(), "Box.glb traz normais");
        check(!mesh->hasUvs(), "Box.glb chega sem UV: precisa de unwrap no editor");
        check(box.materialCount >= 1, "Box.glb traz material");
        std::printf("      %s\n", box.describe().c_str());
        std::printf("      %s\n", mesh->name.empty() ? "<sem nome>" : mesh->name.c_str());
    }

    // --- glTF JSON self-contained (buffer as a data: URI) --------------------------------
    error.clear();
    g3d::ImportResult gltf = importFile("BoxDataUri.gltf", error);
    check(error.empty() && gltf.scene.meshCount() >= 1, "BoxDataUri.gltf importa: " + error);
    if (gltf.scene.meshCount() >= 1) {
        check(gltf.scene.meshAt(0)->triangleCount() == 12, "BoxDataUri.gltf tem 12 triangulos");
        check(gltf.scene.meshAt(0)->hasNormals(), "BoxDataUri.gltf traz normais");
    }

    // glTF JSON with an external buffer: ReadFileFromMemory hides the real path from Assimp
    // (the buffer is presented as $$$magic$$$.<hint>), so the sidecar is never found, whether
    // the hint is a bare name or a path on disk. The user must get an actionable message.
    for (const std::string &hint : {std::string("Box.gltf"), modelPath("Box.gltf")}) {
        error.clear();
        const std::vector<char> sidecarBytes = readAll(modelPath("Box.gltf"));
        g3d::ImportResult noSidecar;
        const bool imported = g3d::importModelFromMemory(sidecarBytes.data(), sidecarBytes.size(),
                                                          hint, noSidecar, error);
        check(!imported && noSidecar.scene.isEmpty(),
              "buffer externo falha mesmo com caminho real no hint (" + hint + ")");
        check(!error.empty() && error.find("GLB") != std::string::npos,
              "erro explica como contornar: " + error);
    }

    // --- a file that points many nodes at one mesh --------------------------------------
    // A glTF is a list of references, so the scene it describes can be far bigger than the file:
    // this fixture sends 320 nodes at one 24-vertex cube, twenty of them landing exactly where
    // another one already landed. Expanded naively that shape of file is what closed the editor
    // on a phone, because the process is killed for the memory before any message can be shown.
    error.clear();
    g3d::ImportResult instanced = importFile("Instanced.gltf", error);
    check(error.empty() && instanced.scene.meshCount() == 300,
          "Instanced.gltf traz as 300 partes colocadas, vieram " +
              std::to_string(instanced.scene.meshCount()) + ": " + error);
    check(hasWarning(instanced, "foram unidas"),
          "as copias exatas no mesmo lugar viram um aviso: " + instanced.describe());

    const uint32_t instancedVertices = instanced.scene.meshAt(0)->vertexCount();
    const uint32_t instancedTriangles = instanced.scene.meshAt(0)->triangleCount();
    const size_t oneMesh = g3d::estimateMeshBytes(instancedVertices, instancedTriangles);

    // The budget is what the app asks for, so the test asks and gives it back afterwards.
    const size_t previousBudget = g3d::importBudgetBytes();
    g3d::setImportBudgetBytes(oneMesh * 5);
    error.clear();
    g3d::ImportResult capped = importFile("Instanced.gltf", error);
    check(error.empty() && capped.scene.meshCount() == 5,
          "o orcamento para a cena em 5 malhas, parou em " +
              std::to_string(capped.scene.meshCount()) + ": " + error);
    check(hasWarning(capped, "ficaram de fora"), "quanto fica de fora e dito ao usuario");
    check(capped.scene.meshAt(0)->triangleCount() == instancedTriangles,
          "as malhas que entraram estao inteiras");

    // One mesh heavier than the whole budget still arrives: assimp already paid for it, and an
    // empty editor with a warning is worse than a heavy one the user can look at.
    g3d::setImportBudgetBytes(oneMesh / 2);
    error.clear();
    g3d::ImportResult smallest = importFile("Instanced.gltf", error);
    check(error.empty() && smallest.scene.meshCount() == 1,
          "a primeira malha passa mesmo sem orcamento, vieram " +
              std::to_string(smallest.scene.meshCount()));
    g3d::setImportBudgetBytes(previousBudget);
    check(g3d::importBudgetBytes() == previousBudget, "o orcamento volta ao que era");

    // --- nomes que o telefone não mostraria -----------------------------------------------
    // Handing a string over to the Java side is strict about its bytes, and a file that carries a
    // name in some other encoding closes the app on the way to the screen. The importer cleans
    // names as it reads them, so a model that arrived with a broken name still arrives.
    error.clear();
    g3d::ImportResult oddNames = importFile("odd-names.obj", error);
    check(error.empty() && oddNames.scene.meshCount() == 3,
          "odd-names.obj importa os tres objetos, vieram " +
              std::to_string(oddNames.scene.meshCount()) + ": " + error);
    if (oddNames.scene.meshCount() == 3) {
        const std::string first = oddNames.scene.meshAt(0)->name;
        const std::string second = oddNames.scene.meshAt(1)->name;
        const std::string third = oddNames.scene.meshAt(2)->name;
        check(first == "caf?", "o byte Latin-1 sozinho vira uma marca: " + first);
        check(second == "écriture", "o nome que ja era UTF-8 passa como veio: " + second);
        check(third == "?cadeira", "um caractere fora do alcance e uma marca so: " + third);
        for (const std::string *name : {&first, &second, &third}) {
            check(isSafeForJava(*name), "nome legivel no telefone: " + *name);
        }
    }

    // --- garbage and empty input --------------------------------------------------------
    error.clear();
    g3d::ImportResult garbage = importFile("not-a-model.txt", error);
    check(garbage.scene.isEmpty() && !error.empty(), "arquivo invalido rejeitado: " + error);

    error.clear();
    g3d::ImportResult empty;
    check(!g3d::importModelFromMemory(nullptr, 0, "empty.obj", empty, error) && !error.empty(),
          "entrada vazia rejeitada: " + error);

    // --- importer output feeds the UV generator -----------------------------------------
    error.clear();
    g3d::UvOptions options;
    options.padding = 2;
    g3d::UvResult uvResult;
    const g3d::Mesh *cubeMesh = cube.scene.meshAt(0);
    const bool generated =
        cubeMesh != nullptr && g3d::generateUv(*cubeMesh, options, uvResult, error);
    check(generated, "malha importada gera UVs com xatlas: " + error);
    if (generated) {
        check(uvResult.mesh.triangleCount() == cubeMesh->triangleCount(),
              "geracao de UV preserva os triangulos importados");
        std::printf("      %s\n", uvResult.describe().c_str());
    }

    // --- the document owns the scene the UI reads ---------------------------------------
    error.clear();
    const std::vector<char> twoObjectsBytes = readAll(modelPath("two-objects.obj"));
    g3d::Document &document = g3d::Document::instance();
    check(document.importModel(twoObjectsBytes.data(), twoObjectsBytes.size(),
                               modelPath("two-objects.obj"), error),
          "documento aceita a importacao: " + error);
    check(document.meshCount() == 2, "documento guarda as duas malhas");
    // The report is taken from the import result, so it must survive the move into the document.
    check(document.lastImportReport().find("malhas=2") != std::string::npos,
          "relatorio da importacao descreve a cena nova: " + document.lastImportReport());
    document.select(1);
    check(document.selected() == 1, "documento lembra a selecao");
    check(document.hierarchy().find("\n") != std::string::npos, "hierarquia tem uma linha por malha");
    document.select(-1);
    check(document.selected() == -1, "desmarcar funciona");

    // A failed import must not destroy the work already open.
    error.clear();
    g3d::Document &sameDocument = g3d::Document::instance();
    check(!sameDocument.importModel(nullptr, 0, "nada.obj", error) && sameDocument.meshCount() == 2,
          "importacao falha preserva a cena anterior");

    // --- o que uma importacao diz de si mesma -------------------------------------------------
    // Conferido depois da corrida, nao durante: o assimp e a caminhada pelos nos reportam do mesmo
    // thread que importa, entao o que fica para tras e exatamente o que o painel teria perguntado,
    // sem depender de tempo nenhum.
    g3d::ImportProgress &report = g3d::importProgress();
    check(!report.running(), "importacao concluida fecha o proprio progresso");
    check(report.percent() == -1, "fora de uma corrida nao ha numero para mostrar");
    check(report.saw(g3d::kImportPhaseRead), "a leitura dos bytes do arquivo se reportou");
    check(report.saw(g3d::kImportPhasePost), "os passos de pos-processamento se reportaram");
    check(report.saw(g3d::kImportPhaseAssemble), "a caminhada pelos nos do arquivo se reportou");
    check(report.peakPercent() == 100,
          "a corrida andou ate o fim do numero, chegou em " + std::to_string(report.peakPercent()));
    check(report.phaseToken() == "assemble",
          "o ultimo lugar em que a importacao estava e a montagem: " + report.phaseToken());

    // Um arquivo recusado antes de qualquer leitura nao abre corrida, e nao reabre a que acabou.
    error.clear();
    g3d::ImportResult refused;
    check(!g3d::importModelFromMemory(nullptr, 0, "nada.obj", refused, error) && !report.running(),
          "arquivo vazio nao abre corrida de progresso");

    // A classe sozinha, fase por fase, com os numeros que uma fonte entrega.
    g3d::ImportProgress local;
    check(!local.running() && local.percent() == -1 && local.phaseToken().empty(),
          "um relatorio sem corrida nao tem o que mostrar");
    local.begin();
    check(local.running() && local.percent() == -1, "corrida comecada ainda nao tem numero");
    local.updateFraction(g3d::kImportPhaseNone, 0.5f);
    check(local.percent() == -1 && local.phaseToken().empty(),
          "um numero que nao diz em que fase esta nao e mostrado");
    local.update(g3d::kImportPhaseRead, 500u, 1000u);
    check(local.percent() == 50 && local.phaseToken() == "read",
          "metade dos bytes lidos dizem metade: " + std::to_string(local.percent()));
    local.update(g3d::kImportPhaseRead, 1500u, 1000u);
    check(local.percent() == 100 && local.peakPercent() == 100,
          "um contador que passa do total e cortado em 100");
    local.update(g3d::kImportPhasePost, 0u, 0u);
    check(local.percent() == -1 && local.phaseToken() == "post" && local.peakPercent() == 100,
          "fase sem contador diz qual e e nao empresta o numero da anterior");
    local.updateFraction(g3d::kImportPhaseAssemble, 0.25f);
    check(local.percent() == 25 && local.phaseToken() == "assemble",
          "a fracao que a biblioteca entrega vira porcentagem da fase");
    local.updateFraction(g3d::kImportPhaseNone, -1.0f);
    check(local.percent() == -1 && local.phaseToken() == "assemble",
          "sem estimativa a tela fica com a fase e sem numero");
    local.end();
    check(!local.running() && local.percent() == -1 && local.phaseToken() == "assemble",
          "fechada a corrida sobra o nome da ultima fase");
    local.begin();
    check(!local.saw(g3d::kImportPhaseRead) && local.peakPercent() == -1,
          "uma corrida nova esquece a que passou");

    // As fases sao registradas uma por uma: o valor de uma nao pode acender a marca das outras.
    g3d::ImportProgress solo;
    solo.begin();
    solo.update(g3d::kImportPhaseAssemble, 3u, 4u);
    check(solo.saw(g3d::kImportPhaseAssemble) && !solo.saw(g3d::kImportPhaseRead) &&
              !solo.saw(g3d::kImportPhasePost),
          "uma fase sozinha nao marca as outras duas");

    std::printf("\n%d verificacoes, %d falhas\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
