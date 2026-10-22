#include "renderer/g3d_renderer.hpp"
#include "renderer/render_palette.hpp"
#include "texture/texture_studio.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <set>
#include <unordered_map>
#include <utility>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#ifdef __ANDROID__
#include <android/log.h>
#define G3D_LOG_ERROR(...) __android_log_print(ANDROID_LOG_ERROR, kTag, __VA_ARGS__)
#else
// On the host the renderer is exercised by tests/host_render_test.cpp through Mesa.
#define G3D_LOG_ERROR(...)        \
    do {                          \
        std::fprintf(stderr, "[g3d] ");  \
        std::fprintf(stderr, __VA_ARGS__); \
        std::fprintf(stderr, "\n");        \
    } while (0)
#endif

#include "core/document.hpp"
#include "core/mesh.hpp"
#include "core/primitives.hpp"
#include "uv/uv_editor.hpp"

namespace {

#ifdef __ANDROID__
constexpr const char *kTag = "G3DRender";
#endif
constexpr float kFovDegrees = 45.0f;
constexpr float kPi = 3.14159265358979323846f;
// The atlas square keeps a little margin so its border is never glued to the edge of the panel.
constexpr float kAtlasMarginRatio = 0.92f;
// Two triangles drawn before everything else, so the grid and the islands have something to sit on.
constexpr uint32_t kAtlasBackgroundVertices = 6;
constexpr int kCheckerSide = 512;
constexpr int kCheckerTile = 64;
const uint8_t kCheckerLight[3] = {232, 232, 238};
const uint8_t kCheckerDark[3] = {64, 70, 84};

const char *kVertexShader = R"GLSL(#version 300 es
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
uniform mat4 uViewProj;
out vec3 vNormal;
out vec2 vUv;
void main() {
    vNormal = aNormal;
    vUv = aUv;
    gl_Position = uViewProj * vec4(aPosition, 1.0);
}
)GLSL";

const char *kFragmentShader = R"GLSL(#version 300 es
precision highp float;
in vec3 vNormal;
in vec2 vUv;
uniform vec4 uColor;
uniform vec3 uLightDir;
uniform int uShading;
uniform sampler2D uTexture;
layout(location = 0) out vec4 outColor;
void main() {
    if (uShading == 2) {
        outColor = uColor;
        return;
    }
    vec3 normal = normalize(vNormal);
    if (uShading == 1) {
        outColor = vec4(normal * 0.5 + 0.5, 1.0);
        return;
    }
    // The reference grid is lit by nothing: its job is to show where the UVs stretch, and a
    // shading term would hide the very distortion the user is looking for.
    if (uShading == 4) {
        outColor = texture(uTexture, vUv);
        return;
    }
    float lit = max(dot(normal, normalize(uLightDir)), 0.0);
    float sky = 0.5 + 0.5 * normal.y;
    lit = 0.24 + 0.62 * lit + 0.14 * sky;
    if (uShading == 3) {
        outColor = vec4(texture(uTexture, vUv).rgb * lit, 1.0);
        return;
    }
    outColor = vec4(uColor.rgb * lit, 1.0);
}
)GLSL";

// The atlas layer is flat: every vertex is already in atlas coordinates and the only transform is
// a scale plus a translation, so no matrix and no depth are involved.
const char *kUvVertexShader = R"GLSL(#version 300 es
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec4 aColor;
uniform vec2 uScale;
uniform vec2 uOffset;
out vec4 vColor;
void main() {
    vColor = aColor;
    gl_Position = vec4(aPosition * uScale + uOffset, 0.0, 1.0);
}
)GLSL";

const char *kUvFragmentShader = R"GLSL(#version 300 es
precision mediump float;
in vec4 vColor;
layout(location = 0) out vec4 outColor;
void main() {
    outColor = vColor;
}
)GLSL";

// The composed texture as the background of the atlas: the same affine the wireframe uses, so the
// island lines land exactly on the pixels they belong to.
const char *kUvTextureVertexShader = R"GLSL(#version 300 es
layout(location = 0) in vec2 aPosition;
uniform vec2 uScale;
uniform vec2 uOffset;
out vec2 vUv;
void main() {
    vUv = aPosition;
    gl_Position = vec4(aPosition * uScale + uOffset, 0.0, 1.0);
}
)GLSL";

const char *kUvTextureFragmentShader = R"GLSL(#version 300 es
precision mediump float;
in vec2 vUv;
uniform sampler2D uTexture;
layout(location = 0) out vec4 outColor;
void main() {
    outColor = texture(uTexture, vUv);
}
)GLSL";

GLuint compileShader(GLenum type, const char *source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        char log[1024];
        GLsizei length = 0;
        glGetShaderInfoLog(shader, sizeof(log), &length, log);
        G3D_LOG_ERROR("shader compile: %.*s", static_cast<int>(length), log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// Meshes imported without normals still need shading: accumulate the face normals of every
// triangle onto its vertices, which is the cheapest result that looks right on a cube.
void computeSmoothNormals(const g3d::Mesh &mesh, std::vector<float> &out) {
    const uint32_t vertices = mesh.vertexCount();
    out.assign(vertices * 3, 0.0f);
    for (uint32_t triangle = 0; triangle + 2 < mesh.indices.size(); triangle += 3) {
        const uint32_t a = mesh.indices[triangle];
        const uint32_t b = mesh.indices[triangle + 1];
        const uint32_t c = mesh.indices[triangle + 2];
        const float *pa = &mesh.positions[a * 3];
        const float *pb = &mesh.positions[b * 3];
        const float *pc = &mesh.positions[c * 3];
        const glm::vec3 e1(pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]);
        const glm::vec3 e2(pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2]);
        const glm::vec3 faceNormal = glm::cross(e1, e2);
        for (const uint32_t index : {a, b, c}) {
            out[index * 3 + 0] += faceNormal.x;
            out[index * 3 + 1] += faceNormal.y;
            out[index * 3 + 2] += faceNormal.z;
        }
    }
    for (uint32_t i = 0; i < vertices; ++i) {
        const float x = out[i * 3];
        const float y = out[i * 3 + 1];
        const float z = out[i * 3 + 2];
        const float length = std::sqrt(x * x + y * y + z * z);
        if (length < 1e-8f) {
            out[i * 3 + 1] = 1.0f;
            continue;
        }
        out[i * 3 + 0] = x / length;
        out[i * 3 + 1] = y / length;
        out[i * 3 + 2] = z / length;
    }
}

