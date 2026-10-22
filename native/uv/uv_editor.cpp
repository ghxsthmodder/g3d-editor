#include "uv/uv_editor.hpp"

#include <algorithm>

#include "core/document.hpp"

namespace g3d {
namespace {

// Reads one mesh out of the scene without holding the editor lock while the document lock is
// taken, so the two locks are never nested.
bool copySceneMesh(uint32_t index, Mesh &out) {
    bool found = false;
    Document::instance().withScene([&out, &found, index](Scene &scene) {
        const Mesh *mesh = scene.meshAt(index);
        if (mesh != nullptr) {
            out = *mesh;
            found = true;
        }
    });
    return found;
}

}  // namespace

UvEditor &UvEditor::instance() {
    static UvEditor editor;
    return editor;
}

bool UvEditor::begin(uint32_t meshIndex, std::string &error) {
    Mesh source;
    if (!copySceneMesh(meshIndex, source)) {
        error = "nao ha malha com esse indice na cena";
        return false;
    }
    std::lock_guard<std::mutex> guard(mutex_);
    session_.clearHistory();
    if (!session_.load(source, error)) {
        active_ = false;
        return false;
    }
    meshIndex_ = meshIndex;
    active_ = true;
    ++revision_;
    return true;
}

bool UvEditor::generate(uint32_t meshIndex, const UvOptions &options, std::string &error) {
    Mesh source;
    if (!copySceneMesh(meshIndex, source)) {
        error = "nao ha malha com esse indice na cena";
        return false;
    }
    UvResult generated;
    if (!generateUv(source, options, generated, error)) {
        return false;
    }
    std::lock_guard<std::mutex> guard(mutex_);
    // A generation replaces the layout, so the state before it has to be in the history: opening a
    // generate straight after begin() has nothing to go back to beyond the file's own UVs.
    if (active_) {
        session_.pushUndoStep();
    }
    if (!session_.load(generated.mesh, error)) {
        active_ = false;
        return false;
    }
    session_.setAtlasSize(generated.atlasWidth, generated.atlasHeight);
    meshIndex_ = meshIndex;
    active_ = true;
    ++revision_;
    return true;
}

bool UvEditor::apply(std::string &error) {
    Mesh edited;
    uint32_t index = 0;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!active_) {
            error = "nada aberto para aplicar";
            return false;
        }
        edited = session_.mesh();
        index = meshIndex_;
    }
    if (!Document::instance().replaceMesh(index, edited)) {
        error = "a malha nao existe mais na cena";
        return false;
    }
    std::lock_guard<std::mutex> guard(mutex_);
    session_.load(edited, error);
    error.clear();
    // The scene holds this layout now. Undo reaching back into the preview would leave the two
    // disagreeing about what the document is, so the window on the previous steps closes here.
    session_.clearHistory();
    ++revision_;
    return true;
}

void UvEditor::cancel() {
    std::lock_guard<std::mutex> guard(mutex_);
    session_.clear();
    session_.clearHistory();
    active_ = false;
    ++revision_;
}

bool UvEditor::isActive() {
    std::lock_guard<std::mutex> guard(mutex_);
    return active_;
}

uint32_t UvEditor::meshIndex() {
    std::lock_guard<std::mutex> guard(mutex_);
    return meshIndex_;
}

uint64_t UvEditor::revision() {
    std::lock_guard<std::mutex> guard(mutex_);
    return revision_;
}

bool UvEditor::select(uint32_t island, bool additive) {
    return edit([island, additive](UvSession &session) {
        return session.selectIsland(island, additive);
    });
}

void UvEditor::selectAll() {
    edit([](UvSession &session) {
        session.selectAll();
        return true;
    });
}

void UvEditor::clearSelection() {
    edit([](UvSession &session) {
        session.clearSelection();
        return true;
    });
}

int UvEditor::pick(float u, float v) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!active_) {
        return -1;
    }
    return session_.pick(u, v);
}

bool UvEditor::translate(float du, float dv) {
    return edit([du, dv](UvSession &session) { return session.translate(du, dv); });
}

bool UvEditor::rotate(float angleRadians, float pivotU, float pivotV) {
    return edit([angleRadians, pivotU, pivotV](UvSession &session) {
        return session.rotate(angleRadians, pivotU, pivotV);
    });
}

bool UvEditor::scale(float factor, float pivotU, float pivotV) {
    return edit([factor, pivotU, pivotV](UvSession &session) {
        return session.scale(factor, pivotU, pivotV);
    });
}

bool UvEditor::fit() {
    return edit([](UvSession &session) { return session.fitSelectionToAtlas(); });
}

bool UvEditor::repack(uint32_t paddingTexels) {
    return edit([paddingTexels](UvSession &session) { return session.repack(paddingTexels); });
}

bool UvEditor::undo() {
    return edit([](UvSession &session) { return session.undo(); });
}

bool UvEditor::redo() {
    return edit([](UvSession &session) { return session.redo(); });
}

void UvEditor::beginGesture() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (active_) {
        session_.beginGesture();
    }
}

void UvEditor::endGesture() {
    std::lock_guard<std::mutex> guard(mutex_);
    session_.endGesture();
}

std::string UvEditor::history() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!active_) {
        return "nenhuma edicao de UV aberta";
    }
    return session_.historyDescribe();
}

bool UvEditor::isSelected(uint32_t island) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!active_) {
        return false;
    }
    const std::vector<uint32_t> &selection = session_.selection();
    return std::find(selection.begin(), selection.end(), island) != selection.end();
}

size_t UvEditor::selectionCount() {
    std::lock_guard<std::mutex> guard(mutex_);
    return active_ ? session_.selection().size() : 0u;
}

bool UvEditor::selectionPivot(float out[2]) {
    std::lock_guard<std::mutex> guard(mutex_);
    float box[4];
    if (!active_ || !session_.selectionBounds(box)) {
        return false;
    }
    out[0] = (box[0] + box[2]) * 0.5f;
    out[1] = (box[1] + box[3]) * 0.5f;
    return true;
}

std::string UvEditor::report() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!active_) {
        return "nenhuma edicao de UV aberta";
    }
    return session_.validate().describe();
}

std::string UvEditor::describe() {
    std::lock_guard<std::mutex> guard(mutex_);
    return session_.describe();
}

bool UvEditor::layout(UvLayout &out) {
    std::lock_guard<std::mutex> guard(mutex_);
    out.active = active_;
    out.revision = revision_;
    out.uvs.clear();
    out.indices.clear();
    out.faceIsland.clear();
    out.selected.clear();
    out.islandBounds.clear();
    out.overlapping.clear();
    if (!active_) {
        return false;
    }
    out.uvs = session_.mesh().uvs;
    out.indices = session_.mesh().indices;
    out.faceIsland = session_.islands().faceIsland;
    out.selected = session_.selection();
    out.islandBounds.reserve(session_.islands().count() * 4);
    for (const UvIsland &island : session_.islands().islands) {
        out.islandBounds.push_back(island.boundsMin[0]);
        out.islandBounds.push_back(island.boundsMin[1]);
        out.islandBounds.push_back(island.boundsMax[0]);
        out.islandBounds.push_back(island.boundsMax[1]);
    }
    out.overlapping = session_.validate().overlapping;
    return true;
}

}  // namespace g3d
