package com.g3d.editor

import android.database.Cursor
import android.net.Uri
import android.os.Bundle
import android.provider.OpenableColumns
import android.view.Menu
import android.view.MenuItem
import android.view.View
import android.widget.CheckBox
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.button.MaterialButton
import com.google.android.material.button.MaterialButtonToggleGroup
import java.io.ByteArrayOutputStream
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Editor screen. Phase 2 brought the import and the 3D preview, phase 3 the UV workspace, and
 * phase 4 the texture compositor: a stack of imported images that the renderer shows on the model
 * and in the atlas, and that exports as the PNG the same code composes.
 */
class MainActivity : AppCompatActivity(), ModelView.Listener {

    private sealed interface ReadOutcome

    private class Contents(val bytes: ByteArray) : ReadOutcome
    private class Failure(val message: String) : ReadOutcome

    private val log = StringBuilder()
    private val rows = mutableListOf<View>()

    private var selectedMesh = -1
    private var busy = false
    private var uvOpen = false
    private var uvMesh = -1
    private var logVisible = true
    private var selectedLayer = -1

    private lateinit var viewport: ModelView
    private lateinit var hierarchyPanel: View
    private lateinit var hierarchyList: LinearLayout
    private lateinit var sceneSummary: TextView
    private lateinit var outputScroll: ScrollView
    private lateinit var outputText: TextView
    private lateinit var importButton: MaterialButton
    private lateinit var frameButton: MaterialButton
    private lateinit var logButton: MaterialButton
    private lateinit var shadingGroup: MaterialButtonToggleGroup
    private lateinit var viewGroup: MaterialButtonToggleGroup
    private lateinit var uvPanel: View
    private lateinit var uvStatusText: TextView
    private lateinit var uvPaddingField: EditText
    private lateinit var uvResolutionField: EditText
    private lateinit var uvAdditiveCheck: CheckBox
    private lateinit var texturePanel: View
    private lateinit var textureList: LinearLayout
    private lateinit var textureSummary: TextView
    private lateinit var textureCanvasField: EditText
    private lateinit var textureBackgroundField: EditText
    private lateinit var textureVisibleCheck: CheckBox
    private lateinit var textureOpacityBar: SeekBar
    private lateinit var textureOpacityLabel: TextView
    private lateinit var textureLinesCheck: CheckBox

    private val textureRows = mutableListOf<Pair<Int, View>>()
    private val textureStats = mutableMapOf<Int, String>()

