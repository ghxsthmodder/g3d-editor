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

    /**
     * What the import under way has to show: an empty string while nothing is running, and
     * "<percent>\t<phase>" while it is, the phase being the loop assimp is inside of — read for the
     * bytes of the file, post for the steps it runs over them, assemble for the walk that copies
     * the scene into the editor's own. Polled from the panel while [nativeImportModel] works on its
     * thread. There is no way to stop an import: the assimp in third_party reports its progress and
     * throws the answer away, so a request to quit would land on deaf ears.
     */
    external fun nativeImportProgress(): String

    external fun nativeSceneInfo(): String

    /**
     * Bytes held by the editor, as `source=N meshes=N layers=N edit=N`: the open file, the geometry
     * built out of it, the compositor with its composed picture and the open UV session with its
     * history. Numbers only, so the panel is the one that says the words.
     */
    external fun nativeMemoryInfo(): String

    external fun nativeSelectMesh(index: Int)

    /**
     * Ceiling, in bytes, for the geometry a single file is allowed to expand into. A glTF is a list
     * of references, so a small file can describe gigabytes of meshes; the importer stops at this
     * budget and reports what it left out. Pass 0 to keep the value already set.
     */
    external fun nativeSetImportBudget(bytes: Long)

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

    /**
     * What the generation under way has to show: an empty string while nothing is running, and
     * "<percent>\t<stage>" while it is, the stage being xatlas' own name for the step it is inside
     * of. Polled from the panel while [nativeUvGenerate] works on its own thread.
     */
    external fun nativeUvGenerationProgress(): String

    /**
     * Asks [nativeUvGenerate] to stop at the next face it is about to take. True when there was a
     * generation running to be told: the unfinished layout is dropped, so the mesh keeps the UVs it
     * had and a session that was already open stays exactly where it was.
     */
    external fun nativeUvCancelGeneration(): Boolean

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
     * Pins what is selected, or releases it. A pinned island stays in the group it was picked with,
     * which is how it is reached again, and no move, rotation, scale, fit or repack lands on it.
     * False when there was nothing to change, so the button can say so without a dialog.
     */
    external fun nativeUvLock(): Boolean

    external fun nativeUvUnlock(): Boolean

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

    /**
     * The biggest texture side this device's GPU reported, in pixels, or 0 while no GL context has
     * been created yet. The compositor refuses a canvas above it, and the panel dims the resolutions
     * the phone cannot sample.
     */
    external fun nativeTextureDeviceLimit(): Int

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

    /**
     * Steps back and forth over the document of the compositor: a resolution, an import, a removal,
     * a nudge. A step remembers the stack, not the pixels, so undoing an import brings the picture
     * back without reading the file again. False means the branch is empty.
     */
    external fun nativeTextureUndo(): Boolean

    external fun nativeTextureRedo(): Boolean

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

    /**
     * The editor as it stands, in the bytes of one project file: the model that came the scene in
     * with, the UVs it holds now and the layer stack painted on the canvas. Null when there is
     * nothing to write, with the reason in [nativeProjectReport].
     */
    external fun nativeProjectBytes(): ByteArray?

    /**
     * What the last project write or read put in the file, or why it did not: sizes, and a line for
     * everything the format could not carry.
     */
    external fun nativeProjectReport(): String

    /**
     * Reads a project back: the model is imported from the bytes inside the file, not from the path
     * it was first picked at, and the UVs, the canvas and the layers come with it. "OK" and the
     * report when it worked, "ERROR:" and the reason when it did not.
     */
    external fun nativeOpenProject(data: ByteArray, fileName: String): String
}
