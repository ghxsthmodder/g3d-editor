#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "uv/uv_edit.hpp"
#include "uv/uv_service.hpp"

namespace g3d {

// A snapshot of the layout, so the GL thread can draw the atlas without ever reading the session
// while the UI thread is transforming it.
struct UvLayout {
    bool active = false;
    uint64_t revision = 0;
    std::vector<float> uvs;            // Two floats per vertex, normalised to the atlas.
    std::vector<uint32_t> indices;     // Triangle list of the mesh being edited.
    std::vector<uint32_t> faceIsland;  // Island per triangle.
    std::vector<uint32_t> selected;    // Islands the user has picked.
    std::vector<float> islandBounds;   // Four floats per island: minU, minV, maxU, maxV.
    std::vector<uint32_t> overlapping; // Islands that share UV space with another.
};

// Process-wide UV editing state. The interface calls the coarse operations below from its own
// thread; the renderer only reads layout(). Nothing nests the document lock inside this one, so
// the two cannot deadlock against each other.
class UvEditor {
public:
    static UvEditor &instance();

    // Opens a session on a scene mesh, either with its current UVs or with a freshly generated
    // layout. Both are previews: the scene keeps what it had until apply() is called.
    bool begin(uint32_t meshIndex, std::string &error);
    bool generate(uint32_t meshIndex, const UvOptions &options, std::string &error);
    bool apply(std::string &error);
    void cancel();
    bool isActive();

    bool select(uint32_t island, bool additive);
    void selectAll();
    void clearSelection();
    int pick(float u, float v);
    bool translate(float du, float dv);
    bool rotate(float angleRadians, float pivotU, float pivotV);
    bool scale(float factor, float pivotU, float pivotV);
    bool fit();
    bool repack(uint32_t paddingTexels);

    // Validation and state as text for the status log.
    std::string report();
    std::string describe();
    uint32_t meshIndex();
    // Changes on every edit, so the renderer can rebuild its atlas geometry only when needed.
    uint64_t revision();
    bool layout(UvLayout &out);

private:
    UvEditor() = default;

    // Runs one whole operation on the session and bumps the revision when it changed anything.
    template <typename Fn>
    bool edit(Fn fn) {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!active_) {
            return false;
        }
        if (!fn(session_)) {
            return false;
        }
        ++revision_;
        return true;
    }

    UvSession session_;
    std::mutex mutex_;
    uint64_t revision_ = 0;
    uint32_t meshIndex_ = 0;
    bool active_ = false;
};

}  // namespace g3d
