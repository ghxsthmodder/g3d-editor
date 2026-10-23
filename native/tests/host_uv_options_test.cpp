// Host test for the xatlas options the UV panel sets. The generation itself is covered by
// host_smoke_test; what is checked here is that each rule the panel offers reaches the library and
// changes what comes out, because an option that is wired to nothing still shows a value on screen.
// Compiled by tests/run_host_test.sh, not part of the APK.

#include <cmath>
#include <cstdio>
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

// A faceted tube: every quad carries its own four vertices and its own flat normal, which is what a
// hard-edged import looks like to xatlas. It is the geometry the seam weight has to have something
// to respect, and 48 faces keep the run in milliseconds.
g3d::Mesh facetedTube(uint32_t segments) {
    g3d::Mesh mesh;
    mesh.name = "tubo";
    const float step = 2.0f * 3.14159265f / static_cast<float>(segments);
    for (uint32_t segment = 0; segment < segments; ++segment) {
        const float first = step * static_cast<float>(segment);
        const float second = first + step;
        const uint32_t base = mesh.vertexCount();
        mesh.positions.insert(mesh.positions.end(),
                              {0.5f * std::cos(first), -0.5f, 0.5f * std::sin(first),
                               0.5f * std::cos(second), -0.5f, 0.5f * std::sin(second),
                               0.5f * std::cos(second), 0.5f, 0.5f * std::sin(second),
                               0.5f * std::cos(first), 0.5f, 0.5f * std::sin(first)});
        const float middle = first + 0.5f * step;
        for (uint32_t corner = 0; corner < 4; ++corner) {
            mesh.normals.insert(mesh.normals.end(),
                                {std::cos(middle), 0.0f, std::sin(middle)});
        }
        mesh.indices.insert(mesh.indices.end(),
                            {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return mesh;
}

// The same cube with one triangle in three wound the other way: the charts that come out of xatlas
// are then wound backwards over the atlas, and fixing the orientation is what the switch is for.
g3d::Mesh badlyWoundCube() {
    g3d::Mesh cube = g3d::makeCube(0.5f);
    for (uint32_t triangle = 0; triangle < cube.triangleCount(); triangle += 3) {
        std::swap(cube.indices[triangle * 3 + 1], cube.indices[triangle * 3 + 2]);
    }
    return cube;
}

// Faces whose UV triangle runs the other way round than the majority of a clean generation does:
// a texture painted over one of them comes out mirrored.
uint32_t backwardsFaces(const g3d::UvResult &result) {
    uint32_t backwards = 0;
    for (uint32_t face = 0; face < result.mesh.triangleCount(); ++face) {
        const uint32_t *corner = &result.mesh.indices[face * 3];
        const float *a = &result.mesh.uvs[corner[0] * 2];
        const float *b = &result.mesh.uvs[corner[1] * 2];
        const float *c = &result.mesh.uvs[corner[2] * 2];
        const float cross = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
        if (cross < 0.0f) {
            ++backwards;
        }
    }
    return backwards;
}

// One generation, and the invariants every option set has to leave behind: an option may change how
// many islands come out and how they are placed, never whether the atlas is a usable one.
bool generate(const g3d::Mesh &mesh, const g3d::UvOptions &options, const std::string &label,
              g3d::UvResult &out) {
    std::string error;
    if (!g3d::generateUv(mesh, options, out, error)) {
        check(false, label + " falhou: " + error);
        return false;
    }
    bool normalised = !out.mesh.uvs.empty();
    for (const float value : out.mesh.uvs) {
        if (!std::isfinite(value) || value < -1e-5f || value > 1.0f + 1e-5f) {
            normalised = false;
        }
    }
    std::string validationError;
    check(out.mesh.validate(validationError), label + " devolve uma malha válida: " +
                                                   validationError);
    check(normalised, label + " devolve UVs dentro do atlas");
    check(out.chartCount >= 1 && out.faceChartIds.size() == out.mesh.triangleCount(),
          label + " dá uma ilha por triângulo");
    std::printf("      %-26s ilhas=%-5u ocupado=%.4f atlas=%ux%u\n", label.c_str(),
                out.chartCount, out.utilization, out.atlasWidth, out.atlasHeight);
    return true;
}

g3d::UvOptions padded() {
    g3d::UvOptions options;
    options.padding = 4;
    return options;
}

}  // namespace

int main() {
    const g3d::Mesh tube = facetedTube(24);
    check(tube.triangleCount() == 48 && tube.vertexCount() == 96,
          "o tubo de teste tem 48 faces e 96 vértices");

    // --- the base run, the numbers every comparison below is measured against ------------
    g3d::UvOptions base = padded();
    g3d::UvResult defaultRun;
    if (!generate(tube, base, "padrão", defaultRun)) {
        std::printf("\n%d verificações, %d falhas\n", checks, failures);
        return 1;
    }
    const uint32_t defaultCharts = defaultRun.chartCount;
    const float defaultUtilization = defaultRun.utilization;
    check(defaultCharts > 1 && defaultCharts < tube.triangleCount() / 2,
          "o padrão agrupa as facetas em poucas ilhas, ilhas=" + std::to_string(defaultCharts));

    // --- the rules that decide how many islands the mesh is cut into ---------------------
    g3d::UvOptions ignoredSeams = base;
    ignoredSeams.normalSeamWeight = 0.0f;
    g3d::UvResult ignored;
    if (generate(tube, ignoredSeams, "peso de costura 0", ignored)) {
        check(ignored.chartCount <= defaultCharts,
              "ignorar as costuras não corta a malha em mais ilhas, ilhas=" +
                  std::to_string(ignored.chartCount));
    }

    g3d::UvOptions respectedSeams = base;
    respectedSeams.normalSeamWeight = 2000.0f;
    g3d::UvResult respected;
    if (generate(tube, respectedSeams, "peso de costura 2000", respected)) {
        check(respected.chartCount > defaultCharts,
              "acima de 1000 a malha é cortada em cada faceta, ilhas=" +
                  std::to_string(respected.chartCount));
    }

    g3d::UvOptions cheap = base;
    uint32_t previousCharts = 0;
    bool costSplits = true;
    for (const float maxCost : {2.0f, 0.5f, 0.05f}) {
        cheap.maxCost = maxCost;
        g3d::UvResult grown;
        const std::string label = "custo máximo " + std::to_string(maxCost);
        if (!generate(tube, cheap, label, grown)) {
            costSplits = false;
            break;
        }
        if (grown.chartCount < previousCharts) {
            costSplits = false;
        }
        previousCharts = grown.chartCount;
    }
    check(costSplits && previousCharts > defaultCharts,
          "custo menor corta a malha em mais ilhas, de " + std::to_string(defaultCharts) +
              " para " + std::to_string(previousCharts));

    g3d::UvOptions limited = base;
    limited.maxChartArea = 0.02f;
    g3d::UvResult split;
    if (generate(tube, limited, "área máxima 0,02", split)) {
        check(split.chartCount > defaultCharts,
              "limitar a área de uma ilha divide as faces grandes, ilhas=" +
                  std::to_string(split.chartCount));
    }

    // --- the switches that decide how the islands are placed -----------------------------
    // Neither of these cuts the mesh, so the island count is the firm part of what they do. How
    // much of the atlas ends up covered is not: xatlas packs on several threads, and the same
    // option set on this geometry measured 0.79 and 0.81 in two builds of the same code. Only the
    // direction the library promises is checked, where the opposite would mean it was ignored.
    g3d::UvOptions exhaustive = base;
    exhaustive.bruteForce = true;
    g3d::UvResult packed;
    if (generate(tube, exhaustive, "busca exaustiva", packed)) {
        check(packed.chartCount == defaultCharts,
              "a busca exaustiva só reorganiza, não recorta, ilhas=" +
                  std::to_string(packed.chartCount));
        check(packed.utilization >= defaultUtilization,
              "procurar mais combinações não deixa o atlas pior aproveitado, de " +
                  std::to_string(defaultUtilization) + " para " +
                  std::to_string(packed.utilization));
    }

    g3d::UvOptions fixedRotation = base;
    fixedRotation.rotateCharts = false;
    g3d::UvResult rotated;
    if (generate(tube, fixedRotation, "girar ilhas desligado", rotated)) {
        check(rotated.chartCount == defaultCharts,
              "não girar as ilhas também não recorta, ilhas=" + std::to_string(rotated.chartCount));
    }

    // --- the switch that turns islands the right way round -------------------------------
    const g3d::Mesh badlyWound = badlyWoundCube();
    g3d::UvResult backwardsRun;
    g3d::UvOptions corrected = base;
    corrected.fixWinding = true;
    g3d::UvResult fixedRun;
    if (generate(badlyWound, base, "orientação invertida, sem corrigir", backwardsRun) &&
        generate(badlyWound, corrected, "orientação invertida, corrigindo", fixedRun)) {
        const uint32_t backwards = backwardsFaces(backwardsRun);
        const uint32_t repaired = backwardsFaces(fixedRun);
        check(backwards * 2 > backwardsRun.mesh.triangleCount(),
              "sem corrigir, a maioria das faces sai ao contrário, ao contrário=" +
                  std::to_string(backwards));
        check(repaired < backwards,
              "corrigir inverte as ilhas que estavam ao contrário, ao contrário=" +
                  std::to_string(repaired));
    }

    std::printf("\n%d verificações, %d falhas\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
