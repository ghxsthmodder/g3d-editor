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
// How much RAM the app is willing to spend on one scene, in bytes. A glTF file is a list of
// references, so a few hundred kilobytes of nodes can describe gigabytes of geometry: every
// reference is expanded into its own copy here. The import stops adding meshes when the next one
// would pass this budget and says how many were left out, instead of letting the device kill the
// process. Setting it to 0 keeps the current value; the editor starts from the memory the device
// reports as free.
void setImportBudgetBytes(size_t bytes);
size_t importBudgetBytes();

// What holding one expanded mesh costs, used both by the pre-flight of a file and by the test
// that checks the budget really stops an import.
size_t estimateMeshBytes(uint32_t vertices, uint32_t triangles);

bool importModelFromMemory(const void *data, size_t length, const std::string &fileNameHint,
                           ImportResult &result, std::string &error);

}  // namespace g3d
