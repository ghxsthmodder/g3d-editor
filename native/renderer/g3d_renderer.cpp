#include "renderer/g3d_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>
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

namespace {

#ifdef __ANDROID__
constexpr const char *kTag = "G3DRender";
#endif
constexpr float kFovDegrees = 45.0f;
constexpr float kBackground[3] = {0.094f, 0.106f, 0.125f};
constexpr float kOutlineColor[4] = {0.98f, 0.72f, 0.22f, 1.0f};
constexpr float kPi = 3.14159265358979323846f;

const char *kVertexShader = R"GLSL(#version 300 es
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
uniform mat4 uViewProj;
out vec3 vNormal;
void main() {
    vNormal = aNormal;
    gl_Position = uViewProj * vec4(aPosition, 1.0);
}
)GLSL";

const char *kFragmentShader = R"GLSL(#version 300 es
precision highp float;
in vec3 vNormal;
uniform vec4 uColor;
uniform vec3 uLightDir;
uniform int uShading;
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
    float lit = max(dot(normal, normalize(uLightDir)), 0.0);
    float sky = 0.5 + 0.5 * normal.y;
    outColor = vec4(uColor.rgb * (0.24 + 0.62 * lit + 0.14 * sky), 1.0);
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
    contextReady_ = false;

    if (!buildShaders()) {
        return false;
    }
    glEnable(GL_DEPTH_TEST);
    glClearColor(kBackground[0], kBackground[1], kBackground[2], 1.0f);
    contextReady_ = true;
    uploadScene();
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
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
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

glm::mat4 Renderer::viewProjection() const {
    std::lock_guard<std::mutex> guard(cameraMutex_);
    const glm::vec3 direction(std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_),
                              std::cos(pitch_) * std::cos(yaw_));
    const glm::vec3 eye = center_ + direction * distance_;
    const float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    const float nearPlane = std::max(radius_ * 0.02f, 0.01f);
    const float farPlane = std::max(radius_ * 60.0f, (distance_ + radius_) * 6.0f);
    const glm::mat4 projection =
        glm::perspective(glm::radians(kFovDegrees), aspect, nearPlane, farPlane);
    const glm::mat4 view = glm::lookAt(eye, center_, glm::vec3(0.0f, 1.0f, 0.0f));
    return projection * view;
}

void Renderer::draw() {
    if (!contextReady_) {
        return;
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (items_.empty()) {
        return;
    }

    const glm::mat4 matrix = this->viewProjection();
    glUseProgram(program_);
    glUniformMatrix4fv(uniformViewProj_, 1, GL_FALSE, &matrix[0][0]);
    glUniform3f(uniformLightDir_, 0.35f, 0.85f, 0.4f);
    glUniform1i(uniformShading_, shadingMode_);

    // Push the triangles a little away from the camera so the selection outline, which sits on the
    // very same edges, wins the depth test instead of flickering against it.
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 2.0f);
    for (Item &item : items_) {
        glBindVertexArray(item.vertexArray);
        glUniform4f(uniformColor_, item.color[0], item.color[1], item.color[2], item.color[3]);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(item.indexCount), GL_UNSIGNED_INT,
                       reinterpret_cast<const void *>(0));
    }
    glDisable(GL_POLYGON_OFFSET_FILL);

    // Read every frame so a tap in the hierarchy panel highlights without re-uploading geometry.
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

    const glm::mat4 matrix = this->viewProjection();
    const glm::mat4 inverted = glm::inverse(matrix);
    const glm::vec3 eye = eyePosition();

    int viewportWidth = 1;
    int viewportHeight = 1;
    {
        std::lock_guard<std::mutex> guard(cameraMutex_);
        viewportWidth = width_;
        viewportHeight = height_;
    }

    // Device pixels map straight to the viewport: the view fills the window.
    const float ndcX = 2.0f * xPixels / static_cast<float>(viewportWidth) - 1.0f;
    const float ndcY = 1.0f - 2.0f * yPixels / static_cast<float>(viewportHeight);
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

void Renderer::setShadingMode(int mode) { shadingMode_ = mode == 1 ? 1 : 0; }

}  // namespace g3d
