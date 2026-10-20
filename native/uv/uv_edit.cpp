#include "uv/uv_edit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numeric>
#include <unordered_set>

namespace g3d {
namespace {

constexpr float kUvEpsilon = 1e-5f;

bool pointInTriangle(float px, float py, float ax, float ay, float bx, float by, float cx,
                     float cy) {
    // Barycentric weights written so that both windings come out positive: UV triangles from
    // either generator or file can be wound either way.
    const float denominator = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
    if (std::fabs(denominator) < 1e-12f) {
        return false;  // Degenerate in UV space: it covers no area, so it cannot be hit.
    }
    const float inverse = 1.0f / denominator;
    const float weightA = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) * inverse;
    const float weightB = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) * inverse;
    return weightA >= -kUvEpsilon && weightB >= -kUvEpsilon &&
           weightA + weightB <= 1.0f + kUvEpsilon;
}

void mergeBounds(const float min[2], const float max[2], float outMin[2], float outMax[2]) {
    for (uint32_t axis = 0; axis < 2; ++axis) {
        outMin[axis] = std::min(outMin[axis], min[axis]);
        outMax[axis] = std::max(outMax[axis], max[axis]);
    }
}

// A model can flag hundreds of islands, and no panel shows a list that long. The count is what the
// user acts on, so only the first ids are named and the rest is added up.
std::string islandIds(const std::vector<uint32_t> &ids) {
    constexpr size_t kShown = 6;
    std::string text;
    for (size_t i = 0; i < ids.size() && i < kShown; ++i) {
        text += (i ? "," : "") + std::to_string(ids[i]);
    }
    if (ids.size() > kShown) {
        text += " +" + std::to_string(ids.size() - kShown);
    }
    return text;
}

std::string percent(float fraction) {
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%.1f", fraction * 100.0f);
    return buffer;
}

}  // namespace

std::string UvEditReport::describe() const {
    std::string text = "selecionadas=" + std::to_string(selectionCount) +
                       " area_atlas=" + percent(coveredArea) + "%";
    if (clean()) {
        return text + " sem problemas";
    }
    if (!outsideAtlas.empty()) {
        text += " fora_do_atlas=" + std::to_string(outsideAtlas.size()) + " (" +
                islandIds(outsideAtlas) + ")";
    }
    if (!overlapping.empty()) {
        text += " sobrepostas=" + std::to_string(overlapping.size()) + " (" +
                islandIds(overlapping) + ")";
    }
    return text;
}

bool UvSession::load(const Mesh &source, std::string &error) {
    clear();
    if (!source.hasUvs()) {
        error = "a malha nao tem UVs para editar";
        return false;
    }
    std::string reason;
    if (!source.validate(reason)) {
        error = "malha invalida: " + reason;
        return false;
    }
    if (!computeIslands(source, islands_, reason)) {
        error = reason;
        return false;
    }
    working_ = source;
    loadedUvs_ = working_.uvs;
    return true;
}

void UvSession::clear() {
    working_.clear();
    loadedUvs_.clear();
    islands_.clear();
    selected_.clear();
}

void UvSession::setAtlasSize(uint32_t width, uint32_t height) {
    atlasWidth_ = width > 0 ? width : 1024u;
    atlasHeight_ = height > 0 ? height : 1024u;
}

bool UvSession::isSelected(uint32_t island) const {
    return std::find(selected_.begin(), selected_.end(), island) != selected_.end();
}

bool UvSession::hasChanges() const {
    return isLoaded() && working_.uvs != loadedUvs_;
}

bool UvSession::selectIsland(uint32_t island, bool additive) {
    if (island >= islands_.count()) {
        return false;
    }
    if (!additive) {
        selected_.clear();
    }
    if (isSelected(island)) {
        selected_.erase(std::remove(selected_.begin(), selected_.end(), island), selected_.end());
        return true;
    }
    selected_.push_back(island);
    std::sort(selected_.begin(), selected_.end());
    return true;
}

void UvSession::selectAll() {
    selected_.resize(islands_.count());
    std::iota(selected_.begin(), selected_.end(), 0u);
}

void UvSession::clearSelection() { selected_.clear(); }

