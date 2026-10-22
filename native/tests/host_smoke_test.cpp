// Host harness for the native core: builds and runs without a device, so the xatlas pipeline can
// be exercised on desktop. Compiled by tests/run_host_test.sh, not part of the APK.

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <thread>
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

// A sheet of faces with something to unwrap: a cube finishes in microseconds, and an interrupt
// asked of a run that is already over is not the behaviour anyone wants to prove.
g3d::Mesh wavySheet(uint32_t sides) {
    g3d::Mesh mesh;
    mesh.name = "sheet";
    const uint32_t points = sides + 1;
    mesh.positions.reserve(points * points * 3);
    mesh.normals.reserve(points * points * 3);
    for (uint32_t row = 0; row < points; ++row) {
        for (uint32_t column = 0; column < points; ++column) {
            const float x = static_cast<float>(column) / static_cast<float>(sides) - 0.5f;
            const float z = static_cast<float>(row) / static_cast<float>(sides) - 0.5f;
            const float y = 0.15f * std::sin(6.0f * x) * std::cos(6.0f * z);
            mesh.positions.insert(mesh.positions.end(), {x, y, z});
            mesh.normals.insert(mesh.normals.end(), {0.0f, 1.0f, 0.0f});
        }
    }
    for (uint32_t row = 0; row < sides; ++row) {
        for (uint32_t column = 0; column < sides; ++column) {
            const uint32_t lower = row * points + column;
            const uint32_t upper = lower + points;
            mesh.indices.insert(mesh.indices.end(), {lower, upper, lower + 1});
            mesh.indices.insert(mesh.indices.end(), {lower + 1, upper, upper + 1});
        }
    }
    return mesh;
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

    // --- progress and cancellation -------------------------------------------------------
    // xatlas stops a run only when the progress function it calls says no, so the report has to be
    // read while the run is under way. The cube is far too quick for that; this sheet is not.
    g3d::UvProgress plain;
    check(!plain.running(), "nothing is running before a generation");
    check(plain.percent() == -1, "there is no percentage to show before a run");
    check(plain.stage().empty(), "no stage has been reported yet");
    plain.begin();
    check(plain.running() && plain.percent() == -1, "an opened run has no number yet");
    check(plain.report(1, 40), "the callback accepts work while nothing was asked");
    check(plain.percent() == 40 && plain.stage() == "Computing charts",
          "the report carries the stage xatlas named");
    plain.requestCancel();
    check(!plain.report(1, 60), "asked to stop, the callback says no");
    check(plain.percent() == 60, "the last number reached stays the one on screen");
    plain.end();
    check(!plain.running() && plain.percent() == -1, "a closed run shows no number");
    check(plain.stage() == "Computing charts", "a closed run still says where it stopped");
    plain.begin();
    check(!plain.cancelRequested(),
          "opening a run discards an interrupt that arrived too late for the previous one");
    plain.end();

    g3d::UvProgress progress;
    g3d::UvResult watched;
    std::string watchedError;
    check(g3d::generateUv(cube, options, watched, watchedError, &progress),
          "generation works with the progress report attached: " + watchedError);
    check(progress.stage() == "Building output meshes",
          "every stage reaches the report, the last is " + progress.stage());
    check(!progress.running(), "a finished generation closes the report");
    check(progress.percent() == -1, "a finished generation leaves no number on screen");

    // Caught mid-run: the first thing xatlas leaves in the report is the stage it is opening, which
    // is also the first moment the screen has anything to show. Asking the interrupt there, while a
    // run of a hundred milliseconds is still at the start, is what the button on the panel does.
    const g3d::Mesh sheet = wavySheet(60);
    g3d::UvProgress live;
    g3d::UvResult interrupted;
    std::string interruptError;
    bool produced = true;
    std::thread worker([&] {
        produced = g3d::generateUv(sheet, options, interrupted, interruptError, &live);
    });
    // The wait is bounded: a run that never reports is the failure this test is looking for, not a
    // reason to hang the suite.
    for (uint32_t spin = 0; spin < 400000 && !(live.running() && !live.stage().empty()); ++spin) {
        std::this_thread::yield();
    }
    const bool reporting = live.running() && live.percent() >= 0 && !live.stage().empty();
    const std::string whereItWas = live.stage();
    live.requestCancel();
    worker.join();
    check(reporting, "a run under way has a stage and a number to show, was " + whereItWas);
    check(!produced, "an interrupt asked in the middle of a run stops it");
    check(interruptError.find("interrompida") != std::string::npos,
          "the reason says the generation was interrupted: " + interruptError);
    check(interrupted.mesh.vertexCount() == 0 && interrupted.chartCount == 0,
          "an interrupted generation hands over no mesh");
    check(!live.running() && live.percent() == -1, "the interrupted run closes the report");

    // What xatlas would have written is not in the document, so the next run starts from the file.
    g3d::UvResult afterwards;
    std::string afterwardsError;
    check(g3d::generateUv(cube, options, afterwards, afterwardsError) && afterwards.chartCount >= 1,
          "a generation after an interrupted one still works: " + afterwardsError);

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
