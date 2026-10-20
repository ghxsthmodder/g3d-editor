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

Neste ambiente o Gradle é chamado com `--offline` (`./gradlew --offline assembleDebug`): as
dependências já estão no cache local (`~/.gradle`) e não há rede. Fora de sandbox, o `--offline`
não é necessário.

## Testar o núcleo nativo sem dispositivo

```bash
bash native/tests/run_host_test.sh
```

O script configura `native/CMakeLists.txt` para o compilador do sistema e roda os cinco testes via
CTest (nenhum deles faz parte do APK):

| Teste | Executável | O que verifica |
| --- | --- | --- |
| `uv_smoke_test` | `host_smoke_test` | geração de UV com xatlas a partir de geometria sintética |
| `import_test` | `host_import_test` | importação de OBJ/glTF/GLB (`native/tests/models`) e estado do documento |
| `uv_islands_test` | `host_uv_test` | agrupamento de UVs em ilhas e conectividade |
| `uv_edit_test` | `host_uv_edit_test` | sessão de edição: seleção, transformações, reempacotamento, validar, aplicar/cancelar |
| `render_test` | `host_render_test` | renderizador GLES 3 real, desenhado headless em um pbuffer, incluindo a vista 2D do atlas |

`render_test` só é construído quando o host tem EGL + OpenGL ES 3 (`libegl-dev`,
`libgles-dev`/Mesa). Ele usa a plataforma *surfaceless* do Mesa e, quando não há contexto
disponível, imprime `SKIP` e sai com sucesso. Com `G3D_RENDER_PREVIEW=/tmp/look` ele também grava
PPM para inspecionar a imagem sem telefone: `look.solido.ppm`, `look.normais.ppm`,
`look.selecao.ppm`, `look.atlas.ppm` (sobreposições em vermelho), `look.ilha.ppm` (ilha selecionada
em âmbar), `look.repack.ppm`, `look.so_atlas.ppm` e `look.vazio.ppm`.

## Estrutura

- `app/` — interface Android (Kotlin, Views + Material, orientação paisagem, tema escuro).
- `native/` — núcleo C++, JNI (`native/bridge`) e `CMakeLists.txt` usado pelo AGP.
- `native/tests/` — harness de teste no host.
- `third_party/` — bibliotecas incorporadas no repositório (ver `third_party/README.md`).