bool rayHitsTriangle(const float *position, const float *direction, const float *vertex0,
                     const float *vertex1, const float *vertex2, float &outDistance) {
    const glm::vec3 origin(position[0], position[1], position[2]);
    const glm::vec3 ray(direction[0], direction[1], direction[2]);
    const glm::vec3 a(vertex0[0], vertex0[1], vertex0[2]);
    const glm::vec3 b(vertex1[0], vertex1[1], vertex1[2]);
    const glm::vec3 c(vertex2[0], vertex2[1], vertex2[2]);

    const glm::vec3 edge1 = b - a;
    const glm::vec3 edge2 = c - a;
    const glm::vec3 crossed = glm::cross(ray, edge2);
    const float determinant = glm::dot(edge1, crossed);
    if (std::abs(determinant) < 1e-8f) {
        return false;
    }
    const glm::vec3 toOrigin = origin - a;
    const float u = glm::dot(toOrigin, crossed) / determinant;
    if (u < 0.0f || u > 1.0f) {
        return false;
    }
    const glm::vec3 crossedOrigin = glm::cross(toOrigin, edge1);
    const float v = glm::dot(ray, crossedOrigin) / determinant;
    if (v < 0.0f || u + v > 1.0f) {
        return false;
    }
    const float distance = glm::dot(edge2, crossedOrigin) / determinant;
    if (distance <= 0.0f) {
        return false;
    }
    outDistance = distance;
    return true;
}

// Golden-angle hues keep neighbouring islands visibly different without a palette to maintain. The
// offset starts the sequence on blue, so no island colour resembles the amber of the selection or
// the red of an overlap.
void islandHue(uint32_t index, float out[3]) {
    const float hue = std::fmod(static_cast<float>(index) * 0.618034f + 0.45f, 1.0f);
    const float saturation = 0.52f;
    const float value = 0.88f;
    const float six = hue * 6.0f;
    const float offset[3] = {0.0f, 4.0f, 2.0f};
    for (int channel = 0; channel < 3; ++channel) {
        const float k = std::fmod(six + offset[channel], 6.0f);
        const float ramp = std::max(0.0f, std::min({k, 4.0f - k, 1.0f}));
        out[channel] = value - value * saturation * ramp;
    }
}

uint64_t uvEdgeKey(uint32_t a, uint32_t b) {
    return a < b ? (static_cast<uint64_t>(a) << 32) | static_cast<uint64_t>(b)
                 : (static_cast<uint64_t>(b) << 32) | static_cast<uint64_t>(a);
}

}  // namespace

namespace g3d {

Renderer &Renderer::instance() {
    static Renderer renderer;
    return renderer;
}

bool Renderer::onContextCreated() {
    // The previous context is gone: its buffer and program names mean nothing here, so they are
    // forgotten instead of deleted.
    items_.clear();
    program_ = 0;
    uvProgram_ = 0;
    uvVertexArray_ = 0;
    uvVertexBuffer_ = 0;
    uvUniformScale_ = -1;
    uvUniformOffset_ = -1;
    uvTextureProgram_ = 0;
    uvTextureUniformScale_ = -1;
    uvTextureUniformOffset_ = -1;
    uvTextureUniformTexture_ = -1;
    uvTextureVertexArray_ = 0;
    uvTextureVertexBuffer_ = 0;
    texture_ = 0;
    checkerTexture_ = 0;
    maxTextureSide_ = 0;
    textureRevision_ = 0;
    hasTexture_ = false;
    uniformTexture_ = -1;
    uvGridCount_ = 0;
    uvTriCount_ = 0;
    uvEdgeCount_ = 0;
    uvGeometryStale_ = true;
    uvLayoutRevision_ = 0;
    contextReady_ = false;

    if (!buildShaders()) {
        return false;
    }
    if (!buildUvShader()) {
        return false;
    }
    glEnable(GL_DEPTH_TEST);
    glClearColor(kBackground[0], kBackground[1], kBackground[2], 1.0f);
    // The ceiling of the driver becomes the ceiling of the canvas: mobile GPUs differ in it and the
    // editor must not offer a resolution this surface cannot sample.
    GLint side = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &side);
    maxTextureSide_ = side > 0 ? static_cast<int>(side) : 0;
    TextureStudio::instance().setDeviceLimit(maxTextureSide_);
    contextReady_ = true;
    uploadScene();
    return true;
}

bool Renderer::buildUvShader() {
    const GLuint vertex = compileShader(GL_VERTEX_SHADER, kUvVertexShader);
    const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, kUvFragmentShader);
    if (vertex == 0 || fragment == 0) {
        if (vertex != 0) {
            glDeleteShader(vertex);
        }
        if (fragment != 0) {
            glDeleteShader(fragment);
        }
        return false;
    }
    uvProgram_ = glCreateProgram();
    glAttachShader(uvProgram_, vertex);
    glAttachShader(uvProgram_, fragment);
    glLinkProgram(uvProgram_);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint ok = GL_FALSE;
    glGetProgramiv(uvProgram_, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        char log[1024];
        GLsizei length = 0;
        glGetProgramInfoLog(uvProgram_, sizeof(log), &length, log);
        G3D_LOG_ERROR("atlas program link: %.*s", static_cast<int>(length), log);
        glDeleteProgram(uvProgram_);
        uvProgram_ = 0;
        return false;
    }
    uvUniformScale_ = glGetUniformLocation(uvProgram_, "uScale");
    uvUniformOffset_ = glGetUniformLocation(uvProgram_, "uOffset");

    const GLuint textureVertex = compileShader(GL_VERTEX_SHADER, kUvTextureVertexShader);
    const GLuint textureFragment = compileShader(GL_FRAGMENT_SHADER, kUvTextureFragmentShader);
    if (textureVertex == 0 || textureFragment == 0) {
        if (textureVertex != 0) {
            glDeleteShader(textureVertex);
        }
        if (textureFragment != 0) {
            glDeleteShader(textureFragment);
        }
        return false;
    }
    uvTextureProgram_ = glCreateProgram();
    glAttachShader(uvTextureProgram_, textureVertex);
    glAttachShader(uvTextureProgram_, textureFragment);
    glLinkProgram(uvTextureProgram_);
    glDeleteShader(textureVertex);
    glDeleteShader(textureFragment);
    glGetProgramiv(uvTextureProgram_, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        G3D_LOG_ERROR("atlas picture program link failed");
        glDeleteProgram(uvTextureProgram_);
        uvTextureProgram_ = 0;
        return false;
    }
    uvTextureUniformScale_ = glGetUniformLocation(uvTextureProgram_, "uScale");
    uvTextureUniformOffset_ = glGetUniformLocation(uvTextureProgram_, "uOffset");
    uvTextureUniformTexture_ = glGetUniformLocation(uvTextureProgram_, "uTexture");

    // The picture is the atlas square itself: two triangles in atlas coordinates, uploaded once
    // per context because it never changes.
    const float quad[kAtlasBackgroundVertices * 2] = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,
                                                      0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    glGenVertexArrays(1, &uvTextureVertexArray_);
    glGenBuffers(1, &uvTextureVertexBuffer_);
    glBindVertexArray(uvTextureVertexArray_);
    glBindBuffer(GL_ARRAY_BUFFER, uvTextureVertexBuffer_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(quad)), quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
    return true;
}

