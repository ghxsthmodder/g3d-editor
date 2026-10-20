#include "importers/assimp_importer.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <exception>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace {

void addWarningOnce(std::vector<std::string> &warnings, const std::string &text) {
    if (std::find(warnings.begin(), warnings.end(), text) == warnings.end()) {
        warnings.push_back(text);
    }
}

std::string toLower(const std::string &text) {
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string extensionOf(const std::string &path) {
    const size_t dot = path.find_last_of('.');
    return dot == std::string::npos ? std::string() : toLower(path.substr(dot + 1));
}

std::string formatLabel(const std::string &hint) {
    const std::string extension = extensionOf(hint);
    if (extension == "obj") return "Wavefront OBJ";
    if (extension == "glb") return "glTF binário (GLB)";
    if (extension == "gltf") return "glTF (JSON)";
    return extension.empty() ? "formato desconhecido" : "formato ." + extension;
}

glm::mat4 toGlm(const aiMatrix4x4 &m) {
    return glm::mat4(m.a1, m.b1, m.c1, m.d1, m.a2, m.b2, m.c2, m.d2, m.a3, m.b3, m.c3, m.d3,
                     m.a4, m.b4, m.c4, m.d4);
}

void readMaterial(const aiMesh *source, const aiScene *scene, g3d::Mesh &mesh,
                  std::vector<std::string> &warnings) {
    if (source->mMaterialIndex >= scene->mNumMaterials || scene->mMaterials == nullptr) {
        return;
    }
    const aiMaterial *material = scene->mMaterials[source->mMaterialIndex];
    aiString name;
    if (material->Get(AI_MATKEY_NAME, name) == AI_SUCCESS) {
        mesh.material.name = name.C_Str();
    }
    aiColor4D diffuse;
    if (material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS) {
        mesh.material.baseColor[0] = diffuse.r;
        mesh.material.baseColor[1] = diffuse.g;
        mesh.material.baseColor[2] = diffuse.b;
        mesh.material.baseColor[3] = diffuse.a;
    }
    float opacity = 1.0f;
    if (material->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS) {
        mesh.material.baseColor[3] = opacity;
    }
    if (material->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
        addWarningOnce(warnings,
                       "o arquivo referencia imagens de material; elas não são aplicadas, a "
                       "textura é montada no compositor por camadas");
    }
}

g3d::Mesh convertMesh(const aiMesh *source, const aiMatrix4x4 &world, const aiScene *scene,
                      const std::string &nodeName, std::vector<std::string> &warnings) {
    g3d::Mesh mesh;
    mesh.name = source->mName.length > 0 ? source->mName.C_Str() : nodeName;
    if (mesh.name.empty()) {
        mesh.name = "malha";
    }

    const glm::mat4 transform = toGlm(world);
    const glm::mat3 normalMatrix = glm::inverseTranspose(glm::mat3(transform));

    const uint32_t vertexCount = source->mNumVertices;
    mesh.positions.resize(static_cast<size_t>(vertexCount) * 3);
    if (source->mNormals != nullptr) {
        mesh.normals.resize(static_cast<size_t>(vertexCount) * 3);
    }
    if (source->mTextureCoords[0] != nullptr && source->mNumUVComponents[0] >= 2) {
        mesh.uvs.resize(static_cast<size_t>(vertexCount) * 2);
    }

    for (uint32_t v = 0; v < vertexCount; ++v) {
        const aiVector3D &position = source->mVertices[v];
        const glm::vec4 transformed =
            transform * glm::vec4(position.x, position.y, position.z, 1.0f);
        mesh.positions[static_cast<size_t>(v) * 3 + 0] = transformed.x;
        mesh.positions[static_cast<size_t>(v) * 3 + 1] = transformed.y;
        mesh.positions[static_cast<size_t>(v) * 3 + 2] = transformed.z;

        if (!mesh.normals.empty()) {
            const aiVector3D &normal = source->mNormals[v];
            const glm::vec3 rotated =
                normalMatrix * glm::vec3(normal.x, normal.y, normal.z);
            const float length = glm::length(rotated);
            const glm::vec3 unit = length > 0.0f ? rotated / length : rotated;
            mesh.normals[static_cast<size_t>(v) * 3 + 0] = unit.x;
            mesh.normals[static_cast<size_t>(v) * 3 + 1] = unit.y;
            mesh.normals[static_cast<size_t>(v) * 3 + 2] = unit.z;
        }

        if (!mesh.uvs.empty()) {
            const aiVector3D &uv = source->mTextureCoords[0][v];
            mesh.uvs[static_cast<size_t>(v) * 2 + 0] = uv.x;
            mesh.uvs[static_cast<size_t>(v) * 2 + 1] = uv.y;
        }
    }

    mesh.indices.reserve(static_cast<size_t>(source->mNumFaces) * 3);
    for (uint32_t face = 0; face < source->mNumFaces; ++face) {
        const aiFace &source_face = source->mFaces[face];
        if (source_face.mNumIndices != 3) {
            addWarningOnce(warnings, "rostos não triangulares encontrados e ignorados");
            continue;
        }
        for (uint32_t i = 0; i < 3; ++i) {
            mesh.indices.push_back(source_face.mIndices[i]);
        }
    }

    if (source->HasBones()) {
        addWarningOnce(warnings, "esqueleto e pesos de skinning não são importados");
    }
    if (source->mColors[0] != nullptr) {
        addWarningOnce(warnings, "cores de vértice não são importadas");
    }
    if (source->GetNumUVChannels() > 1) {
        addWarningOnce(warnings, "apenas o primeiro canal UV é importado");
    }
    if (source->HasTangentsAndBitangents()) {
        addWarningOnce(warnings, "tangentes e bitangentes do arquivo são ignoradas");
    }

    readMaterial(source, scene, mesh, warnings);
    return mesh;
}

void traverse(const aiNode *node, const aiMatrix4x4 &parentWorld, const aiScene *scene,
              g3d::Scene &scene3d, std::vector<std::string> &warnings) {
    if (node == nullptr) {
        return;
    }
    aiMatrix4x4 world = parentWorld * node->mTransformation;
    const std::string nodeName = node->mName.length > 0 ? node->mName.C_Str() : std::string();

    for (uint32_t i = 0; i < node->mNumMeshes; ++i) {
        const aiMesh *source = scene->mMeshes[node->mMeshes[i]];
        if (source == nullptr || source->mNumVertices == 0) {
            continue;
        }
        scene3d.add(convertMesh(source, world, scene, nodeName, warnings));
    }

    for (uint32_t i = 0; i < node->mNumChildren; ++i) {
        traverse(node->mChildren[i], world, scene, scene3d, warnings);
    }
}

}  // namespace

