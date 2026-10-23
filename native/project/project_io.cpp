#include "project/project_io.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <sstream>

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#include "core/document.hpp"
#include "project/project_container.hpp"
#include "texture/g3d_image.hpp"
#include "texture/texture_studio.hpp"
#include "uv/uv_editor.hpp"

namespace g3d {
namespace {

// One block of bytes inside the binary chunk, addressed by offset and length the way glTF addresses
// its buffers, so the description stays text and the data stays data.
struct Blob {
    uint32_t offset = 0;
    uint32_t length = 0;
};

class Sink {
public:
    explicit Sink(std::vector<uint8_t> &buffer) : buffer_(buffer) {}

    Blob append(const void *data, size_t length) {
        while (buffer_.size() % 4u != 0u) {
            buffer_.push_back(0u);
        }
        Blob blob;
        blob.offset = static_cast<uint32_t>(buffer_.size());
        blob.length = static_cast<uint32_t>(length);
        const uint8_t *bytes = static_cast<const uint8_t *>(data);
        buffer_.insert(buffer_.end(), bytes, bytes + length);
        return blob;
    }

    Blob appendFloats(const std::vector<float> &values) {
        return append(values.data(), values.size() * sizeof(float));
    }

    Blob appendUint32s(const std::vector<uint32_t> &values) {
        return append(values.data(), values.size() * sizeof(uint32_t));
    }

private:
    std::vector<uint8_t> &buffer_;
};

// Reads an array out of the binary chunk, taking how many there are from the block itself. Used for
// the geometry, where the count is part of what is being carried rather than something to check a
// mesh against.
template <typename T>
bool readArray(const std::vector<uint8_t> &bin, const Blob &blob, std::vector<T> &out,
               std::string &error) {
    if (blob.length % sizeof(T) != 0u) {
        error = "o bloco tem " + std::to_string(blob.length) + " bytes, que nao se dividem em " +
                std::to_string(sizeof(T)) +
                "; ou a geometria foi gravada torta, ou o arquivo nao e deste editor";
        return false;
    }
    if (static_cast<size_t>(blob.offset) + blob.length > bin.size()) {
        error = "o bloco aponta para fora do arquivo";
        return false;
    }
    out.resize(blob.length / sizeof(T));
    if (!out.empty()) {
        std::memcpy(out.data(), bin.data() + blob.offset, blob.length);
    }
    return true;
}

// Reads a float array out of the binary chunk. False, with the reason, when the reference points
// outside the file: a project that has been truncated is reported, not half applied.
bool readFloats(const std::vector<uint8_t> &bin, const Blob &blob, size_t count,
                std::vector<float> &out, std::string &error) {
    if (blob.length != count * sizeof(float)) {
        error = "o bloco tem " + std::to_string(blob.length) + " bytes, e a contagem pede " +
                std::to_string(count * sizeof(float));
        return false;
    }
    if (static_cast<size_t>(blob.offset) + blob.length > bin.size()) {
        error = "o bloco aponta para fora do arquivo";
        return false;
    }
    out.resize(count);
    std::memcpy(out.data(), bin.data() + blob.offset, blob.length);
    return true;
}

bool readBlob(const rapidjson::Value &owner, const char *name, Blob &blob, size_t limit,
              std::string &error) {
    if (!owner.IsObject()) {
        error = std::string("o campo '") + name + "' nao e um objeto";
        return false;
    }
    const std::string offsetKey = std::string(name) + "Offset";
    const std::string lengthKey = std::string(name) + "Length";
    const auto offset = owner.FindMember(offsetKey.c_str());
    const auto length = owner.FindMember(lengthKey.c_str());
    if (offset == owner.MemberEnd() || !offset->value.IsUint() ||
        length == owner.MemberEnd() || !length->value.IsUint()) {
        error = std::string("falta o bloco '") + name + "' na descricao";
        return false;
    }
    blob.offset = offset->value.GetUint();
    blob.length = length->value.GetUint();
    if (static_cast<size_t>(blob.offset) + blob.length > limit) {
        error = std::string("o bloco '") + name + "' aponta para fora do arquivo";
        return false;
    }
    return true;
}

std::string textOf(const rapidjson::Value &owner, const char *name) {
    const auto found = owner.FindMember(name);
    if (found == owner.MemberEnd() || !found->value.IsString()) {
        return std::string();
    }
    return std::string(found->value.GetString(), found->value.GetStringLength());
}

double numberOf(const rapidjson::Value &owner, const char *name, double fallback) {
    const auto found = owner.FindMember(name);
    if (found == owner.MemberEnd() || !found->value.IsNumber()) {
        return fallback;
    }
    return found->value.GetDouble();
}

bool boolOf(const rapidjson::Value &owner, const char *name, bool fallback) {
    const auto found = owner.FindMember(name);
    if (found == owner.MemberEnd() || !found->value.IsBool()) {
        return fallback;
    }
    return found->value.GetBool();
}

void addWarningOnce(std::vector<std::string> &warnings, const std::string &text) {
    if (std::find(warnings.begin(), warnings.end(), text) == warnings.end()) {
        warnings.push_back(text);
    }
}

std::mutex reportMutex;
std::string lastReport;

// A save hands back bytes and an open hands back a boolean, so neither has room for the reason it
// stopped or for what the file left out. Whichever way the function below returns, the outcome is
// remembered here and the interface reads it with projectReport().
class ReportScope {
public:
    ReportScope(const ProjectReport &report, const std::string &error)
        : report_(report), error_(error) {}