bool Renderer::buildShaders() {
    const GLuint vertex = compileShader(GL_VERTEX_SHADER, kVertexShader);
    const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    if (vertex == 0 || fragment == 0) {
        if (vertex != 0) {
            glDeleteShader(vertex);
        }
        if (fragment != 0) {
            glDeleteShader(fragment);
        }
        return false;
    }
    program_ = glCreateProgram();
    glAttachShader(program_, vertex);
    glAttachShader(program_, fragment);
    glLinkProgram(program_);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint ok = GL_FALSE;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        char log[1024];
        GLsizei length = 0;
        glGetProgramInfoLog(program_, sizeof(log), &length, log);
        G3D_LOG_ERROR("program link: %.*s", static_cast<int>(length), log);
        glDeleteProgram(program_);
        program_ = 0;
        return false;
    }
    uniformViewProj_ = glGetUniformLocation(program_, "uViewProj");
    uniformColor_ = glGetUniformLocation(program_, "uColor");
    uniformLightDir_ = glGetUniformLocation(program_, "uLightDir");
    uniformShading_ = glGetUniformLocation(program_, "uShading");
    uniformTexture_ = glGetUniformLocation(program_, "uTexture");
    return true;
}

void Renderer::onSurfaceChanged(int width, int height) {
    const int viewportWidth = width > 0 ? width : 1;
    const int viewportHeight = height > 0 ? height : 1;
    {
        std::lock_guard<std::mutex> guard(cameraMutex_);
        width_ = viewportWidth;
        height_ = viewportHeight;
    }
    glViewport(0, 0, viewportWidth, viewportHeight);
}

void Renderer::discardItems() {
    for (Item &item : items_) {
        if (item.vertexArray != 0) {
            glDeleteVertexArrays(1, &item.vertexArray);
        }
        if (item.lineVertexArray != 0) {
            glDeleteVertexArrays(1, &item.lineVertexArray);
        }
        if (item.vertexBuffer != 0) {
            glDeleteBuffers(1, &item.vertexBuffer);
        }
        if (item.indexBuffer != 0) {
            glDeleteBuffers(1, &item.indexBuffer);
        }
        if (item.lineIndexBuffer != 0) {
            glDeleteBuffers(1, &item.lineIndexBuffer);
        }
    }
    items_.clear();
}

void Renderer::uploadScene() {
    if (!contextReady_) {
        return;
    }
    const bool firstUpload = items_.empty();
    discardItems();

    Document &document = Document::instance();
    std::vector<Item> uploaded;
    document.withScene([&](Scene &scene) {
        for (uint32_t i = 0; i < scene.meshCount(); ++i) {
            const Mesh *mesh = scene.meshAt(i);
            if (mesh == nullptr) {
                continue;
            }
            Item item;
            uploadItem(*mesh, item);
            if (item.vertexArray != 0) {
                uploaded.push_back(std::move(item));
            }
        }
        float minimum[3];
        float maximum[3];
        if (scene.bounds(minimum, maximum)) {
            const float diagonal = std::sqrt(std::pow(maximum[0] - minimum[0], 2.0f) +
                                             std::pow(maximum[1] - minimum[1], 2.0f) +
                                             std::pow(maximum[2] - minimum[2], 2.0f));
            radius_ = std::max(diagonal * 0.5f, 1e-3f);
        }
    });
    items_ = std::move(uploaded);

    // A fresh view starts framed; later uploads (new UVs, edited islands) keep the camera alone.
    if (firstUpload) {
        frameScene();
    }
}

void Renderer::uploadItem(const Mesh &mesh, Item &item) {
    const uint32_t vertices = mesh.vertexCount();
    if (vertices == 0 || mesh.indices.empty()) {
        return;
    }

    std::vector<float> generatedNormals;
    const std::vector<float> *source = &mesh.normals;
    if (!mesh.hasNormals()) {
        computeSmoothNormals(mesh, generatedNormals);
        source = &generatedNormals;
    }
    const std::vector<float> &normals = *source;

    item.name = mesh.name;
    item.color[0] = mesh.material.baseColor[0];
    item.color[1] = mesh.material.baseColor[1];
    item.color[2] = mesh.material.baseColor[2];
    item.color[3] = 1.0f;
    item.positions = mesh.positions;
    item.indices = mesh.indices;
    item.indexCount = static_cast<uint32_t>(mesh.indices.size());

    std::vector<float> interleaved(static_cast<size_t>(vertices) * 8, 0.0f);
    for (uint32_t i = 0; i < vertices; ++i) {
        interleaved[i * 8 + 0] = mesh.positions[i * 3 + 0];
        interleaved[i * 8 + 1] = mesh.positions[i * 3 + 1];
        interleaved[i * 8 + 2] = mesh.positions[i * 3 + 2];
        interleaved[i * 8 + 3] = normals[i * 3 + 0];
        interleaved[i * 8 + 4] = normals[i * 3 + 1];
        interleaved[i * 8 + 5] = normals[i * 3 + 2];
        // UVs are not sampled yet (phase 4), but the vertex layout keeps room for them so the
        // textured preview does not need a new upload path.
        if (mesh.hasUvs()) {
            interleaved[i * 8 + 6] = mesh.uvs[i * 2 + 0];
            interleaved[i * 8 + 7] = mesh.uvs[i * 2 + 1];
            item.hasUvs = true;
        }
    }

    float minimum[3];
    float maximum[3];
    if (computeBounds(mesh, minimum, maximum)) {
        std::copy(std::begin(minimum), std::end(minimum), item.boundsMin);
        std::copy(std::begin(maximum), std::end(maximum), item.boundsMax);
    }

    glGenVertexArrays(1, &item.vertexArray);
    glGenBuffers(1, &item.vertexBuffer);
    glGenBuffers(1, &item.indexBuffer);

    glBindVertexArray(item.vertexArray);
    glBindBuffer(GL_ARRAY_BUFFER, item.vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(interleaved.size() * sizeof(float)),
                 interleaved.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, item.indexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(uint32_t)),
                 mesh.indices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void *>(0));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                          reinterpret_cast<void *>(3 * sizeof(float)));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                          reinterpret_cast<void *>(6 * sizeof(float)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
}

