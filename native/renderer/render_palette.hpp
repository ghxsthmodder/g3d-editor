#pragma once

#include <cstddef>

namespace g3d {

// The colours the renderer paints with, in linear-ish 0..1 floats. They live in a header because the
// headless test asserts on pixels: comparing a screenshot against a copy of the number would let a
// palette change pass the test while looking wrong on the screen. The values mirror the tokens in
// app/src/main/res/values/colors.xml, so the GL surface and the Android panels meet as one grey.
inline constexpr float kBackground[3] = {0.0863f, 0.0902f, 0.0980f};      // editor_viewport
inline constexpr float kOutlineColor[4] = {0.9451f, 0.6706f, 0.2039f, 1.0f};  // selection_accent
inline constexpr float kAtlasBackground[4] = {0.1137f, 0.1176f, 0.1255f, 1.0f};  // editor_panel
inline constexpr float kAtlasGrid[4] = {0.1922f, 0.2000f, 0.2078f, 1.0f};        // editor_border
inline constexpr float kAtlasBorder[4] = {0.6078f, 0.6235f, 0.6392f, 1.0f};      // editor_text_dim
inline constexpr float kAtlasOverlap[4] = {0.9451f, 0.3020f, 0.2980f, 1.0f};     // axis red

// How much of the surface the 3D preview keeps in the split view. The interface raises it for the
// plain 3D look and lowers it when the atlas is the subject of the screen.
inline constexpr float kDefaultSplitRatio = 0.55f;
inline constexpr float kAtlasDominantSplitRatio = 0.22f;

}  // namespace g3d
