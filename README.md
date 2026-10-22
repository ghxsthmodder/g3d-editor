# g3d-editor

Editor 3D para Android com foco em UV unwrapping (xatlas), edição de ilhas UV e composição de
texturas por camadas. Veja o [PLAN.md](PLAN.md) para objetivos, escopo e fases.

**Estado:** Fase 5 concluída. O fluxo completo existe — importar o modelo, gerar e editar os UVs,
compor a textura, salvar o projeto em arquivo e reabri-lo — e nada dele foi validado em telefone.
A geração e a edição de UVs com xatlas (Fases 2 e 3) e o compositor de
texturas (Fase 4) — importação de PNG/JPEG, pilha de camadas com posição, rotação, escala, opacidade
e visibilidade, cor de fundo, prévia da composição no modelo e no atlas, modo quadriculado de
referência e exportação em PNG — seguem como estavam. O modelo agora sai em **OBJ, glTF e GLB**: a
cena aberta, com os UVs que o editor acabou de criar, é exportada para uma pasta escolhida e o
companheiro do par (`.mtl` / `.bin`) é gravado junto, com aviso do que o formato não guarda. O
editor também **salva e reabre o projeto** em `.g3dp`: os bytes do modelo de onde a cena veio, os
UVs de cada malha e a pilha de camadas com suas imagens, todos dentro de um arquivo só, sem depender
do caminho em que o modelo foi encontrado — o formato está descrito em [docs/PROJECT.md](docs/PROJECT.md).
O desfazer e o refazer cobrem a sessão de UV: um arrasto é um passo, e uma geração pode ser desfeita
até o layout que veio do arquivo. A interface seguiu a proposta do protótipo: paleta de grafite, rail
de glifos Material vendorizados no lugar das duas barras de texto, inspetor com as abas Cena | UV |
Camadas, atlas dominante quando a edição é dele (3D fica com ~22% da tela) e arrastar com um dedo
move as ilhas selecionadas. O comportamento do núcleo é comprovado pelos nove testes do host
(`bash native/tests/run_host_test.sh`), inclusive o renderizador headless, a ida e volta dos três
formatos de modelo, o desfazer/refazer e a reabertura de um projeto salvo. A fase também tratou dos
limites: a **tela do compositor não passa do `GL_MAX_TEXTURE_SIZE`** que o driver informou (as
resoluções acima ficam apagadas no painel), a **memória retida** aparece na aba Cena somada por
origem — arquivo, malhas, camadas com a imagem composta e a sessão de UV — e as chamadas caras do JNI
respondem com erro em vez de deixar uma `std::bad_alloc` atravessar para a pilha do Java. O layout
novo, o salvamento e a leitura via SAF ainda não foram validados em telefone, e o desfazer hoje cobre
a sessão de UV aberta, não o documento inteiro.

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
