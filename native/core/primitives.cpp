#include "core/primitives.hpp"

#include <glm/glm.hpp>

namespace g3d {

namespace {

struct FaceAxes {
    glm::vec3 normal;
    glm::vec3 u;
    glm::vec3 v;
};

}  // namespace

Mesh makeCube(float halfExtent) {
    const FaceAxes faces[6] = {
        {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}},
        {{-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
        {{0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
        {{0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
        {{0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
        {{0.0f, 0.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    };

    Mesh mesh;
    mesh.name = "cube";
    mesh.positions.reserve(24 * 3);
    mesh.normals.reserve(24 * 3);
    mesh.indices.reserve(36);

    for (uint32_t f = 0; f < 6; ++f) {
        const FaceAxes &face = faces[f];
        const glm::vec3 center = face.normal * halfExtent;
        const uint32_t base = f * 4;
        for (int corner = 0; corner < 4; ++corner) {
            const float su = (corner & 1) ? 1.0f : -1.0f;
            const float sv = (corner & 2) ? 1.0f : -1.0f;
            const glm::vec3 position =
                center + face.u * (su * halfExtent) + face.v * (sv * halfExtent);
            mesh.positions.push_back(position.x);
            mesh.positions.push_back(position.y);
            mesh.positions.push_back(position.z);
            mesh.normals.push_back(face.normal.x);
            mesh.normals.push_back(face.normal.y);
            mesh.normals.push_back(face.normal.z);
        }
        // Triangle fan of the quad, counter-clockwise when viewed from outside.
        const uint32_t quad[6] = {base, base + 1, base + 3, base, base + 3, base + 2};
        for (uint32_t index : quad) {
            mesh.indices.push_back(index);
        }
    }
    return mesh;
}

bool computeBounds(const Mesh &mesh, float outMin[3], float outMax[3]) {
    if (mesh.positions.empty()) {
        return false;
    }
    glm::vec3 minimum(mesh.positions[0], mesh.positions[1], mesh.positions[2]);
    glm::vec3 maximum = minimum;
    for (size_t i = 3; i < mesh.positions.size(); i += 3) {
        const glm::vec3 point(mesh.positions[i], mesh.positions[i + 1], mesh.positions[i + 2]);
        minimum = glm::min(minimum, point);
        maximum = glm::max(maximum, point);
    }
    outMin[0] = minimum.x;
    outMin[1] = minimum.y;
    outMin[2] = minimum.z;
    outMax[0] = maximum.x;
    outMax[1] = maximum.y;
    outMax[2] = maximum.z;
    return true;
}

std::string describeMesh(const Mesh &mesh) {
    float minimum[3];
    float maximum[3];
    std::string bounds = "bounds=(none)";
    if (computeBounds(mesh, minimum, maximum)) {
        bounds = "bounds=[" + std::to_string(minimum[0]) + "," + std::to_string(minimum[1]) + "," +
                 std::to_string(minimum[2]) + "]..[" + std::to_string(maximum[0]) + "," +
                 std::to_string(maximum[1]) + "," + std::to_string(maximum[2]) + "]";
    }
    return "name=" + (mesh.name.empty() ? "<unnamed>" : mesh.name) +
           " vertices=" + std::to_string(mesh.vertexCount()) +
           " triangles=" + std::to_string(mesh.triangleCount()) +
           " uvs=" + std::to_string(mesh.uvs.size() / 2) + " normals=" +
           std::to_string(mesh.normals.size() / 3) + " " + bounds;
}

}  // namespace g3d
