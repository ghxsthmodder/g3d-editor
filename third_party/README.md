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

- Finalidade: importação de OBJ, glTF e GLB.
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
  - `ASSIMP_NO_EXPORT=ON` — a exportação será reavaliada na Fase 5;
  - `BUILD_SHARED_LIBS=OFF` (liga estática dentro de `libg3dnative.so`);
  - `ASSIMP_BUILD_TESTS=OFF`, `ASSIMP_BUILD_ASSIMP_TOOLS=OFF`;
  - `ASSIMP_INJECT_DEBUG_POSTFIX=OFF`: sem isso o Assimp define `CMAKE_DEBUG_POSTFIX "d"` no mesmo
    build tree e a biblioteca passaria a `libg3dnatived.so`, quebrando `System.loadLibrary`.
- Tamanho adicionado: `libg3dnative.so` (núcleo + xatlas + Assimp) fica em ~3,8 MB por ABI em
  Debug e ~2,2 MB em Release.

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
  (x86-64, com símbolos); a contribuição no `.so` do APK ainda não foi medida.

## GLM

- Finalidade: matemática 3D (vetores, matrizes, câmeras, bounding boxes).
- Repositório: https://github.com/g-truc/glm
- Versão: tag `1.0.2`, commit `a532f5b1cf27d6a3c099437e6959cf7e398a0a67`.
- Licença: MIT (ver `glm/copying.txt`).
- Arquivos mantidos: cabeçalhos em `glm/`, `copying.txt`, `readme.md`. Testes, documentação e o
  próprio `CMakeLists.txt` da biblioteca foram removidos; GLM é header-only.
- Uso atual: `native/importers/assimp_importer.cpp` (transformações de nó e normais) e
  `native/renderer/g3d_renderer.cpp` (câmera, matrizes de projeção e picking por raio).

## Material Components for Android

- Dependency Maven, não código em `third_party`: `com.google.android.material:material:1.12.0`
  (Apache-2.0), declarada em `app/build.gradle.kts`.

## meshoptimizer

- Finalidade: otimização opcional de malha.
- **Ainda não incorporado** — fora do escopo até o fluxo principal estar estável.
- Licença anticipated: MIT.

## Advertências de licença exigidas na distribuição

Ao distribuir o APK, as cópias de licença MIT/ISC/Unlicense das bibliotecas acima devem ser
incluídas (por exemplo, em uma tela "Sobre/Licenças" ou em `app/src/main/assets/NOTICE`). Isso é
obrigatório antes de qualquer publicação; enquanto o app só roda em desenvolvimento local, os
arquivos `LICENSE` versionados aqui servem de registro.
