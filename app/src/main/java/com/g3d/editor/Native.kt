package com.g3d.editor

/**
 * JNI wrapper around libg3dnative. Only whole operations cross this boundary.
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
}
