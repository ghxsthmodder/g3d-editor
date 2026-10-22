// JNI surface of the editor. Kept deliberately small and coarse: the UI sends whole operations,
// never per-vertex or per-pixel calls.

#include <jni.h>
#include <android/log.h>

#include <exception>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "core/document.hpp"
#include "core/primitives.hpp"
#include "exporters/model_exporter.hpp"
#include "importers/assimp_importer.hpp"
#include "project/project_io.hpp"
#include "renderer/g3d_renderer.hpp"
#include "texture/g3d_image.hpp"
#include "texture/texture_studio.hpp"
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

// Every compositor mutator answers with the same summary line, so the panel never has to guess
// what the stack looks like now.
jstring studioReply(JNIEnv *env, const char *prefix) {
    std::ostringstream stream;
    stream << prefix << "\n" << g3d::TextureStudio::instance().describe();
    return toJString(env, stream.str());
}

jbyteArray toByteArray(JNIEnv *env, const std::vector<uint8_t> &bytes) {
    jbyteArray out = env->NewByteArray(static_cast<jsize>(bytes.size()));
    if (out != nullptr) {
        env->SetByteArrayRegion(out, 0, static_cast<jsize>(bytes.size()),
                                reinterpret_cast<const jbyte *>(bytes.data()));
    }
    return out;
}

std::string readString(JNIEnv *env, jstring text) {
    std::string out;
    if (text != nullptr) {
        const char *chars = env->GetStringUTFChars(text, nullptr);
        if (chars != nullptr) {
            out = chars;
            env->ReleaseStringUTFChars(text, chars);
        }
    }
    return out;
}

// The three formats the interface offers, in the order Native.kt documents them.
g3d::ModelFormat modelFormatOf(jint format) {
    switch (format) {
    case 1:
        return g3d::ModelFormat::Gltf;
    case 2:
        return g3d::ModelFormat::Glb;
    default:
        return g3d::ModelFormat::Obj;
    }
}

// Running out of memory is a fact about the device, not a bug in the core: a 4096 x 4096 canvas, a
// 90 MB model or a xatlas generation all ask for more than a phone may have to give. The JVM has
// no handler for a C++ exception crossing a native method and kills the process on one, so the
// entry points that allocate in bulk answer through these guards instead of unwinding into Java
// frames.
std::string memoryFailure(const char *what, const std::exception &error) {
    __android_log_print(ANDROID_LOG_ERROR, kTag, "%s: %s", what, error.what());
    const std::string reason =
        std::string(error.what()).find("bad_alloc") != std::string::npos
            ? "faltou memoria"
            : error.what();
    return "ERROR: nao foi possivel " + std::string(what) + ": " + reason;
}

template <typename Fn>
std::string guardText(const char *what, Fn fn) {
    try {
        return fn();
    } catch (const std::exception &error) {
        return memoryFailure(what, error);
    }
}

// For the replies that are an array: null is all Kotlin gets, which is how it already reads a
// failure of these functions, and the reason lands in logcat beside the ones reported by hand.
template <typename Fn>
bool guardCall(const char *what, Fn fn) {
    try {
        fn();
        return true;
    } catch (const std::exception &error) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "%s: %s", what, error.what());
        return false;
    }
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
    return toJString(env, guardText("importar o modelo", [&] {
        if (data == nullptr) {
            return std::string("ERROR: arquivo vazio");
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
        // Copied out of the JVM before importing: the JNI array accessors must not be held
        // across the multi-second work an assimp import can take.
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
            return "ERROR: " + error;
        }
        const std::string report = g3d::Document::instance().lastImportReport();
        __android_log_print(ANDROID_LOG_INFO, kTag, "import %s: %s", hint.c_str(), report.c_str());
        return "OK\n" + report;
    }));
}

