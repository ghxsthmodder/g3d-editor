package com.g3d.editor

import android.content.Context
import android.opengl.GLSurfaceView
import android.util.AttributeSet
import android.view.MotionEvent
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10
import kotlin.math.abs
import kotlin.math.hypot

/**
 * 3D viewport. One finger orbits, two fingers pan, pinch zooms, a tap selects a mesh.
 * Every call that touches GL or the native camera is posted to the GL thread with queueEvent.
 */
class ModelView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : GLSurfaceView(context, attrs) {

    interface Listener {
        fun onMeshPicked(index: Int)
    }

    var listener: Listener? = null

    private var lastX = 0f
    private var lastY = 0f
    private var lastPinchDistance = 0f
    private var lastMidX = 0f
    private var lastMidY = 0f
    private var dragDistance = 0f

    private val tapSlopPx: Float = 12f

    init {
        setEGLContextClientVersion(3)
        preserveEGLContextOnPause = true
        setRenderer(object : GLSurfaceView.Renderer {
            override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
                if (!Native.isAvailable || !Native.nativeRenderInit()) {
                    return
                }
                requestRender()
            }

            override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
                if (!Native.isAvailable) return
                queueEvent {
                    Native.nativeRenderResize(width, height)
                    requestRender()
                }
            }

            override fun onDrawFrame(gl: GL10?) {
                if (!Native.isAvailable) return
                Native.nativeRenderDraw()
            }
        })
        renderMode = GLSurfaceView.RENDERMODE_WHEN_DIRTY
    }

    /** Rebuilds the GPU geometry after the document changed. Safe to call from any thread. */
    fun refreshScene() {
        if (!Native.isAvailable) return
        queueEvent {
            Native.nativeRenderUpload()
            requestRender()
        }
    }

    fun frameScene() {
        if (!Native.isAvailable) return
        queueEvent {
            Native.nativeRenderFrameScene()
            requestRender()
        }
    }

    fun setShading(mode: Int) {
        if (!Native.isAvailable) return
        queueEvent {
            Native.nativeRenderSetShading(mode)
            requestRender()
        }
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (!Native.isAvailable) return false

        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                lastX = event.x
                lastY = event.y
                dragDistance = 0f
            }

            MotionEvent.ACTION_POINTER_DOWN -> {
                lastPinchDistance = spacing(event)
                val mid = midpoint(event)
                lastMidX = mid[0]
                lastMidY = mid[1]
                dragDistance = tapSlopPx + 1f
            }

            MotionEvent.ACTION_MOVE -> {
                if (event.pointerCount == 1) {
                    val dx = event.x - lastX
                    val dy = event.y - lastY
                    lastX = event.x
                    lastY = event.y
                    dragDistance += abs(dx) + abs(dy)
                    if (dragDistance > tapSlopPx) {
                        // The render request is queued behind the gesture so the frame always shows
                        // the new camera instead of the previous one.
                        queueEvent {
                            Native.nativeRenderOrbit(dx, dy)
                            requestRender()
                        }
                    }
                } else {
                    val distance = spacing(event)
                    val mid = midpoint(event)
                    val panX = mid[0] - lastMidX
                    val panY = mid[1] - lastMidY
                    lastMidX = mid[0]
                    lastMidY = mid[1]
                    val pinch = if (lastPinchDistance > 0f && distance > 0f) lastPinchDistance / distance else 0f
                    lastPinchDistance = distance
                    queueEvent {
                        if (pinch > 0f) Native.nativeRenderZoom(pinch)
                        Native.nativeRenderPan(panX, panY)
                        requestRender()
                    }
                }
            }

            MotionEvent.ACTION_POINTER_UP -> {
                lastPinchDistance = 0f
                val remaining = if (event.actionIndex == 0) 1 else 0
                lastX = event.getX(remaining)
                lastY = event.getY(remaining)
            }

            MotionEvent.ACTION_UP -> {
                if (dragDistance <= tapSlopPx) {
                    val x = event.x
                    val y = event.y
                    queueEvent {
                        val hit = Native.nativeRenderPick(x, y)
                        post { listener?.onMeshPicked(hit) }
                    }
                }
                dragDistance = 0f
            }

            MotionEvent.ACTION_CANCEL -> dragDistance = 0f
        }
        return true
    }

    private fun spacing(event: MotionEvent): Float {
        if (event.pointerCount < 2) return 0f
        return hypot(event.getX(0) - event.getX(1), event.getY(0) - event.getY(1))
    }

    private fun midpoint(event: MotionEvent): FloatArray {
        val count = minOf(event.pointerCount, 2)
        var x = 0f
        var y = 0f
        for (i in 0 until count) {
            x += event.getX(i)
            y += event.getY(i)
        }
        return floatArrayOf(x / count, y / count)
    }
}
