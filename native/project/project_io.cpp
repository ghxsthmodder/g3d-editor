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

private:
    std::vector<uint8_t> &buffer_;
};

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

    // UVs per mesh, in mesh order, and only the meshes that have any.
    std::vector<std::pair<uint32_t, std::vector<float>>> layouts;
    document.withScene([&layouts](Scene &scene) {
        for (uint32_t index = 0; index < scene.meshCount(); ++index) {
            const Mesh *mesh = scene.meshAt(index);
            if (mesh != nullptr && mesh->hasUvs()) {
                layouts.emplace_back(index, mesh->uvs);
            }
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
    for (size_t i = 0; i < layouts.size(); ++i) {
        uvBlobs[i] = sink.appendFloats(layouts[i].second);
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
        writer.StartObject();
        writer.Key("index");
        writer.Uint(layouts[i].first);
        writer.Key("count");
        writer.Uint(static_cast<uint32_t>(layouts[i].second.size()));
        writer.Key("uvOffset");
        writer.Uint(uvBlobs[i].offset);
        writer.Key("uvLength");
        writer.Uint(uvBlobs[i].length);
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
                // The mesh decides how many UVs it needs: one pair per vertex, whatever the project
                // counted when it was written. A model that was rebuilt since then is the only way
                // these two can disagree, and the disagreement is reported, not patched.
                const size_t wanted = static_cast<size_t>(mesh->vertexCount()) * 2u;
                std::vector<float> uvs;
                std::string tooShort;
                if (!readFloats(chunks.bin, uvBlob, wanted, uvs, tooShort)) {
                    addWarningOnce(report.warnings, "a malha " + std::to_string(index) +
                                                        " nao recebeu os UVs: " + tooShort);
                    continue;
                }
                mesh->uvs = std::move(uvs);
                ++report.meshes;
            }
        });
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
