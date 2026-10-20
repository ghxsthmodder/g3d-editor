# Build do G3D Editor

## Requisitos do ambiente

| Componente | Versão usada | Observação |
| --- | --- | --- |
| JDK | 17 | exigido pelo AGP 8.13 |
| Android SDK platform | android-36 | `compileSdk`/`targetSdk` 36 |
| Android SDK build-tools | 36.0.0 | fixado em `app/build.gradle.kts`; o AGP pediria 35.0.0 por padrão |
| Android NDK | r30 (30.0.16248370) | `ndkVersion` no módulo `app` |
| CMake | 3.31.6 | o AGP rejeita CMake 4.x; é lido de `<sdk>/cmake/3.31.6/bin/cmake` |
| ninja | 1.13.x | no `PATH` |
| Gradle | 8.14.3 | via `./gradlew` |
| AGP | 8.13.2 | definido na raiz |
| Kotlin | 2.2.21 | definido na raiz |

`local.properties` (não versionado) precisa apontar para o SDK:

```
sdk.dir=/opt/android-sdk
```

O NDK é localizado pelo `ndkVersion` dentro de `<sdk>/ndk/30.0.16248370`.

## Compilar o APK de depuração

```bash
./gradlew assembleDebug
```

Saída: `app/build/outputs/apk/debug/app-debug.apk` (ABIs `arm64-v8a` e `x86_64`).

Outros comandos úteis:

```bash
./gradlew installDebug          # requer um dispositivo/emulador conectado
./gradlew assembleRelease
./gradlew clean
```

## Testar o núcleo nativo sem dispositivo

O núcleo C++ (malha, primitivas e serviço de UV sobre xatlas) compila e roda em Linux host:

```bash
bash native/tests/run_host_test.sh
```

Isso constrói `native/tests/build-host/host_smoke_test` com o `g++` do sistema e executa as
verificações de geração de UV. O script não faz parte do APK.

## Estrutura

- `app/` — interface Android (Kotlin, Views + Material, orientação paisagem, tema escuro).
- `native/` — núcleo C++, JNI (`native/bridge`) e `CMakeLists.txt` usado pelo AGP.
- `native/tests/` — harness de teste no host.
- `third_party/` — bibliotecas incorporadas no repositório (ver `third_party/README.md`).
