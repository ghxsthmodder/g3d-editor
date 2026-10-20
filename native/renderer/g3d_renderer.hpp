#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <GLES3/gl3.h>
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace g3d {

struct Mesh;

// OpenGL ES 3.0 preview of the document: solid shading, normals shading and tap selection, plus
// the 2D atlas layer of the UV editor. Every method that touches GL must run on the GL thread; the
// camera and the gesture entry points are guarded by a mutex because they are called from the UI
// thread.
class Renderer {
public:
    static Renderer &instance();

    enum {
        kViewThreeDimensional = 0,  // The atlas is hidden and the 3D view fills the surface.
        kViewSplit = 1,             // 3D on the left, atlas on the right.
        kViewAtlasOnly = 2          // The atlas fills the surface.
    };

    // Called from onSurfaceCreated. Rebuilds the shader state for the new context and uploads the
    // document geometry. Buffer names of a previous context are unreachable, so they are dropped.
    bool onContextCreated();
    void onSurfaceChanged(int width, int height);

    void uploadScene();
    void draw();

    // Gestures. Distances are in pixels, zoom is a multiplicative factor.
    void orbit(float dxPixels, float dyPixels);
    void pan(float dxPixels, float dyPixels);
    void zoom(float factor);
    void frameScene();

    // Mesh index under a tap, or -1. Runs on the GL thread.
    int pickAt(float xPixels, float yPixels);

    void setShadingMode(int mode);  // 0 = solid, 1 = normals
    int shadingMode() const { return shadingMode_; }

    // --- atlas layer ---------------------------------------------------------------------
    void setViewMode(int mode);
    int viewMode() const { return viewMode_; }
    void uvPan(float dxPixels, float dyPixels);
    void uvZoom(float factor);
    void uvFrame();
    // Atlas coordinates under a pixel given in surface coordinates with Y growing downwards.
    // False when the atlas is hidden or the point falls outside its square.
    bool uvAt(float xPixels, float yPixels, float out[2]);

private:
    Renderer() = default;

    struct Item {
        GLuint vertexArray = 0;
        GLuint vertexBuffer = 0;
        GLuint indexBuffer = 0;
        GLuint lineVertexArray = 0;
        GLuint lineIndexBuffer = 0;
        uint32_t indexCount = 0;
        uint32_t lineCount = 0;
        bool lineGeometryBuilt = false;
        std::string name;
        float color[4] = {0.8f, 0.8f, 0.8f, 1.0f};
        // CPU copies kept for tap picking.
        std::vector<float> positions;
        std::vector<uint32_t> indices;
        float boundsMin[3] = {0, 0, 0};
        float boundsMax[3] = {0, 0, 0};
    };

    bool buildShaders();
    bool buildUvShader();
    void discardItems();
    void uploadItem(const Mesh &mesh, Item &item);
    void buildLineGeometry(Item &item);
    glm::mat4 viewProjection(int width, int height) const;
    glm::vec3 eyePosition() const;

    // Rectangle of the surface, in surface pixels with Y growing downwards.
    void rectOf(int part, int out[4]) const;
    void rebuildUvGeometry();
    void drawUv();
    // Affine that maps atlas coordinates onto the clip space of the atlas rectangle:
    // gl_Position.xy = uv * out[0..1] + out[2..3]. V grows upwards on screen.
    void uvAffine(const int rect[4], float out[4]) const;

    GLuint program_ = 0;
    GLint uniformViewProj_ = -1;
    GLint uniformColor_ = -1;
    GLint uniformLightDir_ = -1;
    GLint uniformShading_ = -1;
    std::vector<Item> items_;

    GLuint uvProgram_ = 0;
    GLint uvUniformScale_ = -1;
    GLint uvUniformOffset_ = -1;
    GLuint uvVertexArray_ = 0;
    GLuint uvVertexBuffer_ = 0;
    // Ranges in the shared buffer, drawn in this order: the atlas background square (always six
    // vertices), the grid, the island fills, and finally the island edges.
    uint32_t uvGridCount_ = 0;
    uint32_t uvTriCount_ = 0;
    uint32_t uvEdgeCount_ = 0;
    uint64_t uvLayoutRevision_ = 0;
    bool uvGeometryStale_ = true;
    float uvZoomFactor_ = 1.0f;
    float uvPanX_ = 0.0f;
    float uvPanY_ = 0.0f;
    int viewMode_ = kViewThreeDimensional;

    // Guards everything the UI thread may change while the GL thread is drawing: the 3D camera and
    // the atlas pan/zoom/mode.
    mutable std::mutex cameraMutex_;
    glm::vec3 center_ = glm::vec3(0.0f);
    float yaw_ = 0.65f;
    float pitch_ = 0.42f;
    float distance_ = 3.0f;
    float radius_ = 1.0f;
    int width_ = 1;
    int height_ = 1;
    bool contextReady_ = false;
    int shadingMode_ = 0;
};

}  // namespace g3d
