# g3d-editor

Editor 3D para Android com foco em UV unwrapping (xatlas), edição de ilhas UV e composição de
texturas por camadas. Veja o [PLAN.md](PLAN.md) para objetivos, escopo e fases.

**Estado:** Fase 5 em curso. A geração e a edição de UVs com xatlas (Fases 2 e 3) e o compositor de
texturas (Fase 4) — importação de PNG/JPEG, pilha de camadas com posição, rotação, escala, opacidade
e visibilidade, cor de fundo, prévia da composição no modelo e no atlas, modo quadriculado de
referência e exportação em PNG — seguem como estavam. O modelo agora sai em **OBJ, glTF e GLB**: a
cena aberta, com os UVs que o editor acabou de criar, é exportada para uma pasta escolhida e o
companheiro do par (`.mtl` / `.bin`) é gravado junto, com aviso do que o formato não guarda. A
interface seguiu a proposta do protótipo: paleta de grafite, rail de glifos Material vendorizados no
lugar das duas barras de texto, inspetor com as abas Cena | UV | Camadas, atlas dominante quando a
edição é dele (3D fica com ~22% da tela) e arrastar com um dedo move as ilhas selecionadas. O
comportamento do núcleo é comprovado pelos oito testes do host
(`bash native/tests/run_host_test.sh`), inclusive o renderizador headless, a ida e volta dos três
formatos e o desfazer/refazer da sessão de UV — um arrasto é um passo, e uma geração pode ser desfeita
até o layout que veio do arquivo. O layout novo e a gravação no SAF ainda não foram validados em
telefone. Salvamento de projeto (Fase 5) ainda não existe; o desfazer hoje cobre a edição de UV
aberta, não o documento inteiro.

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
