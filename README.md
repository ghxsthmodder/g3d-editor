# g3d-editor

Editor 3D para Android com foco em UV unwrapping (xatlas), edição de ilhas UV e composição de
texturas por camadas. Veja o [PLAN.md](PLAN.md) para objetivos, escopo e fases.

**Estado:** Fase 2 concluída — importação de OBJ/glTF/GLB (Assimp), cena com múltiplas malhas,
renderizador OpenGL ES 3.0 e seleção de malhas por toque. Editor UV, compositor de texturas,
salvamento de projeto e exportação ainda não existem.

## Compilar

```bash
./gradlew assembleDebug              # APK em app/build/outputs/apk/debug/
bash native/tests/run_host_test.sh   # testa o núcleo nativo (UV, importação e render) sem dispositivo
```

Requisitos e detalhes em [docs/BUILD.md](docs/BUILD.md).

## Layout

- `app/` — interface Android (Kotlin, Material, paisagem, tema escuro).
- `native/` — núcleo C++ (malha, cena, importação Assimp, UV/xatlas, renderizador GLES), JNI e CMake.
- `third_party/` — bibliotecas incorporadas, versões e licenças.