int UvSession::pick(float u, float v) const {
    if (!isLoaded()) {
        return -1;
    }
    // Selected islands are tested first: when two islands cover the same spot, tapping again has
    // to keep the one already chosen instead of jumping to the one underneath.
    for (uint32_t pass = 0; pass < 2; ++pass) {
        for (uint32_t face = 0; face < working_.triangleCount(); ++face) {
            const uint32_t island = islands_.faceIsland[face];
            if (isSelected(island) != (pass == 0)) {
                continue;
            }
            const uint32_t *corner = &working_.indices[face * 3];
            const float *a = &working_.uvs[corner[0] * 2];
            const float *b = &working_.uvs[corner[1] * 2];
            const float *c = &working_.uvs[corner[2] * 2];
            if (pointInTriangle(u, v, a[0], a[1], b[0], b[1], c[0], c[1])) {
                return static_cast<int>(island);
            }
        }
    }
    return -1;
}

bool UvSession::requireSelection(const char *operation, std::string &error) const {
    if (selected_.empty()) {
        error = std::string(operation) + " precisa de pelo menos uma ilha selecionada";
        return false;
    }
    return true;
}

// Every vertex touched by the selection moves as one, so an island never tears itself apart.
void UvSession::transformSelection(float angleRadians, float scale, float pivotU, float pivotV,
                                   float du, float dv) {
    std::vector<uint8_t> marked(working_.vertexCount(), 0);
    for (const uint32_t island : selected_) {
        const UvIsland *group = islands_.island(island);
        if (group == nullptr) {
            continue;
        }
        for (uint32_t slot = group->firstFace; slot < group->firstFace + group->faceCount; ++slot) {
            const uint32_t face = islands_.faceOrder[slot];
            for (uint32_t corner = 0; corner < 3; ++corner) {
                marked[working_.indices[face * 3 + corner]] = 1;
            }
        }
    }

    const float cosine = std::cos(angleRadians) * scale;
    const float sine = std::sin(angleRadians) * scale;
    for (size_t vertex = 0; vertex < marked.size(); ++vertex) {
        if (!marked[vertex]) {
            continue;
        }
        const float x = working_.uvs[vertex * 2 + 0] - pivotU;
        const float y = working_.uvs[vertex * 2 + 1] - pivotV;
        working_.uvs[vertex * 2 + 0] = pivotU + (x * cosine - y * sine) + du;
        working_.uvs[vertex * 2 + 1] = pivotV + (x * sine + y * cosine) + dv;
    }
}

namespace {

// Island boxes are stored with the layout, so transforms have to refresh them.
void refreshBounds(const Mesh &mesh, UvIslands &islands) {
    for (UvIsland &island : islands.islands) {
        float minimum[2] = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        float maximum[2] = {std::numeric_limits<float>::lowest(),
                            std::numeric_limits<float>::lowest()};
        float area = 0.0f;
        for (uint32_t slot = island.firstFace; slot < island.firstFace + island.faceCount; ++slot) {
            const uint32_t face = islands.faceOrder[slot];
            const uint32_t *corner = &mesh.indices[face * 3];
            const float *a = &mesh.uvs[corner[0] * 2];
            const float *b = &mesh.uvs[corner[1] * 2];
            const float *c = &mesh.uvs[corner[2] * 2];
            area += 0.5f * std::fabs((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]));
            const float *points[3] = {a, b, c};
            for (const float *point : points) {
                mergeBounds(point, point, minimum, maximum);
            }
        }
        island.boundsMin[0] = minimum[0];
        island.boundsMin[1] = minimum[1];
        island.boundsMax[0] = maximum[0];
        island.boundsMax[1] = maximum[1];
        island.area = area;
    }
}

}  // namespace

bool UvSession::translate(float du, float dv) {
    std::string error;
    if (!requireSelection("mover", error)) {
        return false;
    }
    transformSelection(0.0f, 1.0f, 0.0f, 0.0f, du, dv);
    refreshBounds(working_, islands_);
    return true;
}

bool UvSession::rotate(float angleRadians, float pivotU, float pivotV) {
    std::string error;
    if (!requireSelection("girar", error) || angleRadians == 0.0f) {
        return false;
    }
    transformSelection(angleRadians, 1.0f, pivotU, pivotV, 0.0f, 0.0f);
    refreshBounds(working_, islands_);
    return true;
}

