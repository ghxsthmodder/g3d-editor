// JNI surface of the editor. Kept deliberately small and coarse: the UI sends whole operations,
// never per-vertex or per-pixel calls.

#include <jni.h>
#include <android/log.h>

#include <sstream>
#include <limits>
#include <string>
#include <vector>

#include "core/document.hpp"
#include "core/primitives.hpp"
#include "renderer/g3d_renderer.hpp"
#include "uv/uv_service.hpp"

namespace {

constexpr const char *kTag = "G3DEditor";

const char *currentAbi() {
#if defined(__aarch64__)
    return "arm64-v8a";
#elif defined(__arm__)
    return "armeabi-v7a";
#elif defined(__x86_64__)
    return "x86_64";
#elif defined(__i386__)
    return "x86";
#else
    return "unknown";
#endif
}

std::string uvRangeReport(const g3d::UvResult &result) {
    float minimum = std::numeric_limits<float>::max();
    float maximum = std::numeric_limits<float>::lowest();
    for (const float value : result.mesh.uvs) {
        minimum = value < minimum ? value : minimum;
        maximum = value > maximum ? value : maximum;
    }
    std::ostringstream stream;
    stream << "uv range=[" << minimum << ".." << maximum << "] "
           << (minimum >= 0.0f && maximum <= 1.0f ? "inside atlas" : "OUTSIDE ATLAS");
    return stream.str();
}

jstring toJString(JNIEnv *env, const std::string &text) {
    return env->NewStringUTF(text.c_str());
}

}  // namespace

extern "C" {

JNIEXPORT jstring JNICALL Java_com_g3d_editor_Native_nativeBuildInfo(JNIEnv *env, jclass) {
    std::ostringstream stream;
    stream << "abi=" << currentAbi() << " api=" << __ANDROID_API__
           << " core=g3d-native xatlas=f700c77 assimp=6.0.5 glm=1.0.2 stb=a7d4c21";
    return toJString(env, stream.str());
}

// --- document -------------------------------------------------------------------------

JNIEXPORT jstring JNICALL Java_com_g3d_editor_Native_nativeImportModel(JNIEnv *env, jclass,
                                                                       jbyteArray data,
                                                                       jstring fileNameHint) {
    if (data == nullptr) {
        return toJString(env, "ERROR: arquivo vazio");
    }
    std::string hint;
    if (fileNameHint != nullptr) {
        const char *chars = env->GetStringUTFChars(fileNameHint, nullptr);
        if (chars != nullptr) {
            hint = chars;
            env->ReleaseStringUTFChars(fileNameHint, chars);
        }
    }

    const jsize length = env->GetArrayLength(data);
    // Copied out of the JVM before importing: the JNI array accessors must not be held across the
    // multi-second work an assimp import can take.
    std::vector<jbyte> buffer(static_cast<size_t>(length > 0 ? length : 0));
    if (!buffer.empty()) {
        env->GetByteArrayRegion(data, 0, length, buffer.data());
    }

    std::string error;
    const bool imported =
        g3d::Document::instance().importModel(buffer.data(), static_cast<size_t>(length), hint,
                                              error);

    if (!imported) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "import %s failed: %s", hint.c_str(),
                            error.c_str());
        return toJString(env, "ERROR: " + error);
    }
    const std::string report = g3d::Document::instance().lastImportReport();
    __android_log_print(ANDROID_LOG_INFO, kTag, "import %s: %s", hint.c_str(), report.c_str());
    return toJString(env, "OK\n" + report);
}

JNIEXPORT jstring JNICALL Java_com_g3d_editor_Native_nativeSceneInfo(JNIEnv *env, jclass) {
    g3d::Document &document = g3d::Document::instance();
    std::ostringstream stream;
    stream << document.describe() << "\n" << document.hierarchy();
    return toJString(env, stream.str());
}

JNIEXPORT void JNICALL Java_com_g3d_editor_Native_nativeSelectMesh(JNIEnv *, jclass, jint index) {
    g3d::Document::instance().select(index);
}

