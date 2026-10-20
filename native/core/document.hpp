#pragma once

#include <cstdint>
#include <mutex>
#include <string>

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

    // Calls fn(Scene&) with the document locked. Used by the renderer to copy geometry.
    template <typename Fn>
    void withScene(Fn fn) {
        std::lock_guard<std::mutex> guard(mutex_);
        fn(scene_);
    }

    // Report of the last import: format, materials, textures, warnings.
    std::string lastImportReport();

private:
    Document();

    Scene scene_;
    std::mutex mutex_;
    std::string lastReport_;
};

}  // namespace g3d