void Renderer::buildLineGeometry(Item &item) {
    if (item.lineGeometryBuilt || item.indices.empty()) {
        return;
    }
    item.lineGeometryBuilt = true;

    std::set<uint64_t> seen;
    std::vector<uint32_t> lines;
    for (size_t triangle = 0; triangle + 2 < item.indices.size(); triangle += 3) {
        const uint32_t corner[3] = {item.indices[triangle], item.indices[triangle + 1],
                                   item.indices[triangle + 2]};
        for (int edge = 0; edge < 3; ++edge) {
            const uint32_t a = corner[edge];
            const uint32_t b = corner[(edge + 1) % 3];
            const uint64_t key = a < b ? (static_cast<uint64_t>(a) << 32) | b
                                        : (static_cast<uint64_t>(b) << 32) | a;
            if (seen.insert(key).second) {
                lines.push_back(a);
                lines.push_back(b);
            }
        }
    }
    if (lines.empty()) {
        return;
    }

    glGenVertexArrays(1, &item.lineVertexArray);
    glGenBuffers(1, &item.lineIndexBuffer);
    glBindVertexArray(item.lineVertexArray);
    // The attribute bindings live in the triangle VAO, so point the line VAO at the same buffer.
    glBindBuffer(GL_ARRAY_BUFFER, item.vertexBuffer);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, item.lineIndexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(lines.size() * sizeof(uint32_t)),
                 lines.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);
    item.lineCount = static_cast<uint32_t>(lines.size());
}

glm::vec3 Renderer::eyePosition() const {
    std::lock_guard<std::mutex> guard(cameraMutex_);
    const glm::vec3 direction(std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_),
                              std::cos(pitch_) * std::cos(yaw_));
    return center_ + direction * distance_;
}

glm::mat4 Renderer::viewProjection(int width, int height) const {
    std::lock_guard<std::mutex> guard(cameraMutex_);
    const glm::vec3 direction(std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_),
                              std::cos(pitch_) * std::cos(yaw_));
    const glm::vec3 eye = center_ + direction * distance_;
    const int safeWidth = width > 0 ? width : 1;
    const int safeHeight = height > 0 ? height : 1;
    const float aspect = static_cast<float>(safeWidth) / static_cast<float>(safeHeight);
    const float nearPlane = std::max(radius_ * 0.02f, 0.01f);
    const float farPlane = std::max(radius_ * 60.0f, (distance_ + radius_) * 6.0f);
    const glm::mat4 projection =
        glm::perspective(glm::radians(kFovDegrees), aspect, nearPlane, farPlane);
    const glm::mat4 view = glm::lookAt(eye, center_, glm::vec3(0.0f, 1.0f, 0.0f));
    return projection * view;
}

// The surface is split between the 3D preview and the atlas. Rectangles are in surface pixels with
// Y growing downwards, matching the touch coordinates the view receives.
void Renderer::rectOf(int part, int out[4]) const {
    int surfaceWidth = 1;
    int surfaceHeight = 1;
    int mode = kViewThreeDimensional;
    float ratio = kDefaultSplitRatio;
    {
        std::lock_guard<std::mutex> guard(cameraMutex_);
        surfaceWidth = width_;
        surfaceHeight = height_;
        mode = viewMode_;
        ratio = splitRatio_;
    }
    out[0] = 0;
    out[1] = 0;
    out[2] = 0;
    out[3] = 0;
    // A portrait phone is short in width and long in height, and the atlas is a square: splitting
    // the width takes the side the square is measured by and leaves the other half empty. So the
    // split follows the short side — stacked here, side by side when the surface is wide.
    const bool stacked = surfaceHeight > surfaceWidth;
    const int split = static_cast<int>(
        static_cast<float>(stacked ? surfaceHeight : surfaceWidth) * ratio);
    if (part == 0) {
        if (mode == kViewAtlasOnly) {
            return;
        }
        if (mode != kViewSplit) {
            out[2] = surfaceWidth;
            out[3] = surfaceHeight;
            return;
        }
        out[2] = stacked ? surfaceWidth : split;
        out[3] = stacked ? split : surfaceHeight;
        return;
    }
    if (mode == kViewThreeDimensional) {
        return;
    }
    if (mode == kViewSplit) {
        if (stacked) {
            out[1] = split;
            out[2] = surfaceWidth;
            out[3] = std::max(surfaceHeight - split, 1);
        } else {
            out[0] = split;
            out[2] = std::max(surfaceWidth - split, 1);
            out[3] = surfaceHeight;
        }
        return;
    }
    out[2] = surfaceWidth;
    out[3] = surfaceHeight;
}

