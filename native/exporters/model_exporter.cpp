#include "exporters/model_exporter.hpp"

#include <algorithm>
#include <memory>

#include <assimp/Exporter.hpp>
#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/mesh.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace {

// aiString holds at most this many bytes, longer names are dropped by Set() rather than truncated.
constexpr size_t kNameLimit = 1023;

void setName(aiString &target, const std::string &text, const char *fallback) {
    std::string value = text.empty() ? fallback : text;
    if (value.size() > kNameLimit) {
        value.resize(kNameLimit);
    }
    // The names reach the output of OBJ and glTF as identifiers: a quote or a newline there would
    // write a file that no longer reads back.
    for (char &c : value) {
        if (c == '\r' || c == '\n' || c == '"' || c == ' ') {
            c = '_';
        }
    }
    target.Set(value.c_str());
}

aiMesh *buildMesh(const g3d::Mesh &source, unsigned int materialIndex) {
    aiMesh *mesh = new aiMesh();
    setName(mesh->mName, source.name, "malha");
    mesh->mPrimitiveTypes = aiPrimitiveType_POLYGON;
    mesh->mMaterialIndex = materialIndex;

    const uint32_t vertexCount = source.vertexCount();
    mesh->mNumVertices = vertexCount;
    mesh->mVertices = new aiVector3D[vertexCount];
    for (uint32_t v = 0; v < vertexCount; ++v) {
        mesh->mVertices[v] = aiVector3D(source.positions[static_cast<size_t>(v) * 3 + 0],
                                       source.positions[static_cast<size_t>(v) * 3 + 1],
                                       source.positions[static_cast<size_t>(v) * 3 + 2]);
    }
    if (source.hasNormals()) {
        mesh->mNormals = new aiVector3D[vertexCount];
        for (uint32_t v = 0; v < vertexCount; ++v) {
            mesh->mNormals[v] = aiVector3D(source.normals[static_cast<size_t>(v) * 3 + 0],
                                          source.normals[static_cast<size_t>(v) * 3 + 1],
                                          source.normals[static_cast<size_t>(v) * 3 + 2]);
        }
    }
    if (source.hasUvs()) {
        mesh->mTextureCoords[0] = new aiVector3D[vertexCount];
        mesh->mNumUVComponents[0] = 2;
        for (uint32_t v = 0; v < vertexCount; ++v) {
            // The third component stays zero: the editor works in 2D and glTF reads two components.
            mesh->mTextureCoords[0][v] = aiVector3D(source.uvs[static_cast<size_t>(v) * 2 + 0],
                                                   source.uvs[static_cast<size_t>(v) * 2 + 1],
                                                   0.0f);
        }
    }

    const uint32_t faceCount = source.triangleCount();
    mesh->mNumFaces = faceCount;
    mesh->mFaces = new aiFace[faceCount];
    for (uint32_t face = 0; face < faceCount; ++face) {
        aiFace &target = mesh->mFaces[face];
        target.mNumIndices = 3;
        target.mIndices = new unsigned int[3];
        for (uint32_t i = 0; i < 3; ++i) {
            target.mIndices[i] = source.indices[static_cast<size_t>(face) * 3 + i];
        }
    }
    return mesh;
}

aiMaterial *buildMaterial(const g3d::Material &source, const std::string &fallbackName) {
    aiMaterial *material = new aiMaterial();
    aiString name;
    setName(name, source.name.empty() ? fallbackName : source.name, "material");
    material->AddProperty(&name, AI_MATKEY_NAME);

    aiColor4D diffuse(source.baseColor[0], source.baseColor[1], source.baseColor[2],
                      source.baseColor[3]);
    material->AddProperty(&diffuse, 1, AI_MATKEY_COLOR_DIFFUSE);
    if (source.baseColor[3] < 1.0f) {
        const ai_real opacity = source.baseColor[3];
        material->AddProperty(&opacity, 1, AI_MATKEY_OPACITY);
    }
    return material;
}

// The internal scene is a flat list of meshes whose positions are already in world space, so the
// aiScene mirrors it exactly: one node with an identity transform holding every mesh. Anything
// the editor did not keep (hierarchy, bones, extra UV channels) is not here to be written.
std::unique_ptr<aiScene> buildScene(const g3d::Scene &scene, const std::string &baseName) {
    const uint32_t meshCount = scene.meshCount();
    std::unique_ptr<aiScene> output(new aiScene());
    output->mNumMeshes = meshCount;
    output->mMeshes = new aiMesh *[meshCount]();
    output->mNumMaterials = meshCount;
    output->mMaterials = new aiMaterial *[meshCount]();

    aiNode *root = new aiNode();
    setName(root->mName, baseName, "g3d_editor");
    root->mNumMeshes = meshCount;
    root->mMeshes = new unsigned int[meshCount];
    for (uint32_t i = 0; i < meshCount; ++i) {
        const g3d::Mesh *mesh = scene.meshAt(i);
        output->mMeshes[i] = buildMesh(*mesh, i);
        output->mMaterials[i] = buildMaterial(mesh->material, mesh->name);
        root->mMeshes[i] = i;
    }
    output->mRootNode = root;
    setName(output->mName, baseName, "g3d_editor");
    return output;
}

