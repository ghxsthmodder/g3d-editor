// Host test for the UV editing session: transforms, selection, validation and the rule that
// nothing reaches the mesh until the user applies. Compiled by tests/run_host_test.sh.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#include "core/document.hpp"
#include "core/primitives.hpp"
#include "core/uv_islands.hpp"
#include "importers/assimp_importer.hpp"
#include "uv/uv_edit.hpp"
#include "uv/uv_editor.hpp"
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

bool closeTo(float value, float expected, float tolerance = 1e-4f) {
    return std::fabs(value - expected) < tolerance;
}

// UV bounds of one island, read straight off the vertex data so the test never trusts the
// bookkeeping it is checking.
void islandBounds(const g3d::Mesh &mesh, const g3d::UvIslands &islands, uint32_t island,
                  float out[4]) {
    float minimum[2] = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    float maximum[2] = {std::numeric_limits<float>::lowest(),
                        std::numeric_limits<float>::lowest()};
    const g3d::UvIsland *group = islands.island(island);
    if (group == nullptr) {
        return;
    }
    for (uint32_t slot = group->firstFace; slot < group->firstFace + group->faceCount; ++slot) {
        const uint32_t face = islands.faceOrder[slot];
        for (uint32_t corner = 0; corner < 3; ++corner) {
            const uint32_t vertex = mesh.indices[face * 3 + corner];
            for (uint32_t axis = 0; axis < 2; ++axis) {
                const float value = mesh.uvs[vertex * 2 + axis];
                minimum[axis] = std::min(minimum[axis], value);
                maximum[axis] = std::max(maximum[axis], value);
            }
        }
    }
    out[0] = minimum[0];
    out[1] = minimum[1];
    out[2] = maximum[0];
    out[3] = maximum[1];
}

