#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "texture/texture_layer.hpp"

namespace g3d {

// The texture stack of the document that is open, one instance for the process. The interface
// thread edits the layers while the GL thread asks for the picture to upload, so the stack and the
// composed copy live behind a lock and each caller only ever sees a snapshot.
//
// Composition is lazy on purpose: the GL thread pays for it once per revision, however many clicks
// produced that revision, and the interface thread never composes at all.
class TextureStudio {
public:
    static TextureStudio &instance();

    bool setCanvas(int width, int height, std::string &error);
    // What the GL driver reported as its biggest texture side, told to the canvas rules.
    // Called from the GL thread when the context is created, and again if a second device
    // takes the surface.
    void setDeviceLimit(int side);
    // What the canvas rules currently allow per side, 0 while no GL context has reported. The
    // interface reads it to dim the resolutions this device cannot sample.
    int deviceLimit() const;
    int canvasWidth() const;
    int canvasHeight() const;
    void setBackground(const uint8_t rgb[3]);
    void background(uint8_t out[3]) const;

    int addLayer(const Image &image, std::string &error);
    // Puts a saved layer back where the project had it. The ids are reassigned: a reopened stack is
    // numbered by this session, not by the one that wrote the file.
    int addLayerAt(const Image &image, const TextureLayer &at, std::string &error);
    // The stack as it stands, for writing a project out. The pictures come along as the shared
    // pointers they already are, which is why saving does not copy a texture per layer.
    std::vector<TextureLayer> layers() const;
    bool removeLayer(int id);
    bool duplicateLayer(int id, int &newId, std::string &error);
    bool moveUp(int id);
    bool moveDown(int id);
    bool setVisible(int id, bool visible);
    bool setOpacity(int id, float opacity);
    bool translate(int id, float dxPixels, float dyPixels);
    bool rotate(int id, float degreesDelta);
    bool scaleBy(int id, float factor);
    size_t layerCount() const;
    size_t memoryBytes() const;

    // The composed picture for the preview: rebuilt only when the stack changed since the last
    // call. False when there is no canvas, no stack or the composition failed.
    bool texture(Image &out);

    // The same picture, always rebuilt: the PNG the user exports must not be a stale preview.
    bool render(Image &out, std::string &error);

    // Forgets the stack and the picture, used when another model replaces the document.
    void reset();

    // --- desfazer e refazer --------------------------------------------------------------
    // A step is the whole compositor as it stood before one click — canvas, colour behind it and
    // the layers with their placement — so undoing an import, a removal or a nudge is the same
    // operation, and a step shares the pictures with the stack instead of copying them.
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    bool undo();
    bool redo();
    // Forgets the records and leaves the stack alone: a project that just opened has no before in
    // this session, and a model import already threw the stack away with reset().
    void clearHistory();
    // The bytes of the pictures only the history still reaches. What the stack shows is counted as
    // layers; what a dropped layer would cost to forget is counted here.
    size_t historyBytes() const;

    // One summary line and one line per layer, topmost first, in the same "id | nome | resto"
    // shape the mesh hierarchy uses so the panel can parse both the same way.
    std::string describe() const;
    std::string layerRows() const;

    // Changes whenever the stack does, so the renderer knows when to upload again.
    uint64_t revision();

private:
    TextureStudio() = default;

    void touch();  // Marks the stack dirty and moves the revision on. Call with the lock held.
    bool renderLocked(Image &out, std::string &error);

    // Pushes the state a mutation is about to overwrite. Called by every operation that really
    // changed the stack — a refused click must not become a step to undo through. Call with the
    // lock held.
    void remember(const TextureState &before);
    // Drops the oldest steps once the window is full. The oldest is always kept: undoing the last
    // click is worth more than an empty history.
    void trim(std::vector<TextureState> &stack) const;

    mutable std::mutex mutex_;
    TextureComposition composition_;
    Image composed_;
    bool dirty_ = true;
    uint64_t revision_ = 0;
    std::vector<TextureState> undo_;
    std::vector<TextureState> redo_;
};

}  // namespace g3d