    private val openModel =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
            if (uri != null) importUri(uri)
        }

    private val openImage =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
            if (uri != null) importTextureImage(uri)
        }

    private val createPng =
        registerForActivityResult(ActivityResultContracts.CreateDocument("image/png")) { uri: Uri? ->
            if (uri != null) writeTexturePng(uri)
            else report(getString(R.string.status_texture_export_cancelled))
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        setSupportActionBar(findViewById(R.id.toolbar))

        viewport = findViewById(R.id.viewport)
        hierarchyPanel = findViewById(R.id.hierarchyPanel)
        hierarchyList = findViewById(R.id.hierarchyList)
        sceneSummary = findViewById(R.id.sceneSummary)
        outputScroll = findViewById(R.id.outputScroll)
        outputText = findViewById(R.id.output)
        importButton = findViewById(R.id.importModel)
        frameButton = findViewById(R.id.frameScene)
        logButton = findViewById(R.id.toggleLog)
        shadingGroup = findViewById(R.id.shadingGroup)
        viewGroup = findViewById(R.id.viewGroup)
        uvPanel = findViewById(R.id.uvPanel)
        uvStatusText = findViewById(R.id.uvStatus)
        uvPaddingField = findViewById(R.id.uvPadding)
        uvResolutionField = findViewById(R.id.uvResolution)
        uvAdditiveCheck = findViewById(R.id.uvAdditive)
        texturePanel = findViewById(R.id.texturePanel)
        textureList = findViewById(R.id.textureList)
        textureSummary = findViewById(R.id.textureSummary)
        textureCanvasField = findViewById(R.id.textureCanvas)
        textureBackgroundField = findViewById(R.id.textureBackground)
        textureVisibleCheck = findViewById(R.id.textureVisible)
        textureOpacityBar = findViewById(R.id.textureOpacity)
        textureOpacityLabel = findViewById(R.id.textureOpacityLabel)
        textureLinesCheck = findViewById(R.id.textureUvLines)

        viewport.listener = this
        importButton.setOnClickListener { openModel.launch(arrayOf("*/*")) }
        frameButton.setOnClickListener {
            viewport.frameScene()
            report("câmera ajustada ao conteúdo")
        }
        findViewById<View>(R.id.togglePanel).setOnClickListener { toggleSidePanel() }
        logButton.setOnClickListener { toggleLog() }
        findViewById<View>(R.id.openUvEditor).setOnClickListener { openUvEditor() }
        shadingGroup.check(R.id.shadingSolid)
        shadingGroup.addOnButtonCheckedListener { _, checkedId, isChecked ->
            if (isChecked) {
                viewport.setShading(
                    when (checkedId) {
                        R.id.shadingNormals -> SHADING_NORMALS
                        R.id.shadingTexture -> SHADING_TEXTURED
                        R.id.shadingChecker -> SHADING_CHECKER
                        else -> SHADING_SOLID
                    }
                )
            }
        }
        viewGroup.check(R.id.viewThreeD)
        viewGroup.addOnButtonCheckedListener { _, checkedId, isChecked ->
            if (isChecked) {
                viewport.setViewMode(
                    when (checkedId) {
                        R.id.viewSplit -> VIEW_SPLIT
                        R.id.viewAtlas -> VIEW_ATLAS
                        else -> VIEW_THREE_D
                    }
                )
            }
        }

        uvAdditiveCheck.setOnCheckedChangeListener { _, checked ->
            viewport.additiveSelection = checked
            // Silent while the editor closes, which clears the box as part of resetting the tools.
            if (uvOpen) {
                report(
                    getString(
                        if (checked) R.string.status_uv_additive_on else R.string.status_uv_additive_off
                    )
                )
            }
        }
        findViewById<View>(R.id.uvGenerate).setOnClickListener { generateUv() }
        findViewById<View>(R.id.uvSelectAll).setOnClickListener {
            uvTool(getString(R.string.uv_name_select_all)) {
                Native.nativeUvSelectAll()
                true
            }
        }
        findViewById<View>(R.id.uvSelectNone).setOnClickListener {
            uvTool(getString(R.string.uv_name_select_none)) {
                Native.nativeUvClearSelection()
                true
            }
        }
        findViewById<View>(R.id.uvMoveLeft).setOnClickListener { moveIslands(-MOVE_STEP, 0.0f) }
        findViewById<View>(R.id.uvMoveRight).setOnClickListener { moveIslands(MOVE_STEP, 0.0f) }
        findViewById<View>(R.id.uvMoveDown).setOnClickListener { moveIslands(0.0f, -MOVE_STEP) }
        findViewById<View>(R.id.uvMoveUp).setOnClickListener { moveIslands(0.0f, MOVE_STEP) }
        findViewById<View>(R.id.uvRotateMinus90).setOnClickListener { rotateIslands(-90.0f) }
        findViewById<View>(R.id.uvRotateMinus15).setOnClickListener { rotateIslands(-15.0f) }
        findViewById<View>(R.id.uvRotatePlus15).setOnClickListener { rotateIslands(15.0f) }
        findViewById<View>(R.id.uvRotatePlus90).setOnClickListener { rotateIslands(90.0f) }
        findViewById<View>(R.id.uvScaleDown).setOnClickListener { scaleIslands(0.8f) }
        findViewById<View>(R.id.uvScaleUp).setOnClickListener { scaleIslands(1.25f) }
        findViewById<View>(R.id.uvFit).setOnClickListener {
            uvTool(getString(R.string.uv_name_fit)) { Native.nativeUvFit() }
        }
        findViewById<View>(R.id.uvRepack).setOnClickListener { repackIslands() }
        findViewById<View>(R.id.uvFrameAtlas).setOnClickListener {
            viewport.frameAtlas()
            report("atlas reenquadrado")
        }
        findViewById<View>(R.id.uvApply).setOnClickListener { applyUv() }
        findViewById<View>(R.id.uvCancel).setOnClickListener { cancelUv() }
        findViewById<View>(R.id.openTexture).setOnClickListener { openTexturePanel() }
        findViewById<View>(R.id.textureImportImage).setOnClickListener { startImageImport() }
        findViewById<View>(R.id.textureSetCanvas).setOnClickListener { applyTextureCanvas() }
        findViewById<View>(R.id.textureSetBackground).setOnClickListener { applyTextureBackground() }
        findViewById<View>(R.id.textureExport).setOnClickListener { exportTexture() }
        findViewById<View>(R.id.textureDuplicate).setOnClickListener { duplicateLayer() }
        findViewById<View>(R.id.textureRemove).setOnClickListener {
            textureTool(getString(R.string.texture_name_remove)) { Native.nativeTextureRemove(selectedLayer) }
        }
        findViewById<View>(R.id.textureMoveUpStack).setOnClickListener {
            textureTool(getString(R.string.texture_name_order_up)) { Native.nativeTextureMoveUp(selectedLayer) }
        }
        findViewById<View>(R.id.textureMoveDownStack).setOnClickListener {
            textureTool(getString(R.string.texture_name_order_down)) {
                Native.nativeTextureMoveDown(selectedLayer)
            }
        }
        findViewById<View>(R.id.textureUp).setOnClickListener { nudgeLayer(0.0f, -TEXTURE_MOVE_STEP) }
        findViewById<View>(R.id.textureDown).setOnClickListener { nudgeLayer(0.0f, TEXTURE_MOVE_STEP) }
        findViewById<View>(R.id.textureLeft).setOnClickListener { nudgeLayer(-TEXTURE_MOVE_STEP, 0.0f) }
        findViewById<View>(R.id.textureRight).setOnClickListener { nudgeLayer(TEXTURE_MOVE_STEP, 0.0f) }
        findViewById<View>(R.id.textureRotateMinus90).setOnClickListener { rotateLayer(-90.0f) }
        findViewById<View>(R.id.textureRotateMinus15).setOnClickListener { rotateLayer(-15.0f) }
        findViewById<View>(R.id.textureRotatePlus15).setOnClickListener { rotateLayer(15.0f) }
        findViewById<View>(R.id.textureRotatePlus90).setOnClickListener { rotateLayer(90.0f) }
        findViewById<View>(R.id.textureScaleDown).setOnClickListener {
            textureTool(getString(R.string.texture_name_scale)) {
                Native.nativeTextureScale(selectedLayer, 0.8f)
            }
        }
        findViewById<View>(R.id.textureScaleUp).setOnClickListener {
            textureTool(getString(R.string.texture_name_scale)) {
                Native.nativeTextureScale(selectedLayer, 1.25f)
            }
        }
        // The layer controls get their listeners back from showLayerTools(), which is also the
        // only place allowed to set them programmatically.
        textureLinesCheck.setOnCheckedChangeListener { _, checked ->
            viewport.setUvOverlay(checked)
            report(
                getString(
                    if (checked) R.string.status_texture_lines_on else R.string.status_texture_lines_off
                )
            )
        }

        if (!Native.isAvailable) {
            importButton.isEnabled = false
            frameButton.isEnabled = false
            shadingGroup.isEnabled = false
            viewGroup.isEnabled = false
            findViewById<View>(R.id.openUvEditor).isEnabled = false
            findViewById<View>(R.id.openTexture).isEnabled = false
            viewport.isEnabled = false
            appendLine(getString(R.string.status_native_missing, Native.loadError ?: "?"))
            renderLog()
            return
        }

        appendLine(getString(R.string.phase_4_status))
        appendLine("nativo: ${Native.nativeBuildInfo()}")
        renderLog()
        refreshHierarchy()
    }

    override fun onCreateOptionsMenu(menu: Menu): Boolean {
        menuInflater.inflate(R.menu.main_menu, menu)
        return true
    }

    // GLSurfaceView needs the lifecycle forwarded, or its render thread keeps running while the
    // activity is stopped.
    override fun onResume() {
        super.onResume()
        viewport.onResume()
    }

    override fun onPause() {
        viewport.onPause()
        super.onPause()
    }

    override fun onOptionsItemSelected(item: MenuItem): Boolean {
        if (item.itemId == R.id.menuNativeDiagnostic) {
            runUvDiagnostic()
            return true
        }
        return super.onOptionsItemSelected(item)
    }

    // --- side panels ---------------------------------------------------------------------

    /** The log is the least important strip on the screen, so it can get out of the way. */
    private fun toggleLog() {
        logVisible = !logVisible
        outputScroll.visibility = if (logVisible) View.VISIBLE else View.GONE
        logButton.setText(if (logVisible) R.string.toggle_log else R.string.toggle_log_hidden)
    }

    /** The right column shows the hierarchy, the UV tools or the texture stack, never two at once. */
    private fun toggleSidePanel() {
        when {
            hierarchyPanel.visibility == View.VISIBLE -> {
                hierarchyPanel.visibility = View.GONE
                if (uvOpen) uvPanel.visibility = View.VISIBLE else texturePanel.visibility = View.VISIBLE
            }
            uvPanel.visibility == View.VISIBLE -> {
                uvPanel.visibility = View.GONE
                texturePanel.visibility = View.VISIBLE
            }
            texturePanel.visibility == View.VISIBLE -> {
                texturePanel.visibility = View.GONE
                hierarchyPanel.visibility = View.VISIBLE
            }
            else -> hierarchyPanel.visibility = View.VISIBLE
        }
        if (texturePanel.visibility == View.VISIBLE) {
            showAtlasForTextures()
            refreshTextureStack()
        }
    }

    // --- editor UV -----------------------------------------------------------------------

    private fun openUvEditor() {
        if (busy || !Native.isAvailable) return
        if (uvOpen) {
            uvPanel.visibility = View.VISIBLE
            hierarchyPanel.visibility = View.GONE
            return
        }
        if (selectedMesh < 0) {
            report(getString(R.string.status_uv_no_mesh))
            return
        }
        val mesh = selectedMesh
        val padding = integerField(uvPaddingField, 2)
        val resolution = integerField(uvResolutionField, 0)
        beginWork(getString(R.string.status_uv_opening, mesh))
        Thread {
            // A mesh imported without UVs has nothing to edit, so the same tap that opens the
            // editor also generates the layout it is about to show.
            val opened = Native.nativeUvBegin(mesh)
            val fromScratch = opened.startsWith("ERROR:")
            val reply = if (fromScratch) Native.nativeUvGenerate(mesh, padding, resolution) else opened
            runOnUiThread {
                if (reply.startsWith("ERROR:")) {
                    finishWork(
                        getString(
                            R.string.status_uv_open_failed,
                            reply.removePrefix("ERROR:").trim()
                        )
                    )
                    return@runOnUiThread
                }
                uvOpen = true
                uvMesh = mesh
                hierarchyPanel.visibility = View.GONE
                uvPanel.visibility = View.VISIBLE
                viewport.frameAtlas()
                viewport.setViewMode(VIEW_SPLIT)
                viewGroup.check(R.id.viewSplit)
                finishWork(
                    getString(
                        if (fromScratch) R.string.status_uv_generated_open else R.string.status_uv_opened,
                        mesh
                    )
                )
                // The state and the validation report belong to the panel; the log keeps the event.
                refreshUvStatus()
            }
        }.start()
    }

    private fun generateUv() {
        if (busy || !uvOpen || !Native.isAvailable) return
        val padding = integerField(uvPaddingField, -1)
        val resolution = integerField(uvResolutionField, -1)
        if (padding < 1 || padding > 9999 || resolution < 0 || resolution > 8192) {
            report(getString(R.string.status_uv_bad_number))
            return
        }
        beginWork(getString(R.string.status_uv_generating, padding, resolution))
        val startedAt = System.currentTimeMillis()
        Thread {
            val reply = Native.nativeUvGenerate(uvMesh, padding, resolution)
            val elapsed = System.currentTimeMillis() - startedAt
            runOnUiThread {
                if (reply.startsWith("ERROR:")) {
                    finishWork(
                        getString(
                            R.string.status_uv_generate_failed,
                            reply.removePrefix("ERROR:").trim()
                        )
                    )
                    return@runOnUiThread
                }
                finishWork(getString(R.string.status_uv_generated, elapsed))
                viewport.frameAtlas()
                refreshUvStatus()
            }
        }.start()
    }

    private fun applyUv() {
        if (busy || !uvOpen || !Native.isAvailable) return
        beginWork(getString(R.string.status_uv_applying))
        Thread {
            val reply = Native.nativeUvApply()
            runOnUiThread {
                if (reply.startsWith("ERROR:")) {
                    finishWork(
                        getString(
                            R.string.status_uv_apply_failed,
                            reply.removePrefix("ERROR:").trim()
                        )
                    )
                    return@runOnUiThread
                }
                val mesh = uvMesh
                closeUvEditor()
                viewport.refreshScene()
                finishWork(getString(R.string.status_uv_applied, mesh))
                refreshHierarchy()
            }
        }.start()
    }

    private fun cancelUv() {
        if (busy || !uvOpen || !Native.isAvailable) return
        Native.nativeUvCancel()
        closeUvEditor()
        report(getString(R.string.status_uv_cancelled))
    }

    private fun closeUvEditor() {
        uvOpen = false
        uvMesh = -1
        viewport.additiveSelection = false
        uvAdditiveCheck.isChecked = false
        uvPanel.visibility = View.GONE
        hierarchyPanel.visibility = View.VISIBLE
        viewport.setViewMode(VIEW_THREE_D)
        viewGroup.check(R.id.viewThreeD)
    }

    /** One tool press: run it, say whether it did anything, and show the new state. */
    private fun uvTool(name: String, action: () -> Boolean) {
        if (busy || !uvOpen || !Native.isAvailable) return
        if (!action()) {
            report(getString(R.string.status_uv_noop, name))
        }
        refreshUvStatus()
        viewport.requestRender()
    }

    private fun moveIslands(du: Float, dv: Float) {
        uvTool(getString(R.string.uv_name_move)) { Native.nativeUvTranslate(du, dv) }
    }

    private fun rotateIslands(degrees: Float) {
        uvTool(getString(R.string.uv_name_rotate)) { Native.nativeUvRotate(degrees) }
    }

    private fun scaleIslands(factor: Float) {
        uvTool(getString(R.string.uv_name_scale)) { Native.nativeUvScale(factor) }
    }

    private fun repackIslands() {
        if (busy || !uvOpen) return
        val padding = integerField(uvPaddingField, 2).coerceAtLeast(1)
        uvTool(getString(R.string.uv_name_repack)) { Native.nativeUvRepack(padding) }
    }

    private fun refreshUvStatus() {
        if (!uvOpen || !Native.isAvailable) return
        uvStatusText.text = Native.nativeUvStatus().lineSequence()
            .filter { it.isNotBlank() }
            .joinToString("\n")
    }

    private fun integerField(field: EditText, fallback: Int): Int =
        field.text.toString().trim().toIntOrNull() ?: fallback

    override fun onIslandPicked(island: Int, additive: Boolean) {
        if (!uvOpen) return
        if (island < 0) {
            report(getString(R.string.status_uv_pick_miss))
            refreshUvStatus()
            return
        }
        Native.nativeUvSelect(island, additive)
        report(
            getString(
                if (additive) R.string.status_uv_pick_add else R.string.status_uv_pick,
                island
            )
        )
        refreshUvStatus()
        viewport.requestRender()
    }

    // --- compositor de texturas -------------------------------------------------------------

    private fun openTexturePanel() {
        if (!Native.isAvailable) return
        hierarchyPanel.visibility = View.GONE
        uvPanel.visibility = View.GONE
        texturePanel.visibility = View.VISIBLE
        showAtlasForTextures()
        refreshTextureStack()
    }

    /** The compositor paints the atlas, so the panel is useless without the 2D view. */
    private fun showAtlasForTextures() {
        if (viewGroup.checkedButtonId != R.id.viewThreeD) return
        viewGroup.check(R.id.viewSplit)
        viewport.frameAtlas()
    }

    private fun startImageImport() {
        if (busy || !Native.isAvailable) return
        // One tap does both: a stack without a canvas cannot hold an image, and asking for the
        // canvas first would be a hidden prerequisite.
        if (!Native.nativeTextureHasCanvas() && !applyTextureCanvas()) return
        openImage.launch(arrayOf("image/*"))
    }

    private fun applyTextureCanvas(): Boolean {
        val side = integerField(textureCanvasField, 0)
        if (side < MIN_TEXTURE_SIDE || side > MAX_TEXTURE_SIDE) {
            report(getString(R.string.status_texture_bad_canvas))
            return false
        }
        val reply = Native.nativeTextureSetCanvas(side, side)
        if (reply.startsWith("ERROR:")) {
            report(getString(R.string.status_texture_canvas_failed, reply.removePrefix("ERROR:").trim()))
            return false
        }
        report(getString(R.string.status_texture_canvas, side, side))
        refreshTextureStack()
        viewport.requestRender()
        return true
    }

    private fun applyTextureBackground() {
        if (!Native.isAvailable) return
        val hex = textureBackgroundField.text.toString().trim().removePrefix("#")
        val rgb = hex.toLongOrNull(16)
        if (hex.length != 6 || rgb == null) {
            report(getString(R.string.status_texture_bad_background))
            return
        }
        Native.nativeTextureSetBackground(rgb.toInt())
        report(getString(R.string.status_texture_background, hex.uppercase(Locale.US)))
        refreshTextureStack()
        viewport.requestRender()
    }

    private fun importTextureImage(uri: Uri) {
        if (busy) return
        val name = displayName(uri)
        val extension = name.substringAfterLast('.', "").lowercase(Locale.US)
        if (extension !in SUPPORTED_IMAGE_EXTENSIONS) {
            report(getString(R.string.status_texture_unsupported_image, extension.ifEmpty { "?" }))
            return
        }
        beginWork(getString(R.string.status_texture_importing, name))
        Thread {
            val outcome = try {
                readContents(uri, MAX_IMAGE_BYTES)
            } catch (error: Throwable) {
                Failure(getString(R.string.status_read_error, error.message ?: "?"))
            }
            var added = -1
            val message = when (outcome) {
                is Failure -> outcome.message
                is Contents -> {
                    val reply = Native.nativeTextureImportImage(outcome.bytes, name)
                    if (reply.startsWith("ERROR:")) {
                        getString(
                            R.string.status_texture_import_failed,
                            name,
                            reply.removePrefix("ERROR:").trim()
                        )
                    } else {
                        added = reply.lineSequence()
                            .firstOrNull { it.startsWith("camada=") }
                            ?.substringAfter('=')
                            ?.toIntOrNull() ?: -1
                        getString(R.string.status_texture_imported, added, name)
                    }
                }
            }
            runOnUiThread {
                if (added >= 0) {
                    selectedLayer = added
                }
                finishWork(message)
                refreshTextureStack()
                viewport.requestRender()
            }
        }.start()
    }

    private fun duplicateLayer() {
        if (busy || selectedLayer < 0) {
            report(getString(R.string.status_texture_no_layer))
            return
        }
        val reply = Native.nativeTextureDuplicate(selectedLayer)
        if (reply.startsWith("ERROR:")) {
            report(getString(R.string.status_texture_failed, reply.removePrefix("ERROR:").trim()))
        } else {
            reply.lineSequence()
                .firstOrNull { it.startsWith("camada=") }
                ?.substringAfter('=')
                ?.toIntOrNull()
                ?.let { selectedLayer = it }
        }
        refreshTextureStack()
        viewport.requestRender()
    }

    private fun nudgeLayer(dxPixels: Float, dyPixels: Float) {
        textureTool(getString(R.string.texture_name_move)) {
            Native.nativeTextureTranslate(selectedLayer, dxPixels, dyPixels)
        }
    }

    private fun rotateLayer(degrees: Float) {
        textureTool(getString(R.string.texture_name_rotate)) { Native.nativeTextureRotate(selectedLayer, degrees) }
    }

    /** One press on a layer tool: run it, say whether it did anything, and show the new stack. */
    private fun textureTool(name: String, action: () -> Boolean) {
        if (busy || !Native.isAvailable) return
        if (selectedLayer < 0) {
            report(getString(R.string.status_texture_no_layer))
            return
        }
        if (!action()) {
            report(getString(R.string.status_texture_noop, name))
        }
        refreshTextureStack()
        viewport.requestRender()
    }

    private fun refreshTextureStack() {
        if (!Native.isAvailable) return
        val lines = Native.nativeTextureInfo().lineSequence().filter { it.isNotBlank() }.toList()
        var summary = lines.firstOrNull().orEmpty()
        textureList.removeAllViews()
        textureRows.clear()
        textureStats.clear()
        for (line in lines.drop(1)) {
            val parts = line.split(" | ")
            val id = parts.firstOrNull()?.trim()?.toIntOrNull() ?: continue
            val name = parts.getOrElse(1) { "camada #$id" }
            val stats = parts.drop(2).joinToString(" · ")
            textureStats[id] = stats
            val row = layoutInflater.inflate(R.layout.hierarchy_row, textureList, false)
            row.findViewById<TextView>(R.id.rowName).text = name
            row.findViewById<TextView>(R.id.rowStats).text = stats
            row.setOnClickListener { selectLayer(id) }
            textureList.addView(row)
            textureRows.add(id to row)
        }
        if (textureRows.isEmpty()) {
            selectedLayer = -1
            summary += "\n" + getString(R.string.texture_none)
        } else if (textureStats[selectedLayer] == null) {
            selectedLayer = textureRows.first().first
        }
        textureSummary.text = summary
        highlightLayers()
        showLayerTools()
    }

    private fun selectLayer(id: Int) {
        selectedLayer = id
        highlightLayers()
        showLayerTools()
        report(getString(R.string.status_texture_selected, id))
    }

    private fun highlightLayers() {
        textureRows.forEach { (id, row) ->
            row.findViewById<View>(R.id.rowHighlight).visibility =
                if (id == selectedLayer) View.VISIBLE else View.GONE
        }
    }

    /** The controls mirror the selected layer, so they are read back from the stack every time. */
    private fun showLayerTools() {
        val stats = textureStats[selectedLayer]
        textureVisibleCheck.setOnCheckedChangeListener(null)
        textureOpacityBar.setOnSeekBarChangeListener(null)
        textureVisibleCheck.isEnabled = stats != null
        textureOpacityBar.isEnabled = stats != null
        val visible = stats?.contains("oculta") == false
        val percent = stats?.let { OPACITY_PATTERN.find(it)?.groupValues?.get(1)?.toIntOrNull() } ?: 100
        textureVisibleCheck.isChecked = visible
        textureOpacityBar.progress = percent
        textureOpacityLabel.text = getString(R.string.texture_opacity) + " " + percent + "%"
        textureVisibleCheck.setOnCheckedChangeListener { _, checked ->
            if (selectedLayer >= 0 && Native.nativeTextureSetVisible(selectedLayer, checked)) {
                refreshTextureStack()
                viewport.requestRender()
            }
        }
        textureOpacityBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(bar: SeekBar?, value: Int, fromUser: Boolean) {
                textureOpacityLabel.text = getString(R.string.texture_opacity) + " " + value + "%"
                if (fromUser && selectedLayer >= 0) {
                    Native.nativeTextureSetOpacity(selectedLayer, value / 100.0f)
                    refreshTextureStack()
                    viewport.requestRender()
                }
            }

            override fun onStartTrackingTouch(bar: SeekBar?) = Unit

            override fun onStopTrackingTouch(bar: SeekBar?) = Unit
        })
    }

    private fun exportTexture() {
        if (busy || !Native.isAvailable) return
        if (!Native.nativeTextureHasCanvas()) {
            report(getString(R.string.status_texture_bad_canvas))
            return
        }
        createPng.launch("textura.png")
    }

    private fun writeTexturePng(uri: Uri) {
        if (busy) return
        beginWork(getString(R.string.status_texture_exporting))
        Thread {
            val startedAt = System.currentTimeMillis()
            val message = try {
                val png = Native.nativeTexturePng()
                if (png == null || png.isEmpty()) {
                    getString(R.string.status_texture_export_empty)
                } else {
                    val stream = contentResolver.openOutputStream(uri)
                    if (stream == null) {
                        getString(R.string.status_read_error, "fluxo indisponível")
                    } else {
                        stream.use { it.write(png) }
                        getString(
                            R.string.status_texture_exported,
                            "${png.size / 1024} KB em ${System.currentTimeMillis() - startedAt} ms"
                        )
                    }
                }
            } catch (error: Throwable) {
                getString(R.string.status_texture_export_failed, error.message ?: "?")
            }
            runOnUiThread { finishWork(message) }
        }.start()
    }

    // --- import --------------------------------------------------------------------------

    private fun importUri(uri: Uri) {
        if (busy) return
        val name = displayName(uri)
        val extension = name.substringAfterLast('.', "").lowercase(Locale.US)
        if (extension !in SUPPORTED_EXTENSIONS) {
            report(getString(R.string.status_unsupported_extension, extension.ifEmpty { "?" }))
            return
        }

        beginWork(getString(R.string.status_importing, name))

        Thread {
            val startedAt = System.currentTimeMillis()
            val outcome = try {
                readContents(uri)
            } catch (error: Throwable) {
                Failure(getString(R.string.status_read_error, error.message ?: "?"))
            }

            var imported = false
            val message = when (outcome) {
                is Failure -> outcome.message
                is Contents -> {
                    if (outcome.bytes.isEmpty()) {
                        getString(R.string.status_file_empty)
                    } else {
                        val reply = Native.nativeImportModel(outcome.bytes, name)
                        val elapsed = System.currentTimeMillis() - startedAt
                        if (reply.startsWith("ERROR:")) {
                            getString(
                                R.string.status_import_failed,
                                name,
                                reply.removePrefix("ERROR:").trim()
                            )
                        } else {
                            imported = true
                            getString(R.string.status_import_done, elapsed, name)
                        }
                    }
                }
            }

            runOnUiThread {
                if (imported) {
                    // The scene was rebuilt, so an open session would apply to the wrong mesh.
                    if (uvOpen) {
                        Native.nativeUvCancel()
                        closeUvEditor()
                    }
                    selectedMesh = -1
                    viewport.refreshScene()
                    viewport.frameScene()
                    refreshHierarchy()
                }
                finishWork(message)
            }
        }.start()
    }

    private fun displayName(uri: Uri): String {
        var name = uri.lastPathSegment ?: "modelo"
        try {
            val projection = arrayOf(OpenableColumns.DISPLAY_NAME)
            contentResolver.query(uri, projection, null, null, null)?.use { cursor: Cursor ->
                if (cursor.moveToFirst() && !cursor.isNull(0)) {
                    name = cursor.getString(0)
                }
            }
        } catch (_: Throwable) {
            // The provider may not expose a display name; the last path segment is enough.
        }
        return name
    }

    /** Caps the size before allocating, because a huge file must not take the process down. */
    private fun readContents(uri: Uri, maxBytes: Long = MAX_MODEL_BYTES): ReadOutcome {
        val stream = contentResolver.openInputStream(uri)
            ?: return Failure(getString(R.string.status_read_error, "fluxo indisponível"))
        stream.use {
            val collected = ByteArrayOutputStream(STREAM_CHUNK)
            val chunk = ByteArray(STREAM_CHUNK)
            var total = 0L
            while (true) {
                val read = it.read(chunk)
                if (read < 0) break
                total += read
                if (total > maxBytes) {
                    return Failure(
                        getString(
                            R.string.status_file_too_large,
                            (total / MEGABYTE).toInt(),
                            (maxBytes / MEGABYTE).toInt()
                        )
                    )
                }
                collected.write(chunk, 0, read)
            }
            return Contents(collected.toByteArray())
        }
    }

    // --- hierarchy and selection ---------------------------------------------------------

    private fun refreshHierarchy() {
        if (!Native.isAvailable) return
        val lines = Native.nativeSceneInfo().lineSequence().filter { it.isNotBlank() }.toList()
        val summary = lines.firstOrNull() ?: ""
        sceneSummary.text = summary

        hierarchyList.removeAllViews()
        rows.clear()
        for (line in lines.drop(1)) {
            val parts = line.split(" | ")
            val index = parts.firstOrNull()?.trim()?.toIntOrNull() ?: continue
            val label = parts.getOrElse(1) { "malha #$index" }
            val stats = parts.drop(2).joinToString(" · ")
            val row = layoutInflater.inflate(R.layout.hierarchy_row, hierarchyList, false)
            row.findViewById<TextView>(R.id.rowName).text = "$index. $label"
            row.findViewById<TextView>(R.id.rowStats).text = stats
            row.setOnClickListener { selectMesh(index) }
            hierarchyList.addView(row)
            rows.add(row)
        }
        if (rows.isEmpty()) {
            sceneSummary.text = getString(R.string.hierarchy_none)
        }
        highlightRows()
    }

    private fun highlightRows() {
        rows.forEachIndexed { position, row ->
            val chosen = position == selectedMesh
            row.findViewById<View>(R.id.rowHighlight).visibility = if (chosen) View.VISIBLE else View.GONE
        }
    }

    private fun selectMesh(index: Int) {
        if (!Native.isAvailable) return
        Native.nativeSelectMesh(index)
        selectedMesh = index
        highlightRows()
        // The summary reports the selection, and only the native side knows it, so re-read it.
        sceneSummary.text = Native.nativeSceneInfo().lineSequence().firstOrNull().orEmpty()
        viewport.requestRender()
        val label = rows.getOrNull(index)
            ?.findViewById<TextView>(R.id.rowName)?.text?.toString().orEmpty()
        report(if (label.isEmpty()) getString(R.string.status_no_selection) else "selecionada: $label")
    }

    override fun onMeshPicked(index: Int) {
        if (index < 0) {
            report(getString(R.string.status_pick_miss))
            return
        }
        selectMesh(index)
    }

    // --- diagnostics ---------------------------------------------------------------------

    private fun runUvDiagnostic() {
        if (busy) return
        beginWork(getString(R.string.uv_report_title))
        Thread {
            val startedAt = System.currentTimeMillis()
            val reply = try {
                Native.nativeUvSmokeTest(padding = 2, resolution = 0)
            } catch (error: Throwable) {
                "ERROR: ${error.javaClass.simpleName}: ${error.message}"
            }
            val elapsed = System.currentTimeMillis() - startedAt
            val outcome = if (reply.startsWith("ERROR:")) {
                "geração de UV falhou em ${elapsed}ms"
            } else {
                buildString {
                    append("geração de UV ok em ").append(elapsed).append("ms\n")
                    append(reply.lineSequence().filter { it.isNotBlank() }.joinToString("\n") { "  $it" })
                }
            }
            runOnUiThread { finishWork(outcome) }
        }.start()
    }

    // --- status log ----------------------------------------------------------------------

    private fun beginWork(message: String) {
        busy = true
        importButton.isEnabled = false
        appendLine(message)
        renderLog()
    }

    private fun finishWork(message: String) {
        busy = false
        importButton.isEnabled = true
        message.lineSequence().forEach { appendLine(it) }
        renderLog()
    }

    private fun report(message: String) {
        appendLine(message)
        renderLog()
    }

    private fun appendLine(text: String) {
        val stamp = SimpleDateFormat("HH:mm:ss", Locale.US).format(Date())
        log.append("[$stamp] $text\n")
        if (log.length > LOG_LIMIT) {
            log.delete(0, log.length - LOG_LIMIT / 2)
        }
    }

    private fun renderLog() {
        outputText.text = log.toString()
        outputScroll.post { outputScroll.fullScroll(View.FOCUS_DOWN) }
    }

    private companion object {
        val SUPPORTED_EXTENSIONS = setOf("obj", "gltf", "glb")
        val SUPPORTED_IMAGE_EXTENSIONS = setOf("png", "jpg", "jpeg")
        val OPACITY_PATTERN = Regex("op=(\\d+)%")
        const val MEGABYTE = 1024L * 1024L
        const val MAX_MODEL_BYTES = 96L * MEGABYTE
        const val MAX_IMAGE_BYTES = 32L * MEGABYTE
        const val MIN_TEXTURE_SIDE = 16
        const val MAX_TEXTURE_SIDE = 8192

        // A button press moves the layer by this many canvas pixels; the atlas zoom is the
        // fine tool, so the step has to stay useful from 16 px to 8192 px.
        const val TEXTURE_MOVE_STEP = 16.0f

        // Renderer shading modes, mirrored in g3d_renderer.hpp.
        const val SHADING_SOLID = 0
        const val SHADING_NORMALS = 1
        const val SHADING_TEXTURED = 3
        const val SHADING_CHECKER = 4
        const val STREAM_CHUNK = 64 * 1024
        const val LOG_LIMIT = 8000

        // Renderer view modes, mirrored in g3d_renderer.hpp.
        const val VIEW_THREE_D = 0
        const val VIEW_SPLIT = 1
        const val VIEW_ATLAS = 2

        // A button press moves the selection by this much of the atlas; dragging is the fine tool.
        const val MOVE_STEP = 0.05f
    }
}
