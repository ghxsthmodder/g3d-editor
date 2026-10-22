#include "core/mesh.hpp"

namespace g3d {

uint32_t Mesh::vertexCount() const { return static_cast<uint32_t>(positions.size() / 3); }

uint32_t Mesh::triangleCount() const { return static_cast<uint32_t>(indices.size() / 3); }

bool Mesh::hasNormals() const { return normals.size() >= positions.size(); }

bool Mesh::hasUvs() const { return uvs.size() >= vertexCount() * 2; }

void Mesh::clear() {
    name.clear();
    positions.clear();
    normals.clear();
    uvs.clear();
    indices.clear();
}

size_t Mesh::memoryBytes() const {
    // The capacity is what the allocator holds, which is the number that matters on a phone: a mesh
    // that once had more vertices keeps the buffer it grew to.
    return positions.capacity() * sizeof(float) + normals.capacity() * sizeof(float) +
           uvs.capacity() * sizeof(float) + indices.capacity() * sizeof(uint32_t);
}

bool Mesh::validate(std::string &error) const {
    if (positions.empty() || positions.size() % 3 != 0) {
        error = "positions must be a non-empty multiple of 3";
        return false;
    }
    if (indices.empty() || indices.size() % 3 != 0) {
        error = "indices must be a non-empty multiple of 3";
        return false;
    }
    if (!normals.empty() && normals.size() < positions.size()) {
        error = "normals array is shorter than positions";
        return false;
    }
    if (!uvs.empty() && uvs.size() < vertexCount() * 2) {
        error = "uv array is shorter than vertexCount * 2";
        return false;
    }
    const uint32_t count = vertexCount();
    for (size_t i = 0; i < indices.size(); ++i) {
        if (indices[i] >= count) {
            error = "index " + std::to_string(indices[i]) + " out of range at position " +
                    std::to_string(i);
            return false;
        }
    }
    return true;
}

}  // namespace g3d
