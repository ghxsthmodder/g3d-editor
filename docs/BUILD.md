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

Medidas do último *build* de depuração: `app-debug.apk` de 14.527.803 bytes (13,9 MiB), com
`libg3dnative.so` despojado de 2.979.544 bytes (arm64-v8a) e 3.100.968 bytes (x86_64), os dois
empacotados sem compressão — byte por byte os mesmos `.so` do APK de distribuição, porque a metade
nativa agora é compilada da mesma maneira nos dois (ver *O tipo de compilação nativo* abaixo). O
módulo de projeto traz o RapidJSON que já vinha com o Assimp (cabeçalhos, instanciados onde o
projeto é escrito e lido). A forma de distribuição:
`assembleRelease` produz `app-release-unsigned.apk` de 11.418.313 bytes (10,9 MiB), com o mesmo
`libg3dnative.so` de 2.979.544 bytes (arm64-v8a) e 3.100.968 bytes (x86_64). É o APK sem
assinatura: o bloco `META-INF` que a assinatura acrescenta não foi medido. Os exportadores de OBJ e
glTF do Assimp, ligados nesta contagem, custam cerca de 100 KB por ABI. Os glifos vetoriais da
interface são 29 arquivos XML que somam 13.927 bytes de fonte; no APK eles entram no bloco de
recursos compilados, que não é medido à parte aqui. As licenças a reproduzir são seis arquivos em
`app/src/main/assets/licenses/`, 26.750 bytes crus e 10.167 comprimidos no APK, cada um conferido
contra a fonte em `third_party/` byte por byte depois de extrair do APK:

```bash
APK=app/build/outputs/apk/debug/app-debug.apk
unzip -o -q "$APK" "assets/licenses/*" -d /tmp/apkx
cmp /tmp/apkx/assets/licenses/xatlas.txt third_party/xatlas/LICENSE
```

O `.so` não mudou com nada disto: a tela de licenças e a lista de recentes são Kotlin e recursos, e
a ponte não ganhou chamada nenhuma — a paridade segue 71 de cada lado.

## O tipo de compilação nativo

O Gradle escolhe o `CMAKE_BUILD_TYPE` pela variante: `Debug` para o APK de depuração e
`RelWithDebInfo` para o de distribuição. Aqui isso não é troca de gosto — é a diferença entre o app
fechar ou não nas mãos de quem usa. Com as asserções vivas, a importação de **qualquer** glTF ou GLB
aborta dentro do RapidJSON vendorizado: a sonda que o importador glTF do Assimp faz antes de ler
(`glTF2Importer::CanRead` → `Asset::CanRead` → `AssetMetadata::Read`) cai no `FindMember` de
`document.h`, e a exportação em glTF cai do mesmo modo. Um OBJ não passa por ali. Uma asserção que
falha chama `abort()`, e no telefone `abort()` é o editor fechando sem dizer nada — foi assim que
ele se fechou ao abrir a `doghouse.glb`.

Por isso `app/build.gradle.kts` pede `-DCMAKE_BUILD_TYPE=RelWithDebInfo` para a variante `debug`: a
metade nativa do APK de depuração passa a ser a mesma que a do APK testado em distribuição. O
`-g` fica, então uma falha continua legível; o que sai são as asserções. As do próprio editor
seguem vivas onde elas servem, no *build* de host em Debug.

```bash
# que configuração entrou realmente no APK de depuração
grep -m1 "^CMAKE_BUILD_TYPE" app/.cxx/RelWithDebInfo/*/arm64-v8a/CMakeCache.txt
# os nove testes do host, nos três tipos
for t in Debug RelWithDebInfo Release; do
  G3D_HOST_BUILD_TYPE=$t bash native/tests/run_host_test.sh
done
```

Os três tipos passam os nove testes, e nenhum deles reproduz a queda: ela dependia de qual cópia
embutida do `FindMember` o ligador guardava no binário, e é justamente por não se poder escolher
isso no telefone que a asserção vendorizada não pode continuar ali. O que falta conferir no aparelho
é se a `doghouse.glb` abre até o fim.

## Conferir a ponte JNI

Toda `external fun` de `app/src/main/java/com/g3d/editor/Native.kt` precisa de um símbolo
`Java_com_g3d_editor_Native_*` exportado pela biblioteca nativa. Nenhum dos dois lados reclama de um
encontro faltando — só dá `UnsatisfiedLinkError` no telefone, no momento em que a função é chamada.
Depois de `assembleDebug`:

```bash
SO=app/build/intermediates/merged_native_libs/debug/mergeDebugNativeLibs/out/lib/arm64-v8a/libg3dnative.so
diff <(nm -D --defined-only "$SO" | grep -o 'Java_com_g3d_editor_Native_[a-zA-Z_]*' |
      sed 's/.*_Native_//' | sort) \
     <(grep -o 'external fun native[A-Za-z]*' app/src/main/java/com/g3d/editor/Native.kt |
      sed 's/external fun //' | sort)
```

Sem saída, os dois lados batem. São 71 funções declaradas e 71 exportadas neste momento.

## Testar o núcleo nativo sem dispositivo

```bash
bash native/tests/run_host_test.sh
```

O script configura `native/CMakeLists.txt` para o compilador do sistema e roda os nove testes via
CTest (nenhum deles faz parte do APK):

