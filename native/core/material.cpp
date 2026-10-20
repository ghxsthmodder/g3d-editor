#include "core/material.hpp"

namespace g3d {

std::string describeMaterial(const Material &material) {
    return "material=" + (material.name.empty() ? "<default>" : material.name) + " rgba=(" +
           std::to_string(material.baseColor[0]).substr(0, 4) + "," +
           std::to_string(material.baseColor[1]).substr(0, 4) + "," +
           std::to_string(material.baseColor[2]).substr(0, 4) + "," +
           std::to_string(material.baseColor[3]).substr(0, 4) + ")";
}

}  // namespace g3d