    ~ReportScope() {
        std::lock_guard<std::mutex> guard(reportMutex);
        lastReport = error_.empty() ? report_.describe() : ("erro: " + error_);
    }

private:
    const ProjectReport &report_;
    const std::string &error_;
};

// What one mesh carries into the project beyond the model file: its UVs, and for a mesh the UV
// session rebuilt, the geometry those UVs are measured against. A mesh the file still describes
// needs only the first — its vertices are in the file already, and writing a second copy of them
// would buy nothing.
struct MeshEntry {
    uint32_t index = 0;
    std::vector<float> uvs;
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<uint32_t> triangles;
    bool rebuilt = false;
};

}  // namespace

std::string projectReport() {
    std::lock_guard<std::mutex> guard(reportMutex);
    return lastReport;
}

std::string ProjectReport::describe() const {
    std::ostringstream stream;
    stream << "arquivo=" << bytes / 1024 << "KB modelo=" << modelBytes / 1024
           << "KB imagens=" << imageBytes / 1024 << "KB malhas=" << meshes << " camadas=" << layers;
    for (const std::string &warning : warnings) {
        stream << "\naviso: " << warning;
    }
    return stream.str();
}

bool saveProject(std::vector<uint8_t> &out, ProjectReport &report, std::string &error) {
    const ReportScope remember(report, error);
    Document &document = Document::instance();
    if (!document.hasSource()) {
        error = "nao ha modelo importado para guardar: o projeto carrega o arquivo de onde a cena "
                "veio";
        return false;
    }
    const std::string modelName = document.sourceName();

    // UVs per mesh, in mesh order, and only the meshes that have any. A mesh the UV session
    // replaced does not match the embedded file any more, so its geometry comes along too.
    const std::vector<uint32_t> rebuilt = document.rebuiltMeshes();
    std::vector<MeshEntry> layouts;
    document.withScene([&layouts, &rebuilt](Scene &scene) {
        for (uint32_t index = 0; index < scene.meshCount(); ++index) {
            const Mesh *mesh = scene.meshAt(index);
            if (mesh == nullptr || !mesh->hasUvs()) {
                continue;
            }
            MeshEntry entry;
            entry.index = index;
            entry.uvs = mesh->uvs;
            entry.rebuilt = std::find(rebuilt.begin(), rebuilt.end(), index) != rebuilt.end();
            if (entry.rebuilt) {
                entry.positions = mesh->positions;
                entry.normals = mesh->normals;
                entry.triangles = mesh->indices;
            }
            layouts.push_back(std::move(entry));
        }
    });

    TextureStudio &studio = TextureStudio::instance();
    std::vector<TextureLayer> layers = studio.layers();
    uint8_t background[3] = {0};
    studio.background(background);

    std::vector<uint8_t> bin;
    Sink sink(bin);
    Blob modelBlob;
    document.withSource([&sink, &modelBlob](const uint8_t *bytes, size_t length) {
        modelBlob = sink.append(bytes, length);
    });
    report.modelBytes = modelBlob.length;

    std::vector<Blob> imageBlobs(layers.size());
    std::vector<std::vector<uint8_t>> encoded(layers.size());
    for (size_t i = 0; i < layers.size(); ++i) {
        std::string encodeError;
        if (!encodePng(*layers[i].image, encoded[i], encodeError)) {
            // The layer is dropped with a word about it rather than failing the whole save: losing
            // one picture is survivable, losing the project is not.
            addWarningOnce(report.warnings,
                           "a camada " + layers[i].name + " nao virou PNG: " + encodeError);
            continue;
        }
        imageBlobs[i] = sink.append(encoded[i].data(), encoded[i].size());
        report.imageBytes += encoded[i].size();
    }

    std::vector<Blob> uvBlobs(layouts.size());
    std::vector<Blob> positionBlobs(layouts.size());
    std::vector<Blob> normalBlobs(layouts.size());
    std::vector<Blob> triangleBlobs(layouts.size());
    for (size_t i = 0; i < layouts.size(); ++i) {
        const MeshEntry &entry = layouts[i];
        uvBlobs[i] = sink.appendFloats(entry.uvs);
        if (entry.rebuilt) {
            positionBlobs[i] = sink.appendFloats(entry.positions);
            normalBlobs[i] = sink.appendFloats(entry.normals);
            triangleBlobs[i] = sink.appendUint32s(entry.triangles);
        }
    }

    rapidjson::StringBuffer text;
    rapidjson::Writer<rapidjson::StringBuffer> writer(text);
    writer.StartObject();
    writer.Key("format");
    writer.Uint(kProjectVersion);
    writer.Key("model");
    writer.StartObject();
    writer.Key("name");
    writer.String(modelName.c_str(), static_cast<rapidjson::SizeType>(modelName.size()));
    writer.Key("modelOffset");
    writer.Uint(modelBlob.offset);
    writer.Key("modelLength");
    writer.Uint(modelBlob.length);
    writer.EndObject();
    writer.Key("meshes");
    writer.StartArray();
    for (size_t i = 0; i < layouts.size(); ++i) {
        const MeshEntry &entry = layouts[i];
        writer.StartObject();
        writer.Key("index");
        writer.Uint(entry.index);
        writer.Key("count");
        writer.Uint(static_cast<uint32_t>(entry.uvs.size()));
        writer.Key("uvOffset");
        writer.Uint(uvBlobs[i].offset);
        writer.Key("uvLength");
        writer.Uint(uvBlobs[i].length);
        if (entry.rebuilt) {
            // Read before the UVs on the way back in: the file would put a mesh with a different
            // number of vertices where this one is, and the UVs have nothing to land on until the
            // shape they were made for is standing there.
            writer.Key("rebuilt");
            writer.Bool(true);
            writer.Key("positionOffset");
            writer.Uint(positionBlobs[i].offset);
            writer.Key("positionLength");
            writer.Uint(positionBlobs[i].length);
            writer.Key("normalOffset");
            writer.Uint(normalBlobs[i].offset);
            writer.Key("normalLength");
            writer.Uint(normalBlobs[i].length);
            writer.Key("triangleOffset");
            writer.Uint(triangleBlobs[i].offset);
            writer.Key("triangleLength");
            writer.Uint(triangleBlobs[i].length);
        }
        writer.EndObject();
    }
    writer.EndArray();
    writer.Key("texture");
    writer.StartObject();
    writer.Key("width");
    writer.Uint(static_cast<uint32_t>(studio.canvasWidth()));
    writer.Key("height");
    writer.Uint(static_cast<uint32_t>(studio.canvasHeight()));
    writer.Key("background");
    writer.StartArray();
    for (uint8_t channel : background) {
        writer.Uint(channel);
    }
    writer.EndArray();
    writer.Key("layers");
    writer.StartArray();
    for (size_t i = 0; i < layers.size(); ++i) {
        if (imageBlobs[i].length == 0u) {
            continue;  // the picture that would not encode, already warned about
        }
        const TextureLayer &layer = layers[i];
        writer.StartObject();
        writer.Key("name");
        writer.String(layer.name.c_str(), static_cast<rapidjson::SizeType>(layer.name.size()));
        writer.Key("imageOffset");
        writer.Uint(imageBlobs[i].offset);
        writer.Key("imageLength");
        writer.Uint(imageBlobs[i].length);
        writer.Key("centreX");
        writer.Double(layer.centreX);
        writer.Key("centreY");
        writer.Double(layer.centreY);
        writer.Key("rotation");
        writer.Double(layer.rotationDegrees);
        writer.Key("scaleX");
        writer.Double(layer.scaleX);
        writer.Key("scaleY");
        writer.Double(layer.scaleY);
        writer.Key("opacity");
        writer.Double(layer.opacity);
        writer.Key("visible");
        writer.Bool(layer.visible);
        writer.EndObject();
    }
    writer.EndArray();
    writer.EndObject();
    writer.EndObject();

    report.layers = 0;
    for (const Blob &blob : imageBlobs) {
        report.layers += blob.length == 0u ? 0u : 1u;
    }
    report.meshes = layouts.size();

    // What the project does not carry, said before writing the file instead of discovered when it
    // opens. Applying leaves the session open on the layout the scene now has, so an open one is
    // worth a word only when it holds work the scene does not have yet, or steps to walk back.
    UvEditor &editor = UvEditor::instance();
    if (editor.isActive()) {
        UvLayout view;
        bool unapplied = true;
        const uint32_t editing = editor.meshIndex();
        if (editor.layout(view)) {
            document.withScene([&view, editing, &unapplied](Scene &scene) {
                const Mesh *mesh = scene.meshAt(editing);
                unapplied = mesh == nullptr || mesh->uvs != view.uvs;
            });
        }
        if (unapplied) {
            addWarningOnce(report.warnings,
                           "a edicao de UV aberta nao esta aplicada: o projeto grava a cena, e nao "
                           "o que esta na tela de edicao");
        } else if (editor.hasHistory()) {
            addWarningOnce(report.warnings,
                           "o desfazer da sessao de UV aberta nao vai no projeto");
        }
    }
    if (report.layers != layers.size()) {
        addWarningOnce(report.warnings, "algumas camadas ficaram de fora por causa do PNG");
    }

    std::string json(text.GetString(), text.GetSize());
    if (!writeProjectContainer(json, bin, out, error)) {
        return false;
    }
    report.bytes = out.size();
    return true;
}

bool openProject(const void *data, size_t length, const std::string &name, ProjectReport &report,
                 std::string &error) {
    const ReportScope remember(report, error);
    ProjectChunks chunks;
    if (!readProjectContainer(data, length, chunks, error)) {
        return false;
    }
    report.bytes = length;

    rapidjson::Document json;
    json.Parse(chunks.json.c_str(), chunks.json.size());
    if (json.HasParseError() || !json.IsObject()) {
        error = "a descricao do projeto nao e JSON valido";
        return false;
    }
    const auto format = json.FindMember("format");
    if (format == json.MemberEnd() || !format->value.IsUint() ||
        format->value.GetUint() != kProjectVersion) {
        error = "o projeto nao e da versao " + std::to_string(kProjectVersion);
        return false;
    }

    const auto model = json.FindMember("model");
    if (model == json.MemberEnd()) {
        error = "o projeto nao traz o modelo";
        return false;
    }
    Blob modelBlob;
    if (!readBlob(model->value, "model", modelBlob, chunks.bin.size(), error)) {
        return false;
    }
    if (modelBlob.length == 0u) {
        error = "o projeto nao traz o modelo";
        return false;
    }
    const std::string modelName = textOf(model->value, "name");
    Document &document = Document::instance();
    if (!document.importModel(chunks.bin.data() + modelBlob.offset, modelBlob.length,
                              modelName.empty() ? name : modelName, error)) {
        return false;
    }
    report.modelBytes = modelBlob.length;

    const auto meshes = json.FindMember("meshes");
    std::vector<uint32_t> restoredShapes;
    if (meshes != json.MemberEnd() && meshes->value.IsArray()) {
        std::string reason;
        document.withScene([&](Scene &scene) {
            for (const rapidjson::Value &entry : meshes->value.GetArray()) {
                if (!entry.IsObject()) {
                    continue;
                }
                Blob uvBlob;
                if (!readBlob(entry, "uv", uvBlob, chunks.bin.size(), reason)) {
                    addWarningOnce(report.warnings, "os UVs nao vieram: " + reason);
                    continue;
                }
                const uint32_t index = static_cast<uint32_t>(numberOf(entry, "index", -1));
                Mesh *mesh = scene.meshAt(index);
                if (mesh == nullptr) {
                    addWarningOnce(report.warnings,
                                   "a malha " + std::to_string(index) +
                                       " nao existe mais no modelo: os UVs dela ficam de fora");
                    continue;
                }
                const bool carriesShape = boolOf(entry, "rebuilt", false);
                std::vector<float> positions;
                std::vector<float> normals;
                std::vector<uint32_t> triangles;
                size_t vertices = mesh->vertexCount();
                // A project written after a UV generation carries the mesh those UVs were made for,
                // because the file imported above describes one with fewer vertices in its place.
                // Everything lands in locals first: a project that cannot hand over a whole mesh
                // leaves the mesh standing there as it is, rather than half restored.
                if (carriesShape) {
                    Blob positionBlob;
                    Blob normalBlob;
                    Blob triangleBlob;
                    std::string shapeReason;
                    bool fits = readBlob(entry, "position", positionBlob, chunks.bin.size(),
                                         shapeReason) &&
                                readBlob(entry, "normal", normalBlob, chunks.bin.size(),
                                         shapeReason) &&
                                readBlob(entry, "triangle", triangleBlob, chunks.bin.size(),
                                         shapeReason) &&
                                readArray(chunks.bin, positionBlob, positions, shapeReason) &&
                                readArray(chunks.bin, normalBlob, normals, shapeReason) &&
                                readArray(chunks.bin, triangleBlob, triangles, shapeReason);
                    vertices = positions.size() / 3u;
                    // The corners have to address the positions delivered with them: they are
                    // uploaded together, and a file that says otherwise would read past the end.
                    for (const uint32_t corner : triangles) {
                        if (corner >= vertices) {
                            fits = false;
                            shapeReason = "um indice aponta para o vertex " +
                                          std::to_string(corner) + ", e a malha tem " +
                                          std::to_string(vertices);
                            break;
                        }
                    }
                    if (!fits) {
                        addWarningOnce(report.warnings, "a malha " + std::to_string(index) +
                                                            " nao trouxe a geometria: " +
                                                            shapeReason);
                        continue;
                    }
                }
                // One pair per vertex, of the mesh the UVs were measured against.
                std::vector<float> uvs;
                std::string tooShort;
                if (!readFloats(chunks.bin, uvBlob, vertices * 2u, uvs, tooShort)) {
                    addWarningOnce(report.warnings, "a malha " + std::to_string(index) +
                                                        " nao recebeu os UVs: " + tooShort);
                    continue;
                }
                if (carriesShape) {
                    mesh->positions = std::move(positions);
                    mesh->normals = std::move(normals);
                    mesh->indices = std::move(triangles);
                    restoredShapes.push_back(index);
                }
                mesh->uvs = std::move(uvs);
                ++report.meshes;
            }
        });
    }
    // The scene now holds geometry the embedded file does not describe, so the next save keeps
    // carrying it for the same reason this one had to.
    for (const uint32_t index : restoredShapes) {
        document.noteRebuilt(index);
    }

    const auto texture = json.FindMember("texture");
    // The stack of the file being opened replaces whatever was there, texture section or not: a
    // project that carries no compositor must not inherit the layers of the one before it.
    TextureStudio &studio = TextureStudio::instance();
    studio.reset();
    if (texture == json.MemberEnd() || !texture->value.IsObject()) {
        return true;  // a project without a compositor is still a model with UVs
    }
    const int width = static_cast<int>(numberOf(texture->value, "width", 0));
    const int height = static_cast<int>(numberOf(texture->value, "height", 0));
    std::string canvasError;
    if (width > 0 && height > 0 && !studio.setCanvas(width, height, canvasError)) {
        addWarningOnce(report.warnings, "a tela salva nao coube aqui: " + canvasError);
    }
    const auto background = texture->value.FindMember("background");
    if (background != texture->value.MemberEnd() && background->value.IsArray() &&
        background->value.Size() == 3) {
        uint8_t rgb[3] = {0};
        for (rapidjson::SizeType i = 0; i < 3; ++i) {
            rgb[i] = static_cast<uint8_t>(background->value[i].IsUint()
                                              ? background->value[i].GetUint()
                                              : 0u);
        }
        studio.setBackground(rgb);
    }

    const auto layers = texture->value.FindMember("layers");
    if (layers != texture->value.MemberEnd() && layers->value.IsArray()) {
        for (const rapidjson::Value &entry : layers->value.GetArray()) {
            if (!entry.IsObject()) {
                continue;
            }
            Blob imageBlob;
            std::string reason;
            if (!readBlob(entry, "image", imageBlob, chunks.bin.size(), reason)) {
                addWarningOnce(report.warnings, "a camada nao veio no arquivo: " + reason);
                continue;
            }
            Image image;
            const std::string layerName = textOf(entry, "name");
            if (!decodeImage(chunks.bin.data() + imageBlob.offset, imageBlob.length, layerName,
                             image, reason)) {
                addWarningOnce(report.warnings, "a imagem da camada nao decodificou: " + reason);
                continue;
            }
            TextureLayer at;
            at.name = layerName;
            at.centreX = static_cast<float>(numberOf(entry, "centreX", width * 0.5));
            at.centreY = static_cast<float>(numberOf(entry, "centreY", height * 0.5));
            at.rotationDegrees = static_cast<float>(numberOf(entry, "rotation", 0.0));
            at.scaleX = static_cast<float>(numberOf(entry, "scaleX", 1.0));
            at.scaleY = static_cast<float>(numberOf(entry, "scaleY", 1.0));
            at.opacity = static_cast<float>(numberOf(entry, "opacity", 1.0));
            at.visible = boolOf(entry, "visible", true);
            std::string layerError;
            if (studio.addLayerAt(image, at, layerError) < 0) {
                addWarningOnce(report.warnings, "a camada nao coube no compositor: " + layerError);
                continue;
            }
            ++report.layers;
            report.imageBytes += imageBlob.length;
        }
    }
    // The stack as it was rebuilt from the file is the starting point, not a step: undoing after a
    // reopened project has to stop at the project the user saved, the same way the UV session does.
    studio.clearHistory();
    return true;
}

}  // namespace g3d
