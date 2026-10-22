# third_party

Bibliotecas incorporadas no repositório. Cada entrada registra a versão efetivamente usada, a
licença e os arquivos mantidos.

## xatlas

- Finalidade: geração automática de UVs, criação de charts (ilhas) e empacotamento no atlas.
- Repositório: https://github.com/jpcy/xatlas
- Versão: `master` no commit `f700c7790aaa030e794b52ba7791a05c085faf0c` (o projeto não publica tags).
- Licença: MIT (também cobre trechos derivados de thekla_atlas, NVIDIA/Codux e MESA, todos MIT).
- Arquivos mantidos: `xatlas.h`, `xatlas.cpp`, `LICENSE`. `xatlas_c.h` não foi incorporado
  (API C opcional, desligada por padrão via `XATLAS_C_API 0`).
- Compilação: entra como unidade de `libg3dnative` em `native/CMakeLists.txt`, com avisos
  desativados (`-w`).

## Assimp

- Finalidade: importação de OBJ, glTF e GLB e exportação dos mesmos três formatos.
- Repositório: https://github.com/assimp/assimp
- Versão: tag `v6.0.5`, commit `392a658f9c271be965271f45e7521a1b80ea4392`.
- Licença: ISC (arquivo `LICENSE`), que cobre também os subcomponentes em `contrib/`.
- Arquivos mantidos: `CMakeLists.txt`, `assimp.pc.in`, `code/`, `include/`, `cmake-modules/` e os
  diretórios de `contrib/` exigidos pelo build (`zlib`, `zip`, `unzip`, `rapidjson`, `pugixml`,
  `utf8cpp`, `stb`, `poly2tri`, `earcut-hpp`, `clipper`, `openddlparser`, `Open3DGC`, `meshlab`).
  `test/` (211 MB de modelos), `tools/`, `doc/`, `samples/`, `port/`, `draco`, `googletest`,
  `tinyusdz` e `android-cmake` foram removidos.
- Configuração de build (definida em `native/CMakeLists.txt`, com `FORCE`):
  - `ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=OFF`, apenas `OBJ` e `GLTF` habilitados;
  - `ASSIMP_NO_EXPORT=OFF` com `ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT=OFF` e apenas `OBJ` e `GLTF`
    habilitados na saída — o glTF cobre também o GLB, que é o mesmo documento em binário;
  - `BUILD_SHARED_LIBS=OFF` (liga estática dentro de `libg3dnative.so`);
  - `ASSIMP_BUILD_TESTS=OFF`, `ASSIMP_BUILD_ASSIMP_TOOLS=OFF`;
  - `ASSIMP_INJECT_DEBUG_POSTFIX=OFF`: sem isso o Assimp define `CMAKE_DEBUG_POSTFIX "d"` no mesmo
    build tree e a biblioteca passaria a `libg3dnatived.so`, quebrando `System.loadLibrary`.
- Tamanho adicionado: os dois exportadores medidos no mesmo *build* de depuração, comparando o
  `libg3dnative.so` despojado de dentro do APK com a configuração anterior: +97 KB em arm64-v8a e
  +108 KB em x86_64 (4,34 → 4,43 MB e 4,20 → 4,31 MB). O Assimp continua sendo a maior parte do
  módulo; o total do `.so` e do APK está medido em `docs/BUILD.md`.
- O que a versão **não** faz: a documentação de `ProgressHandler` promete "retorne false para
  abortar o carregamento", mas no código desta tag nenhuma chamada verifica o retorno —
  `code/Common/Importer.cpp:703,710,836,867` e `code/AssetLib/Obj/ObjFileParser.cpp:152` ligam o
  relatório e jogam a resposta fora, e não existe `ShouldContinue` na árvore. Uma importação,
  portanto, só reporta: ela não pode ser interrompida. O progresso da geração de UV, que usa
  xatlas, tem o caminho inverso — `ProgressFunc` devolve o bool e a biblioteca obedece — e é a
  única operação com botão de interromper no app. Conferido nesta tag, não por suposição;
  medição: um OBJ de 106 MB leva 10,4 s no host, das quais 9,7 s são dentro do Assimp.

## stb

- Finalidade: decodificação e gravação de imagens no compositor de texturas.
- Repositório: https://github.com/nothings/stb
- Versão: `master` no commit `a7d4c2175630e1ea5b6f8dd00ab96fed0b97b96d`.
- Licença: escolha entre MIT e Unlicense (arquivo `LICENSE` com as duas).
- Arquivos mantidos: `stb_image.h`, `stb_image_write.h`, `LICENSE`.
- Redimensionamento: `stb_image_resize2.h` foi movido para `deprecated/` no upstream e por isso
  **não** foi incorporado; a composição remostra cada camada pela transformada inversa, sem
  redimensionar o arquivo de origem.
