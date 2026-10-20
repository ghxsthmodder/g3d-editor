#include "core/scene.hpp"

#include <algorithm>
#include <limits>

#include "core/primitives.hpp"

namespace g3d {

void Scene::clear() {
    meshes_.clear();
    selected_ = -1;
}

void Scene::add(Mesh mesh) {
    meshes_.push_back(std::move(mesh));
}

uint32_t Scene::meshCount() const { return static_cast<uint32_t>(meshes_.size()); }

bool Scene::isEmpty() const { return meshes_.empty(); }

const Mesh *Scene::meshAt(uint32_t index) const {
    return index < meshes_.size() ? &meshes_[index] : nullptr;
}

Mesh *Scene::meshAt(uint32_t index) {
    return index < meshes_.size() ? &meshes_[index] : nullptr;
}

uint32_t Scene::totalVertices() const {
    uint32_t total = 0;
    for (const Mesh &mesh : meshes_) {
        total += mesh.vertexCount();
    }
    return total;
}

uint32_t Scene::totalTriangles() const {
    uint32_t total = 0;
    for (const Mesh &mesh : meshes_) {
        total += mesh.triangleCount();
    }
    return total;
}

int Scene::selected() const { return selected_; }

void Scene::select(int index) {
    selected_ = (index >= 0 && static_cast<uint32_t>(index) < meshes_.size()) ? index : -1;
}

bool Scene::bounds(float outMin[3], float outMax[3]) const {
    if (meshes_.empty()) {
        return false;
    }
    float minimum[3] = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                        std::numeric_limits<float>::max()};
    float maximum[3] = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                        std::numeric_limits<float>::lowest()};
    bool any = false;
    for (const Mesh &mesh : meshes_) {
        float meshMin[3];
        float meshMax[3];
        if (!computeBounds(mesh, meshMin, meshMax)) {
            continue;
        }
        for (uint32_t axis = 0; axis < 3; ++axis) {
            minimum[axis] = std::min(minimum[axis], meshMin[axis]);
            maximum[axis] = std::max(maximum[axis], meshMax[axis]);
        }
        any = true;
    }
    if (!any) {
        return false;
    }
    std::copy(std::begin(minimum), std::end(minimum), outMin);
    std::copy(std::begin(maximum), std::end(maximum), outMax);
    return true;
}

std::string Scene::hierarchy() const {
    std::string text;
    for (uint32_t i = 0; i < meshes_.size(); ++i) {
        const Mesh &mesh = meshes_[i];
        text += std::to_string(i) + " | " + (mesh.name.empty() ? "<sem nome>" : mesh.name) +
                " | " + std::to_string(mesh.vertexCount()) + " vertices, " +
                std::to_string(mesh.triangleCount()) + " triangulos" +
                (mesh.hasUvs() ? ", com UV" : ", sem UV") + "\n";
    }
    return text;
}

std::string Scene::describe() const {
    return "malhas=" + std::to_string(meshCount()) + " vertices=" +
           std::to_string(totalVertices()) + " triangulos=" + std::to_string(totalTriangles()) +
           " selecionada=" + (selected_ < 0 ? std::string("nenhuma") : std::to_string(selected_));
}

}  // namespace g3d
