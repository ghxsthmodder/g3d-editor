package com.g3d.editor

/**
 * JNI wrapper around libg3dnative. Only whole operations cross this boundary: the UI sends a file
 * or a gesture, never per-vertex or per-pixel data.
 */
object Native {

    private val libraryLoadError: String? =
        runCatching { System.loadLibrary("g3dnative") }
            .exceptionOrNull()
            ?.let { "${it.javaClass.simpleName}: ${it.message}" }

    val isAvailable: Boolean
        get() = libraryLoadError == null

    val loadError: String?
        get() = libraryLoadError

    external fun nativeBuildInfo(): String

    external fun nativeUvSmokeTest(padding: Int, resolution: Int): String

    external fun nativeImportModel(data: ByteArray, fileNameHint: String): String

    external fun nativeSceneInfo(): String

    external fun nativeSelectMesh(index: Int)

    external fun nativeRenderInit(): Boolean

    external fun nativeRenderResize(width: Int, height: Int)

    external fun nativeRenderUpload()

    external fun nativeRenderDraw()

    external fun nativeRenderOrbit(dx: Float, dy: Float)

    external fun nativeRenderPan(dx: Float, dy: Float)

    external fun nativeRenderZoom(factor: Float)

    external fun nativeRenderFrameScene()

    external fun nativeRenderPick(x: Float, y: Float): Int

    external fun nativeRenderSetShading(mode: Int)
}