- Compilação: as implementações ficam em uma única unidade (`native/texture/stb_impl.cpp`), com
  avisos desativados (`-w`) porque o stb é código C que alerta sob `-Wextra`; o resto do núcleo
  usa apenas as declarações dos cabeçalhos via `native/texture/g3d_image.cpp`.
- Estado: em uso desde a Fase 4 (importação de imagens e exportação PNG do compositor).
- Tamanho adicionado: a unidade de implementação do stb mede 263 KB como objeto de host em Release
  (x86-64, com símbolos). No APK ela é a maior parte do módulo de imagem; a contribuição isolada do
  stb no `.so` não foi medida, e o total do `libg3dnative.so` e do `app-debug.apk` está em
  `docs/BUILD.md`.

## GLM

- Finalidade: matemática 3D (vetores, matrizes, câmeras, bounding boxes).
- Repositório: https://github.com/g-truc/glm
- Versão: tag `1.0.2`, commit `a532f5b1cf27d6a3c099437e6959cf7e398a0a67`.
- Licença: MIT (ver `glm/copying.txt`).
- Arquivos mantidos: cabeçalhos em `glm/`, `copying.txt`, `readme.md`. Testes, documentação e o
  próprio `CMakeLists.txt` da biblioteca foram removidos; GLM é header-only.
- Uso atual: `native/importers/assimp_importer.cpp` (transformações de nó e normais) e
  `native/renderer/g3d_renderer.cpp` (câmera, matrizes de projeção e picking por raio).

## RapidJSON (dentro do Assimp)

- Finalidade: ler e escrever a descrição JSON do arquivo de projeto `.g3dp`.
- Repositório: https://github.com/Tencent/rapidjson — não foi baixado à parte; é a cópia que vem em
  `third_party/assimp/contrib/rapidjson`, que o próprio Assimp usa no importador de glTF.
- Licença: MIT (`third_party/assimp/contrib/rapidjson/license.txt`, com a nota da Tencent). Está
  também sob a declaração ISC do Assimp, que cobre os componentes de `contrib/`.
- Arquivos usados: só os cabeçalhos de `include/rapidjson` (`document.h`, `stringbuffer.h`,
  `writer.h`). Nada é compilado à parte — RapidJSON é header-only.
- Como entra: `native/CMakeLists.txt` adiciona `contrib/rapidjson/include` a `G3D_INCLUDES`, e
  `native/project/project_io.cpp` é a única unidade nossa que o usa. Nenhum código do Assimp é
  chamado para isto; a cópia é apenas o cabeçalho já versionado aqui.

## Material Components for Android

- Dependency Maven, não código em `third_party`: `com.google.android.material:material:1.12.0`
  (Apache-2.0), declarada em `app/build.gradle.kts`.

## Material Icons

- Finalidade: glifos dos botões do rail de ferramentas e das abas do inspetor.
- Repositório: https://github.com/google/material-design-icons
- Versão: tag `3.0.1`.
- Licença: Apache-2.0 (`material-icons/LICENSE`).
- Arquivos mantidos: `material-icons/svg/` com 28 SVGs upstream de 24 px, mais
  `material-icons/README.md`, que é a tabela que liga cada glifo ao drawable gerado e à ação que ele
  representa no editor.
- Como entram: o traçado é copiado sem edição em `app/src/main/res/drawable/ic_*.xml`; o preenchimento
  preto veda em `@android:color/white` porque quem define a cor é o `iconTint` do botão. Nada de
  reamostragem, simplificação ou redesenho.
- Tamanho adicionado: os 28 SVGs guardados somam 8,3 KB; os 28 drawables XML gerados a partir deles
  somam 13,2 KB. No APK eles entram no bloco de recursos compilados, que não foi medido separadamente.

## meshoptimizer

- Finalidade: otimização opcional de malha.
- **Ainda não incorporado** — fora do escopo até o fluxo principal estar estável.
- Licença anticipated: MIT.

## Advertências de licença exigidas na distribuição

Ao distribuir o APK, as cópias de licença MIT/ISC/Unlicense das bibliotecas acima devem ser
incluídas (por exemplo, em uma tela "Sobre/Licenças" ou em `app/src/main/assets/NOTICE`). Isso é
obrigatório antes de qualquer publicação; enquanto o app só roda em desenvolvimento local, os
arquivos `LICENSE` versionados aqui servem de registro.
