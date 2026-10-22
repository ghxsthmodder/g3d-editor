#include "importers/assimp_importer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <exception>
#include <unordered_set>
#include <utility>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace {

// The budget lives here, next to the only code that spends it. The app lowers it to what the
// device reports as free before an import; the default is what a mid phone can hold without the
// low memory killer showing up.
size_t gImportBudgetBytes = 128u * 1024u * 1024u;

// One expanded vertex is carried twice: positions, normals and uvs in the document, and the
// interleaved buffer the renderer keeps beside its own GPU copy. Indices follow the same split.
// The estimate is deliberately round: it decides whether a scene is tried at all.
constexpr size_t kBytesPerVertex = 64u;
constexpr size_t kBytesPerTriangle = 24u;

void addWarningOnce(std::vector<std::string> &warnings, const std::string &text) {
    if (std::find(warnings.begin(), warnings.end(), text) == warnings.end()) {
        warnings.push_back(text);
    }
}

// How many bytes one UTF-8 sequence announces, or 0 when the lead byte starts nothing valid.
// Four-byte sequences are refused on purpose: the runtime of the phone wants its supplementary
// characters as a surrogate pair, never as four bytes, so an emoji in a name is not worth the risk.
size_t sequenceLength(unsigned char lead) {
    if (lead < 0x80u) return 1u;
    if (lead >= 0xC2u && lead <= 0xDFu) return 2u;
    if (lead >= 0xE0u && lead <= 0xEFu) return 3u;
    return 0u;
}

// A three-byte sequence is only real UTF-8 when it is neither overlong (E0 80..) nor a surrogate
// disguised as one (ED A0.. is U+D800, which has no meaning on its own).
bool threeBytesValid(unsigned char lead, unsigned char next) {
    if (lead == 0xE0u && next < 0xA0u) {
        return false;
    }
    if (lead == 0xEDu && next >= 0xA0u) {
        return false;
    }
    return true;
}

// How many bytes the lead announced, whether or not the sequence is one we let through: the
// continuation bytes of a character that failed belong to it, and consuming them together with it
// keeps one broken character to one mark instead of one per byte.
size_t announcedLength(unsigned char lead) {
    if (lead < 0xC0u) return 1u;
    if (lead < 0xE0u) return 2u;
    if (lead < 0xF0u) return 3u;
    return 4u;
}

// A name read from a file is only ever shown back to the user, and the runtime of the phone is
// strict about how: one stray Latin-1 byte, which OBJ and FBX files do carry, is enough for it to
// abort the process on the way to the screen. Valid UTF-8 passes through untouched, control bytes
// become spaces, and anything that is not a sequence at all is replaced, so a model that arrived
// with a broken name still arrives. The length is capped because the panel is where names are
// read, and a file is free to put a megabyte in a field meant for a few words.
std::string readableName(const std::string &raw) {
    static constexpr size_t kMaxNameBytes = 96u;

    std::string out;
    out.reserve(std::min(raw.size(), kMaxNameBytes));
    for (size_t i = 0; i < raw.size(); ++i) {
        const unsigned char byte = static_cast<unsigned char>(raw[i]);
        const size_t length = sequenceLength(byte);
        bool valid = false;
        if (length == 1u) {
            valid = byte >= 0x20u && byte != 0x7Fu;
        } else if (length >= 2u && i + length <= raw.size()) {
            valid = length != 3u ||
                    threeBytesValid(byte, static_cast<unsigned char>(raw[i + 1]));
            for (size_t follow = 1; follow < length && valid; ++follow) {
                valid = (static_cast<unsigned char>(raw[i + follow]) & 0xC0u) == 0x80u;
            }
        }
        if (valid) {
            out.append(raw, i, length);
            i += length - 1u;
            continue;
        }
        if (byte == '\t' || byte == '\n' || byte == '\r') {
            out.push_back(' ');
            continue;
        }
        out.push_back('?');
        size_t consumed = 1u;
        const size_t announced = announcedLength(byte);
        while (consumed < announced && i + consumed < raw.size() &&
               (static_cast<unsigned char>(raw[i + consumed]) & 0xC0u) == 0x80u) {
            ++consumed;
        }
        i += consumed - 1u;
        if (out.size() >= kMaxNameBytes) {
            break;
        }
    }
    return out;
}

// Two nodes that point at the same mesh through the same world matrix expand into byte-identical
// geometry, which the editor can only ever edit as one of the two. The matrix is taken bit for
// bit: an exporter that scattered copies of an object writes the same numbers each time.
struct Reference {
    uint32_t meshIndex = 0;
    std::array<float, 16> matrix{};

