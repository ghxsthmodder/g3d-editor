#include "core/uv_islands.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>

namespace g3d {
namespace {

// Union-find over faces, keyed by the smallest face index of the group.
struct DisjointSet {
    explicit DisjointSet(uint32_t size) : parent(size) {
        for (uint32_t i = 0; i < size; ++i) {
            parent[i] = i;
        }
    }

    uint32_t root(uint32_t value) {
        while (parent[value] != value) {
            parent[value] = parent[parent[value]];
            value = parent[value];
        }
        return value;
    }

    void join(uint32_t left, uint32_t right) {
        const uint32_t leftRoot = root(left);
        const uint32_t rightRoot = root(right);
        if (leftRoot == rightRoot) {
            return;
        }
        // Keeping the smaller index as the root makes island numbering stable across runs.
        if (leftRoot < rightRoot) {
            parent[rightRoot] = leftRoot;
        } else {
            parent[leftRoot] = rightRoot;
        }
    }

    std::vector<uint32_t> parent;
};

uint64_t edgeKey(uint32_t first, uint32_t second) {
    return first < second ? (static_cast<uint64_t>(first) << 32) | second
                          : (static_cast<uint64_t>(second) << 32) | first;
}

float triangleArea(const Mesh &mesh, uint32_t a, uint32_t b, uint32_t c) {
    const float ax = mesh.uvs[a * 2 + 0], ay = mesh.uvs[a * 2 + 1];
    const float bx = mesh.uvs[b * 2 + 0], by = mesh.uvs[b * 2 + 1];
    const float cx = mesh.uvs[c * 2 + 0], cy = mesh.uvs[c * 2 + 1];
    return 0.5f * std::fabs((bx - ax) * (cy - ay) - (by - ay) * (cx - ax));
}

}  // namespace

void UvIslands::clear() {
    faceIsland.clear();
    faceOrder.clear();
    islands.clear();
}

const UvIsland *UvIslands::island(uint32_t index) const {
    return index < islands.size() ? &islands[index] : nullptr;
}

std::string UvIslands::describe() const {
    float total = 0.0f;
    for (const UvIsland &island : islands) {
        total += island.area;
    }
    return "ilhas=" + std::to_string(count()) + " area_uv=" + std::to_string(total);
}

bool computeIslands(const Mesh &mesh, UvIslands &out, std::string &error) {
    out.clear();
    const uint32_t faceCount = mesh.triangleCount();
    if (faceCount == 0) {
        error = "a malha nao tem triangulos";
        return false;
    }
    if (!mesh.hasUvs()) {
        error = "a malha nao tem UVs";
        return false;
    }

    DisjointSet sets(faceCount);
    std::unordered_map<uint64_t, uint32_t> edgeOwner;
    edgeOwner.reserve(static_cast<size_t>(faceCount) * 3);
    for (uint32_t face = 0; face < faceCount; ++face) {
        for (uint32_t corner = 0; corner < 3; ++corner) {
            const uint32_t a = mesh.indices[face * 3 + corner];
            const uint32_t b = mesh.indices[face * 3 + (corner + 1) % 3];
            if (a == b) {
                continue;  // Degenerate edge: it cannot connect two faces.
            }
            const auto inserted = edgeOwner.insert(std::make_pair(edgeKey(a, b), face));
            if (!inserted.second) {
                sets.join(face, inserted.first->second);
            }
        }
    }

    // Faces are visited in index order, so island 0 is the one that owns the lowest face index.
    std::unordered_map<uint32_t, uint32_t> rootToIsland;
    rootToIsland.reserve(faceCount);
    out.faceIsland.assign(faceCount, kNoIsland);
    out.faceOrder.resize(faceCount);
    for (uint32_t face = 0; face < faceCount; ++face) {
        const uint32_t root = sets.root(face);
        auto entry = rootToIsland.find(root);
        if (entry == rootToIsland.end()) {
            const uint32_t index = static_cast<uint32_t>(out.islands.size());
            entry = rootToIsland.insert(std::make_pair(root, index)).first;
            out.islands.push_back(UvIsland());
        }
        out.faceIsland[face] = entry->second;
        ++out.islands[entry->second].faceCount;
    }

    // Numbering by lowest face index makes the ranges a prefix sum of the face counts.
    uint32_t cursor = 0;
    for (UvIsland &island : out.islands) {
        island.firstFace = cursor;
        cursor += island.faceCount;
    }
    std::vector<uint32_t> filled(out.islands.size(), 0);
    for (uint32_t face = 0; face < faceCount; ++face) {
        const uint32_t index = out.faceIsland[face];
        out.faceOrder[out.islands[index].firstFace + filled[index]++] = face;
    }

    for (uint32_t index = 0; index < out.islands.size(); ++index) {
        UvIsland &island = out.islands[index];
        island.boundsMin[0] = island.boundsMin[1] = std::numeric_limits<float>::max();
        island.boundsMax[0] = island.boundsMax[1] = std::numeric_limits<float>::lowest();
        island.area = 0.0f;
        for (uint32_t slot = island.firstFace; slot < island.firstFace + island.faceCount; ++slot) {
            const uint32_t face = out.faceOrder[slot];
            const uint32_t a = mesh.indices[face * 3 + 0];
            const uint32_t b = mesh.indices[face * 3 + 1];
            const uint32_t c = mesh.indices[face * 3 + 2];
            const uint32_t corners[3] = {a, b, c};
            for (const uint32_t vertex : corners) {
                for (uint32_t axis = 0; axis < 2; ++axis) {
                    const float value = mesh.uvs[vertex * 2 + axis];
                    island.boundsMin[axis] = std::min(island.boundsMin[axis], value);
                    island.boundsMax[axis] = std::max(island.boundsMax[axis], value);
                }
            }
            island.area += triangleArea(mesh, a, b, c);
        }
    }
    return true;
}

}  // namespace g3d
