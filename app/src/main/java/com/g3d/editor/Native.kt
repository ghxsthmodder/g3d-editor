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

    // --- editor UV ---
    // Each call is one whole operation on the native session. Only nativeUvGenerate is slow enough
    // to need a background thread.

    external fun nativeUvBegin(meshIndex: Int): String

    external fun nativeUvGenerate(meshIndex: Int, padding: Int, resolution: Int): String

    external fun nativeUvApply(): String

    external fun nativeUvCancel()

    external fun nativeUvStatus(): String

    external fun nativeUvSelect(island: Int, additive: Boolean): Boolean

    external fun nativeUvSelectAll()

    external fun nativeUvClearSelection()

    external fun nativeUvTranslate(du: Float, dv: Float): Boolean

    external fun nativeUvRotate(degrees: Float): Boolean

    external fun nativeUvScale(factor: Float): Boolean

    external fun nativeUvFit(): Boolean

    external fun nativeUvRepack(padding: Int): Boolean

    /**
     * Steps back and forth over the edits of the open session. A drag of the atlas is one step: the
     * grab opened it and [nativeUvEndGesture] closes it, so the five moves of a swipe are undone
     * together. Applying the layout closes the history, which then belongs to the document.
     */
    external fun nativeUvUndo(): Boolean

    external fun nativeUvRedo(): Boolean

    external fun nativeUvEndGesture()

    // --- vista e atlas ---

    external fun nativeViewSetMode(mode: Int)

    external fun nativeSetSplitRatio(ratio: Float)

    /**
     * Asks what a drag starting at a surface point would move, and makes that island part of the
     * selection. False means the gesture moves no islands, so the atlas should pan instead.
     */
    external fun nativeUvGrab(x: Float, y: Float, additive: Boolean): Boolean

    external fun nativeUvDragPixels(dx: Float, dy: Float)

    external fun nativeUvPan(dx: Float, dy: Float)

    external fun nativeUvZoom(factor: Float)

    external fun nativeUvFrame()

    external fun nativeUvAt(x: Float, y: Float, out: FloatArray): Boolean

    external fun nativeUvPick(x: Float, y: Float): Int

    external fun nativeSetUvOverlay(visible: Boolean)

    // --- compositor de texturas ---
    // The stack lives in native code and guards itself, so these are safe from the UI thread.
    // Importing and exporting are the two that can take long enough to need a background thread.

    external fun nativeTextureImportImage(data: ByteArray, fileNameHint: String): String

    external fun nativeTextureInfo(): String

    external fun nativeTextureHasCanvas(): Boolean

    external fun nativeTextureSetCanvas(width: Int, height: Int): String

    external fun nativeTextureSetBackground(rgb: Int)

    external fun nativeTextureRemove(id: Int): Boolean

    external fun nativeTextureDuplicate(id: Int): String

    external fun nativeTextureMoveUp(id: Int): Boolean

    external fun nativeTextureMoveDown(id: Int): Boolean

    external fun nativeTextureSetVisible(id: Int, visible: Boolean): Boolean

    external fun nativeTextureSetOpacity(id: Int, opacity: Float): Boolean

    external fun nativeTextureTranslate(id: Int, dxPixels: Float, dyPixels: Float): Boolean

    external fun nativeTextureRotate(id: Int, degrees: Float): Boolean

    external fun nativeTextureScale(id: Int, factor: Float): Boolean

    external fun nativeTexturePng(): ByteArray?

    /**
     * One file of a model export: the name to write it under, and its bytes. OBJ and glTF come out
     * as a pair, the description and the companion it references by that very name, so both have to
     * land in the same folder with those exact names.
     */
    class ExportedFile(val name: String, val bytes: ByteArray)

    /**
     * What the format cannot hold about the open scene, one line each, before the user picks a
     * folder. Exporting reports the same list.
     */
    external fun nativeExportWarnings(format: Int): String

    /**
     * Writes the open scene as `format`: 0 OBJ, 1 glTF, 2 GLB. `baseName` is the file name without
     * its extension, because the exporters put it inside the description as the reference to the
     * companion. Null when there is nothing to export.
     */
    external fun nativeExportModel(baseName: String, format: Int): Array<ExportedFile>?
}
