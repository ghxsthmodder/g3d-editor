#include "core/document.hpp"

#include <algorithm>
#include <utility>

#include "core/primitives.hpp"
#include "importers/assimp_importer.hpp"

namespace g3d {
namespace {

// Records a mesh index once: the list is read by the save, which does not care how often a mesh was
// rebuilt, only that it was.
void addOnce(std::vector<uint32_t> &list, uint32_t index) {
    if (std::find(list.begin(), list.end(), index) == list.end()) {
        list.push_back(index);
    }
}

// What the document holds before the user brings anything in: the view is never empty, so gestures
// can be checked before a model is imported.
Mesh starterMesh() {
    Mesh cube = makeCube(0.5f);
    cube.name = "cubo de exemplo";
    return cube;
}

}  // namespace

Document &Document::instance() {
    static Document document;
    return document;
}

Document::Document() {
    scene_.add(starterMesh());
    scene_.select(0);
    lastReport_ = "cena inicial: cubo de exemplo";
}

bool Document::importModel(const void *data, size_t length, const std::string &hint,
                           std::string &error) {
    ImportResult result;
    if (!importModelFromMemory(data, length, hint, result, error)) {
        return false;
    }
    // The report is built before the scene moves out of the result: describe() reads it.
    std::string report = result.describe();
    for (const std::string &warning : result.warnings) {
        report += "\naviso: " + warning;
    }

    std::lock_guard<std::mutex> guard(mutex_);
    scene_ = std::move(result.scene);
    lastReport_ = std::move(report);
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    source_.assign(bytes, bytes + length);
    sourceName_ = hint;
    // Every mesh is the one the new file describes again, so none of them needs its geometry kept
    // in a project any more.
    rebuilt_.clear();
    return true;
}

// Puts the document back to what a fresh start holds. The file the model came in with goes too:
// a project saved after this would otherwise carry a model the scene no longer shows.
void Document::reset() {
    std::lock_guard<std::mutex> guard(mutex_);
    scene_.clear();
    scene_.add(starterMesh());
    scene_.select(0);
    source_.clear();
    sourceName_.clear();
    rebuilt_.clear();
    lastReport_ = "documento novo: cubo de exemplo";
}

bool Document::hasSource() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return !source_.empty();
}

std::string Document::sourceName() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return sourceName_;
}

std::string Document::describe() {
    std::lock_guard<std::mutex> guard(mutex_);
    return scene_.describe();
}

size_t Document::sourceBytes() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return source_.capacity();
}

size_t Document::meshBytes() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return scene_.memoryBytes();
}

std::string Document::hierarchy() {
    std::lock_guard<std::mutex> guard(mutex_);
    return scene_.hierarchy();
}

bool Document::bounds(float outMin[3], float outMax[3]) {
    std::lock_guard<std::mutex> guard(mutex_);
    return scene_.bounds(outMin, outMax);
}

uint32_t Document::meshCount() {
    std::lock_guard<std::mutex> guard(mutex_);
    return scene_.meshCount();
}

int Document::selected() {
    std::lock_guard<std::mutex> guard(mutex_);
    return scene_.selected();
}

void Document::select(int index) {
    std::lock_guard<std::mutex> guard(mutex_);
    scene_.select(index);
}

bool Document::replaceMesh(uint32_t index, const Mesh &mesh) {
    std::lock_guard<std::mutex> guard(mutex_);
    Mesh *target = scene_.meshAt(index);
    if (target == nullptr) {
        return false;
    }
    *target = mesh;
    addOnce(rebuilt_, index);
    return true;
}

std::vector<uint32_t> Document::rebuiltMeshes() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return rebuilt_;
}

void Document::noteRebuilt(uint32_t index) {
    std::lock_guard<std::mutex> guard(mutex_);
    addOnce(rebuilt_, index);
}

std::string Document::lastImportReport() {
    std::lock_guard<std::mutex> guard(mutex_);
    return lastReport_;
}

}  // namespace g3d
