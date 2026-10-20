#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace g3d {

// Common internal representation of a mesh, independent of the file format it came from.
// Attributes are parallel arrays: positions[i*3..], normals[i*3..], uvs[i*2..].
// Indices are a triangle list.
struct Mesh {
    std::string name;
    std::vector<float> positions;
    std::vector<float> normals;  // optional: empty when the source has none
    std::vector<float> uvs;      // optional: empty until UVs are generated or imported
    std::vector<uint32_t> indices;

    uint32_t vertexCount() const;
    uint32_t triangleCount() const;
    bool hasNormals() const;
    bool hasUvs() const;
    void clear();

    // Sets error when the mesh cannot be handed to xatlas or to the renderer.
    bool validate(std::string &error) const;
};

}  // namespace g3d