JNIEXPORT jstring JNICALL Java_com_g3d_editor_Native_nativeSceneInfo(JNIEnv *env, jclass) {
    g3d::Document &document = g3d::Document::instance();
    std::ostringstream stream;
    stream << document.describe() << "\n" << document.hierarchy();
    return toJString(env, stream.str());
}

/**
 * What the editor holds, in bytes, counted by part: the file the model came in with, the geometry
 * built out of it, the compositor with its composed picture, and the open UV session with its
 * history. Numbers rather than a sentence, because the panel decides the words.
 */
JNIEXPORT jstring JNICALL Java_com_g3d_editor_Native_nativeMemoryInfo(JNIEnv *env, jclass) {
    std::ostringstream stream;
    stream << "source=" << g3d::Document::instance().sourceBytes()
           << " meshes=" << g3d::Document::instance().meshBytes()
           << " layers=" << g3d::TextureStudio::instance().memoryBytes()
           << " edit=" << g3d::UvEditor::instance().memoryBytes();
    return toJString(env, stream.str());
}

JNIEXPORT void JNICALL Java_com_g3d_editor_Native_nativeSelectMesh(JNIEnv *, jclass, jint index) {
    g3d::Document::instance().select(index);
}

// How much of this device the editor is willing to spend on one scene. The app measures what is
// free before an import; the importer decides what that number means in vertices.
JNIEXPORT void JNICALL Java_com_g3d_editor_Native_nativeSetImportBudget(JNIEnv *, jclass,
                                                                        jlong bytes) {
    g3d::setImportBudgetBytes(bytes > 0 ? static_cast<size_t>(bytes) : 0u);
}

// --- renderer -------------------------------------------------------------------------
// All of these must run on the GL thread; the Kotlin view queues them through queueEvent().

