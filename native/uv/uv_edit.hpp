#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/mesh.hpp"
#include "core/uv_islands.hpp"

namespace g3d {

// What the editor knows about the current layout, so the user can be warned without being blocked:
// overlaps are legal on symmetric models, and UVs outside the atlas are a mistake to fix, not a
// reason to refuse the edit.
struct UvEditReport {
    std::vector<uint32_t> outsideAtlas;  // Islands with any UV outside 0..1.
    std::vector<uint32_t> overlapping;   // Islands sharing UV space with another island.
    uint32_t selectionCount = 0;
    float coveredArea = 0.0f;  // Atlas area actually claimed by islands.

    bool clean() const { return outsideAtlas.empty() && overlapping.empty(); }
    std::string describe() const;
};

// Working copy of one mesh's UVs. Nothing reaches the scene until applyTo() is called, so the UVs
// the file brought along survive every preview and every cancelled edit.
class UvSession {
public:
    // Copies the mesh and groups its UVs into islands. False when there is nothing to edit.
    bool load(const Mesh &source, std::string &error);
    void clear();

    bool isLoaded() const { return !working_.indices.empty(); }
    const Mesh &mesh() const { return working_; }
    const UvIslands &islands() const { return islands_; }
    uint32_t islandCount() const { return islands_.count(); }
    const std::vector<uint32_t> &selection() const { return selected_; }
    bool isSelected(uint32_t island) const;
    bool hasChanges() const;

    // Selection. Additive selection keeps what is already chosen, which is how the multi-island
    // tools are reached without a keyboard.
    bool selectIsland(uint32_t island, bool additive);
    void selectAll();
    void clearSelection();
    // Island whose UV triangle covers (u, v), preferring one that is already selected so an
    // island on top of another can still be re-picked. -1 when the point hits nothing.
    int pick(float u, float v) const;

    // Rigid edits applied to the selection. Angles are radians, pivots are in UV space, and a
    // missing selection is reported instead of silently doing nothing.
    bool translate(float du, float dv);
    bool rotate(float angleRadians, float pivotU, float pivotV);
    bool scale(float factor, float pivotU, float pivotV);

    // Centre and size of the selection's UV box, used to place gizmos and pick a pivot.
    bool selectionBounds(float out[4]) const;
    // Move and shrink the selection so it fits the atlas again, keeping its proportions.
    bool fitSelectionToAtlas();
    // Re-pack every island into the atlas with the given texel padding. Island shapes are kept
    // exactly; only their placement changes.
    bool repack(uint32_t paddingTexels);

    // Undo and redo over the edits of this session. A step is the state the session held before an
    // operation, kept whole, so a drag, a repack and a fresh xatlas run are undone the same way and
    // the UVs a file brought back are reachable again after a generate.
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    bool undo();
    bool redo();
    // A gesture has to be one step: while it is open the first mutation records and the ones that
    // follow it are absorbed into that same record.
    void beginGesture();
    void endGesture();
    // Drops every record. Called when the session's edits stop being undoable because they left the
    // preview and became the document, or because a new mesh was opened.
    void clearHistory();
    // Closes the current state into the undo stack. For the operations a session cannot see coming
    // from inside itself, such as a generate that replaces the layout wholesale.
    void pushUndoStep();
    size_t undoSteps() const { return undo_.size(); }
    size_t redoSteps() const { return redo_.size(); }
    std::string historyDescribe() const;
    // The layout, the grouping and every recorded step: what an open session keeps on the phone,
    // which is why the history window is trimmed.
    size_t memoryBytes() const;

    // The atlas the layout targets, in texels. Padding is expressed in texels, so the session
    // needs the size to turn it into UV units.
    void setAtlasSize(uint32_t width, uint32_t height);
    uint32_t atlasWidth() const { return atlasWidth_; }
    uint32_t atlasHeight() const { return atlasHeight_; }

    UvEditReport validate() const;
    std::string describe() const;

private:
    // Everything a step has to bring back: the layout as a whole, which islands were picked and
    // the atlas the padding was measured against.
    struct Step {
        Mesh mesh;
        std::vector<uint32_t> selected;
        uint32_t atlasWidth = 1024;
        uint32_t atlasHeight = 1024;
    };

    bool requireSelection(const char *operation, std::string &error) const;
    void transformSelection(float angleRadians, float scale, float pivotU, float pivotV,
                            float du, float dv);
    // The same moves without a record in front of them. An operation made of several moves, such as
    // a repack, is one undo step, so it moves islands through these.
    bool translatePlain(float du, float dv);
    bool scalePlain(float factor, float pivotU, float pivotV);

    Step capture() const;
    void record();
    void restore(const Step &step);
    size_t stepBytes(const Step &step) const;
    void trim(std::vector<Step> &stack) const;

    Mesh working_;
    std::vector<float> loadedUvs_;
    UvIslands islands_;
    std::vector<uint32_t> selected_;
    uint32_t atlasWidth_ = 1024;
    uint32_t atlasHeight_ = 1024;
    std::vector<Step> undo_;
    std::vector<Step> redo_;
    bool inGesture_ = false;
    bool gestureRecorded_ = false;
};

}  // namespace g3d
