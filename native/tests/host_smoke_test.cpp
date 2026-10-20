// Host harness for the native core: builds and runs without a device, so the xatlas pipeline can
// be exercised on desktop. Compiled by tests/run_host_test.sh, not part of the APK.

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "core/primitives.hpp"
#include "uv/uv_service.hpp"

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

void checkUvsNormalised(const g3d::UvResult &result, const std::string &label) {
    bool inside = !result.mesh.uvs.empty();
    for (const float value : result.mesh.uvs) {
        if (!std::isfinite(value) || value < -1e-5f || value > 1.0f + 1e-5f) {
            inside = false;
        }
    }
    check(inside, label);
}

}  // namespace

int main() {
    const g3d::Mesh cube = g3d::makeCube(0.5f);

    std::string error;
    check(cube.validate(error), "cube passes validation");
    check(cube.vertexCount() == 24, "cube has 24 vertices");
    check(cube.triangleCount() == 12, "cube has 12 triangles");
    check(!cube.hasUvs(), "cube starts without UVs");

    // A second mesh keeps its authored UVs so we can prove generation does not touch the input.
    g3d::Mesh withExistingUvs = cube;
    withExistingUvs.uvs.assign(withExistingUvs.vertexCount() * 2, 0.25f);
    const std::vector<float> before = withExistingUvs.uvs;

    g3d::UvOptions options;
    options.padding = 4;
    g3d::UvResult result;
    const bool generated = g3d::generateUv(withExistingUvs, options, result, error);
    check(generated, "xatlas generates UVs for the cube: " + error);

    if (generated) {
        std::printf("      %s\n", result.describe().c_str());
        check(result.chartCount >= 1, "at least one island was created");
        check(result.atlasWidth > 0 && result.atlasHeight > 0, "atlas dimensions are known");
        check(result.mesh.vertexCount() >= cube.vertexCount(), "seams may duplicate vertices");
        check(result.mesh.triangleCount() == cube.triangleCount(), "triangle count is preserved");
        check(result.mesh.validate(error), "generated mesh passes validation: " + error);
        checkUvsNormalised(result, "generated UVs are normalised to 0..1");

        uint32_t unassigned = 0;
        for (const uint32_t chartId : result.faceChartIds) {
            if (chartId == std::numeric_limits<uint32_t>::max()) {
                ++unassigned;
            }
        }
        check(unassigned == 0, "every triangle belongs to an island");
        check(result.faceChartIds.size() == result.mesh.triangleCount(),
              "one island id per triangle");

        // Positions come from the source vertices through xatlas xref.
        bool positionsMatch = true;
        for (size_t i = 0; i < result.mesh.positions.size(); ++i) {
            if (std::fabs(result.mesh.positions[i]) > 0.5001f) {
                positionsMatch = false;
            }
        }
        check(positionsMatch, "rebuilt positions stay inside the cube bounds");

        float minimum[3];
        float maximum[3];
        check(g3d::computeBounds(result.mesh, minimum, maximum) &&
                  std::fabs(minimum[0] + 0.5f) < 1e-5f && std::fabs(maximum[2] - 0.5f) < 1e-5f,
              "bounds of the rebuilt mesh match the cube");
    }

    check(withExistingUvs.uvs == before, "input UVs are untouched by generation");

    // Rejected inputs must not crash and must report why.
    g3d::Mesh broken = cube;
    broken.indices.push_back(broken.vertexCount());
    g3d::UvResult unused;
    std::string brokenError;
    check(!g3d::generateUv(broken, options, unused, brokenError) && !brokenError.empty(),
          "out-of-range indices are rejected: " + brokenError);

    g3d::Mesh empty;
    std::string emptyError;
    check(!g3d::generateUv(empty, options, unused, emptyError) && !emptyError.empty(),
          "an empty mesh is rejected: " + emptyError);

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