// Composing is CPU work and the compositor already made the picture top-down, the way a PNG stores
// it; GL reads its first row at v = 0, so the rows are flipped once here and every consumer can
// sample v straight from the UVs.
bool Renderer::uploadTextureImage(GLuint name, const Image &image) {
    // The flip below is the cost of the upload; an over-sized picture is refused before paying it.
    if (maxTextureSide_ > 0 && (image.width > maxTextureSide_ || image.height > maxTextureSide_)) {
        return false;
    }
    const size_t stride = static_cast<size_t>(image.width) * 4;
    uploadScratch_.assign(image.pixels.size(), 0);
    for (int row = 0; row < image.height; ++row) {
        std::memcpy(&uploadScratch_[(static_cast<size_t>(image.height - 1 - row)) * stride],
                    &image.pixels[static_cast<size_t>(row) * stride], stride);
    }
    glBindTexture(GL_TEXTURE_2D, name);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width, image.height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, uploadScratch_.data());
    // The atlas has no tiling to repeat and an arbitrary side length, so no mipmaps: GL ES 3 filters
    // a non-power-of-two texture with the edges clamped, which is exactly the atlas square.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void Renderer::refreshTexture() {
    TextureStudio &studio = TextureStudio::instance();
    const uint64_t revision = studio.revision();
    if (revision != textureRevision_) {
        textureRevision_ = revision;
        Image picture;
        hasTexture_ = studio.texture(picture);
        if (hasTexture_) {
            if (texture_ == 0) {
                glGenTextures(1, &texture_);
            }
            // A picture this context cannot hold leaves the preview without a texture: the model
            // draws in the flat shading, the canvas and the exported PNG are untouched.
            hasTexture_ = uploadTextureImage(texture_, picture);
        }
    }

    int mode = 0;
    {
        std::lock_guard<std::mutex> guard(cameraMutex_);
        mode = shadingMode_;
    }
    if (mode == kShadeChecker && checkerTexture_ == 0) {
        // The reference grid is the same picture everywhere, so it is built once per context and
        // repeats: a power of two lets it have mipmaps for the oblique views.
        Image grid;
        grid.allocate(kCheckerSide, kCheckerSide, nullptr);
        fillCheckerboard(grid, kCheckerTile, kCheckerLight, kCheckerDark);
        glGenTextures(1, &checkerTexture_);
        uploadTextureImage(checkerTexture_, grid);
        glBindTexture(GL_TEXTURE_2D, checkerTexture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

// The composed picture behind the atlas wireframe.
void Renderer::drawUvBackground(const float affine[4]) {
    if (uvTextureProgram_ == 0 || texture_ == 0) {
        return;
    }
    glUseProgram(uvTextureProgram_);
    glUniform2f(uvTextureUniformScale_, affine[0], affine[1]);
    glUniform2f(uvTextureUniformOffset_, affine[2], affine[3]);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glUniform1i(uvTextureUniformTexture_, 0);
    glBindVertexArray(uvTextureVertexArray_);
    glDrawArrays(GL_TRIANGLES, 0, kAtlasBackgroundVertices);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Renderer::draw() {
    if (!contextReady_) {
        return;
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    int surfaceWidth = 1;
    int surfaceHeight = 1;
    {
        std::lock_guard<std::mutex> guard(cameraMutex_);
        surfaceWidth = width_;
        surfaceHeight = height_;
    }

    // The compositor and the atlas live on their own schedule: the picture has to be re-uploaded
    // even when the 3D rectangle is hidden, or the atlas would show yesterday's texture.
    refreshTexture();

    int three[4];
    rectOf(0, three);
    if (three[2] > 0 && !items_.empty()) {
        // GL viewports are measured from the bottom left, the surface rectangles are not.
        glViewport(three[0], surfaceHeight - three[1] - three[3], three[2], three[3]);
        const glm::mat4 matrix = this->viewProjection(three[2], three[3]);
        int shading = kShadeSolid;
        {
            std::lock_guard<std::mutex> guard(cameraMutex_);
            shading = shadingMode_;
        }

        glUseProgram(program_);
        glUniformMatrix4fv(uniformViewProj_, 1, GL_FALSE, &matrix[0][0]);
        glUniform3f(uniformLightDir_, 0.35f, 0.85f, 0.4f);
        glUniform1i(uniformShading_, shading);
        // The grid repeats over the whole UV square, the atlas picture does not: one sampler,
        // whichever texture the mode asked for.
        const GLuint bound = shading == kShadeChecker ? checkerTexture_ : texture_;
        if (bound != 0) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, bound);
            glUniform1i(uniformTexture_, 0);
        }

        // Push the triangles a little away from the camera so the selection outline, which sits on
        // the very same edges, wins the depth test instead of flickering against it.
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.0f, 2.0f);
        for (Item &item : items_) {
            glBindVertexArray(item.vertexArray);
            glUniform4f(uniformColor_, item.color[0], item.color[1], item.color[2], item.color[3]);
            // A mesh the user never gave UVs to cannot show a texture: it keeps the shaded
            // material instead of sampling the atlas corner for every pixel of it.
            const bool needsUvs = shading == kShadeTextured || shading == kShadeChecker;
            glUniform1i(uniformShading_, needsUvs && !item.hasUvs ? kShadeSolid : shading);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(item.indexCount), GL_UNSIGNED_INT,
                           reinterpret_cast<const void *>(0));
        }
        glDisable(GL_POLYGON_OFFSET_FILL);
        glBindTexture(GL_TEXTURE_2D, 0);

        // Read every frame so a tap in the hierarchy panel highlights without re-uploading.
        const int selected = Document::instance().selected();
        if (selected >= 0 && static_cast<size_t>(selected) < items_.size()) {
            Item &item = items_[static_cast<size_t>(selected)];
            buildLineGeometry(item);
            if (item.lineCount > 0) {
                glUniform1i(uniformShading_, 2);
                glUniform4fv(uniformColor_, 1, kOutlineColor);
                glBindVertexArray(item.lineVertexArray);
                glDrawElements(GL_LINES, static_cast<GLsizei>(item.lineCount), GL_UNSIGNED_INT,
                               reinterpret_cast<const void *>(0));
            }
        }
        glBindVertexArray(0);
    }

    int atlas[4];
    rectOf(1, atlas);
    if (atlas[2] > 0 && atlas[3] > 0) {
        glViewport(atlas[0], surfaceHeight - atlas[1] - atlas[3], atlas[2], atlas[3]);
        drawUv();
    }
    glViewport(0, 0, surfaceWidth, surfaceHeight);
}

void Renderer::orbit(float dxPixels, float dyPixels) {
    std::lock_guard<std::mutex> guard(cameraMutex_);
    yaw_ -= dxPixels * 0.006f;
    pitch_ += dyPixels * 0.006f;
    const float limit = static_cast<float>(kPi) * 0.49f;
    pitch_ = std::min(limit, std::max(-limit, pitch_));
    const float fullTurn = 2.0f * static_cast<float>(kPi);
    yaw_ = std::fmod(yaw_, fullTurn);
}

