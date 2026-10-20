// Host harness for the importer: runs Assimp against the fixtures in tests/models and checks the
// conversion into the internal representation. Built by the host CMake branch, not part of the APK.

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "core/document.hpp"
#include "core/primitives.hpp"
#include "importers/assimp_importer.hpp"
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

    std::printf("\n%d verificacoes, %d falhas\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
