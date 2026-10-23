plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// arm64-v8a for devices, x86_64 for emulators.
val targetAbis = listOf("arm64-v8a", "x86_64")

android {
    namespace = "com.g3d.editor"
    compileSdk = 36
    // Pinned: AGP 8.13 would otherwise request build-tools 35, which is not installed here.
    buildToolsVersion = "36.0.0"
    ndkVersion = "30.0.16248370"

    defaultConfig {
        applicationId = "com.g3d.editor"
        minSdk = 26
        targetSdk = 36
        versionCode = 1
        versionName = "0.1.0"

        ndk {
            abiFilters += targetAbis
        }
        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++17"
                arguments += "-DANDROID_STL=c++_static"
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../native/CMakeLists.txt")
            version = "3.31.6"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildTypes {
        getByName("debug") {
            // The native half of a debug APK is built the way the release APK builds it. Assimp's
            // glTF importer runs a probe that trips an assertion inside vendored RapidJSON, and an
            // assertion calls abort(): on the phone that is the editor closing on a file it can
            // read. RelWithDebInfo keeps the -g symbols a fault is read with and leaves NDEBUG on.
            externalNativeBuild {
                cmake {
                    arguments += "-DCMAKE_BUILD_TYPE=RelWithDebInfo"
                }
            }
        }
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    lint {
        abortOnError = false
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.17.0")
    implementation("androidx.appcompat:appcompat:1.7.1")
    implementation("com.google.android.material:material:1.12.0")
}
