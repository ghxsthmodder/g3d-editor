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
#include "uv/uv_editor.hpp"
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

// --- editor UV -------------------------------------------------------------------------
// Each call is one whole operation on the native session, which guards itself, so these run on any
// UI thread. The heavy ones (generate, apply) are expected to be called off the main thread.

JNIEXPORT jstring JNICALL Java_com_g3d_editor_Native_nativeUvBegin(JNIEnv *env, jclass,
                                                                   jint index) {
    std::string error;
    if (!g3d::UvEditor::instance().begin(index < 0 ? 0u : static_cast<uint32_t>(index), error)) {
        return toJString(env, "ERROR: " + error);
    }
    g3d::UvEditor &editor = g3d::UvEditor::instance();
    std::ostringstream stream;
    stream << "OK\n" << editor.describe() << "\n" << editor.report();
    return toJString(env, stream.str());
}

JNIEXPORT jstring JNICALL Java_com_g3d_editor_Native_nativeUvGenerate(JNIEnv *env, jclass,
                                                                      jint index, jint padding,
                                                                      jint resolution) {
    g3d::UvOptions options;
    options.padding = padding > 0 ? static_cast<uint32_t>(padding) : 2u;
    options.resolution = resolution > 0 ? static_cast<uint32_t>(resolution) : 0u;

    std::string error;
    if (!g3d::UvEditor::instance().generate(
            index < 0 ? 0u : static_cast<uint32_t>(index), options, error)) {
        return toJString(env, "ERROR: " + error);
    }
    g3d::UvEditor &editor = g3d::UvEditor::instance();
    std::ostringstream stream;
    stream << "OK\n" << editor.describe() << "\n" << editor.report();
    __android_log_print(ANDROID_LOG_INFO, kTag, "uv generate mesh=%d: %s", index,
                        editor.describe().c_str());
    return toJString(env, stream.str());
}

JNIEXPORT jstring JNICALL Java_com_g3d_editor_Native_nativeUvApply(JNIEnv *env, jclass) {
    std::string error;
    if (!g3d::UvEditor::instance().apply(error)) {
        return toJString(env, "ERROR: " + error);
    }
    return toJString(env, "OK");
}

JNIEXPORT void JNICALL Java_com_g3d_editor_Native_nativeUvCancel(JNIEnv *, jclass) {
    g3d::UvEditor::instance().cancel();
}

// State and validation as text, so the panel and the log can show what the session thinks.
JNIEXPORT jstring JNICALL Java_com_g3d_editor_Native_nativeUvStatus(JNIEnv *env, jclass) {
    g3d::UvEditor &editor = g3d::UvEditor::instance();
    std::ostringstream stream;
    stream << editor.describe() << "\n" << editor.report();
    return toJString(env, stream.str());
}

JNIEXPORT jboolean JNICALL Java_com_g3d_editor_Native_nativeUvSelect(JNIEnv *, jclass, jint island,
                                                                     jboolean additive) {
    return g3d::UvEditor::instance().select(
               island < 0 ? 0u : static_cast<uint32_t>(island), additive == JNI_TRUE)
               ? JNI_TRUE
               : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_g3d_editor_Native_nativeUvSelectAll(JNIEnv *, jclass) {
    g3d::UvEditor::instance().selectAll();
}

JNIEXPORT void JNICALL Java_com_g3d_editor_Native_nativeUvClearSelection(JNIEnv *, jclass) {
    g3d::UvEditor::instance().clearSelection();
}

JNIEXPORT jboolean JNICALL Java_com_g3d_editor_Native_nativeUvTranslate(JNIEnv *, jclass, jfloat du,
                                                                        jfloat dv) {
    return g3d::UvEditor::instance().translate(du, dv) ? JNI_TRUE : JNI_FALSE;
}

// Rotation and scaling pivot on the centre of the selection, which is the only place the user can
// see on the atlas without a gizmo.
JNIEXPORT jboolean JNICALL Java_com_g3d_editor_Native_nativeUvRotate(JNIEnv *, jclass,
                                                                     jfloat degrees) {
    g3d::UvEditor &editor = g3d::UvEditor::instance();
    float pivot[2];
    if (!editor.selectionPivot(pivot)) {
        return JNI_FALSE;
    }
    return editor.rotate(degrees * 0.017453292519943295f, pivot[0], pivot[1]) ? JNI_TRUE
                                                                              : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_g3d_editor_Native_nativeUvScale(JNIEnv *, jclass,
                                                                    jfloat factor) {
    g3d::UvEditor &editor = g3d::UvEditor::instance();
    float pivot[2];
    if (!editor.selectionPivot(pivot)) {
        return JNI_FALSE;
    }
    return editor.scale(factor, pivot[0], pivot[1]) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_g3d_editor_Native_nativeUvFit(JNIEnv *, jclass) {
    return g3d::UvEditor::instance().fit() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_g3d_editor_Native_nativeUvRepack(JNIEnv *, jclass,
                                                                     jint padding) {
    return g3d::UvEditor::instance().repack(padding > 0 ? static_cast<uint32_t>(padding) : 1u)
               ? JNI_TRUE
               : JNI_FALSE;
}

// --- vista e atlas ---------------------------------------------------------------------
// The atlas rectangle is derived from the surface size, so these read state the GL thread owns,
// but none of them issues a GL call: they are safe from the UI thread.

JNIEXPORT void JNICALL Java_com_g3d_editor_Native_nativeViewSetMode(JNIEnv *, jclass, jint mode) {
    g3d::Renderer::instance().setViewMode(mode);
}

JNIEXPORT void JNICALL Java_com_g3d_editor_Native_nativeUvPan(JNIEnv *, jclass, jfloat dx,
                                                              jfloat dy) {
    g3d::Renderer::instance().uvPan(dx, dy);
}

JNIEXPORT void JNICALL Java_com_g3d_editor_Native_nativeUvZoom(JNIEnv *, jclass, jfloat factor) {
    g3d::Renderer::instance().uvZoom(factor);
}

JNIEXPORT void JNICALL Java_com_g3d_editor_Native_nativeUvFrame(JNIEnv *, jclass) {
    g3d::Renderer::instance().uvFrame();
}

// Atlas coordinates under a surface pixel, written into out[0..1]. False when the atlas is hidden
// or the point falls outside it, which is how the view decides whom a gesture belongs to.
JNIEXPORT jboolean JNICALL Java_com_g3d_editor_Native_nativeUvAt(JNIEnv *env, jclass, jfloat x,
                                                                 jfloat y, jfloatArray out) {
    if (out == nullptr || env->GetArrayLength(out) < 2) {
        return JNI_FALSE;
    }
    float uv[2];
    if (!g3d::Renderer::instance().uvAt(x, y, uv)) {
        return JNI_FALSE;
    }
    env->SetFloatArrayRegion(out, 0, 2, uv);
    return JNI_TRUE;
}

// Island under a surface pixel, or -1. Additive selection is the caller's decision.
JNIEXPORT jint JNICALL Java_com_g3d_editor_Native_nativeUvPick(JNIEnv *, jclass, jfloat x,
                                                               jfloat y) {
    float uv[2];
    if (!g3d::Renderer::instance().uvAt(x, y, uv)) {
        return -1;
    }
    return static_cast<jint>(g3d::UvEditor::instance().pick(uv[0], uv[1]));
}

}  // extern "C"
