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
blocos são alinhados a 4 para que os floats do projeto (só os UVs) possam ser lidos direto do buffer.

## O que vai dentro

```json
{
  "format": 1,
  "model":   { "name": "caixa.obj", "modelOffset": 0, "modelLength": 3120 },
  "meshes":  [ { "index": 0, "count": 48, "uvOffset": 3124, "uvLength": 384 } ],
  "texture": {
    "width": 1024, "height": 1024, "background": [ 46, 50, 60 ],
    "layers": [ {
      "name": "decal.png", "imageOffset": 3508, "imageLength": 1180,
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
  importador. Só entram as malhas que têm UVs. Ao abrir, é a malha que manda: ela exige
  `vertexCount * 2` floats e, se o bloco não bate, a malha fica com os UVs que vieram do modelo e o
  projeto avisa — em vez de meio aplicar e seguir como se nada fosse.
- **`texture`** — a tela do compositor (`width`/`height`), a cor de fundo em três bytes e a pilha de
  camadas **de baixo para cima**, cada uma com sua imagem em PNG e o lugar exato onde estava:
  centro em pixels, giro em graus, escala por eixo, opacidade de 0 a 1 e visibilidade. As imagens
  são gravadas em PNG e decodificadas de volta, então o projeto reabre com as camadas originais e
  não com a textura já rasterizada, que é o que o PLAN pede que não aconteça.

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