void addWarningOnce(std::vector<std::string> &warnings, const std::string &text) {
    if (std::find(warnings.begin(), warnings.end(), text) == warnings.end()) {
        warnings.push_back(text);
    }
}

std::string fileNameOf(const aiExportDataBlob &blob, const std::string &baseName,
                       const std::string &extension, bool primary) {
    if (primary || blob.name.length == 0) {
        return baseName + "." + extension;
    }
    return blob.name.C_Str();
}

}  // namespace

namespace g3d {

const char *modelFormatId(ModelFormat format) {
    switch (format) {
    case ModelFormat::Gltf:
        return "gltf2";
    case ModelFormat::Glb:
        return "glb2";
    case ModelFormat::Obj:
        break;
    }
    return "obj";
}

std::string modelFormatLabel(ModelFormat format) {
    switch (format) {
    case ModelFormat::Gltf:
        return "glTF";
    case ModelFormat::Glb:
        return "GLB";
    case ModelFormat::Obj:
        break;
    }
    return "OBJ";
}

std::string modelFormatExtension(ModelFormat format) {
    switch (format) {
    case ModelFormat::Gltf:
        return "gltf";
    case ModelFormat::Glb:
        return "glb";
    case ModelFormat::Obj:
        break;
    }
    return "obj";
}

size_t ModelExport::totalBytes() const {
    size_t total = 0;
    for (const ExportedFile &file : files) {
        total += file.bytes.size();
    }
    return total;
}

std::string ModelExport::describe() const {
    std::string text = "arquivos=" + std::to_string(files.size()) +
                       " | bytes=" + std::to_string(totalBytes());
    if (!warnings.empty()) {
        text += " | avisos=" + std::to_string(warnings.size());
    }
    return text;
}

bool exportModel(const Scene &scene, ModelFormat format, const std::string &baseName,
                 ModelExport &output, std::string &error) {
    output.files.clear();
    output.warnings.clear();

    if (scene.isEmpty()) {
        error = "não há malhas abertas para exportar";
        return false;
    }
    if (baseName.empty()) {
        error = "dê um nome ao arquivo";
        return false;
    }

    for (uint32_t i = 0; i < scene.meshCount(); ++i) {
        const Mesh *mesh = scene.meshAt(i);
        std::string meshError;
        if (mesh == nullptr || !mesh->validate(meshError)) {
            error = "a malha " + std::to_string(i) + " não pode ser exportada: " + meshError;
            return false;
        }
        if (!mesh->hasUvs()) {
            addWarningOnce(output.warnings,
                           "há malhas sem UV; elas saem do arquivo sem coordenadas de textura");
        }
    }

    std::unique_ptr<aiScene> aiScene = buildScene(scene, baseName);

    Assimp::Exporter exporter;
    Assimp::ExportProperties properties;
    properties.SetPropertyString(AI_CONFIG_EXPORT_BLOB_NAME, baseName.c_str());

    const aiExportDataBlob *blob =
        exporter.ExportToBlob(aiScene.get(), modelFormatId(format), 0u, &properties);
    if (blob == nullptr) {
        const char *detail = exporter.GetErrorString();
        error = detail != nullptr && detail[0] != '\0'
                    ? std::string("exportação falhou: ") + detail
                    : std::string("exportação falhou");
        return false;
    }

    const std::string extension = modelFormatExtension(format);
    for (const aiExportDataBlob *cur = blob; cur != nullptr; cur = cur->next) {
        ExportedFile file;
        file.name = fileNameOf(*cur, baseName, extension, cur == blob);
        const uint8_t *bytes = static_cast<const uint8_t *>(cur->data);
        if (cur->data != nullptr && cur->size > 0) {
            file.bytes.assign(bytes, bytes + cur->size);
        }
        if (file.bytes.empty()) {
            exporter.FreeBlob();
            error = "o exportador não produziu dados para " + file.name;
            return false;
        }
        output.files.push_back(std::move(file));
    }
    exporter.FreeBlob();

    // What the editor holds that the destination format cannot carry, told rather than hidden:
    // PLAN asks the editor to say when information could not be preserved.
    addWarningOnce(output.warnings,
                   "a textura composta não vai no arquivo do modelo; exporte o PNG e associe-o "
                   "no destino");
    if (format == ModelFormat::Obj) {
        addWarningOnce(output.warnings,
                       "OBJ guarda só nome, cor difusa e opacidade: sem hierarquia de materiais "
                       "nem PBR");
    } else {
        addWarningOnce(output.warnings,
                       "apenas o primeiro canal UV e a cor difusa do material vão no arquivo");
    }
    if (scene.selected() >= 0) {
        addWarningOnce(output.warnings, "o arquivo contém todas as malhas, não só a selecionada");
    }
    return true;
}

}  // namespace g3d