// --- renderer -------------------------------------------------------------------------
// All of these must run on the GL thread; the Kotlin view queues them through queueEvent().

JNIEXPORT jboolean JNICALL Java_com_g3d_editor_Native_nativeRenderInit(JNIEnv *, jclass) {
    const bool ready = g3d::Renderer::instance().onContextCreated();
    if (!ready) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "GL context setup failed");
    }
    return ready ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_g3d_editor_Native_nativeRenderResize(JNIEnv *, jclass, jint width,
                                                                     jint height) {
    g3d::Renderer::instance().onSurfaceChanged(width, height);
}

JNIEXPORT void JNICALL Java_com_g3d_editor_Native_nativeRenderUpload(JNIEnv *, jclass) {
    g3d::Renderer::instance().uploadScene();
}

JNIEXPORT void JNICALL Java_com_g3d_editor_Native_nativeRenderDraw(JNIEnv *, jclass) {
    g3d::Renderer::instance().draw();
}

JNIEXPORT void JNICALL Java_com_g3d_editor_Native_nativeRenderOrbit(JNIEnv *, jclass, jfloat dx,
                                                                    jfloat dy) {
    g3d::Renderer::instance().orbit(dx, dy);
}

JNIEXPORT void JNICALL Java_com_g3d_editor_Native_nativeRenderPan(JNIEnv *, jclass, jfloat dx,
                                                                  jfloat dy) {
    g3d::Renderer::instance().pan(dx, dy);
}

JNIEXPORT void JNICALL Java_com_g3d_editor_Native_nativeRenderZoom(JNIEnv *, jclass, jfloat factor) {
    g3d::Renderer::instance().zoom(factor);
}

JNIEXPORT void JNICALL Java_com_g3d_editor_Native_nativeRenderFrameScene(JNIEnv *, jclass) {
    g3d::Renderer::instance().frameScene();
}

JNIEXPORT jint JNICALL Java_com_g3d_editor_Native_nativeRenderPick(JNIEnv *, jclass, jfloat x,
                                                                   jfloat y) {
    return g3d::Renderer::instance().pickAt(x, y);
}

JNIEXPORT void JNICALL Java_com_g3d_editor_Native_nativeRenderSetShading(JNIEnv *, jclass,
                                                                         jint mode) {
    g3d::Renderer::instance().setShadingMode(mode);
}

JNIEXPORT jstring JNICALL Java_com_g3d_editor_Native_nativeUvSmokeTest(JNIEnv *env, jclass,
                                                                        jint padding,
                                                                        jint resolution) {
    const g3d::Mesh cube = g3d::makeCube(0.5f);

    std::string error;
    if (!cube.validate(error)) {
        return toJString(env, "ERROR: cube validation: " + error);
    }

    g3d::UvOptions options;
    options.padding = padding > 0 ? static_cast<uint32_t>(padding) : 2u;
    options.resolution = resolution > 0 ? static_cast<uint32_t>(resolution) : 0u;

    g3d::UvResult result;
    if (!g3d::generateUv(cube, options, result, error)) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "uv smoke test failed: %s", error.c_str());
        return toJString(env, "ERROR: " + error);
    }

    std::string validationError;
    if (!result.mesh.validate(validationError)) {
        return toJString(env, "ERROR: generated mesh invalid: " + validationError);
    }

    uint32_t unassigned = 0;
    for (const uint32_t chartId : result.faceChartIds) {
        if (chartId == std::numeric_limits<uint32_t>::max()) {
            ++unassigned;
        }
    }

    std::ostringstream stream;
    stream << "input: " << g3d::describeMesh(cube) << "\n"
           << "output: " << result.describe() << "\n"
           << uvRangeReport(result) << "\n"
           << "faces without island: " << unassigned << "\n"
           << "generated mesh is valid";
    const std::string report = stream.str();
    __android_log_print(ANDROID_LOG_INFO, kTag, "%s", report.c_str());
    return toJString(env, report);
}

}  // extern "C"