bool UvSession::scale(float factor, float pivotU, float pivotV) {
    std::string error;
    if (!requireSelection("escalar", error) || factor <= 0.0f) {
        return false;
    }
    transformSelection(0.0f, factor, pivotU, pivotV, 0.0f, 0.0f);
    refreshBounds(working_, islands_);
    return true;
}

bool UvSession::selectionBounds(float out[4]) const {
    if (selected_.empty()) {
        return false;
    }
    float minimum[2] = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    float maximum[2] = {std::numeric_limits<float>::lowest(),
                        std::numeric_limits<float>::lowest()};
    for (const uint32_t island : selected_) {
        const UvIsland *group = islands_.island(island);
        if (group == nullptr) {
            continue;
        }
        mergeBounds(group->boundsMin, group->boundsMax, minimum, maximum);
    }
    out[0] = minimum[0];
    out[1] = minimum[1];
    out[2] = maximum[0];
    out[3] = maximum[1];
    return true;
}

bool UvSession::fitSelectionToAtlas() {
    std::string error;
    if (!requireSelection("ajustar", error)) {
        return false;
    }
    float box[4];
    if (!selectionBounds(box)) {
        return false;
    }
    const float width = box[2] - box[0];
    const float height = box[3] - box[1];
    if (width > 1.0f + kUvEpsilon || height > 1.0f + kUvEpsilon) {
        const float factor = 0.98f / std::max(width, height);
        scale(factor, (box[0] + box[2]) * 0.5f, (box[1] + box[3]) * 0.5f);
        if (!selectionBounds(box)) {
            return false;
        }
    }
    // Nudge it back inside along whichever axis is furthest out; the other axis is left alone.
    const float du = box[0] < 0.0f ? -box[0] : (box[2] > 1.0f ? 1.0f - box[2] : 0.0f);
    const float dv = box[1] < 0.0f ? -box[1] : (box[3] > 1.0f ? 1.0f - box[3] : 0.0f);
    if (du != 0.0f || dv != 0.0f) {
        translate(du, dv);
    }
    return true;
}

bool UvSession::repack(uint32_t paddingTexels) {
    if (!isLoaded()) {
        return false;
    }
    const uint32_t count = islands_.count();
    if (count == 0) {
        return false;
    }

    struct Slot {
        uint32_t island;
        float width;
        float height;
        float x;
        float y;
    };
    std::vector<Slot> slots(count);
    for (uint32_t index = 0; index < count; ++index) {
        const UvIsland *group = islands_.island(index);
        slots[index].island = index;
        slots[index].width = group->boundsMax[0] - group->boundsMin[0];
        slots[index].height = group->boundsMax[1] - group->boundsMin[1];
    }
    // Tallest first keeps the shelves few and the packing tight.
    std::sort(slots.begin(), slots.end(), [](const Slot &left, const Slot &right) {
        if (left.height != right.height) {
            return left.height > right.height;
        }
        return left.width > right.width;
    });

    const float baseGapU = paddingTexels > 0 ? static_cast<float>(paddingTexels) /
                                                   static_cast<float>(atlasWidth_)
                                             : 0.0f;
    const float baseGapV = paddingTexels > 0 ? static_cast<float>(paddingTexels) /
                                                   static_cast<float>(atlasHeight_)
                                             : 0.0f;

    // Lays the shelves out at the given scale and reports how tall the stack came out, so the
    // caller can shrink the whole layout instead of letting the last islands fall off the bottom.
    const auto shelfPack = [&](float layoutScale) {
        const float gapU = baseGapU * layoutScale;
        const float gapV = baseGapV * layoutScale;
        float cursorX = gapU;
        float cursorY = gapV;
        float shelfHeight = 0.0f;
        float usedHeight = 0.0f;
        for (Slot &slot : slots) {
            const float width = slot.width * layoutScale;
            const float height = slot.height * layoutScale;
            if (cursorX + width + gapU > 1.0f && cursorX > gapU) {
                cursorX = gapU;
                cursorY += shelfHeight + gapV;
                shelfHeight = 0.0f;
            }
            slot.x = cursorX;
            slot.y = cursorY;
            cursorX += width + gapU;
            shelfHeight = std::max(shelfHeight, height);
            usedHeight = std::max(usedHeight, cursorY + height + gapV);
        }
        return usedHeight;
    };

    float layoutScale = 1.0f;
    float usedHeight = shelfPack(layoutScale);
    for (uint32_t attempt = 0; attempt < 6 && usedHeight > 1.0f; ++attempt) {
        layoutScale /= usedHeight;
        usedHeight = shelfPack(layoutScale);
    }

    const std::vector<uint32_t> previousSelection = selected_;
    for (const Slot &slot : slots) {
        const UvIsland *group = islands_.island(slot.island);
        selected_.assign(1, slot.island);
        if (layoutScale != 1.0f) {
            scale(layoutScale, group->boundsMin[0], group->boundsMin[1]);
        }
        float box[4];
        if (!selectionBounds(box)) {
            continue;
        }
        translate(slot.x - box[0], slot.y - box[1]);
    }
    selected_ = previousSelection;
    refreshBounds(working_, islands_);
    return true;
}

