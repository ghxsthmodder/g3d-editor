// Host harness for the model exporters: takes the scene of a fixture, writes it with every format
// the editor offers, reads it back and compares the geometry corner by corner. The corner list is
// the multiset of (position, uv, normal) of every face corner of the scene, sorted, so it survives
// the welding and the reordering the formats are allowed to do. Built by the host CMake branch,
// not part of the APK.

#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "core/document.hpp"
#include "exporters/model_exporter.hpp"
#include "importers/assimp_importer.hpp"
#include "uv/uv_editor.hpp"
#include "uv/uv_service.hpp"

#ifndef G3D_TEST_MODEL_DIR
#define G3D_TEST_MODEL_DIR "."
#endif

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const std::string &label) {
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("FAIL  %s\n", label.c_str());
    } else {
        std::printf("ok    %s\n", label.c_str());
    }
}

std::vector<char> readAll(const std::string &path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    return std::vector<char>((std::istreambuf_iterator<char>(stream)),
                             std::istreambuf_iterator<char>());
}

std::string modelPath(const std::string &name) {
    return std::string(G3D_TEST_MODEL_DIR) + "/" + name;
}

g3d::ImportResult importFile(const std::string &name, std::string &error) {
    const std::string path = modelPath(name);
    const std::vector<char> bytes = readAll(path);
    g3d::ImportResult result;
    if (!g3d::importModelFromMemory(bytes.data(), bytes.size(), path, result, error)) {
        result.scene.clear();
    }
    return result;
}

// --- corner sets -----------------------------------------------------------------------------

struct Corner {
    float value[8] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    // 3 = position only, 5 = position + uv, 6 = position + uv + normal, 8 = position + normal.
    size_t used = 3;
};

bool cornerLess(const Corner &left, const Corner &right) {
    for (size_t i = 0; i < std::max(left.used, right.used); ++i) {
        if (left.value[i] != right.value[i]) {
            return left.value[i] < right.value[i];
        }
    }
    return false;
}

void appendCorners(const float *positions, const float *uvs, const float *normals, size_t count,
                   bool hasUvs, std::vector<Corner> &out) {
    for (size_t i = 0; i < count; ++i) {
        Corner corner;
        corner.value[0] = positions[i * 3 + 0];
        corner.value[1] = positions[i * 3 + 1];
        corner.value[2] = positions[i * 3 + 2];
        size_t used = 3;
        if (hasUvs) {
            corner.value[3] = uvs[i * 2 + 0];
            corner.value[4] = uvs[i * 2 + 1];
            used = 5;
        }
        if (normals != nullptr) {
            corner.value[used + 0] = normals[i * 3 + 0];
            corner.value[used + 1] = normals[i * 3 + 1];
            corner.value[used + 2] = normals[i * 3 + 2];
            used += 3;
        }
        corner.used = used;
        out.push_back(corner);
    }
}

std::vector<Corner> cornersOf(const g3d::Scene &scene) {
    std::vector<Corner> out;
    for (uint32_t i = 0; i < scene.meshCount(); ++i) {
        const g3d::Mesh *mesh = scene.meshAt(i);
        if (mesh == nullptr) {
            continue;
        }
        for (uint32_t face = 0; face < mesh->triangleCount(); ++face) {
            std::vector<float> positions(9);
            std::vector<float> uvs;
            std::vector<float> normals;
            const bool hasUvs = mesh->hasUvs();
            if (hasUvs) {
                uvs.resize(6);
            }
            if (mesh->hasNormals()) {
                normals.resize(9);
            }
            for (uint32_t corner = 0; corner < 3; ++corner) {
                const uint32_t index = mesh->indices[static_cast<size_t>(face) * 3 + corner];
                for (uint32_t axis = 0; axis < 3; ++axis) {
                    positions[corner * 3 + axis] =
                        mesh->positions[static_cast<size_t>(index) * 3 + axis];
                    if (!normals.empty()) {
                        normals[corner * 3 + axis] =
                            mesh->normals[static_cast<size_t>(index) * 3 + axis];
                    }
                }
                if (hasUvs) {
                    uvs[corner * 2 + 0] = mesh->uvs[static_cast<size_t>(index) * 2 + 0];
                    uvs[corner * 2 + 1] = mesh->uvs[static_cast<size_t>(index) * 2 + 1];
                }
            }
            appendCorners(positions.data(), hasUvs ? uvs.data() : nullptr,
                          normals.empty() ? nullptr : normals.data(), 3, hasUvs, out);
        }
    }
    std::sort(out.begin(), out.end(), cornerLess);
    return out;
}

