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

// OpenGL ES 3.0 preview of the document: solid shading, normals shading and tap selection.
// Every method that touches GL must run on the GL thread; the camera and the gesture entry points
// are guarded by a mutex because they are called from the UI thread.
class Renderer {
public:
    static Renderer &instance();

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
    void discardItems();
    void uploadItem(const Mesh &mesh, Item &item);
    void buildLineGeometry(Item &item);
    glm::mat4 viewProjection() const;
    glm::vec3 eyePosition() const;

    GLuint program_ = 0;
    GLint uniformViewProj_ = -1;
    GLint uniformColor_ = -1;
    GLint uniformLightDir_ = -1;
    GLint uniformShading_ = -1;
    std::vector<Item> items_;

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
