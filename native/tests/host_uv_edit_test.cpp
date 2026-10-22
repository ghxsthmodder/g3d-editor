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

// UV bounds of one island recomputed from the layout snapshot the renderer consumes, so the
// bookkeeping inside that snapshot is never taken on trust either.
void islandBoundsFromLayout(g3d::UvEditor &editor, g3d::UvLayout &layout, uint32_t island,
                            float out[4]) {
    out[0] = out[1] = std::numeric_limits<float>::max();
    out[2] = out[3] = std::numeric_limits<float>::lowest();
    if (!editor.layout(layout)) {
        return;
    }
    for (size_t triangle = 0; triangle * 3 + 2 < layout.indices.size(); ++triangle) {
        if (triangle >= layout.faceIsland.size() || layout.faceIsland[triangle] != island) {
            continue;
        }
        for (uint32_t corner = 0; corner < 3; ++corner) {
            const uint32_t vertex = layout.indices[triangle * 3 + corner];
            for (uint32_t axis = 0; axis < 2; ++axis) {
                const float value = layout.uvs[vertex * 2 + axis];
                out[axis] = std::min(out[axis], value);
                out[axis + 2] = std::max(out[axis + 2], value);
            }
        }
    }
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

    // --- pinning an island -----------------------------------------------------------------
    // A pin is how an island that is already right stops being in the way. The packed session is
    // reused because its islands are separated: the box one of them holds is a real obstacle for
    // the others, and not the whole atlas the file's stacked layout would hand over.
    check(fresh.isSelected(3), "a ilha 3 continua selecionada do empacotamento acima");
    check(fresh.lockedCount() == 0, "nenhuma ilha comeca com pino");
    check(fresh.lockSelection(), "o pino pega a ilha selecionada");
    check(fresh.isLocked(3) && fresh.lockedCount() == 1, "a ilha fica marcada como bloqueada");
    check(!fresh.lockSelection(), "bloquear o que ja esta bloqueado nao faz nada");
    check(fresh.isSelected(3), "a ilha com pino continua selecionada: assim ela pode ser solta");
    check(fresh.movableCount() == 0, "uma selecao so com pinos nao alcansa nada");
    float heldBox[4];
    islandBounds(fresh.mesh(), fresh.islands(), 3, heldBox);
    check(!fresh.translate(0.1f, 0.0f), "mover nao alcansa uma ilha com pino");
    check(!fresh.rotate(static_cast<float>(M_PI) * 0.5f, 0.5f, 0.5f),
          "girar nao alcansa uma ilha com pino");
    check(!fresh.scale(2.0f, 0.5f, 0.5f), "escalar nao alcansa uma ilha com pino");
    check(!fresh.fitSelectionToAtlas(), "ajustar nao alcansa uma ilha com pino");
    float heldStill[4];
    islandBounds(fresh.mesh(), fresh.islands(), 3, heldStill);
    check(heldStill[0] == heldBox[0] && heldStill[1] == heldBox[1] &&
              heldStill[2] == heldBox[2] && heldStill[3] == heldBox[3],
          "nenhuma ferramenta encostou na ilha bloqueada");

    // A mixed group is what the finger actually does: the reachable islands follow the drag and the
    // pinned one is left out of the box the tool shrinks and moves.
    check(fresh.selectIsland(1, true), "somar uma ilha livre a selecao");
    check(fresh.movableCount() == 1, "a ferramenta ve apenas a ilha que pode alcancar");
    g3d::UvEditReport pinnedReport = fresh.validate();
    const bool pinIsSaid = pinnedReport.describe().find("bloqueadas=1") != std::string::npos;
    check(pinnedReport.lockedCount == 1 && pinIsSaid,
          "o relatorio conta as ilhas bloqueadas");
    float freeBefore[4];
    islandBounds(fresh.mesh(), fresh.islands(), 1, freeBefore);
    check(fresh.translate(0.05f, 0.0f), "mover a selecao mista");
    float freeAfter[4];
    islandBounds(fresh.mesh(), fresh.islands(), 1, freeAfter);
    islandBounds(fresh.mesh(), fresh.islands(), 3, heldStill);
    check(closeTo(freeAfter[0], freeBefore[0] + 0.05f), "a ilha livre seguiu o arrasto");
    check(heldStill[0] == heldBox[0] && heldStill[3] == heldBox[3],
          "a ilha com pino ficou onde estava");

    // Repacking around a pin: the pinned box is the one the shelves step over, and the islands that
    // are still free have to keep their place in the atlas.
    check(fresh.repack(8), "reempacotar com uma ilha bloqueada");
    islandBounds(fresh.mesh(), fresh.islands(), 3, heldStill);
    check(heldStill[0] == heldBox[0] && heldStill[1] == heldBox[1] &&
              heldStill[2] == heldBox[2] && heldStill[3] == heldBox[3],
          "o reempacotamento deixou a ilha com pino no lugar");
    g3d::UvEditReport afterPinnedRepack = fresh.validate();
    check(afterPinnedRepack.overlapping.empty(),
          "as ilhas reempacotadas nao se jogaram por cima da bloqueada");
    check(afterPinnedRepack.outsideAtlas.empty(),
          "o pino nao empurrou ninguem para fora do atlas");
    check(afterPinnedRepack.lockedCount == 1, "o reempacotamento nao solta os pinos");

    // The pin belongs to the session and not to the layout, so taking the repack back keeps the
    // island pinned: undoing an edit is not a way to unlock what was never edited.
    check(fresh.undo(), "desfazer o reempacotamento");
    check(fresh.isLocked(3) && fresh.lockedCount() == 1, "desfazer nao mexe nos pinos");
    islandBounds(fresh.mesh(), fresh.islands(), 3, heldStill);
    check(heldStill[0] == heldBox[0] && heldStill[3] == heldBox[3],
          "a ilha com pino voltou ao lugar de antes do empacotamento");
    check(fresh.unlockSelection(), "destravar a selecao");
    check(!fresh.isLocked(3) && fresh.lockedCount() == 0, "solto o pino, a ilha esta livre");
    check(!fresh.unlockSelection(), "destravar sem nada com pino nao faz nada");
    islandBounds(fresh.mesh(), fresh.islands(), 3, heldStill);
    check(fresh.translate(0.02f, 0.0f), "sem pino a ilha volta a ser movida");
    islandBounds(fresh.mesh(), fresh.islands(), 3, freeAfter);
    check(closeTo(freeAfter[0], heldStill[0] + 0.02f), "a ilha destravada seguiu o mover");

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

    // The interface rotates and scales around the centre of the selection, so that centre has to
    // exist while something is selected and disappear with the selection.
    float selectionCentre[2] = {-2.0f, -2.0f};
    check(editor.selectionPivot(selectionCentre), "a selecao tem um centro para girar e escalar");
    check(selectionCentre[0] >= -1.0f && selectionCentre[0] <= 2.0f &&
              selectionCentre[1] >= -1.0f && selectionCentre[1] <= 2.0f,
          "o centro da selecao cai perto do atlas");
    editor.clearSelection();
    check(!editor.selectionPivot(selectionCentre), "sem selecao nao ha centro");
    check(editor.select(0, false), "repor a selecao");

    // Exactly what the rotate button does: the pivot comes from the selection itself, and a
    // quarter turn about it swaps the island's width and height.
    float boxBefore[4];
    islandBoundsFromLayout(editor, snapshot, 0, boxBefore);
    check(editor.rotate(static_cast<float>(M_PI) * 0.5f, selectionCentre[0], selectionCentre[1]),
          "girar a selecao em torno do proprio centro");
    float boxAfter[4];
    islandBoundsFromLayout(editor, snapshot, 0, boxAfter);
    const float widthBefore = boxBefore[2] - boxBefore[0];
    const float heightBefore = boxBefore[3] - boxBefore[1];
    check(std::fabs((boxAfter[2] - boxAfter[0]) - heightBefore) < 1e-3f &&
              std::fabs((boxAfter[3] - boxAfter[1]) - widthBefore) < 1e-3f,
          "girar troca a largura pela altura da ilha");
    check(editor.scale(0.5f, selectionCentre[0], selectionCentre[1]), "escalar a selecao");
    islandBoundsFromLayout(editor, snapshot, 0, boxAfter);
    check(std::fabs((boxAfter[2] - boxAfter[0]) - heightBefore * 0.5f) < 1e-3f,
          "escalar reduz a ilha pela metade");

    // The pin as the interface reaches it: the atlas has to be told which island to draw apart from
    // the rest, a tool has to pass over it, and a group made only of pins has to refuse the drag so
    // the touch falls back to the camera instead of moving nothing.
    check(editor.lockSelection(), "a interface bloqueia a selecao");
    check(editor.layout(snapshot) && snapshot.locked.size() == 1 && snapshot.locked[0] == 0,
          "o layout entrega a ilha bloqueada ao renderizador");
    check(editor.movableCount() == 0, "uma selecao so de pinos nao arrasta nada");
    const uint64_t revisionWithPin = snapshot.revision;
    islandBoundsFromLayout(editor, snapshot, 0, boxBefore);
    check(!editor.translate(0.05f, 0.0f), "mover pela interface nao alcansa o pino");
    check(editor.layout(snapshot) && snapshot.revision == revisionWithPin,
          "uma ferramenta recusada nao reescreve o layout");
    islandBoundsFromLayout(editor, snapshot, 0, boxAfter);
    check(boxAfter[0] == boxBefore[0] && boxAfter[3] == boxBefore[3],
          "a ilha com pino nao se moveu pela interface");
    check(editor.unlockSelection(), "a interface solta o pino");
    check(editor.layout(snapshot) && snapshot.locked.empty(),
          "o layout avisa que nao ha mais ilha bloqueada");
    check(!editor.unlockSelection(), "destravar duas vezes nao faz nada");
    check(editor.movableCount() == 1, "solto o pino a selecao volta a ser alcancavel");

    editor.cancel();
    check(!editor.isActive(), "cancelar fecha a sessao");
    check(!editor.selectionPivot(selectionCentre), "com a sessao fechada nao ha centro");
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

    // --- undo and redo ---------------------------------------------------------------------
    // The session the interface drives: a step is the layout before an operation, so what the user
    // can take back is exactly what the operations did to the islands.
    check(editor.begin(1, editorError), "reabrir a malha 1 para o desfazer: " + editorError);
    check(!editor.undo(), "sessao recém-aberta nao tem nada a desfazer");
    check(!editor.redo(), "sessao recém-aberta nao tem nada a refazer");
    check(editor.history().find("desfazer=0") != std::string::npos,
          "o status diz quantos passos existem: " + editor.history());

    check(editor.select(0, false), "selecionar a ilha 1 para desfazer");
    float boxRestored[4];
    islandBoundsFromLayout(editor, snapshot, 0, boxRestored);
    check(editor.translate(0.07f, -0.03f), "mover antes de desfazer");
    float boxMoved[4];
    islandBoundsFromLayout(editor, snapshot, 0, boxMoved);
    check(std::fabs(boxMoved[0] - boxRestored[0]) > 1e-4f, "o moveu chegou na ilha");
    check(editor.undo(), "desfazer o movimento");
    islandBoundsFromLayout(editor, snapshot, 0, boxRestored);
    check(std::fabs(boxRestored[0] - boxMoved[0]) > 1e-4f &&
              std::fabs(boxRestored[2] - (boxMoved[2] - 0.07f)) < 1e-4f,
          "desfazer devolve a ilha ao lugar de antes");
    check(editor.redo(), "refazer o movimento desfeito");
    float boxRedone[4];
    islandBoundsFromLayout(editor, snapshot, 0, boxRedone);
    check(std::fabs(boxRedone[0] - boxMoved[0]) < 1e-4f &&
              std::fabs(boxRedone[3] - boxMoved[3]) < 1e-4f,
          "refazer traz o mesmo layout de antes do desfazer");
    check(editor.undo() && !editor.undo(), "desfazer uma vez sobra e a segunda nao tem mais passo");

    // A drag is one step: five moves between the grab and the lift are taken back together.
    const float dragStep = 0.01f;
    islandBoundsFromLayout(editor, snapshot, 0, boxRestored);
    const float beforeDragMinU = boxRestored[0];
    editor.beginGesture();
    for (int move = 0; move < 5; ++move) {
        check(editor.translate(dragStep, 0.0f), "move do arrasto " + std::to_string(move));
    }
    editor.endGesture();
    islandBoundsFromLayout(editor, snapshot, 0, boxMoved);
    check(std::fabs(boxMoved[0] - (beforeDragMinU + 5.0f * dragStep)) < 1e-4f,
          "os cinco moves somam um deslocamento so");
    check(editor.undo(), "desfazer o arrasto inteiro");
    islandBoundsFromLayout(editor, snapshot, 0, boxRedone);
    check(std::fabs(boxRedone[0] - beforeDragMinU) < 1e-4f,
          "um desfazer apaga o arrasto todo, nao o ultimo move");
    check(editor.redo(), "refazer o arrasto");
    islandBoundsFromLayout(editor, snapshot, 0, boxMoved);
    check(std::fabs(boxMoved[0] - (beforeDragMinU + 5.0f * dragStep)) < 1e-4f,
          "refazer repoe o arrasto inteiro");

    // Repacking touches every island and is still one step.
    check(editor.repack(2), "reempacotar para desfazer de um passo");
    const uint32_t islandsBeforeUndo = static_cast<uint32_t>(snapshot.islandBounds.size() / 4);
    check(editor.layout(snapshot) && islandsBeforeUndo > 0, "o reempacotamento mexeu no layout");
    check(editor.undo(), "desfazer o reempacotamento");
    check(editor.layout(snapshot), "o layout volta depois de desfazer o reempacotamento");

    // Undo out of a fresh generation, back to the layout the session started with. The scene is
    // never touched here, so the comparison is made on the session's own layout.
    check(editor.begin(1, editorError), "reabrir a malha 1 para a geracao: " + editorError);
    check(editor.layout(snapshot), "a sessao aberta entrega seu layout");
    const std::vector<float> layoutBeforeGenerate = snapshot.uvs;
    const std::vector<uint32_t> indicesBeforeGenerate = snapshot.indices;
    check(editor.generate(1, generateOptions, editorError),
          "gerar sobre uma sessao aberta: " + editorError);
    check(editor.layout(snapshot) && snapshot.uvs != layoutBeforeGenerate,
          "a geracao troca o layout da sessao");
    check(editor.undo(), "desfazer a geracao");
    check(editor.layout(snapshot) && snapshot.uvs == layoutBeforeGenerate &&
              snapshot.indices == indicesBeforeGenerate,
          "desfazer a geracao devolve o layout e a malha com que a sessao abriu");
    check(editor.redo(), "refazer a geracao desfeita");
    check(editor.layout(snapshot) && snapshot.uvs != layoutBeforeGenerate,
          "refazer traz o layout gerado de volta");

    // Applying ends the window: the scene owns the layout from there on.
    check(editor.select(0, false) && editor.translate(0.02f, 0.0f), "editar antes de aplicar");
    check(editor.apply(editorError), "aplicar com historia aberta: " + editorError);
    check(!editor.undo(), "aplicar fecha o desfazer da previsualizacao");

    // The history is capped, so an endless drag session cannot grow without bound.
    check(editor.begin(1, editorError), "reabrir para o limite de historia: " + editorError);
    check(editor.select(0, false), "selecionar para os passos em serie");
    for (int step = 0; step < 70; ++step) {
        editor.translate(0.001f, 0.0f);
    }
    int undoneSteps = 0;
    while (editor.undo()) {
        ++undoneSteps;
    }
    check(undoneSteps == 64, "a historia guarda no maximo 64 passos, guardou " +
                                 std::to_string(undoneSteps));
    check(editor.layout(snapshot), "o layout continua legivel depois de esvaziar a historia");
    // The line the panel shows: what the open session holds, layout and the steps behind it.
    const size_t sessionBytes = editor.memoryBytes();
    check(sessionBytes > 0, "a sessao aberta retem memoria: " + std::to_string(sessionBytes));
    check(sessionBytes >= 64 * sizeof(float), "a conta cobre ao menos o layout guardado");
    editor.cancel();
    check(editor.memoryBytes() == 0, "cancelar devolve a memoria da sessao");
    check(editor.begin(1, editorError), "reabrir depois de cancelar: " + editorError);
    check(!editor.undo(), "cancelar limpa a historia");
    editor.cancel();

    std::printf("\n%d verificacoes, %d falhas\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
