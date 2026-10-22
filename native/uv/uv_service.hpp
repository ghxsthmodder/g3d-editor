#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "core/mesh.hpp"

namespace g3d {

// How far a generation has come, and the request to stop it. xatlas calls its progress function
// from its own worker threads, so every field is an atomic and the interface only ever reads what
// the library left there. The caller of generateUv owns one of these.
class UvProgress {
public:
    // Open and close a run, by generateUv. Closing is what tells the screen the number is over,
    // and opening discards an interrupt that arrived too late for the run it was meant for.
    void begin();
    void end();
    bool running() const { return running_.load(std::memory_order_relaxed) != 0; }

    // Percent for the stage under way, or -1 outside a run: a stale number would restart a
    // progress bar the interface already closed.
    int percent() const { return running() ? percent_.load(std::memory_order_relaxed) : -1; }
    // The stage last reported, by xatlas' own name, empty before the first call. It outlives the
    // run, so a generation that is over still says what it was last doing.
    std::string stage() const;

    void requestCancel() { cancel_.store(1, std::memory_order_relaxed); }
    bool cancelRequested() const { return cancel_.load(std::memory_order_relaxed) != 0; }

    // What the C callback hands over: false is the only way xatlas is told to stop.
    bool report(int category, int value);

private:
    std::atomic<int> running_{0};
    std::atomic<int> percent_{-1};
    std::atomic<int> category_{-1};
    std::atomic<int> cancel_{0};
};

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
// caller assigns the result. Returns false and fills error on invalid input, on xatlas failure, or
// when progress was asked to stop and did. The progress report is optional: without it xatlas is
// left with no callback to call.
bool generateUv(const Mesh &input, const UvOptions &options, UvResult &out, std::string &error,
                UvProgress *progress = nullptr);

}  // namespace g3d