JNIEXPORT jboolean JNICALL Java_com_g3d_editor_Native_nativeRenderInit(JNIEnv *, jclass) {
    bool ready = false;
    guardCall("criar o contexto GL", [&] {
        ready = g3d::Renderer::instance().onContextCreated();
        if (!ready) {
            __android_log_print(ANDROID_LOG_ERROR, kTag, "GL context setup failed");
        }
    });
    return ready ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_g3d_editor_Native_nativeRenderResize(JNIEnv *, jclass, jint width,
                                                                     jint height) {
    g3d::Renderer::instance().onSurfaceChanged(width, height);
}

JNIEXPORT void JNICALL Java_com_g3d_editor_Native_nativeRenderUpload(JNIEnv *, jclass) {
    // The two GL entry points that allocate: a frame that cannot get its geometry or its composed
    // picture is a frame that does not show it, not a reason to stop the process.
    guardCall("enviar a geometria", [&] { g3d::Renderer::instance().uploadScene(); });
}

JNIEXPORT void JNICALL Java_com_g3d_editor_Native_nativeRenderDraw(JNIEnv *, jclass) {
    guardCall("desenhar o quadro", [&] { g3d::Renderer::instance().draw(); });
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
    // Opening a session copies the whole mesh, so both of these allocation-heavy calls are guarded.
    return toJString(env, guardText("abrir a edicao de UV", [&] {
        std::string error;
        if (!g3d::UvEditor::instance().begin(index < 0 ? 0u : static_cast<uint32_t>(index),
                                             error)) {
            return "ERROR: " + error;
        }
        g3d::UvEditor &editor = g3d::UvEditor::instance();
        std::ostringstream stream;
        stream << "OK\n" << editor.describe() << "\n" << editor.report();
        return stream.str();
    }));
}

JNIEXPORT jstring JNICALL Java_com_g3d_editor_Native_nativeUvGenerate(JNIEnv *env, jclass,
                                                                      jint index, jint padding,
                                                                      jint resolution) {
    return toJString(env, guardText("gerar os UVs", [&] {
        g3d::UvOptions options;
        options.padding = padding > 0 ? static_cast<uint32_t>(padding) : 2u;
        options.resolution = resolution > 0 ? static_cast<uint32_t>(resolution) : 0u;

        std::string error;
        if (!g3d::UvEditor::instance().generate(
                index < 0 ? 0u : static_cast<uint32_t>(index), options, error)) {
            return "ERROR: " + error;
        }
        g3d::UvEditor &editor = g3d::UvEditor::instance();
        std::ostringstream stream;
        stream << "OK\n" << editor.describe() << "\n" << editor.report();
        __android_log_print(ANDROID_LOG_INFO, kTag, "uv generate mesh=%d: %s", index,
                            editor.describe().c_str());
        return stream.str();
    }));
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
    stream << editor.describe() << "\n" << editor.report() << "\n" << editor.history();
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
    // Repacking asks xatlas for a layout of every island, which is the second biggest allocation
    // the editor makes after a generation.
    bool packed = false;
    guardCall("reempacotar as ilhas", [&] {
        const uint32_t pad = padding > 0 ? static_cast<uint32_t>(padding) : 1u;
        packed = g3d::UvEditor::instance().repack(pad);
    });
    return packed ? JNI_TRUE : JNI_FALSE;
}

// --- undo and redo ---------------------------------------------------------------------

JNIEXPORT jboolean JNICALL Java_com_g3d_editor_Native_nativeUvUndo(JNIEnv *, jclass) {
    return g3d::UvEditor::instance().undo() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_g3d_editor_Native_nativeUvRedo(JNIEnv *, jclass) {
    return g3d::UvEditor::instance().redo() ? JNI_TRUE : JNI_FALSE;
}

// --- vista e atlas ---------------------------------------------------------------------
// The atlas rectangle is derived from the surface size, so these read state the GL thread owns,
// but none of them issues a GL call: they are safe from the UI thread.

JNIEXPORT void JNICALL Java_com_g3d_editor_Native_nativeViewSetMode(JNIEnv *, jclass, jint mode) {
    g3d::Renderer::instance().setViewMode(mode);
}

JNIEXPORT void JNICALL Java_com_g3d_editor_Native_nativeSetSplitRatio(JNIEnv *, jclass,
                                                                      jfloat ratio) {
    g3d::Renderer::instance().setSplitRatio(ratio);
}

JNIEXPORT jboolean JNICALL Java_com_g3d_editor_Native_nativeUvGrab(JNIEnv *, jclass, jfloat x,
                                                                   jfloat y,
                                                                   jboolean additive) {
    g3d::Renderer &renderer = g3d::Renderer::instance();
    const bool grabbed = renderer.uvGrab(x, y, additive == JNI_TRUE);
    // The grab is the start of the drag: from here the moves belong to one undo step until the
    // finger lifts and nativeUvEndGesture is called.
    if (grabbed) {
        g3d::UvEditor::instance().beginGesture();
    }
    return grabbed ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_g3d_editor_Native_nativeUvEndGesture(JNIEnv *, jclass) {
    g3d::UvEditor::instance().endGesture();
}

JNIEXPORT void JNICALL Java_com_g3d_editor_Native_nativeUvDragPixels(JNIEnv *, jclass, jfloat dx,
                                                                     jfloat dy) {
    g3d::Renderer::instance().uvDragPixels(dx, dy);
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

// The UV wireframe is a viewing aid, so hiding it is a renderer state and not a change to the
// composed picture.
JNIEXPORT void JNICALL Java_com_g3d_editor_Native_nativeSetUvOverlay(JNIEnv *, jclass,
                                                                     jboolean visible) {
    g3d::Renderer::instance().setUvOverlayVisible(visible == JNI_TRUE);
}

// --- compositor de texturas ---------------------------------------------------------------
// The stack guards itself, so these run on any thread. Only importing and exporting do enough
// work to be worth a background thread; the UI refreshes the list from nativeTextureInfo().

JNIEXPORT jstring JNICALL
Java_com_g3d_editor_Native_nativeTextureImportImage(JNIEnv *env, jclass, jbyteArray data,
                                                   jstring fileNameHint) {
    return toJString(env, guardText("importar a imagem", [&] {
        if (data == nullptr || env->GetArrayLength(data) == 0) {
            return std::string("ERROR: arquivo vazio");
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
        std::vector<jbyte> buffer(static_cast<size_t>(length));
        env->GetByteArrayRegion(data, 0, length, buffer.data());

        // Decoding and composing are CPU work on a buffer the JVM owns; the copy above releases the
        // array before any of it happens.
        g3d::Image image;
        std::string error;
        if (!g3d::decodeImage(buffer.data(), static_cast<size_t>(length), hint, image, error)) {
            return "ERROR: " + error;
        }
        const int id = g3d::TextureStudio::instance().addLayer(image, error);
        if (id <= 0) {
            return "ERROR: " + error;
        }
        std::ostringstream stream;
        stream << "OK\ncamada=" << id << "\n" << g3d::TextureStudio::instance().describe();
        __android_log_print(ANDROID_LOG_INFO, kTag, "texture layer %d from %s", id, hint.c_str());
        return stream.str();
    }));
}

JNIEXPORT jstring JNICALL Java_com_g3d_editor_Native_nativeTextureInfo(JNIEnv *env, jclass) {
    g3d::TextureStudio &studio = g3d::TextureStudio::instance();
    std::ostringstream stream;
    stream << studio.describe() << "\n" << studio.layerRows();
    return toJString(env, stream.str());
}

JNIEXPORT jboolean JNICALL Java_com_g3d_editor_Native_nativeTextureHasCanvas(JNIEnv *, jclass) {
    g3d::TextureStudio &studio = g3d::TextureStudio::instance();
    return studio.canvasWidth() > 0 && studio.canvasHeight() > 0 ? JNI_TRUE : JNI_FALSE;
}

// 0 until the GL thread has a context to ask; the interface dims the resolutions above it.
JNIEXPORT jint JNICALL Java_com_g3d_editor_Native_nativeTextureDeviceLimit(JNIEnv *, jclass) {
    return static_cast<jint>(g3d::TextureStudio::instance().deviceLimit());
}

JNIEXPORT jstring JNICALL Java_com_g3d_editor_Native_nativeTextureSetCanvas(JNIEnv *env, jclass,
                                                                            jint width,
                                                                            jint height) {
    std::string error;
    if (!g3d::TextureStudio::instance().setCanvas(width, height, error)) {
        return toJString(env, "ERROR: " + error);
    }
    return studioReply(env, "OK");
}

// 0xRRGGBB, which is how Android already spells a colour.
JNIEXPORT void JNICALL Java_com_g3d_editor_Native_nativeTextureSetBackground(JNIEnv *, jclass,
                                                                             jint rgb) {
    const uint8_t colour[3] = {
        static_cast<uint8_t>((rgb >> 16) & 0xff),
        static_cast<uint8_t>((rgb >> 8) & 0xff),
        static_cast<uint8_t>(rgb & 0xff),
    };
    g3d::TextureStudio::instance().setBackground(colour);
}

JNIEXPORT jboolean JNICALL Java_com_g3d_editor_Native_nativeTextureRemove(JNIEnv *, jclass,
                                                                          jint id) {
    return g3d::TextureStudio::instance().removeLayer(id) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL Java_com_g3d_editor_Native_nativeTextureDuplicate(JNIEnv *env, jclass,
                                                                           jint id) {
    int newId = 0;
    std::string error;
    if (!g3d::TextureStudio::instance().duplicateLayer(id, newId, error)) {
        return toJString(env, "ERROR: " + error);
    }
    std::ostringstream stream;
    stream << "OK\ncamada=" << newId;
    return toJString(env, stream.str());
}

JNIEXPORT jboolean JNICALL Java_com_g3d_editor_Native_nativeTextureMoveUp(JNIEnv *, jclass,
                                                                          jint id) {
    return g3d::TextureStudio::instance().moveUp(id) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_g3d_editor_Native_nativeTextureMoveDown(JNIEnv *, jclass,
                                                                            jint id) {
    return g3d::TextureStudio::instance().moveDown(id) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_g3d_editor_Native_nativeTextureSetVisible(JNIEnv *, jclass,
                                                                              jint id,
                                                                              jboolean visible) {
    return g3d::TextureStudio::instance().setVisible(id, visible == JNI_TRUE) ? JNI_TRUE
                                                                              : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_g3d_editor_Native_nativeTextureSetOpacity(JNIEnv *, jclass,
                                                                              jint id,
                                                                              jfloat opacity) {
    return g3d::TextureStudio::instance().setOpacity(id, opacity) ? JNI_TRUE : JNI_FALSE;
}

// Distances are canvas pixels, with y growing downwards like it does in the exported PNG.
JNIEXPORT jboolean JNICALL Java_com_g3d_editor_Native_nativeTextureTranslate(JNIEnv *, jclass,
                                                                             jint id, jfloat dx,
                                                                             jfloat dy) {
    return g3d::TextureStudio::instance().translate(id, dx, dy) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_g3d_editor_Native_nativeTextureRotate(JNIEnv *, jclass, jint id,
                                                                          jfloat degrees) {
    return g3d::TextureStudio::instance().rotate(id, degrees) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL Java_com_g3d_editor_Native_nativeTextureScale(JNIEnv *, jclass, jint id,
                                                                         jfloat factor) {
    return g3d::TextureStudio::instance().scaleBy(id, factor) ? JNI_TRUE : JNI_FALSE;
}

// The exported file is the exact picture the preview uploads, straight out of the compositor: no
// second code path that could disagree with the screen. Null when there is nothing to write; the
// reason goes to logcat because a byte array has no room for a message.
JNIEXPORT jbyteArray JNICALL Java_com_g3d_editor_Native_nativeTexturePng(JNIEnv *env, jclass) {
    std::string error;
    g3d::Image picture;
    std::vector<uint8_t> png;
    jbyteArray out = nullptr;
    // Composing paints a canvas-sized picture and encoding keeps its bytes: both ask the allocator
    // for the biggest single buffer the compositor has.
    const bool composed = guardCall("compor a textura", [&] {
        if (!g3d::TextureStudio::instance().render(picture, error)) {
            __android_log_print(ANDROID_LOG_ERROR, kTag, "texture render: %s", error.c_str());
            return;
        }
        if (!g3d::encodePng(picture, png, error)) {
            __android_log_print(ANDROID_LOG_ERROR, kTag, "texture encode: %s", error.c_str());
            return;
        }
        out = toByteArray(env, png);
    });
    return composed ? out : nullptr;
}

// --- model export ----------------------------------------------------------------------

JNIEXPORT jstring JNICALL
Java_com_g3d_editor_Native_nativeExportWarnings(JNIEnv *env, jclass, jint format) {
    std::ostringstream stream;
    g3d::Document::instance().withScene([&stream, &format](g3d::Scene &scene) {
        for (const std::string &warning : g3d::exportWarnings(scene, modelFormatOf(format))) {
            stream << warning << "\n";
        }
    });
    return toJString(env, stream.str());
}

/**
 * One coarse operation: writes the open scene and hands back every file the format produces, the
 * one the user names first. Null when nothing could be exported; the reason goes to logcat.
 */
JNIEXPORT jobjectArray JNICALL
Java_com_g3d_editor_Native_nativeExportModel(JNIEnv *env, jclass, jstring baseName, jint format) {
    const std::string name = readString(env, baseName);
    g3d::ModelExport output;
    std::string error;
    bool exported = false;
    // An export holds every file of the format in memory before the picker writes them out, so a
    // big scene can be the thing that does not fit.
    const bool guarded = guardCall("exportar o modelo", [&] {
        g3d::Document::instance().withScene([&](g3d::Scene &scene) {
            exported = g3d::exportModel(scene, modelFormatOf(format), name, output, error);
        });
    });
    if (!guarded || !exported) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "export: %s", error.c_str());
        return nullptr;
    }

    jclass fileClass = env->FindClass("com/g3d/editor/Native$ExportedFile");
    if (fileClass == nullptr) {
        return nullptr;  // FindClass already reported the missing class
    }
    jmethodID constructor = env->GetMethodID(fileClass, "<init>", "(Ljava/lang/String;[B)V");
    if (constructor == nullptr) {
        return nullptr;
    }
    jobjectArray files =
        env->NewObjectArray(static_cast<jsize>(output.files.size()), fileClass, nullptr);
    if (files == nullptr) {
        return nullptr;
    }
    for (size_t i = 0; i < output.files.size(); ++i) {
        jstring fileName = env->NewStringUTF(output.files[i].name.c_str());
        jbyteArray bytes = toByteArray(env, output.files[i].bytes);
        jobject file = env->NewObject(fileClass, constructor, fileName, bytes);
        if (file == nullptr) {
            return nullptr;
        }
        env->SetObjectArrayElement(files, static_cast<jsize>(i), file);
        env->DeleteLocalRef(fileName);
        env->DeleteLocalRef(bytes);
        env->DeleteLocalRef(file);
    }
    return files;
}

// --- project file --------------------------------------------------------------------

/**
 * The editor as it stands, in the bytes of one .g3dp file, for Kotlin to hand to the storage
 * framework wherever the picker allows. Null when there is nothing to write; what went in, or why
 * nothing did, comes from nativeProjectReport().
 */
JNIEXPORT jbyteArray JNICALL Java_com_g3d_editor_Native_nativeProjectBytes(JNIEnv *env, jclass) {
    std::vector<uint8_t> file;
    g3d::ProjectReport report;
    std::string error;
    jbyteArray out = nullptr;
    // A project carries the model file and every layer picture inside one buffer, so it is the
    // largest allocation the editor makes.
    const bool saved = guardCall("gravar o projeto", [&] {
        if (!g3d::saveProject(file, report, error)) {
            __android_log_print(ANDROID_LOG_ERROR, kTag, "save project: %s", error.c_str());
            return;
        }
        out = toByteArray(env, file);
    });
    return saved ? out : nullptr;
}

JNIEXPORT jstring JNICALL Java_com_g3d_editor_Native_nativeProjectReport(JNIEnv *env, jclass) {
    return toJString(env, g3d::projectReport());
}

JNIEXPORT jstring JNICALL Java_com_g3d_editor_Native_nativeOpenProject(JNIEnv *env, jclass,
                                                                        jbyteArray data,
                                                                        jstring fileName) {
    return toJString(env, guardText("abrir o projeto", [&] {
        if (data == nullptr) {
            return std::string("ERROR: arquivo vazio");
        }
        const std::string name = readString(env, fileName);
        const jsize length = env->GetArrayLength(data);
        std::vector<jbyte> buffer(static_cast<size_t>(length > 0 ? length : 0));
        if (!buffer.empty()) {
            env->GetByteArrayRegion(data, 0, length, buffer.data());
        }

        g3d::ProjectReport report;
        std::string error;
        const bool opened = g3d::openProject(buffer.data(), static_cast<size_t>(length), name,
                                             report, error);
        const std::string text = g3d::projectReport();
        __android_log_print(ANDROID_LOG_INFO, kTag, "open project %s: %s", name.c_str(),
                            text.c_str());
        if (!opened) {
            return "ERROR: " + error;
        }
        return "OK\n" + text;
    }));
}

}  // extern "C"
