# Formato de projeto do G3D Editor

O projeto não é um dos formatos de modelo: ele guarda o editor, não a malha. Um `.obj` ou `.glb`
exportado descreve geometria para qualquer programa ler; o `.g3dp` descreve a sessão de trabalho —
o arquivo de onde a cena veio, os UVs como estão agora e a pilha de camadas montada em cima deles.
Por isso o container tem mágica própria e não se parece com nada que o importador aceitaria.

A extensão e a estrutura foram decididas aqui, antes de o salvamento ser consolidado, como pede o
PLAN.

## O arquivo

```
0                4                8                12               16
+----------------+----------------+----------------+----------------+
|     "G3DP"     |     version    |   jsonLength   |   binLength    |
+----------------+----------------+----------------+----------------+
|  JSON, preenchido com espaços até múltiplo de 4                    |
+-------------------------------------------------------------------+
|  binário, preenchido com zeros até múltiplo de 4                    |
+-------------------------------------------------------------------+
```

Cabeçalho de 16 bytes, todos os inteiros em *little-endian*:

| Campo | Bytes | Conteúdo |
| --- | --- | --- |
| mágica | 4 | `G3DP` |
| versão | 4 | `1` |
| `jsonLength` | 4 | comprimento do bloco JSON, múltiplo de 4 |
| `binLength` | 4 | comprimento do bloco binário, múltiplo de 4 |

A forma é a do glTF binário — uma descrição em texto apontando para um bloco de dados no mesmo
arquivo — porque o editor já lê esse desenho e não há motivo para inventar outro. O que muda é a
mágica: um projeto não deve ser confundido com um modelo.

O JSON é a única coisa que o leitor interpreta. Todo byte que ele referencia vive no bloco binário e
é endereçado por par `…Offset` / `…Length`, em bytes a partir do começo daquele bloco. Os dois
blocos são alinhados a 4 para que os números do projeto (UVs e geometria) possam ser lidos direto do
buffer.

## O que vai dentro

```json
{
  "format": 1,
  "model":   { "name": "caixa.obj", "modelOffset": 0, "modelLength": 3120 },
  "meshes":  [ { "index": 0, "count": 48, "uvOffset": 3120, "uvLength": 384 },
               { "index": 1, "count": 48, "uvOffset": 3504, "uvLength": 384, "rebuilt": true,
                 "positionOffset": 3888, "positionLength": 288,
                 "normalOffset": 4176, "normalLength": 288,
                 "triangleOffset": 4464, "triangleLength": 144 } ],
  "texture": {
    "width": 1024, "height": 1024, "background": [ 46, 50, 60 ],
    "layers": [ {
      "name": "decal.png", "imageOffset": 4608, "imageLength": 1180,
      "centreX": 512, "centreY": 512, "rotation": 30,
      "scaleX": 1.5, "scaleY": 1.5, "opacity": 0.4, "visible": true
    } ]
  }
}
```

- **`model`** — os bytes *inteiros* do arquivo que o importador recebeu, com o nome com que entrou.
  O projeto não guarda o caminho: um caminho de SAF vale até o dono do arquivo movê-lo, e um projeto
  que não encontra seu modelo deixou de ser editável. Importar de novo a partir desses bytes é o
  mesmo caminho que a tela de importação usa.
- **`meshes`** — os UVs de cada malha da cena, dois `float32` por vértice, na ordem dos índices do
  importador. Só entram as malhas que têm UVs.
- **`rebuilt`** — três arrays a mais para a malha que *deixa* de ser a que o arquivo descreve: as
  posições (`float32` × 3), as normais (`float32` × 3, comprimento zero quando a malha não tem) e os
  índices dos triângulos (`uint32`). Uma geração de UV entrega à cena uma malha soldada e reaberta
  nas emendas que abriu, e o xatlas não garante a ordem dos vértices: a caixa de vinte e quatro
  vértices pode voltar com os mesmos vinte e quatro em outra ordem. Os UVs dessa malha *caberiam* na
  que o arquivo reconstroi, e o projeto reabriria com a textura aplicada nos cantos errados sem que
  nada o dissesse. Só o passo que substitui a malha, que é aplicar uma sessão de UV, sabe quais são
  dessas, e é ele que marca a malha; mover uma ilha sem gerar não marca nada, e o projeto continua
  do tamanho que era.
