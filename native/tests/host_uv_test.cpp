// Host test for UV island grouping: the editor selects and transforms islands, so the grouping
// has to agree with the xatlas charts and work on UVs that came straight from a file.
// Compiled by tests/run_host_test.sh, not part of the APK.

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/primitives.hpp"
#include "core/uv_islands.hpp"
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

// A square split into two triangles. When `welded`, the triangles share the diagonal vertices;
// otherwise they are duplicated, which is exactly what a UV seam looks like.
g3d::Mesh makeSquare(bool welded) {
    g3d::Mesh mesh;
    mesh.name = "quadrado";
    mesh.positions = {-0.5f, -0.5f, 0.0f, 0.5f, -0.5f, 0.0f, 0.5f, 0.5f, 0.0f, -0.5f, 0.5f, 0.0f};
    mesh.normals = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    mesh.uvs = {0.0f, 0.0f, 0.5f, 0.0f, 0.5f, 0.5f, 0.0f, 0.5f};
    if (welded) {
        mesh.indices = {0, 1, 2, 0, 2, 3};
        return mesh;
    }
    mesh.positions.insert(mesh.positions.end(),
                          {-0.5f, -0.5f, 0.0f, 0.5f, -0.5f, 0.0f, 0.5f, 0.5f, 0.0f, -0.5f, 0.5f,
                           0.0f});
    mesh.normals.insert(mesh.normals.end(), {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1});
    mesh.uvs.insert(mesh.uvs.end(), {0.0f, 0.0f, 0.5f, 0.0f, 0.5f, 0.5f, 0.0f, 0.5f});
    mesh.indices = {0, 1, 2, 4, 5, 6};
    return mesh;
}

// A copy of the square laid exactly on top of itself: the same UV space reached twice, which is
// allowed on purpose for symmetric models and has to stay two islands.
g3d::Mesh makeOverlappingSquares() {
    g3d::Mesh mesh = makeSquare(true);
    const uint32_t second = 4;
    for (uint32_t vertex = 0; vertex < 4; ++vertex) {
        for (uint32_t axis = 0; axis < 3; ++axis) {
            mesh.positions.push_back(mesh.positions[vertex * 3 + axis]);
        }
        mesh.normals.push_back(0.0f);
        mesh.normals.push_back(0.0f);
        mesh.normals.push_back(1.0f);
        for (uint32_t axis = 0; axis < 2; ++axis) {
            mesh.uvs.push_back(mesh.uvs[vertex * 2 + axis]);
        }
    }
    mesh.indices = {0, 1, 2, 0, 2, 3, second, second + 1, second + 2, second, second + 2,
                    second + 3};
    return mesh;
}

// Walks every island range and reports how many times each face shows up, so a broken grouping
// fails loudly instead of leaving a subtly wrong island list behind.
bool rangesCoverEveryFace(const g3d::Mesh &mesh, const g3d::UvIslands &islands) {
    std::vector<uint32_t> seen(mesh.triangleCount(), 0);
    for (uint32_t index = 0; index < islands.count(); ++index) {
        const g3d::UvIsland *island = islands.island(index);
        if (island == nullptr || island->firstFace + island->faceCount > islands.faceOrder.size()) {
            return false;
        }
        for (uint32_t slot = island->firstFace; slot < island->firstFace + island->faceCount;
             ++slot) {
            const uint32_t face = islands.faceOrder[slot];
            if (face >= seen.size() || ++seen[face] != 1 || islands.faceIsland[face] != index) {
                return false;
            }
        }
    }
    for (const uint32_t count : seen) {
        if (count != 1) {
            return false;
        }
    }
    return true;
}

float totalUvArea(const g3d::Mesh &mesh) {
    float total = 0.0f;
    for (uint32_t face = 0; face < mesh.triangleCount(); ++face) {
        const uint32_t *corner = &mesh.indices[face * 3];
        const float *a = &mesh.uvs[corner[0] * 2];
        const float *b = &mesh.uvs[corner[1] * 2];
        const float *c = &mesh.uvs[corner[2] * 2];
        total += 0.5f * std::fabs((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]));
    }
    return total;
}

}  // namespace

