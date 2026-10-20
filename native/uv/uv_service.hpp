#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/mesh.hpp"

namespace g3d {

// Subset of xatlas options that make sense to expose in the editor UI.
struct UvOptions {
    uint32_t padding = 2;           // PackOptions::padding, in texels between islands.
    uint32_t resolution = 0;        // PackOptions::resolution. 0 lets xatlas estimate a ~1024 atlas.
    float maxChartArea = 0.0f;      // ChartOptions::maxChartArea. 0 means no limit.
    float maxCost = 2.0f;           // ChartOptions::maxCost. Lower values produce more islands.
    float normalSeamWeight = 4.0f;  // ChartOptions::normalSeamWeight. > 1000 respects seams fully.
    bool bruteForce = false;        // PackOptions::bruteForce. Slower, better packing.
    bool rotateCharts = true;       // PackOptions::rotateCharts.
    bool fixWinding = false;        // ChartOptions::fixWinding.
};

struct UvResult {
    // Mesh rebuilt by xatlas: vertices are welded and duplicated along seams, so the
    // topology can grow. uvs are normalised to 0..1 of the atlas.
    Mesh mesh;
    std::vector<uint32_t> faceChartIds;  // Island (chart) per output triangle.
    uint32_t chartCount = 0;
    uint32_t atlasWidth = 0;
    uint32_t atlasHeight = 0;
    float utilization = 0.0f;

    std::string describe() const;
};

// Generates UVs into out. The input mesh is never modified, so existing UVs survive until the
// caller assigns the result. Returns false and fills error on invalid input or xatlas failure.
bool generateUv(const Mesh &input, const UvOptions &options, UvResult &out, std::string &error);

}  // namespace g3d