// Reads a scene written by the exporter straight from disk with Assimp's own reader, which is what
// the destination tools will do. Positions are already world space in our output, so the node
// transform is identity, and UVs are taken the same way the editor's importer takes them.
std::vector<Corner> cornersFromAssimpScene(const aiScene *scene) {
    std::vector<Corner> out;
    if (scene == nullptr) {
        return out;
    }
    for (uint32_t m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh *mesh = scene->mMeshes[m];
        if (mesh == nullptr || !mesh->HasPositions() || !mesh->HasFaces()) {
            continue;
        }
        const bool hasUvs = mesh->mTextureCoords[0] != nullptr && mesh->mNumUVComponents[0] >= 2;
        for (uint32_t face = 0; face < mesh->mNumFaces; ++face) {
            const aiFace &source = mesh->mFaces[face];
            if (source.mNumIndices != 3) {
                continue;
            }
            float positions[9];
            float uvs[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
            float normals[9];
            const bool hasNormals = mesh->HasNormals();
            for (uint32_t corner = 0; corner < 3; ++corner) {
                const uint32_t index = source.mIndices[corner];
                positions[corner * 3 + 0] = mesh->mVertices[index].x;
                positions[corner * 3 + 1] = mesh->mVertices[index].y;
                positions[corner * 3 + 2] = mesh->mVertices[index].z;
                if (hasNormals) {
                    normals[corner * 3 + 0] = mesh->mNormals[index].x;
                    normals[corner * 3 + 1] = mesh->mNormals[index].y;
                    normals[corner * 3 + 2] = mesh->mNormals[index].z;
                }
                if (hasUvs) {
                    uvs[corner * 2 + 0] = mesh->mTextureCoords[0][index].x;
                    uvs[corner * 2 + 1] = mesh->mTextureCoords[0][index].y;
                }
            }
            appendCorners(positions, hasUvs ? uvs : nullptr, hasNormals ? normals : nullptr, 3,
                          hasUvs, out);
        }
    }
    std::sort(out.begin(), out.end(), cornerLess);
    return out;
}

std::string describeWorst(const std::vector<Corner> &left, const std::vector<Corner> &right,
                          float &worst) {
    worst = 0.0f;
    size_t worstCorner = 0;
    size_t worstAxis = 0;
    for (size_t i = 0; i < left.size() && i < right.size(); ++i) {
        for (size_t a = 0; a < std::max(left[i].used, right[i].used); ++a) {
            const float delta = std::abs(left[i].value[a] - right[i].value[a]);
            if (delta > worst) {
                worst = delta;
                worstCorner = i;
                worstAxis = a;
            }
        }
    }
    return "corner " + std::to_string(worstCorner) + " axis " + std::to_string(worstAxis) +
           " (orig " + std::to_string(left[worstCorner].value[worstAxis]) + ", volta " +
           std::to_string(right[worstCorner].value[worstAxis]) + ")";
}

bool sameCorners(const std::vector<Corner> &left, const std::vector<Corner> &right,
                 std::string &detail) {
    if (left.size() != right.size()) {
        detail = "contagem " + std::to_string(left.size()) + " vs " +
                 std::to_string(right.size());
        return false;
    }
    // 1e-3: OBJ writes six significant digits and a packed atlas puts seams a couple of texels
    // apart at 1024, which is 2e-3. Anything coarser than that is a real change, not rounding.
    constexpr float kTolerance = 1e-3f;
    float worst = 0.0f;
    for (size_t i = 0; i < left.size(); ++i) {
        for (size_t a = 0; a < std::max(left[i].used, right[i].used); ++a) {
            if (std::abs(left[i].value[a] - right[i].value[a]) > kTolerance) {
                detail = describeWorst(left, right, worst) + " delta " + std::to_string(worst);
                return false;
            }
        }
    }
    detail = std::to_string(left.size()) + " corners iguais";
    return true;
}

// --- writing the exported files --------------------------------------------------------------

std::string tempDirectory() {
    static std::string directory;
    if (directory.empty()) {
        char pattern[] = "/tmp/g3d-export-XXXXXX";
        const char *made = mkdtemp(pattern);
        directory = made != nullptr ? made : "/tmp";
    }
    return directory;
}

bool writeOut(const std::string &path, const std::vector<uint8_t> &bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return false;
    }
    stream.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(stream);
}

std::string textOf(const std::vector<uint8_t> &bytes) {
    return std::string(bytes.begin(), bytes.end());
}

g3d::Scene reimportFromMemory(const std::string &path, std::string &error) {
    const std::vector<char> bytes = readAll(path);
    g3d::ImportResult result;
    if (!g3d::importModelFromMemory(bytes.data(), bytes.size(), path, result, error)) {
        result.scene.clear();
    }
    return result.scene;
}

