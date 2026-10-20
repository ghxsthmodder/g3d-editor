#include "uv/uv_service.hpp"

#include <cmath>
#include <limits>

#include "core/primitives.hpp"
#include "xatlas.h"

namespace g3d {

std::string UvResult::describe() const {
    return "islands=" + std::to_string(chartCount) +
           " atlas=" + std::to_string(atlasWidth) + "x" + std::to_string(atlasHeight) +
           " utilization=" + std::to_string(utilization * 100.0f) + "% " + describeMesh(mesh);
}

bool generateUv(const Mesh &input, const UvOptions &options, UvResult &out, std::string &error) {
    std::string reason;
    if (!input.validate(reason)) {
        error = "invalid mesh: " + reason;
        return false;
    }

    out.mesh.clear();
    out.faceChartIds.clear();
    out.chartCount = 0;
    out.atlasWidth = 0;
    out.atlasHeight = 0;
    out.utilization = 0.0f;

    xatlas::Atlas *atlas = xatlas::Create();
    if (atlas == nullptr) {
        error = "xatlas: out of memory creating the atlas";
        return false;
    }

    xatlas::MeshDecl decl;
    decl.vertexCount = input.vertexCount();
    decl.vertexPositionData = input.positions.data();
    decl.vertexPositionStride = sizeof(float) * 3;
    if (input.hasNormals()) {
        decl.vertexNormalData = input.normals.data();
        decl.vertexNormalStride = sizeof(float) * 3;
    }
    if (input.hasUvs()) {
        // Passed as a hint to the chart generator; existing UVs stay untouched in the input mesh.
        decl.vertexUvData = input.uvs.data();
        decl.vertexUvStride = sizeof(float) * 2;
    }
    decl.indexCount = static_cast<uint32_t>(input.indices.size());
    decl.indexData = input.indices.data();
    decl.indexFormat = xatlas::IndexFormat::UInt32;

    const xatlas::AddMeshError addError = xatlas::AddMesh(atlas, decl, 1);
    if (addError != xatlas::AddMeshError::Success) {
        error = std::string("xatlas: AddMesh failed: ") + xatlas::StringForEnum(addError);
        xatlas::Destroy(atlas);
        return false;
    }

    xatlas::ChartOptions chartOptions;
    chartOptions.maxChartArea = options.maxChartArea;
    chartOptions.maxCost = options.maxCost;
    chartOptions.normalSeamWeight = options.normalSeamWeight;
    chartOptions.fixWinding = options.fixWinding;

    xatlas::PackOptions packOptions;
    packOptions.padding = options.padding;
    packOptions.resolution = options.resolution;
    packOptions.bruteForce = options.bruteForce;
    packOptions.rotateCharts = options.rotateCharts;

    xatlas::Generate(atlas, chartOptions, packOptions);

    const bool succeeded = atlas->meshCount > 0 && atlas->meshes[0].vertexCount > 0 &&
                           atlas->meshes[0].indexCount > 0;
    if (!succeeded) {
        error = "xatlas: no output mesh was produced";
        xatlas::Destroy(atlas);
        return false;
    }

    const xatlas::Mesh &output = atlas->meshes[0];
    const float atlasWidth = atlas->width > 0 ? static_cast<float>(atlas->width) : 1.0f;
    const float atlasHeight = atlas->height > 0 ? static_cast<float>(atlas->height) : 1.0f;

    out.mesh.name = input.name;
    out.mesh.positions.resize(output.vertexCount * 3);
    out.mesh.normals.resize(output.vertexCount * 3);
    out.mesh.uvs.resize(output.vertexCount * 2);

    for (uint32_t i = 0; i < output.vertexCount; ++i) {
        const xatlas::Vertex &vertex = output.vertexArray[i];
        const uint32_t source = vertex.xref;
        if (source >= input.vertexCount()) {
            error = "xatlas: vertex xref " + std::to_string(source) + " out of range";
            xatlas::Destroy(atlas);
            out.mesh.clear();
            return false;
        }
        for (uint32_t axis = 0; axis < 3; ++axis) {
            out.mesh.positions[i * 3 + axis] = input.positions[source * 3 + axis];
            out.mesh.normals[i * 3 + axis] =
                input.normals.empty() ? 0.0f : input.normals[source * 3 + axis];
        }
        out.mesh.uvs[i * 2 + 0] = vertex.uv[0] / atlasWidth;
        out.mesh.uvs[i * 2 + 1] = vertex.uv[1] / atlasHeight;
    }

    out.mesh.indices.assign(output.indexArray, output.indexArray + output.indexCount);

    const uint32_t faceCount = output.indexCount / 3;
    out.faceChartIds.assign(faceCount, std::numeric_limits<uint32_t>::max());
    for (uint32_t chart = 0; chart < output.chartCount; ++chart) {
        const xatlas::Chart &island = output.chartArray[chart];
        for (uint32_t f = 0; f < island.faceCount; ++f) {
            const uint32_t face = island.faceArray[f];
            if (face < faceCount) {
                out.faceChartIds[face] = chart;
            }
        }
    }

    out.chartCount = output.chartCount;
    out.atlasWidth = atlas->width;
    out.atlasHeight = atlas->height;
    if (atlas->utilization != nullptr && atlas->atlasCount > 0) {
        out.utilization = atlas->utilization[0];
    }

    xatlas::Destroy(atlas);
    return true;
}

}  // namespace g3d
