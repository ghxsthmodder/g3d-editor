#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace g3d {

// The container a project file is stored in: sixteen header bytes, then the JSON that describes the
// project, then the binary chunk every byte reference in that JSON points into. It is the shape of
// glTF's binary form, which the editor already reads, under the project's own magic: a project is
// not a model file and must not be mistaken for one.
//
// Both chunks are padded to a multiple of four so the binary chunk starts aligned: the floats the
// project carries (UVs, and nothing else) are read straight out of the buffer.
inline constexpr uint32_t kProjectVersion = 1;
inline constexpr size_t kProjectHeaderBytes = 16;

struct ProjectChunks {
    uint32_t version = 0;
    std::string json;
    std::vector<uint8_t> bin;
};

// The four magic bytes, as text for the log and for the file name the interface offers.
const char *projectMagic();

bool writeProjectContainer(const std::string &json, const std::vector<uint8_t> &bin,
                           std::vector<uint8_t> &out, std::string &error);

// Reads and validates a buffer as a container. Errors say what is wrong in the file rather than
// failing quietly, because the usual cause is a project saved by a different build.
bool readProjectContainer(const void *data, size_t length, ProjectChunks &out, std::string &error);

}  // namespace g3d