void Renderer::pan(float dxPixels, float dyPixels) {
    std::lock_guard<std::mutex> guard(cameraMutex_);
    const glm::vec3 forward(std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_),
                            std::cos(pitch_) * std::cos(yaw_));
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));
    // One pixel moves the target by the world height of one pixel at the target depth.
    const float perPixel = 2.0f * std::tan(glm::radians(kFovDegrees) * 0.5f) * distance_ /
                           static_cast<float>(height_);
    // Screen Y grows downwards, so the vertical term is inverted relative to the world up vector.
    center_ -= right * (dxPixels * perPixel);
    center_ += up * (dyPixels * perPixel);
}

void Renderer::zoom(float factor) {
    std::lock_guard<std::mutex> guard(cameraMutex_);
    if (factor <= 0.0f) {
        return;
    }
    const float next = distance_ * factor;
    distance_ = std::min(std::max(next, radius_ * 0.12f), radius_ * 80.0f);
}

void Renderer::frameScene() {
    std::lock_guard<std::mutex> guard(cameraMutex_);
    if (items_.empty()) {
        center_ = glm::vec3(0.0f);
        distance_ = 3.0f;
        return;
    }
    glm::vec3 minimum(items_[0].boundsMin[0], items_[0].boundsMin[1], items_[0].boundsMin[2]);
    glm::vec3 maximum = minimum;
    for (const Item &item : items_) {
        minimum = glm::min(minimum, glm::vec3(item.boundsMin[0], item.boundsMin[1], item.boundsMin[2]));
        maximum = glm::max(maximum, glm::vec3(item.boundsMax[0], item.boundsMax[1], item.boundsMax[2]));
    }
    center_ = (minimum + maximum) * 0.5f;
    const float radius = std::max(glm::length(maximum - minimum) * 0.5f, 1e-3f);
    const float sine = std::sin(glm::radians(kFovDegrees) * 0.5f);
    distance_ = std::max(radius / sine * 1.15f, 0.2f);
}

int Renderer::pickAt(float xPixels, float yPixels) {
    if (items_.empty()) {
        return -1;
    }

    int rect[4];
    rectOf(0, rect);
    if (rect[2] <= 0 || xPixels < static_cast<float>(rect[0]) ||
        xPixels >= static_cast<float>(rect[0] + rect[2]) ||
        yPixels < static_cast<float>(rect[1]) ||
        yPixels >= static_cast<float>(rect[1] + rect[3])) {
        return -1;  // The tap landed on the atlas, not on the model.
    }

    const glm::mat4 matrix = this->viewProjection(rect[2], rect[3]);
    const glm::mat4 inverted = glm::inverse(matrix);
    const glm::vec3 eye = eyePosition();

    // Pixels inside the rectangle map onto the clip space of that rectangle.
    const float ndcX = 2.0f * (xPixels - rect[0]) / static_cast<float>(rect[2]) - 1.0f;
    const float ndcY = 1.0f - 2.0f * (yPixels - rect[1]) / static_cast<float>(rect[3]);
    // GLM was configured with GLM_FORCE_DEPTH_ZERO_TO_ONE, so window depth runs from 0 to 1.
    const glm::vec4 nearPoint = inverted * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    const glm::vec4 farPoint = inverted * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    const glm::vec3 near3(nearPoint.x / nearPoint.w, nearPoint.y / nearPoint.w,
                          nearPoint.z / nearPoint.w);
    const glm::vec3 far3(farPoint.x / farPoint.w, farPoint.y / farPoint.w, farPoint.z / farPoint.w);
    const glm::vec3 ray = glm::normalize(far3 - near3);

    const float origin[3] = {eye.x, eye.y, eye.z};
    const float direction[3] = {ray.x, ray.y, ray.z};

    int best = -1;
    float bestDistance = std::numeric_limits<float>::max();
    for (size_t index = 0; index < items_.size(); ++index) {
        const Item &item = items_[index];
        for (size_t triangle = 0; triangle + 2 < item.indices.size(); triangle += 3) {
            const uint32_t a = item.indices[triangle];
            const uint32_t b = item.indices[triangle + 1];
            const uint32_t c = item.indices[triangle + 2];
            float hitDistance = 0.0f;
            if (rayHitsTriangle(origin, direction, &item.positions[a * 3], &item.positions[b * 3],
                                 &item.positions[c * 3], hitDistance) &&
                hitDistance < bestDistance) {
                bestDistance = hitDistance;
                best = static_cast<int>(index);
            }
        }
    }
    return best;
}

void Renderer::setShadingMode(int mode) {
    std::lock_guard<std::mutex> guard(cameraMutex_);
    shadingMode_ = (mode == kShadeNormals || mode == kShadeTextured || mode == kShadeChecker)
                       ? mode
                       : kShadeSolid;
}

void Renderer::setUvOverlayVisible(bool visible) {
    std::lock_guard<std::mutex> guard(cameraMutex_);
    uvOverlayVisible_ = visible;
}

bool Renderer::uvOverlayVisible() const {
    std::lock_guard<std::mutex> guard(cameraMutex_);
    return uvOverlayVisible_;
}

void Renderer::setSplitRatio(float ratio) {
    std::lock_guard<std::mutex> guard(cameraMutex_);
    // Either panel can be given less than a tenth of the surface and become unusable, so the
    // interface can ask for a ratio and get one it can actually work with.
    splitRatio_ = std::min(std::max(ratio, 0.1f), 0.9f);
}

float Renderer::splitRatio() const {
    std::lock_guard<std::mutex> guard(cameraMutex_);
    return splitRatio_;
}

void Renderer::setViewMode(int mode) {
    std::lock_guard<std::mutex> guard(cameraMutex_);
    viewMode_ = mode == kViewSplit ? kViewSplit : (mode == kViewAtlasOnly ? kViewAtlasOnly
                                                                          : kViewThreeDimensional);
}

void Renderer::uvPan(float dxPixels, float dyPixels) {
    std::lock_guard<std::mutex> guard(cameraMutex_);
    uvPanX_ += dxPixels;
    // Surface pixels grow downwards, and the atlas is drawn the same way, so a drag follows
    // the finger exactly.
    uvPanY_ += dyPixels;
}

