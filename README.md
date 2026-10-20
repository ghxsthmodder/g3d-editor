# g3d-editor

Editor 3D para Android com foco em UV unwrapping (xatlas), edição de ilhas UV e composição de
texturas por camadas. Veja o [PLAN.md](PLAN.md) para objetivos, escopo e fases.

**Estado:** Fase 3 implementada — geração de UVs com xatlas, sessão de edição com pré-visualização
(selecionar ilhas, mover, girar, escalar, ajustar ao atlas, reempacotar), vista 2D do atlas no
renderizador OpenGL ES 3.0 e aplicar/cancelar com relatório de validação. O comportamento do núcleo
é comprovado pelos testes no host (`bash native/tests/run_host_test.sh`); a tela do editor UV ainda
não foi validada em telefone. Compositor de texturas, salvamento de projeto e exportação ainda não
existem.

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