namespace g3d {

std::string ImportResult::describe() const {
    std::string text = "formato=" + format + " | " + scene.describe() +
                       " | materiais=" + std::to_string(materialCount);
    if (!warnings.empty()) {
        text += " | avisos=" + std::to_string(warnings.size());
    }
    return text;
}

bool importModelFromMemory(const void *data, size_t length, const std::string &fileNameHint,
                           ImportResult &result, std::string &error) {
    if (data == nullptr || length == 0) {
        error = "arquivo vazio";
        return false;
    }

    result.scene.clear();
    result.warnings.clear();
    result.format = formatLabel(fileNameHint);
    result.materialCount = 0;
    result.textureCount = 0;
    result.animationCount = 0;

    // JoinIdenticalVertices welds the per-triangle corners that OBJ files duplicate: without it a
    // cube comes in with 36 vertices instead of 24, which makes UV islands harder to edit. It does
    // not merge vertices with different normals or UVs, so seams stay intact.
    const unsigned int flags = aiProcess_Triangulate | aiProcess_SortByPType |
                               aiProcess_GenNormals | aiProcess_JoinIdenticalVertices;

    Assimp::Importer importer;
    const aiScene *scene = nullptr;
    try {
        scene = importer.ReadFileFromMemory(data, length, flags, fileNameHint.c_str());
    } catch (const std::exception &exception) {
        error = std::string("falha ao ler o arquivo: ") + exception.what();
        return false;
    }

    if (scene == nullptr || !scene->HasMeshes()) {
        const char *detail = importer.GetErrorString();
        error = detail != nullptr && detail[0] != '\0'
                    ? std::string("importação falhou: ") + detail
                    : std::string("o arquivo não contém malhas");
        if (extensionOf(fileNameHint) == "gltf") {
            error += " — arquivos .gltf com buffers externos precisam ser convertidos para GLB";
        }
        return false;
    }

    traverse(scene->mRootNode, aiMatrix4x4(), scene, result.scene, result.warnings);

    result.materialCount = scene->mNumMaterials;
    result.textureCount = scene->mNumTextures;
    result.animationCount = scene->mNumAnimations;

    if (scene->mNumTextures > 0) {
        addWarningOnce(result.warnings,
                       "imagens embutidas no arquivo não são carregadas; use o compositor");
    }
    if (scene->mNumAnimations > 0) {
        addWarningOnce(result.warnings, "animações não são importadas");
    }
    if (scene->mNumCameras > 0 || scene->mNumLights > 0) {
        addWarningOnce(result.warnings, "câmeras e luzes do arquivo não são importadas");
    }

    if (result.scene.isEmpty()) {
        error = "nenhuma malha utilizável encontrada no arquivo";
        return false;
    }

    for (uint32_t i = 0; i < result.scene.meshCount(); ++i) {
        const Mesh *mesh = result.scene.meshAt(i);
        std::string meshError;
        if (mesh != nullptr && !mesh->validate(meshError)) {
            error = "malha importada inválida: " + meshError;
            result.scene.clear();
            return false;
        }
    }
    return true;
}

}  // namespace g3d
