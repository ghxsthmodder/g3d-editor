#pragma once

#include <string>

namespace g3d {

// Base colour and name of a mesh. Full PBR material graphs are deliberately out of scope until
// the UV and texture composition flow is stable (see PLAN.md).
struct Material {
    std::string name;
    float baseColor[4] = {0.78f, 0.79f, 0.82f, 1.0f};
};

std::string describeMaterial(const Material &material);

}  // namespace g3d
