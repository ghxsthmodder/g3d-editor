#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "core/scene.hpp"

namespace g3d {

struct ImportResult {
    Scene scene;
    std::string format;                     // human-readable label of the source format
    std::vector<std::string> warnings;      // data the importer could not preserve
    uint32_t materialCount = 0;
    uint32_t textureCount = 0;              // images embedded in the file
    uint32_t animationCount = 0;

    std::string describe() const;
};

// Imports a model that was already read into memory (the Android UI hands over the bytes it got
// from the system file picker). fileNameHint must carry the extension so Assimp picks the right
// importer; .gltf files that reference sidecar buffers cannot be resolved from memory.
bool importModelFromMemory(const void *data, size_t length, const std::string &fileNameHint,
                           ImportResult &result, std::string &error);

}  // namespace g3d