| Teste | Executável | O que verifica |
| --- | --- | --- |
| `uv_smoke_test` | `host_smoke_test` | geração de UV com xatlas a partir de geometria sintética, o relatório de progresso que a biblioteca preenche enquanto roda — etapa, porcentagem, o cancelamento pedido no meio de uma geração de verdade e o que ele devolve — e o que fica de fora do relatório quando o trabalho termina |
| `import_test` | `host_import_test` | importação de OBJ/glTF/GLB (`native/tests/models`) e estado do documento, inclusive as referências repetidas de um mesmo glTF unidas no lugar, o orçamento de memória que para a cena e diz quantas partes ficaram de fora, os nomes com bytes fora do UTF-8 que o telefone recusaria, e o relatório de progresso da importação — as três fases por que todo arquivo passa, o número dentro de cada uma, o que fica registrado depois que a corrida acaba e o que uma recusa antes de começar não muda |
| `uv_islands_test` | `host_uv_test` | agrupamento de UVs em ilhas e conectividade |
| `uv_edit_test` | `host_uv_edit_test` | sessão de edição: seleção, transformações, reempacotamento, o pino que tira a ilha do alcance das ferramentas e que o reempacotamento contorna, validar, aplicar/cancelar, o desfazer/refazer, inclusive o arrasto inteiro num passo, o progresso que a sessão publicada devolve quando não há geração alguma — vazio, e um cancelar que não encontra nada — e a memória que a sessão aberta retém |
| `export_test` | `host_export_test` | exportação OBJ/glTF/GLB em memória, ida e volta relida pelo importador: posição, UV e normal por canto, os pares `.gltf`+`.bin` e os avisos de perda |
| `compose_test` | `host_compose_test` | pilha de camadas: fundo, mover, girar, escalar, opacidade, visibilidade, ordem, duplicar, remover, as duas recusas de tela (limite da GPU e orçamento de megapixels), a conta de memória retida com a imagem composta, o desfazer/refazer do documento — tela, fundo e pilha, com as imagens compartilhadas e o teto de 64 passos — e o PNG resultante |
| `project_test` | `host_project_test` | container `.g3dp`: escrita e leitura, cabeçalho cortado, mágica errada, versão futura, bloco desalinhado, arquivo truncado, e a ida e volta do editor inteiro — UVs aplicados byte a byte, tela, fundo, ordem, posição, giro, escala, opacidade e visibilidade das camadas |
| `image_test` | `host_image_test` | decodificação PNG/JPEG (stb), ida e volta sem perdas do PNG, paleta com transparência, limite de pixels e quadriculado de referência |
| `render_test` | `host_render_test` | renderizador GLES 3 real, desenhado headless em um pbuffer: vista 3D, vista 2D do atlas, a divisão da superfície pelo lado curto (numa superfície alta os dois painéis ficam um sobre o outro e o atlas mede-se pela largura quase toda — no telefone, travado em paisagem, quem manda é a altura e a divisão continua sendo pela largura, com o painel do atlas cortado no que um quadrado consegue usar e a sobra devolvida ao modelo), o pino de ilha publicado no preenchimento sem tirar o contorno da seleção, a textura composta aplicada nos dois modos e o `GL_MAX_TEXTURE_SIZE` do driver publicado como limite de tela do compositor |

`render_test` só é construído quando o host tem EGL + OpenGL ES 3 (`libegl-dev`,
`libgles-dev`/Mesa). Ele usa a plataforma *surfaceless* do Mesa e, quando não há contexto
disponível, imprime `SKIP` e sai com sucesso. Com `G3D_RENDER_PREVIEW=/tmp/look` ele também grava
PPM para inspecionar a imagem sem telefone: `look.solido.ppm`, `look.normais.ppm`,
`look.selecao.ppm`, `look.atlas.ppm` (sobreposições em vermelho), `look.atlas_dominante.ppm` (o
atlas grande com a tira 3D estreita), `look.ilha.ppm` (ilha selecionada em âmbar),
`look.arrastar_ilha.ppm` (a mesma ilha depois do arrasto), `look.repack.ppm`, `look.so_atlas.ppm` e
`look.vazio.ppm`. Da Fase 4 entram `look.textura.ppm` (modelo sombreado com a composição),
`look.grade.ppm` (grade quadriculada de referência), `look.atlas_textura.ppm` (a composição desenhada
no atlas), `look.atlas_sem_linhas.ppm` (atlas com as linhas de UV ocultadas) e
`look.atlas_mudanca.ppm` (atlas depois de mover uma camada, provando que as duas vistas acompanham).

As fixtures de imagem usadas pelo `image_test` estão em `native/tests/models`: `gradient.jpg`
(64×64, JPEG *baseline* — o stb recusa JPEG progressivo), `palette.png` (32×32 em paleta com um
índice transparente) e `oversized.png`, que são só os cabeçalhos PNG declarando 9000×9000 px e
serve para provar que o orçamento de pixels recusa a imagem antes de alocá-la.

## Estrutura

- `app/` — interface Android (Kotlin, Views + Material, orientação paisagem, tema escuro).
- `native/` — núcleo C++, JNI (`native/bridge`) e `CMakeLists.txt` usado pelo AGP.
- `native/tests/` — harness de teste no host.
- `native/project/` — o formato `.g3dp`: container e leitura/escrita do documento (ver `docs/PROJECT.md`).
- `third_party/` — bibliotecas incorporadas no repositório (ver `third_party/README.md`).