void Renderer::uvZoom(float factor) {
    if (factor <= 0.0f) {
        return;
    }
    std::lock_guard<std::mutex> guard(cameraMutex_);
    uvZoomFactor_ = std::min(std::max(uvZoomFactor_ * factor, 0.2f), 20.0f);
}

void Renderer::uvFrame() {
    std::lock_guard<std::mutex> guard(cameraMutex_);
    uvZoomFactor_ = 1.0f;
    uvPanX_ = 0.0f;
    uvPanY_ = 0.0f;
}

void Renderer::uvAffine(const int rect[4], float out[4]) const {
    float zoom = 1.0f;
    float panX = 0.0f;
    float panY = 0.0f;
    {
        std::lock_guard<std::mutex> guard(cameraMutex_);
        zoom = uvZoomFactor_;
        panX = uvPanX_;
        panY = uvPanY_;
    }
    const float width = rect[2] > 0 ? static_cast<float>(rect[2]) : 1.0f;
    const float height = rect[3] > 0 ? static_cast<float>(rect[3]) : 1.0f;
    // The atlas stays square in pixels, so both axes are sized by the shorter side.
    const float side = std::min(width, height) * kAtlasMarginRatio * zoom;
    out[0] = 2.0f * side / width;
    out[1] = 2.0f * side / height;
    out[2] = 2.0f * panX / width - out[0] * 0.5f;
    // Clip Y grows upwards while surface Y grows downwards: the minus sign puts V=1 at the top.
    out[3] = -2.0f * panY / height - out[1] * 0.5f;
}

bool Renderer::uvGrab(float xPixels, float yPixels, bool additive) {
    UvEditor &editor = UvEditor::instance();
    float uv[2];
    // Outside the atlas square the gesture belongs to the 3D view, whatever the selection is.
    if (!uvAt(xPixels, yPixels, uv)) {
        return false;
    }
    const int island = editor.pick(uv[0], uv[1]);
    // select() toggles, so an island already in the group must not be handed to it: dragging a
    // multi-island selection by one of its members would otherwise drop that member.
    if (island >= 0) {
        const uint32_t index = static_cast<uint32_t>(island);
        if (!editor.isSelected(index)) {
            editor.select(index, additive);
        }
    }
    return editor.selectionCount() > 0;
}

// The affine maps UV to clip space, so one surface pixel is 2 / (scale * panelWidth) of a UV unit
// on that axis. Clip Y grows upwards while surface Y grows downwards, which is the minus sign on
// the vertical axis: the island follows the finger.
void Renderer::uvDragPixels(float dxPixels, float dyPixels) {
    int rect[4];
    rectOf(1, rect);
    if (rect[2] <= 0 || rect[3] <= 0) {
        return;
    }
    float affine[4];
    uvAffine(rect, affine);
    if (affine[0] == 0.0f || affine[1] == 0.0f) {
        return;
    }
    const float du = 2.0f * dxPixels / (affine[0] * static_cast<float>(rect[2]));
    const float dv = -2.0f * dyPixels / (affine[1] * static_cast<float>(rect[3]));
    UvEditor::instance().translate(du, dv);
}

bool Renderer::uvAt(float xPixels, float yPixels, float out[2]) {
    int rect[4];
    rectOf(1, rect);
    if (rect[2] <= 0 || rect[3] <= 0) {
        return false;
    }
    if (xPixels < static_cast<float>(rect[0]) || xPixels >= static_cast<float>(rect[0] + rect[2]) ||
        yPixels < static_cast<float>(rect[1]) || yPixels >= static_cast<float>(rect[1] + rect[3])) {
        return false;  // The tap landed on the 3D preview, not on the atlas.
    }
    float affine[4];
    uvAffine(rect, affine);
    const float ndcX = 2.0f * (xPixels - static_cast<float>(rect[0])) / static_cast<float>(rect[2]) -
                       1.0f;
    const float ndcY = 1.0f - 2.0f * (yPixels - static_cast<float>(rect[1])) /
                              static_cast<float>(rect[3]);
    out[0] = (ndcX - affine[2]) / affine[0];
    out[1] = (ndcY - affine[3]) / affine[1];
    return out[0] >= 0.0f && out[0] <= 1.0f && out[1] >= 0.0f && out[1] <= 1.0f;
}