int main() {
    std::string error;

    // --- the rule the grouping is built on -------------------------------------------------
    g3d::UvIslands welded;
    const g3d::Mesh weldedSquare = makeSquare(true);
    check(g3d::computeIslands(weldedSquare, welded, error), "quadrado soldado agrupa: " + error);
    check(welded.count() == 1, "diagonal compartilhada faz 1 ilha");
    check(rangesCoverEveryFace(weldedSquare, welded), "cada face aparece em uma unica faixa");
    const g3d::UvIsland *first = welded.island(0);
    check(first != nullptr && std::fabs(first->area - 0.25f) < 1e-5f, "area UV do quadrado e 0.25");
    check(first != nullptr && std::fabs(first->boundsMax[0] - 0.5f) < 1e-5f,
          "bounds da ilha vao ate 0.5 em U");

    g3d::UvIslands split;
    const g3d::Mesh splitSquare = makeSquare(false);
    check(g3d::computeIslands(splitSquare, split, error), "quadrado emendado agrupa: " + error);
    check(split.count() == 2, "a mesma diagonal duplicada vira costura e separa 2 ilhas");

    g3d::UvIslands doubled;
    const g3d::Mesh overlapping = makeOverlappingSquares();
    check(g3d::computeIslands(overlapping, doubled, error),
          "duas camadas sobrepostas agrupam: " + error);
    check(doubled.count() == 2, "sobreposicao intencional continua sendo 2 ilhas");

    // --- rejected inputs -------------------------------------------------------------------
    g3d::UvIslands unused;
    const g3d::Mesh noUvs = g3d::makeCube(0.5f);
    std::string missingUvError;
    check(!g3d::computeIslands(noUvs, unused, missingUvError) && !missingUvError.empty(),
          "malha sem UV e rejeitada: " + missingUvError);
    g3d::Mesh empty;
    std::string emptyError;
    check(!g3d::computeIslands(empty, unused, emptyError) && !emptyError.empty(),
          "malha vazia e rejeitada: " + emptyError);
    check(unused.count() == 0, "uma falha nao deixa ilhas para tras");

    // --- xatlas output: connectivity must agree with the chart ids -------------------------
    g3d::UvOptions options;
    options.padding = 4;
    g3d::UvResult generated;
    std::string generateError;
    check(g3d::generateUv(noUvs, options, generated, generateError),
          "xatlas gera UVs para o cubo: " + generateError);
    g3d::UvIslands fromCharts;
    check(g3d::computeIslands(generated.mesh, fromCharts, error),
          "ilhas do resultado do xatlas: " + error);
    std::printf("      %s\n", fromCharts.describe().c_str());
    check(fromCharts.count() == generated.chartCount,
          "a conectividade UV reproduce os charts do xatlas (" +
              std::to_string(fromCharts.count()) + " contra " +
              std::to_string(generated.chartCount) + ")");
    check(rangesCoverEveryFace(generated.mesh, fromCharts),
          "cada triangulo do cubo aparece em uma unica faixa");

    bool insideAtlas = true;
    float islandArea = 0.0f;
    for (const g3d::UvIsland &island : fromCharts.islands) {
        if (island.faceCount == 0 || island.boundsMin[0] < -1e-5f ||
            island.boundsMax[0] > 1.0f + 1e-5f || island.boundsMin[1] < -1e-5f ||
            island.boundsMax[1] > 1.0f + 1e-5f) {
            insideAtlas = false;
        }
        islandArea += island.area;
    }
    check(insideAtlas, "todas as ilhas ficam dentro do atlas");
    check(std::fabs(islandArea - totalUvArea(generated.mesh)) < 1e-4f,
          "a soma das areas das ilhas fecha com a malha");

    // --- UVs that came from a file, never touched by xatlas --------------------------------
    const std::string path = std::string(G3D_TEST_MODEL_DIR) + "/cube.obj";
    const std::vector<char> bytes = readAll(path);
    g3d::ImportResult imported;
    std::string importError;
    check(g3d::importModelFromMemory(bytes.data(), bytes.size(), path, imported, importError),
          "importa cube.obj: " + importError);
    const g3d::Mesh *authored = imported.scene.meshAt(0);
    check(authored != nullptr && authored->hasUvs(), "o cubo do arquivo traz UVs proprios");
    if (authored != nullptr) {
        g3d::UvIslands authoredIslands;
        check(g3d::computeIslands(*authored, authoredIslands, error),
              "ilhas dos UVs do arquivo: " + error);
        std::printf("      %s\n", authoredIslands.describe().c_str());
        check(authoredIslands.count() == 6, "as 6 faces do cubo do arquivo viram 6 ilhas");
        check(rangesCoverEveryFace(*authored, authoredIslands),
              "as ilhas do arquivo cobrem todas as faces");
    }

    std::printf("\n%d verificacoes, %d falhas\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
