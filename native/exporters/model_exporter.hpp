#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/scene.hpp"

namespace g3d {

// The model formats the editor offers. GLB is glTF's own binary packaging, not a third exporter.
enum class ModelFormat { Obj, Gltf, Glb };

// One file out of an exporter. OBJ and glTF come out as two pieces, the description and the
// companion it references (MTL and BIN); GLB is a single file. The first entry is the one the
// user names, the rest keep the names written inside it.
struct ExportedFile {
    std::string name;
    std::vector<uint8_t> bytes;
};

struct ModelExport {
    std::vector<ExportedFile> files;
    std::vector<std::string> warnings;  // what the destination format could not hold

    size_t totalBytes() const;
    std::string describe() const;
};

// The identifier Assimp's exporter registry uses for the format.
const char *modelFormatId(ModelFormat format);
// Name shown to the user, and the extension the main file is written with.
std::string modelFormatLabel(ModelFormat format);
std::string modelFormatExtension(ModelFormat format);

// Writes the scene through Assimp in memory. The base name has to be the file name the user chose
// without its extension: the exporters put it inside the description as the reference to the
// companion, so a piece renamed afterwards would point at a file that does not exist.
bool exportModel(const Scene &scene, ModelFormat format, const std::string &baseName,
                 ModelExport &output, std::string &error);

}  // namespace g3d
