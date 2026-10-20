package com.g3d.editor

import android.database.Cursor
import android.net.Uri
import android.os.Bundle
import android.provider.OpenableColumns
import android.view.Menu
import android.view.MenuItem
import android.view.View
import android.widget.LinearLayout
import android.widget.ScrollView
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
 * Model preview screen of phase 2: import a model, orbit it, and pick which mesh the tools will
 * act on. UV editing and texture composition arrive in the following phases.
 */
class MainActivity : AppCompatActivity(), ModelView.Listener {

    private sealed interface ReadOutcome

    private class Contents(val bytes: ByteArray) : ReadOutcome
    private class Failure(val message: String) : ReadOutcome

    private val log = StringBuilder()
    private val rows = mutableListOf<View>()

    private var selectedMesh = -1
    private var busy = false

    private lateinit var viewport: ModelView
    private lateinit var hierarchyPanel: View
    private lateinit var hierarchyList: LinearLayout
    private lateinit var sceneSummary: TextView
    private lateinit var outputScroll: ScrollView
    private lateinit var outputText: TextView
    private lateinit var importButton: MaterialButton
    private lateinit var frameButton: MaterialButton
    private lateinit var shadingGroup: MaterialButtonToggleGroup

    private val openModel =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
            if (uri != null) importUri(uri)
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
        shadingGroup = findViewById(R.id.shadingGroup)

        viewport.listener = this
        importButton.setOnClickListener { openModel.launch(arrayOf("*/*")) }
        frameButton.setOnClickListener {
            viewport.frameScene()
            report("câmera ajustada ao conteúdo")
        }
        findViewById<View>(R.id.togglePanel).setOnClickListener {
            hierarchyPanel.visibility = if (hierarchyPanel.visibility == View.VISIBLE) View.GONE else View.VISIBLE
        }
        shadingGroup.check(R.id.shadingSolid)
        shadingGroup.addOnButtonCheckedListener { _, checkedId, isChecked ->
            if (isChecked) {
                viewport.setShading(if (checkedId == R.id.shadingNormals) 1 else 0)
            }
        }

        if (!Native.isAvailable) {
            importButton.isEnabled = false
            frameButton.isEnabled = false
            shadingGroup.isEnabled = false
            viewport.isEnabled = false
            appendLine(getString(R.string.status_native_missing, Native.loadError ?: "?"))
            renderLog()
            return
        }

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
    private fun readContents(uri: Uri): ReadOutcome {
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
                if (total > MAX_MODEL_BYTES) {
                    return Failure(
                        getString(
                            R.string.status_file_too_large,
                            (total / MEGABYTE).toInt(),
                            (MAX_MODEL_BYTES / MEGABYTE).toInt()
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
        const val MEGABYTE = 1024L * 1024L
        const val MAX_MODEL_BYTES = 96L * MEGABYTE
        const val STREAM_CHUNK = 64 * 1024
        const val LOG_LIMIT = 8000
    }
}
