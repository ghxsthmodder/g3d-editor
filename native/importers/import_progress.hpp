#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace g3d {

// Which part of an import the report is inside of. The names are tokens the interface turns into
// its own words, so the core never names a step in the language of the screen.
enum ImportPhase : int {
    kImportPhaseNone = 0,
    kImportPhaseRead = 1,       // assimp is turning the bytes of the file into its own scene
    kImportPhasePost = 2,       // the post-processing steps assimp runs over it
    kImportPhaseAssemble = 3,   // our own walk, which copies the scene into the editor's
};

// How far an import has come. Every number here comes from a counter that exists: assimp reports
// the bytes it has read and the post-processing step it is on, and the walk counts the nodes it
// has visited. The percentage is of the phase under way, the same way a UV generation reports the
// stage it is inside of, because the three phases have no honest weight between them.
//
// There is deliberately no cancel here. The version of assimp in third_party/ calls its progress
// handler and throws the answer away — no caller checks the return value of Update(), and the
// library has no ShouldContinue — so a stop request would land on deaf ears while the largest part
// of the wait is inside the file reader.
class ImportProgress {
public:
    // Open and close a run. Outside one nothing is published, so a finished import cannot leave a
    // number moving on the screen.
    void begin();
    void end();
    bool running() const { return running_.load(std::memory_order_relaxed) != 0; }
    int percent() const { return running() ? percent_.load(std::memory_order_relaxed) : -1; }
    // The phase last reported, as a token, empty before the first one. It outlives the run, so an
    // import that is over still says what it was last doing.
    std::string phaseToken() const;

    // Counted progress: total 0 means the source has no estimate, and the number goes with it.
    void update(ImportPhase phase, uint32_t done, uint32_t total);
    // The fraction assimp hands over when it reports a number on its own, -1 for no estimate.
    void updateFraction(ImportPhase phase, float fraction);

    // What a run passed through, kept after it ends: the way to check the handler was really wired
    // without racing a run from another thread. One bit per phase, so the value of the phase is not
    // the mask that records it.
    bool saw(ImportPhase phase) const {
        return (phases_.load(std::memory_order_relaxed) & (1 << phase)) != 0;
    }
    // Highest percentage any phase reached, -1 when no number was ever given.
    int peakPercent() const { return peak_.load(std::memory_order_relaxed); }

private:
    std::atomic<int> running_{0};
    std::atomic<int> percent_{-1};
    std::atomic<int> phase_{kImportPhaseNone};
    std::atomic<int> phases_{0};
    std::atomic<int> peak_{-1};
};

// The report of the import under way. One import runs at a time, as with the memory budget, so the
// screen can ask it what the worker thread has reached.
ImportProgress &importProgress();

}  // namespace g3d
