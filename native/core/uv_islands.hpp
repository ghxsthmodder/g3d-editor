#pragma once

#include <cstdint>
#include <vector>

#include "core/mesh.hpp"

namespace g3d {

inline constexpr uint32_t kNoIsland = 0xffffffffu;

// One connected group of faces in UV space: what the editor selects, moves and packs.
struct UvIsland {
    uint32_t firstFace = 0;   // Range into UvIslands::faceOrder.
    uint32_t faceCount = 0;
    float boundsMin[2] = {0.0f, 0.0f};
    float boundsMax[2] = {0.0f, 0.0f};
    float area = 0.0f;  // UV area, so a sliver island is not mistaken for a large one.
};

struct UvIslands {
    std::vector<uint32_t> faceIsland;  // Per face: island index or kNoIsland.
    std::vector<uint32_t> faceOrder;   // Faces grouped by island, so an island is one range.
    std::vector<UvIsland> islands;

    uint32_t count() const { return static_cast<uint32_t>(islands.size()); }
    void clear();

    // Faces of one island, as the half-open range [firstFace, firstFace + faceCount) of faceOrder.
    const UvIsland *island(uint32_t index) const;
    std::string describe() const;
};

// Groups the faces of a mesh that touch in UV space. Two faces share an island when they use the
// same edge, meaning the same two vertex indices: because a vertex carries exactly one UV pair, a
// shared edge can never straddle a seam, so no UV comparison is needed.
//
// This works on any mesh with UVs, whether they came from xatlas or from the imported file, which
// is why the editor does not have to track which generator produced the current layout.
// Returns false when the mesh has no UVs or no triangles.
bool computeIslands(const Mesh &mesh, UvIslands &out, std::string &error);

}  // namespace g3d