    bool operator==(const Reference &other) const {
        return meshIndex == other.meshIndex && matrix == other.matrix;
    }
};

struct ReferenceHash {
    size_t operator()(const Reference &reference) const {
        size_t hash = std::hash<uint32_t>()(reference.meshIndex);
        for (const float value : reference.matrix) {
            std::array<unsigned char, sizeof(float)> bytes{};
            std::memcpy(bytes.data(), &value, sizeof(float));
            hash = hash * 131u + std::hash<std::string>()(
                                      std::string(bytes.begin(), bytes.end()));
        }
        return hash;
    }
};

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
        mesh.material.name = readableName(name.C_Str());
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
    mesh.name = source->mName.length > 0 ? readableName(source->mName.C_Str())
                                         : readableName(nodeName);
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

// Everything one pass over the node tree has to remember between nodes.
struct Expansion {
    size_t bytes = 0;                  // what the scene already costs to hold
    uint32_t merged = 0;               // references that duplicated geometry already in the scene
    uint32_t skipped = 0;              // references left out because the budget was reached
    std::unordered_set<Reference, ReferenceHash> seen;
};

void traverse(const aiNode *node, const aiMatrix4x4 &parentWorld, const aiScene *scene,
              g3d::Scene &scene3d, std::vector<std::string> &warnings, Expansion &expansion) {
    if (node == nullptr) {
        return;
    }
    aiMatrix4x4 world = parentWorld * node->mTransformation;
    const std::string nodeName = node->mName.length > 0 ? node->mName.C_Str() : std::string();

    for (uint32_t i = 0; i < node->mNumMeshes; ++i) {
        const uint32_t meshIndex = node->mMeshes[i];
        if (meshIndex >= scene->mNumMeshes || scene->mMeshes == nullptr) {
            continue;
        }
        const aiMesh *source = scene->mMeshes[meshIndex];
        if (source == nullptr || source->mNumVertices == 0) {
            continue;
        }

        Reference reference;
        reference.meshIndex = meshIndex;
        reference.matrix = {world.a1, world.b1, world.c1, world.d1, world.a2, world.b2,
                            world.c2, world.d2, world.a3, world.b3, world.c3, world.d3,
                            world.a4, world.b4, world.c4, world.d4};
        if (expansion.seen.count(reference) != 0) {
            ++expansion.merged;
            continue;
        }

        const size_t cost = g3d::estimateMeshBytes(source->mNumVertices, source->mNumFaces);
        // The first mesh of a file is kept no matter what it costs: assimp has already paid for it
        // by the time this loop runs, an empty scene with a warning is worse than a heavy scene the
        // user can still look at, and refusing cannot undo a gigabyte that is already allocated.
        if (!scene3d.isEmpty() && expansion.bytes + cost > gImportBudgetBytes) {
            ++expansion.skipped;
            continue;
        }
        expansion.seen.insert(reference);
        expansion.bytes += cost;
        scene3d.add(convertMesh(source, world, scene, nodeName, warnings));
    }

    for (uint32_t i = 0; i < node->mNumChildren; ++i) {
        traverse(node->mChildren[i], world, scene, scene3d, warnings, expansion);
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

    Expansion expansion;
    traverse(scene->mRootNode, aiMatrix4x4(), scene, result.scene, result.warnings, expansion);

    if (expansion.merged > 0) {
        addWarningOnce(result.warnings,
                       std::to_string(expansion.merged) +
                           " cópias exatas de malhas já importadas foram unidas");
    }
    if (expansion.skipped > 0) {
        addWarningOnce(result.warnings,
                       std::to_string(expansion.skipped) +
                           " partes ficaram de fora: a cena passaria do orçamento de memória de " +
                           std::to_string(gImportBudgetBytes / (1024u * 1024u)) +
                           " MB deste editor. Abra o arquivo no programa de origem e reduza as "
                           "instâncias antes de importar de novo.");
    }

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

void setImportBudgetBytes(size_t bytes) {
    if (bytes > 0) {
        gImportBudgetBytes = bytes;
    }
}

size_t importBudgetBytes() { return gImportBudgetBytes; }

size_t estimateMeshBytes(uint32_t vertices, uint32_t triangles) {
    return static_cast<size_t>(vertices) * kBytesPerVertex +
           static_cast<size_t>(triangles) * kBytesPerTriangle;
}

}  // namespace g3d