float islandArea(const g3d::Mesh &mesh, const g3d::UvIslands &islands, uint32_t island) {
    const g3d::UvIsland *group = islands.island(island);
    if (group == nullptr) {
        return 0.0f;
    }
    float total = 0.0f;
    for (uint32_t slot = group->firstFace; slot < group->firstFace + group->faceCount; ++slot) {
        const uint32_t face = islands.faceOrder[slot];
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

    // A cube whose six faces all sit on the same UV square: the worst case for overlap detection
    // and the reason re-packing exists.
    const std::string path = std::string(G3D_TEST_MODEL_DIR) + "/cube.obj";
    const std::vector<char> bytes = readAll(path);
    g3d::ImportResult imported;
    check(g3d::importModelFromMemory(bytes.data(), bytes.size(), path, imported, error),
          "importa cube.obj: " + error);
    const g3d::Mesh *source = imported.scene.meshAt(0);
    check(source != nullptr, "o arquivo traz uma malha");
    if (source == nullptr) {
        std::printf("\n%d verificacoes, %d falhas\n", checks, failures);
        return 1;
    }
    const std::vector<float> sourceUvs = source->uvs;

    // --- loading and the promise that the source is untouched ------------------------------
    g3d::UvSession session;
    check(session.load(*source, error), "sessao carrega a malha: " + error);
    check(session.islandCount() == 6, "a sessao ve as 6 ilhas do cubo");
    check(!session.hasChanges(), "recem-carregada nao tem mudancas");
    check(session.selection().empty(), "nada comeca selecionado");
    check(source->uvs == sourceUvs, "carregar nao altera a malha de origem");

    g3d::UvSession rejected;
    const g3d::Mesh noUvs = g3d::makeCube(0.5f);
    std::string rejectError;
    check(!rejected.load(noUvs, rejectError) && !rejectError.empty() && !rejected.isLoaded(),
          "malha sem UV nao abre sessao: " + rejectError);

    // --- selection -------------------------------------------------------------------------
    check(!session.selectIsland(99, false), "ilha inexistente nao seleciona");
    check(session.selection().empty(), "selecao continua vazia depois de um indice invalido");
    check(session.selectIsland(2, false) && session.selection().size() == 1 &&
              session.isSelected(2),
          "selecionar uma ilha funciona");
    check(session.selectIsland(4, true) && session.selection().size() == 2,
          "selecao aditiva acumula");
    check(session.selectIsland(4, true) && session.selection().size() == 1,
          "tocar de novo na mesma ilha a remove da selecao");
    session.selectAll();
    check(session.selection().size() == 6, "selecionar tudo pega as 6 ilhas");
    session.clearSelection();
    check(session.selection().empty(), "limpar selecao funciona");

    // --- picking in UV space ---------------------------------------------------------------
    session.selectIsland(0, false);
    const int picked = session.pick(0.5f, 0.5f);
    check(picked >= 0 && picked < 6, "tocar dentro de uma ilha devolve uma ilha");
    check(picked == 0, "uma ilha selecionada vence a que esta por baixo no mesmo ponto");
    check(session.pick(-2.0f, 3.0f) == -1, "tocar no vazio nao seleciona nada");

    // --- transforms ------------------------------------------------------------------------
    session.clearSelection();
    check(!session.translate(0.1f, 0.0f), "mover sem selecao nao faz nada");
    check(!session.rotate(1.0f, 0.0f, 0.0f), "girar sem selecao nao faz nada");
    check(!session.scale(2.0f, 0.0f, 0.0f), "escalar sem selecao nao faz nada");
    check(!session.fitSelectionToAtlas(), "ajustar sem selecao nao faz nada");

    session.selectIsland(1, false);
    float before[4];
    islandBounds(session.mesh(), session.islands(), 1, before);
    float otherBefore[4];
    islandBounds(session.mesh(), session.islands(), 3, otherBefore);
    check(session.translate(0.25f, -0.1f), "mover a ilha selecionada");
    float after[4];
    islandBounds(session.mesh(), session.islands(), 1, after);
    check(closeTo(after[0], before[0] + 0.25f) && closeTo(after[1], before[1] - 0.1f),
          "a ilha foi para onde foi pedido");
    float otherAfter[4];
    islandBounds(session.mesh(), session.islands(), 3, otherAfter);
    check(otherBefore[0] == otherAfter[0] && otherBefore[1] == otherAfter[1],
          "mover uma ilha nao arrasta as outras");
    check(session.hasChanges(), "a sessao sabe que ha mudancas");
    check(source->uvs == sourceUvs, "transformar nao toca na malha de origem");

    session.clearSelection();
    session.selectIsland(2, false);
    const float pivot[2] = {0.0f, 0.0f};
    check(session.rotate(static_cast<float>(M_PI) * 0.5f, pivot[0], pivot[1]),
          "girar 90 graus em torno da origem");
    float rotated[4];
    islandBounds(session.mesh(), session.islands(), 2, rotated);
    check(closeTo(rotated[0], -1.0f) && closeTo(rotated[2], 0.0f),
          "o giro de 90 graus levou U de [0,1] para [-1,0]");
    check(session.scale(0.5f, 0.0f, 0.0f), "escalar metade");
    float scaled[4];
    islandBounds(session.mesh(), session.islands(), 2, scaled);
    check(closeTo(scaled[0], -0.5f) && closeTo(scaled[2], 0.0f),
          "a escala se comporta em torno do mesmo pivo");
    check(!session.scale(0.0f, 0.0f, 0.0f), "escala zero e recusada");
    check(!session.rotate(0.0f, 0.0f, 0.0f), "girar zero angulo nao conta como edicao");

    // --- fitting back inside the atlas -----------------------------------------------------
    session.clearSelection();
    session.selectIsland(5, false);
    check(session.translate(4.0f, 4.0f), "empurra a ilha para fora do atlas");
    float outsideBox[4];
    islandBounds(session.mesh(), session.islands(), 5, outsideBox);
    check(outsideBox[0] > 1.0f, "a ilha esta mesmo fora do atlas");
    g3d::UvEditReport overflowing = session.validate();
    check(!overflowing.clean(), "a validacao percebe do atlas");
    check(!overflowing.outsideAtlas.empty(), "a ilha fora do atlas aparece no relatorio");
    std::printf("      %s\n", overflowing.describe().c_str());
    check(session.fitSelectionToAtlas(), "ajustar ao atlas aceita o pedido");
    islandBounds(session.mesh(), session.islands(), 5, outsideBox);
    check(outsideBox[0] >= -1e-4f && outsideBox[2] <= 1.0f + 1e-4f,
          "a ilha voltou para dentro do atlas");

    // --- overlap detection and re-packing --------------------------------------------------
    g3d::UvSession fresh;
    check(fresh.load(*source, error), "sessao nova para o empacotamento: " + error);
    g3d::UvEditReport stacked = fresh.validate();
    check(stacked.overlapping.size() == 6,
          "as 6 ilhas sobrepostas do cubo do arquivo sao apontadas");
    std::printf("      %s\n", stacked.describe().c_str());

    std::vector<float> areasBefore(fresh.islandCount());
    std::vector<float> boxesBefore(fresh.islandCount() * 4);
    for (uint32_t index = 0; index < fresh.islandCount(); ++index) {
        areasBefore[index] = islandArea(fresh.mesh(), fresh.islands(), index);
        islandBounds(fresh.mesh(), fresh.islands(), index, &boxesBefore[index * 4]);
    }
    fresh.selectIsland(3, false);
    check(fresh.repack(8), "reempacotar as ilhas");
    check(fresh.selection().size() == 1 && fresh.isSelected(3),
          "reempacotar preserva a selecao do usuario");
    g3d::UvEditReport packed = fresh.validate();
    check(packed.overlapping.empty(), "depois de reempacotar nao ha sobreposicao");
    check(packed.outsideAtlas.empty(), "depois de reempacotar nada sobra fora do atlas");
    check(packed.coveredArea > 0.0f && packed.coveredArea <= 1.0f,
          "o relatorio informa a area coberta do atlas");
    std::printf("      %s\n", packed.describe().c_str());

    // A shelf pack only moves islands and shrinks them uniformly, so each box keeps its aspect
    // ratio and its area falls by the square of its sides.
    bool shapesKept = true;
    for (uint32_t index = 0; index < fresh.islandCount(); ++index) {
        const g3d::UvIsland *group = fresh.islands().island(index);
        const float width = group->boundsMax[0] - group->boundsMin[0];
        const float height = group->boundsMax[1] - group->boundsMin[1];
        const float beforeWidth = boxesBefore[index * 4 + 2] - boxesBefore[index * 4 + 0];
        const float beforeHeight = boxesBefore[index * 4 + 3] - boxesBefore[index * 4 + 1];
        const float scaleX = width / beforeWidth;
        const float scaleY = height / beforeHeight;
        if (!std::isfinite(scaleX) || !std::isfinite(scaleY) || scaleX <= 0.0f ||
            scaleY <= 0.0f || scaleX > 1.001f || scaleY > 1.001f ||
            std::fabs(scaleX - scaleY) > 1e-3f) {
            shapesKept = false;
            continue;
        }
        if (std::fabs(group->area - areasBefore[index] * scaleX * scaleY) > 1e-3f) {
            shapesKept = false;
        }
    }
    check(shapesKept, "reempacotar so move e reduz proporcionalmente, sem deformar as ilhas");

    // --- a generated layout is a valid starting point too ----------------------------------
    g3d::UvOptions options;
    options.padding = 4;
    g3d::UvResult generated;
    check(g3d::generateUv(g3d::makeCube(0.5f), options, generated, error),
          "xatlas gera UVs para o cubo: " + error);
    g3d::UvSession fromGeneration;
    check(fromGeneration.load(generated.mesh, error),
          "sessao abre sobre o resultado do xatlas: " + error);
    check(fromGeneration.islandCount() == generated.chartCount,
          "a sessao reconstroi as ilhas do xatlas sem depender dos ids de chart");
    g3d::UvEditReport generatedReport = fromGeneration.validate();
    check(generatedReport.overlapping.empty(), "o atlas gerado pelo xatlas nao tem sobreposicao");
    check(generatedReport.outsideAtlas.empty(), "o atlas gerado pelo xatlas cabe no atlas");
    check(generatedReport.coveredArea > 0.0f, "o atlas gerado pelo xatlas cobre area do atlas");
    std::printf("      %s\n", generatedReport.describe().c_str());
    check(!fromGeneration.describe().empty(), "a sessao se descreve para o log");

    // --- the editor singleton: the surface the interface actually talks to ------------------
    const std::string scenePath = std::string(G3D_TEST_MODEL_DIR) + "/two-objects.obj";
    const std::vector<char> sceneBytes = readAll(scenePath);
    g3d::Document &document = g3d::Document::instance();
    check(document.importModel(sceneBytes.data(), sceneBytes.size(), scenePath, error),
          "importa two-objects.obj no documento: " + error);
    check(document.meshCount() == 2, "a cena tem 2 malhas");

    g3d::UvEditor &editor = g3d::UvEditor::instance();
    g3d::UvLayout snapshot;
    check(!editor.layout(snapshot) && !snapshot.active, "nenhuma sessao comeca inativa");

    std::string editorError;
    check(editor.begin(1, editorError), "abre a segunda malha para editar: " + editorError);
    check(editor.isActive() && editor.meshIndex() == 1, "o editor sabe qual malha esta aberta");
    check(editor.select(0, false), "selecionar pela interface funciona");
    check(editor.layout(snapshot) && snapshot.active, "o layout fica legivel para a thread GL");
    check(snapshot.selected.size() == 1 && snapshot.selected[0] == 0,
          "o layout carrega a selecao");
    check(snapshot.faceIsland.size() * 3 == snapshot.indices.size(),
          "o layout traz uma ilha por triangulo");
    check(snapshot.islandBounds.size() % 4 == 0 && !snapshot.islandBounds.empty(),
          "o layout traz os limites de cada ilha em grupos de quatro floats");
    const uint32_t islandsInLayout = static_cast<uint32_t>(snapshot.islandBounds.size() / 4);
    bool overlapIdsValid = true;
    for (const uint32_t island : snapshot.overlapping) {
        if (island >= islandsInLayout) {
            overlapIdsValid = false;
        }
    }
    check(overlapIdsValid && islandsInLayout == 6,
          "as sobreposicoes do layout apontam para ilhas que existem");

    const uint64_t revisionBefore = snapshot.revision;
    std::vector<float> uvsBefore;
    document.withScene([&uvsBefore](g3d::Scene &scene) { uvsBefore = scene.meshAt(1)->uvs; });
    check(editor.translate(0.05f, 0.02f), "mover pela interface");
    check(editor.layout(snapshot), "o layout e relido depois da edicao");
    check(snapshot.revision != revisionBefore, "editar muda a revisao, e so a revisao conta");
    std::vector<float> uvsDuringEdit;
    document.withScene([&uvsDuringEdit](g3d::Scene &scene) { uvsDuringEdit = scene.meshAt(1)->uvs; });
    check(uvsDuringEdit == uvsBefore, "editar nao encosta na cena antes de aplicar");

    check(editor.apply(editorError), "aplicar grava na cena: " + editorError);
    std::vector<float> uvsAfterApply;
    document.withScene([&uvsAfterApply](g3d::Scene &scene) { uvsAfterApply = scene.meshAt(1)->uvs; });
    check(uvsAfterApply != uvsBefore, "a cena recebe os UVs aplicados");
    check(!editor.describe().empty(), "o editor se descreve para o log");
    check(!editor.report().empty(), "o relatorio de validacao chega como texto");
    std::printf("      %s\n", editor.report().c_str());

    // Cancel must leave the scene exactly as it was.
    std::vector<float> uvsCommitted = uvsAfterApply;
    check(editor.begin(1, editorError), "reabrir a malha aplicada: " + editorError);
    check(editor.select(0, false) && editor.translate(-0.2f, 0.0f), "editar de novo");
    editor.cancel();
    check(!editor.isActive(), "cancelar fecha a sessao");
    std::vector<float> uvsAfterCancel;
    document.withScene([&uvsAfterCancel](g3d::Scene &scene) { uvsAfterCancel = scene.meshAt(1)->uvs; });
    check(uvsAfterCancel == uvsCommitted, "cancelar devolve a cena ao estado aplicado");

    // Generation through the editor replaces the whole mesh, so the scene has to grow.
    g3d::UvOptions generateOptions;
    generateOptions.padding = 2;
    uint32_t verticesBefore = 0;
    document.withScene([&verticesBefore](g3d::Scene &scene) {
        verticesBefore = scene.meshAt(0)->vertexCount();
    });
    check(editor.generate(0, generateOptions, editorError),
          "gerar UVs pela interface: " + editorError);
    check(editor.layout(snapshot) && snapshot.active, "a geracao abre a sessao de preview");
    check(editor.apply(editorError), "aplicar o UV gerado: " + editorError);
    uint32_t verticesAfter = 0;
    document.withScene([&verticesAfter](g3d::Scene &scene) { verticesAfter = scene.meshAt(0)->vertexCount(); });
    check(verticesAfter >= verticesBefore, "a malha gerada pode duplicar vertices nas costuras");
    bool appliedUvs = false;
    document.withScene([&appliedUvs](g3d::Scene &scene) {
        const g3d::Mesh *mesh = scene.meshAt(0);
        appliedUvs = mesh != nullptr && mesh->hasUvs();
    });
    check(appliedUvs, "a cena passa a ter UVs depois de aplicar a geracao");
    editor.cancel();

    std::printf("\n%d verificacoes, %d falhas\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
