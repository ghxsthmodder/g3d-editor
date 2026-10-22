#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "core/scene.hpp"

namespace g3d {

// The document currently open in the editor. One instance for the process: the JNI bridge
// imports from a worker thread while the renderer reads from the GL thread, so every access
// to the scene goes through the lock.
class Document {
public:
    static Document &instance();

    // Replaces the scene with the imported model. On failure the previous scene is kept.
    // The last successful format and warnings are remembered for the status line.
    bool importModel(const void *data, size_t length, const std::string &hint, std::string &error);

    std::string describe();
    std::string hierarchy();
    bool bounds(float outMin[3], float outMax[3]);
    uint32_t meshCount();
    int selected();
    void select(int index);

    // Replaces one mesh, keeping the rest of the scene. Used when an edited UV layout is applied.
    bool replaceMesh(uint32_t index, const Mesh &mesh);

    // Calls fn(Scene&) with the document locked. Used by the renderer to copy geometry.
    template <typename Fn>
    void withScene(Fn fn) {
        std::lock_guard<std::mutex> guard(mutex_);
        fn(scene_);
    }

    // Report of the last import: format, materials, textures, warnings.
    std::string lastImportReport();

    // The file the model came in with, kept whole. A project embeds these bytes, not the path
    // they were read from: the picker's address is good until the user moves or deletes the file,
    // and a project that cannot find its model is not editable any more.
    bool hasSource() const;
    std::string sourceName() const;

    // Calls fn(const uint8_t *, size_t) with the document locked, for writing those bytes straight
    // into a project: a model of ninety megabytes must not be copied again on the way to the file.
    template <typename Fn>
    void withSource(Fn fn) const {
        std::lock_guard<std::mutex> guard(mutex_);
        fn(source_.data(), source_.size());
    }

    // What the document holds, in bytes, counted apart because the panel names the parts: the file
    // the model came from and the geometry built out of it. A project embeds the model, so opening
    // one carries those bytes twice on purpose and the total says so.
    size_t sourceBytes() const;
    size_t meshBytes() const;

private:
    Document();

    Scene scene_;
    mutable std::mutex mutex_;
    std::string lastReport_;
    std::vector<uint8_t> source_;
    std::string sourceName_;
};

}  // namespace g3d
