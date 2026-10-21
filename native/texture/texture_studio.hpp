#pragma once

#include <cstdint>
#include <mutex>
#include <string>

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
    int canvasWidth() const;
    int canvasHeight() const;
    void setBackground(const uint8_t rgb[3]);
    void background(uint8_t out[3]) const;

    int addLayer(const Image &image, std::string &error);
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

    // Changes whenever the stack does, so the renderer knows when to upload again.
    uint64_t revision();

private:
    TextureStudio() = default;

    void touch();  // Marks the stack dirty and moves the revision on. Call with the lock held.
    bool renderLocked(Image &out, std::string &error);

    mutable std::mutex mutex_;
    TextureComposition composition_;
    Image composed_;
    bool dirty_ = true;
    uint64_t revision_ = 0;
};

}  // namespace g3d