std::vector<Corner> reimportFromDisk(const std::string &path, std::string &error) {
    Assimp::Importer importer;
    const unsigned int flags = aiProcess_Triangulate | aiProcess_SortByPType |
                               aiProcess_GenNormals | aiProcess_JoinIdenticalVertices;
    const aiScene *scene = importer.ReadFile(path, flags);
    if (scene == nullptr) {
        error = importer.GetErrorString() != nullptr ? importer.GetErrorString() : "sem leitura";
        return {};
    }
    error.clear();
    return cornersFromAssimpScene(scene);
}

// Exports one scene with one format and reports how the read-back compares.
void roundTrip(const std::string &label, const g3d::Scene &source, g3d::ModelFormat format) {
    const std::vector<Corner> before = cornersOf(source);
    std::string error;
    g3d::ModelExport output;
    const bool exported =
        g3d::exportModel(source, format, std::string("saida_") + label, output, error);
    check(exported, label + ": exporta em " + g3d::modelFormatLabel(format) +
                        (exported ? "" : " -> " + error));
    if (!exported) {
        return;
    }

    const std::string directory = tempDirectory();
    std::string mainPath;
    for (size_t i = 0; i < output.files.size(); ++i) {
        const std::string path = directory + "/" + output.files[i].name;
        if (!writeOut(path, output.files[i].bytes)) {
            check(false, label + ": escreve " + output.files[i].name);
            return;
        }
        if (i == 0) {
            mainPath = path;
        }
    }
    // Reading only after every piece is on disk: the description points at its companion by name,
    // so the pair is only valid together, exactly as it will be in the folder the user picks.
    std::vector<Corner> after;
    std::string readError;
    std::string detail;
    if (format == g3d::ModelFormat::Gltf) {
        after = reimportFromDisk(mainPath, readError);
    } else {
        const g3d::Scene back = reimportFromMemory(mainPath, readError);
        check(readError.empty(), label + ": reimporta o " + g3d::modelFormatLabel(format) +
                                  (readError.empty() ? "" : " -> " + readError));
        after = cornersOf(back);
        check(back.totalTriangles() == source.totalTriangles(),
              label + ": " + std::to_string(source.totalTriangles()) +
                      " triangulos voltam, vieram " + std::to_string(back.totalTriangles()));
    }
    check(!after.empty(), label + ": a leitura devolve cantos" +
                              (readError.empty() ? "" : " -> " + readError));
    const bool cornersMatch = sameCorners(before, after, detail);
    check(cornersMatch, label + ": " + g3d::modelFormatLabel(format) +
                            " ida e volta preserva posicao, UV e normal (" + detail + ")");
    std::printf("      %s: %s\n", label.c_str(), output.describe().c_str());
}

}  // namespace

