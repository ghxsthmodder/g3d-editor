#include "core/document.hpp"

#include <utility>

#include "core/primitives.hpp"
#include "importers/assimp_importer.hpp"

namespace g3d {

Document &Document::instance() {
    static Document document;
    return document;
}

Document::Document() {
    // The editor starts with a cube so the view is never empty and gestures can be checked
    // before a model is imported. Importing replaces it.
    Mesh cube = makeCube(0.5f);
    cube.name = "cubo de exemplo";
    scene_.add(std::move(cube));
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
    return true;
}

std::string Document::describe() {
    std::lock_guard<std::mutex> guard(mutex_);
    return scene_.describe();
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

std::string Document::lastImportReport() {
    std::lock_guard<std::mutex> guard(mutex_);
    return lastReport_;
}

}  // namespace g3d