UvEditReport UvSession::validate() const {
    UvEditReport report;
    if (!isLoaded()) {
        return report;
    }
    report.selectionCount = static_cast<uint32_t>(selected_.size());

    constexpr int grid = 64;
    std::vector<uint32_t> owner(grid * grid, kNoIsland);
    std::unordered_set<uint32_t> overlapping;
    uint32_t coveredCells = 0;

    for (uint32_t index = 0; index < islands_.count(); ++index) {
        const UvIsland *group = islands_.island(index);
        if (group->boundsMin[0] < -kUvEpsilon || group->boundsMin[1] < -kUvEpsilon ||
            group->boundsMax[0] > 1.0f + kUvEpsilon || group->boundsMax[1] > 1.0f + kUvEpsilon) {
            report.outsideAtlas.push_back(index);
        }
        const float minimum[2] = {std::max(group->boundsMin[0], 0.0f),
                                  std::max(group->boundsMin[1], 0.0f)};
        const float maximum[2] = {std::min(group->boundsMax[0], 1.0f),
                                  std::min(group->boundsMax[1], 1.0f)};
        const int firstX = static_cast<int>(minimum[0] * grid);
        const int lastX = static_cast<int>(std::ceil(maximum[0] * grid)) - 1;
        const int firstY = static_cast<int>(minimum[1] * grid);
        const int lastY = static_cast<int>(std::ceil(maximum[1] * grid)) - 1;
        for (int cellY = std::max(firstY, 0); cellY <= std::min(lastY, grid - 1); ++cellY) {
            for (int cellX = std::max(firstX, 0); cellX <= std::min(lastX, grid - 1); ++cellX) {
                const float u = (cellX + 0.5f) / grid;
                const float v = (cellY + 0.5f) / grid;
                bool inside = false;
                for (uint32_t slot = group->firstFace;
                     slot < group->firstFace + group->faceCount && !inside; ++slot) {
                    const uint32_t face = islands_.faceOrder[slot];
                    const uint32_t *corner = &working_.indices[face * 3];
                    const float *a = &working_.uvs[corner[0] * 2];
                    const float *b = &working_.uvs[corner[1] * 2];
                    const float *c = &working_.uvs[corner[2] * 2];
                    inside = pointInTriangle(u, v, a[0], a[1], b[0], b[1], c[0], c[1]);
                }
                if (!inside) {
                    continue;
                }
                uint32_t &cell = owner[cellY * grid + cellX];
                if (cell == kNoIsland) {
                    cell = index;
                    ++coveredCells;
                } else if (cell != index) {
                    overlapping.insert(cell);
                    overlapping.insert(index);
                }
            }
        }
    }

    report.coveredArea = static_cast<float>(coveredCells) / static_cast<float>(grid * grid);
    report.overlapping.assign(overlapping.begin(), overlapping.end());
    std::sort(report.overlapping.begin(), report.overlapping.end());
    return report;
}

std::string UvSession::describe() const {
    if (!isLoaded()) {
        return "sessao de UV vazia";
    }
    return "ilhas=" + std::to_string(islands_.count()) +
           " selecionadas=" + std::to_string(selected_.size()) +
           " faces=" + std::to_string(working_.triangleCount()) +
           (hasChanges() ? " com_edicoes" : " sem_edicoes");
}

}  // namespace g3d