int main() {
    std::string error;

    // --- the exporter refuses an empty scene and an empty name -----------------------------
    g3d::Scene nothing;
    g3d::ModelExport emptyExport;
    error.clear();
    check(!g3d::exportModel(nothing, g3d::ModelFormat::Glb, "vazio", emptyExport, error) &&
                  !error.empty(),
          "cena vazia e recusada: " + error);

    error.clear();
    const g3d::ImportResult cube = importFile("cube.obj", error);
    check(cube.scene.meshCount() == 1, "cube.obj importa: " + error);

    error.clear();
    check(!g3d::exportModel(cube.scene, g3d::ModelFormat::Glb, "", emptyExport, error) &&
                  !error.empty(),
          "nome vazio e recusado: " + error);

    if (cube.scene.meshCount() == 1) {
        // --- the file the editor opened comes back out of the same door ----------------------
        roundTrip("cubo", cube.scene, g3d::ModelFormat::Obj);
        roundTrip("cubo", cube.scene, g3d::ModelFormat::Gltf);
        roundTrip("cubo", cube.scene, g3d::ModelFormat::Glb);

        // The two pieces of each format have to point at each other by the names we hand back.
        g3d::ModelExport objExport;
        error.clear();
        if (g3d::exportModel(cube.scene, g3d::ModelFormat::Obj, "modelo", objExport, error)) {
            check(objExport.files.size() == 2, "OBJ devolve o .obj e o .mtl");
            if (objExport.files.size() == 2) {
                check(objExport.files[0].name == "modelo.obj" &&
                          objExport.files[1].name == "modelo.mtl",
                      "nomes: " + objExport.files[0].name + " e " + objExport.files[1].name);
                check(textOf(objExport.files[0].bytes).find("mtllib modelo.mtl") !=
                          std::string::npos,
                      "o .obj referencia o .mtl pelo nome devolvido");
            }
        } else {
            check(false, "OBJ do cubo exporta: " + error);
        }

        g3d::ModelExport gltfExport;
        error.clear();
        if (g3d::exportModel(cube.scene, g3d::ModelFormat::Gltf, "modelo", gltfExport, error)) {
            check(gltfExport.files.size() == 2, "glTF devolve o .gltf e o .bin");
            if (gltfExport.files.size() == 2) {
                const std::string json = textOf(gltfExport.files[0].bytes);
                const std::string &binName = gltfExport.files[1].name;
                check(gltfExport.files[0].name == "modelo.gltf",
                      "nome principal: " + gltfExport.files[0].name);
                // Assimp gives the buffer a unique id derived from the base name, so the sidecar is
                // "modelo_0.bin" here rather than "modelo.bin". What has to hold is the reference.
                check(binName.size() > 4 && binName.compare(binName.size() - 4, 4, ".bin") == 0 &&
                          binName.compare(0, 6, "modelo") == 0,
                      "o companheiro sai ao lado do principal: " + binName);
                check(json.find(binName) != std::string::npos,
                      "o JSON referencia o .bin pelo nome devolvido: " + binName);
            }
        } else {
            check(false, "glTF do cubo exporta: " + error);
        }

        g3d::ModelExport glbExport;
        error.clear();
        check(g3d::exportModel(cube.scene, g3d::ModelFormat::Glb, "modelo", glbExport, error) &&
                      glbExport.files.size() == 1 && glbExport.files[0].name == "modelo.glb",
              "GLB e um arquivo unico: " + error);

        // --- what the format cannot hold is said, not hidden ---------------------------------
        check(!glbExport.warnings.empty(), "a exportacao avisa o que nao coube no formato");
        bool mentionsTexture = false;
        for (const std::string &warning : glbExport.warnings) {
            if (warning.find("textura") != std::string::npos) {
                mentionsTexture = true;
            }
        }
        check(mentionsTexture, "um aviso fala da textura composta");

        // The interface shows the list before the folder is picked, so it has to be the same list
        // the export itself ends up reporting.
        const std::vector<std::string> preflight =
            g3d::exportWarnings(cube.scene, g3d::ModelFormat::Glb);
        check(preflight == glbExport.warnings,
              "os avisos previos sao os da exportacao (" + std::to_string(preflight.size()) +
                  " vs " + std::to_string(glbExport.warnings.size()) + ")");
    }

    // --- several meshes stay several meshes --------------------------------------------------
    error.clear();
    const g3d::ImportResult twoObjects = importFile("two-objects.obj", error);
    check(twoObjects.scene.meshCount() == 2, "two-objects.obj importa duas malhas: " + error);
    if (twoObjects.scene.meshCount() == 2) {
        roundTrip("duas", twoObjects.scene, g3d::ModelFormat::Glb);
        const g3d::Scene back =
            reimportFromMemory(tempDirectory() + "/saida_duas.glb", error);
        check(back.meshCount() == 2,
              "as duas malhas continuam separadas no GLB, voltaram " +
                  std::to_string(back.meshCount()));
    }

    // --- the UV layout edited in this session is what leaves the app -------------------------
    // The whole flow of the editor in one line: open the file, unwrap a mesh that came in without
    // UVs, apply it, export. The read-back has to carry the layout that was just made.
    error.clear();
    const std::vector<char> boxBytes = readAll(modelPath("Box.glb"));
    g3d::Document &document = g3d::Document::instance();
    check(document.importModel(boxBytes.data(), boxBytes.size(), modelPath("Box.glb"), error),
          "Box.glb abre no documento: " + error);
    if (document.meshCount() >= 1) {
        g3d::UvEditor &editor = g3d::UvEditor::instance();
        g3d::UvOptions generateOptions;
        generateOptions.padding = 2;
        std::string editorError;
        check(editor.generate(0, generateOptions, editorError),
              "gerar UVs pela interface: " + editorError);
        check(editor.apply(editorError), "aplicar o UV gerado na cena: " + editorError);
        editor.cancel();

        g3d::Scene unwrapped;
        document.withScene([&unwrapped](g3d::Scene &scene) { unwrapped = scene; });
        const bool hasUvs = unwrapped.meshAt(0) != nullptr && unwrapped.meshAt(0)->hasUvs();
        check(hasUvs, "a cena do documento ficou com o UV gerado");
        if (hasUvs) {
            roundTrip("uv novo", unwrapped, g3d::ModelFormat::Glb);
            roundTrip("uv novo", unwrapped, g3d::ModelFormat::Obj);
        }
    }

    std::printf("\n%d verificacoes, %d falhas\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
