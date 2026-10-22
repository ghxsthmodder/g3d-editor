#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace g3d {

// What went into a project, or what came back out of one, plus the things that could not be carried
// and are said rather than dropped silently.
struct ProjectReport {
    size_t bytes = 0;          // the whole file
    size_t modelBytes = 0;     // the model embedded in it
    size_t imageBytes = 0;     // every layer picture, PNG encoded
    size_t meshes = 0;         // meshes with UVs written, or restored
    size_t layers = 0;         // layers written, or restored
    std::vector<std::string> warnings;

    std::string describe() const;
};

// Saves the editor as it stands: the model file the document came from, the UVs each mesh holds
// now, the layer stack with the pictures it uses and the canvas they are painted on. Everything is
// inside the one file, because the path a picker handed over does not survive the week.
bool saveProject(std::vector<uint8_t> &out, ProjectReport &report, std::string &error);

// Reads a project back into the document, the compositor and the scene, replacing what was open.
bool openProject(const void *data, size_t length, const std::string &name, ProjectReport &report,
                 std::string &error);

// The report of the last save or open, in the shape the status line already prints for imports. A
// save hands back bytes and has no other way to say what the file left out.
std::string projectReport();

}  // namespace g3d
