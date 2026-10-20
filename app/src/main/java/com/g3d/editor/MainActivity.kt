package com.g3d.editor

import android.os.Bundle
import android.widget.Button
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class MainActivity : AppCompatActivity() {

    private val log = StringBuilder()
    private var running = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        if (!Native.isAvailable) {
            appendLine("native library NOT loaded -> ${Native.loadError}")
            findViewById<Button>(R.id.runSmokeTest).isEnabled = false
        } else {
            appendLine("native: ${Native.nativeBuildInfo()}")
        }
        renderLog()

        findViewById<Button>(R.id.runSmokeTest).setOnClickListener { runSmokeTest() }
    }

    private fun runSmokeTest() {
        if (running) return
        running = true
        val button = findViewById<Button>(R.id.runSmokeTest)
        button.isEnabled = false
        appendLine("generating UVs for the test cube ...")
        renderLog()

        Thread {
            val startedAt = System.currentTimeMillis()
            val report = try {
                Native.nativeUvSmokeTest(padding = 2, resolution = 0)
            } catch (error: Throwable) {
                "ERROR: ${error.javaClass.simpleName}: ${error.message}"
            }
            val elapsedMs = System.currentTimeMillis() - startedAt
            runOnUiThread {
                appendLine(if (report.startsWith("ERROR:")) "FAILED in ${elapsedMs}ms" else "OK in ${elapsedMs}ms")
                report.lineSequence().filter { it.isNotBlank() }.forEach { appendLine("  $it") }
                renderLog()
                button.isEnabled = true
                running = false
            }
        }.start()
    }

    private fun appendLine(text: String) {
        val stamp = SimpleDateFormat("HH:mm:ss", Locale.US).format(Date())
        log.append("[$stamp] $text\n")
    }

    private fun renderLog() {
        findViewById<TextView>(R.id.output).text = log.toString()
    }
}