- **`texture`** — a tela do compositor (`width`/`height`), a cor de fundo em três bytes e a pilha de
  camadas **de baixo para cima**, cada uma com sua imagem em PNG e o lugar exato onde estava:
  centro em pixels, giro em graus, escala por eixo, opacidade de 0 a 1 e visibilidade. As imagens
  são gravadas em PNG e decodificadas de volta, então o projeto reabre com as camadas originais e
  não com a textura já rasterizada, que é o que o PLAN pede que não aconteça.

Ao abrir, a malha que está na cena manda o número de floats de UV quando o projeto não traz
geometria: se o bloco não bate, a malha fica com os UVs que vieram do modelo e o projeto avisa, em
vez de meio aplicar e seguir como se nada fosse. Quando o projeto traz geometria, os arrays e os UVs
são lidos primeiro e conferidos uns contra os outros — os vértices que as posições dão, o par de
flutuantes por vértice, os índices dentro das posições — e uma malha que não fecha inteira é
recusada inteira, sem tocar na que está de pé.

Nada mais é guardado nesta versão: câmera, material, seleção e histórico de desfazer ficam de fora,
e o salvamento diz isso em voz alta quando encontra uma sessão de UV ainda aberta com trabalho por
aplicar.

## Como o código se divide

| Arquivo | Papel |
| --- | --- |
| `native/project/project_container.{hpp,cpp}` | escreve e lê os dois blocos, e recusa o que não é projeto: tamanho de menos, mágica errada, versão que não existe, bloco desalinhado, arquivo truncado, JSON que não é objeto |
| `native/project/project_io.{hpp,cpp}` | traduz o documento e o compositor para dentro dos blocos e de volta; o `ProjectReport` conta o que entrou e o que ficou de fora |
| `native/bridge/g3d_jni.cpp` | `nativeProjectBytes`, `nativeProjectReport`, `nativeOpenProject` |
| `app/src/main/java/com/g3d/editor/MainActivity.kt` | dois botões na aba Cena, o *save-as* e o *open* do Storage Access Framework |

O JSON é lido e escrito com o RapidJSON que vem dentro do Assimp (`third_party/assimp/contrib`),
licença MIT, já presente no repositório — nada novo foi baixado para isto.

## Limites

- O arquivo inteiro é lido na memória antes de abrir, com teto de 192 MB na interface. Um projeto é
  um modelo mais suas imagens, então o teto é maior que o de um modelo sozinho.
- Uma camada que não conseguisse virar PNG é deixada de fora com um aviso, e o restante do projeto é
  gravado: perder uma imagem é recuperável, perder o arquivo não.
- Abrir um projeto substitui o que estava aberto e fecha a sessão de UV, que apontava para a cena
  anterior.
- A versão é rígida: um arquivo de versão maior que `1` é recusado com o número na mensagem, em vez
  de ser lido por cima do que não se entende.
- `rebuilt` não mudou esse número porque não precisa: um leitor que não conhece a chave lê o UV do
  mesmo jeito que sempre leu e, se ele não cabe na malha do arquivo, avisa e segue — exatamente o
  que fazia antes de a geometria existir. Quem escreve é que passou a saber mais.
- A tela do compositor é checada contra o `GL_MAX_TEXTURE_SIZE` do telefone onde o projeto abre, que
  é publicado no `native/renderer/g3d_renderer.cpp` assim que o contexto GL existe. Um projeto
  gravado com atlas de 4096 aberto num telefone que só segura 2048 abre do mesmo jeito, com um aviso
  dizendo que a tela salva não coube e qual é o limite do aparelho, e fica sem tela até o usuário
  escolher uma resolução que caiba. O arquivo não é reescrito por causa disto.
- Um glTF é uma lista de referências, então o que ele descreve pode ser muito maior que o arquivo:
  quatro megabytes de nós chegaram a expandir em 28,7 milhões de vértices e um gigabyte e meio de
  memória, e o processo morria antes de qualquer mensagem. A importação junta as referências que
  caem exatamente no mesmo lugar e para de acrescentar malhas quando a cena passaria do orçamento
  que o aplicativo mediu no telefone (`nativeSetImportBudget`, um quarto do que o aparelho declara
  livre, com piso de 32 MB), avisando quantas partes ficaram de fora. A primeira malha do arquivo
  entra sempre: assimp já a pagou, e um editor vazio com um aviso é pior que um pesado. Um `.g3dp`
  gravado com a cena limitada reabre limitada do mesmo jeito, e o arquivo não é reescrito por causa
  disto.
