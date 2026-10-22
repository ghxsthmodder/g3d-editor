#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/mesh.hpp"

namespace g3d {

// The meshes currently being edited, plus which one the tools act on.
// Positions are already in the scene space when the meshes come out of an importer.
class Scene {
public:
    void clear();
    void add(Mesh mesh);

    uint32_t meshCount() const;
    bool isEmpty() const;
    const Mesh *meshAt(uint32_t index) const;
    Mesh *meshAt(uint32_t index);  // nullptr when the index is out of range

    uint32_t totalVertices() const;
    uint32_t totalTriangles() const;
    // Bytes the meshes hold between them, for the panel's memory line.
    size_t memoryBytes() const;

    // -1 means nothing selected. Indices outside the scene select nothing.
    int selected() const;
    void select(int index);

    // Combined bounds of every mesh, used to frame the camera. False when the scene is empty.
    bool bounds(float outMin[3], float outMax[3]) const;

    // One line per mesh, in draw order, for the hierarchy panel.
    std::string hierarchy() const;
    std::string describe() const;

private:
    std::vector<Mesh> meshes_;
    int selected_ = -1;
};

}  // namespace g3d
