#pragma once

#include <string>

#include "core/mesh.hpp"

namespace g3d {

// Axis-aligned cube centred on the origin, with per-face normals and no UVs.
// Used by the native smoke test and as the fallback geometry of an empty scene.
Mesh makeCube(float halfExtent = 0.5f);

// Axis-aligned bounding box of the mesh positions. False when the mesh has no vertices.
bool computeBounds(const Mesh &mesh, float outMin[3], float outMax[3]);

std::string describeMesh(const Mesh &mesh);

}  // namespace g3d
