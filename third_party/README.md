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

- Finalidade planejada: importação/exportação de OBJ, glTF e GLB.
- **Ainda não incorporado.** Será adicionado na Fase 2, com a configuração de build restrita aos
  formatos necessários.
- Repositório: https://github.com/assimp/assimp — licença ISC (compatível com MIT/BSD).

## stb

- Finalidade: decodificação e gravação de imagens no compositor de texturas.
- Repositório: https://github.com/nothings/stb
- Versão: `master` no commit `a7d4c2175630e1ea5b6f8dd00ab96fed0b97b96d`.
- Licença: escolha entre MIT e Unlicense (arquivo `LICENSE` com as duas).
- Arquivos mantidos: `stb_image.h`, `stb_image_write.h`, `LICENSE`.
- Redimensionamento: `stb_image_resize2.h` foi movido para `deprecated/` no upstream e por isso
  **não** foi incorporado; se a composição precisar de resize, a alternativa será decidida na
  Fase 4.
- Estado: ainda não compilado no app (uso previsto na Fase 4).

## GLM

- Finalidade: matemática 3D (vetores, matrizes, câmeras, bounding boxes).
- Repositório: https://github.com/g-truc/glm
- Versão: tag `1.0.2`, commit `a532f5b1cf27d6a3c099437e6959cf7e398a0a67`.
- Licença: MIT (ver `glm/copying.txt`).
- Arquivos mantidos: cabeçalhos em `glm/`, `copying.txt`, `readme.md`. Testes, documentação e o
  próprio `CMakeLists.txt` da biblioteca foram removidos; GLM é header-only.
- Uso atual: `native/core/primitives.cpp` (bounding box e construção do cubo).

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