// Atlas geometry is rebuilt only when the editing session changed, because every transform copies
// the whole layout out of the session.
void Renderer::rebuildUvGeometry() {
    UvEditor &editor = UvEditor::instance();
    const uint64_t revision = editor.revision();
    if (!uvGeometryStale_ && revision == uvLayoutRevision_) {
        return;
    }
    uvGeometryStale_ = false;
    uvLayoutRevision_ = revision;

    std::vector<float> vertices;  // Six floats per vertex: atlas position, then colour.
    const auto pushVertex = [&vertices](float u, float v, const float color[4]) {
        vertices.insert(vertices.end(), {u, v, color[0], color[1], color[2], color[3]});
    };
    const auto pushLine = [&pushVertex](float u0, float v0, float u1, float v1,
                                        const float color[4]) {
        pushVertex(u0, v0, color);
        pushVertex(u1, v1, color);
    };

    UvLayout layout;
    const bool hasLayout = editor.layout(layout) && !layout.indices.empty();
    std::set<uint32_t> selectedSet(layout.selected.begin(), layout.selected.end());
    std::set<uint32_t> overlapSet(layout.overlapping.begin(), layout.overlapping.end());

    // Colour of one island, written into color: amber when selected, red while it overlaps another
    // island, and a hue otherwise. Selection wins because the user needs to see what a tool acts on.
    float color[4];
    const auto islandColor = [&selectedSet, &overlapSet, &color](uint32_t island, float alpha) {
        if (selectedSet.count(island) != 0) {
            std::copy(std::begin(kOutlineColor), std::end(kOutlineColor), &color[0]);
        } else if (overlapSet.count(island) != 0) {
            std::copy(std::begin(kAtlasOverlap), std::end(kAtlasOverlap), &color[0]);
        } else {
            islandHue(island, &color[0]);
            color[3] = 1.0f;
        }
        color[3] = alpha;
    };
    const auto uvOf = [&layout](uint32_t vertex) {
        return vertex * 2 + 1 < layout.uvs.size();
    };

    const float background[kAtlasBackgroundVertices][2] = {
        {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
    for (const auto &corner : background) {
        pushVertex(corner[0], corner[1], kAtlasBackground);
    }

    for (int step = 0; step <= 4; ++step) {
        const float position = static_cast<float>(step) * 0.25f;
        const bool border = (step == 0 || step == 4);
        const float *gridColor = border ? kAtlasBorder : kAtlasGrid;
        pushLine(position, 0.0f, position, 1.0f, gridColor);
        pushLine(0.0f, position, 1.0f, position, gridColor);
    }
    uvGridCount_ = static_cast<uint32_t>(vertices.size() / 6) - kAtlasBackgroundVertices;

    // Edges are classified by who owns them: an edge used twice inside one island is interior and
    // is drawn dim, everything else is a seam or a silhouette and is drawn bright.
    struct EdgeInfo {
        uint32_t island = kNoIsland;
        uint32_t uses = 0;
    };
    std::unordered_map<uint64_t, EdgeInfo> edges;
    for (size_t face = 0; hasLayout && face + 2 < layout.indices.size(); face += 3) {
        const uint32_t corner[3] = {layout.indices[face], layout.indices[face + 1],
                                    layout.indices[face + 2]};
        if (!uvOf(corner[0]) || !uvOf(corner[1]) || !uvOf(corner[2])) {
            continue;
        }
        const uint32_t island = face / 3 < layout.faceIsland.size()
                                    ? layout.faceIsland[face / 3]
                                    : kNoIsland;

        // Translucent fills make an overlap visible before the report is ever read.
        islandColor(island, selectedSet.count(island) != 0 ? 0.45f : 0.28f);
        for (const uint32_t index : corner) {
            pushVertex(layout.uvs[index * 2], layout.uvs[index * 2 + 1], color);
        }

        for (int edge = 0; edge < 3; ++edge) {
            const uint32_t a = corner[edge];
            const uint32_t b = corner[(edge + 1) % 3];
            EdgeInfo &info = edges[uvEdgeKey(a, b)];
            if (info.uses == 0) {
                info.island = island;
            } else if (info.island != island) {
                info.island = kNoIsland;  // Two islands meet on this edge.
            }
            ++info.uses;
        }
    }
    uvTriCount_ = static_cast<uint32_t>(vertices.size() / 6) - kAtlasBackgroundVertices -
                  uvGridCount_;

    // Warnings are drawn last, so an outline can never be buried under the islands it warns about:
    // stacked islands share the very same pixels and only the last stroke survives.
    const auto edgeRank = [&selectedSet, &overlapSet](const EdgeInfo &info) {
        if (info.island == kNoIsland) {
            return 1;  // A seam between two islands.
        }
        if (selectedSet.count(info.island) != 0) {
            return 3;
        }
        return overlapSet.count(info.island) != 0 ? 2 : 0;
    };
    std::vector<std::pair<uint64_t, EdgeInfo>> ordered(edges.begin(), edges.end());
    std::stable_sort(ordered.begin(), ordered.end(),
                     [&edgeRank](const std::pair<uint64_t, EdgeInfo> &left,
                                 const std::pair<uint64_t, EdgeInfo> &right) {
                         return edgeRank(left.second) < edgeRank(right.second);
                     });

    for (const auto &entry : ordered) {
        const EdgeInfo &info = entry.second;
        const uint32_t a = static_cast<uint32_t>(entry.first >> 32);
        const uint32_t b = static_cast<uint32_t>(entry.first & 0xffffffffu);
        if (info.island == kNoIsland) {
            std::copy(std::begin(kAtlasBorder), std::end(kAtlasBorder), &color[0]);
        } else {
            islandColor(info.island, 1.0f);
        }
        float *lineColor = &color[0];
        if (info.uses == 2 && info.island != kNoIsland) {
            // Interior edges fade towards the atlas background so the silhouette reads clearly.
            for (int channel = 0; channel < 3; ++channel) {
                lineColor[channel] = 0.35f * lineColor[channel] + 0.65f * kAtlasBackground[channel];
            }
        }
        pushLine(layout.uvs[a * 2], layout.uvs[a * 2 + 1], layout.uvs[b * 2],
                 layout.uvs[b * 2 + 1], lineColor);
    }
    uvEdgeCount_ = static_cast<uint32_t>(vertices.size() / 6) - kAtlasBackgroundVertices -
                   uvGridCount_ - uvTriCount_;
    if (vertices.empty()) {
        return;
    }

    if (uvVertexArray_ == 0) {
        glGenVertexArrays(1, &uvVertexArray_);
    }
    if (uvVertexBuffer_ == 0) {
        glGenBuffers(1, &uvVertexBuffer_);
    }
    glBindVertexArray(uvVertexArray_);
    glBindBuffer(GL_ARRAY_BUFFER, uvVertexBuffer_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
                 vertices.data(), GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          reinterpret_cast<void *>(2 * sizeof(float)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

void Renderer::drawUv() {
    if (uvProgram_ == 0) {
        return;
    }
    int rect[4];
    rectOf(1, rect);
    if (rect[2] <= 0 || rect[3] <= 0) {
        return;
    }
    rebuildUvGeometry();
    float affine[4];
    uvAffine(rect, affine);

    // The atlas is flat: no depth, and the island fills blend so overlaps pile up visibly.
    glDisable(GL_DEPTH_TEST);
    drawUvBackground(affine);

    glUseProgram(uvProgram_);
    glUniform2f(uvUniformScale_, affine[0], affine[1]);
    glUniform2f(uvUniformOffset_, affine[2], affine[3]);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(uvVertexArray_);

    // Where the composed picture exists it already covers the square; without one the atlas still
    // has to read as a square.
    uint32_t first = 0;
    if (!hasTexture_) {
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(first), kAtlasBackgroundVertices);
    }
    first += kAtlasBackgroundVertices;
    if (uvOverlayVisible()) {
        glDrawArrays(GL_LINES, static_cast<GLint>(first), static_cast<GLsizei>(uvGridCount_));
        first += uvGridCount_;
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(first),
                     static_cast<GLsizei>(uvTriCount_));
        first += uvTriCount_;
        glDrawArrays(GL_LINES, static_cast<GLint>(first), static_cast<GLsizei>(uvEdgeCount_));
    }
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

}  // namespace g3d
