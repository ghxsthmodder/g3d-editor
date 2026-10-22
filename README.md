# g3d-editor

Editor 3D para Android com foco em UV unwrapping (xatlas), edição de ilhas UV e composição de
texturas por camadas. Veja o [PLAN.md](PLAN.md) para objetivos, escopo e fases.

**Estado:** Fase 5 concluída. O fluxo completo existe — importar o modelo, gerar e editar os UVs,
compor a textura, salvar o projeto em arquivo e reabri-lo. O app rodou no telefone, e foi ao abrir um
glb de teste que ele apareceu fechado: nada limitava a cena que um arquivo descreve com poucas
palavras, e o assunto está tratado nos limites abaixo.
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
até o layout que veio do arquivo. Cobrem também o documento do compositor — resolução da tela, cor de
fundo e a pilha inteira, um passo por clique —, e como as imagens das camadas são compartilhadas, um
passo não copia pixel nenhum: desfazer uma remoção traz de volta a mesma imagem que estava lá. A
interface seguiu a proposta do protótipo: paleta de grafite, rail
de glifos Material vendorizados no lugar das duas barras de texto, inspetor com as abas Cena | UV |
Camadas **feito coluna à direita, fechada por padrão** — a tela deste app é travada em paisagem
(2400 × 1080 px, ~960 × 432 dp), então o lado escasso do telefone é a altura e o atlas é um quadrado
medido pelo menor dos dois: a coluna gasta 216 dp de largura, o lado que sobra, e o quadrado do
atlas mede ~372 dp de lado com as ferramentas abertas ou fechadas; uma gaveta na beirada de baixo
gastaria 210 dp da altura e deixaria o atlas com 151 dp (aritmética de tela, ainda não conferida no
aparelho) —, atlas dominante quando a edição é dele, a superfície dividida pelo seu lado curto —
e no deitado o painel do atlas recebe no máximo o que um quadrado consegue usar, a sobra voltando
para o modelo em vez de virar tarja vazia ao lado das ilhas — e arrastar com um dedo move as ilhas
selecionadas. A seleção aceita pino: a ilha bloqueada continua destacada para poder ser destravada,
mas nenhuma ferramenta a alcança, um arrasto cujo grupo inteiro está pinado é recusado e o dedo
volta para a câmera, e o reempacotamento contorna a caixa que ela ocupa em vez de soterrá-la — o
azul do preenchimento é o que diz qual está pinada, o contorno âmbar continua dizendo sobre o que o
clique age, e a linha de estado da aba UV conta `bloqueadas=N`. Gerar UV é a etapa longa, e ela
agora diz onde está: o xatlas reporta o que faz — preparar a malha, criar as ilhas, empacotar o
atlas, montar a malha — e uma barra com a porcentagem aparece na faixa sobre a beirada do canvas,
ao lado do botão **Interromper**, porque cancelar não pode depender de um gesto escondido. Uma
geração interrompida chega como o que foi pedida — a mensagem diz que foi interrompida, não que
falhou — e a faixa some com o trabalho, sem gastar um dp do modelo. Importar também diz em que fase
está — lendo o arquivo, arrumando a malha, montando a cena —, cada uma com o seu número, mas não tem
botão de interromper: o Assimp desta tag reporta e deita fora a resposta, de modo que um pedido de
parada não teria a quem chegar, e um botão morto durante a maior parte da espera seria um botão que
mente. O que o núcleo pode parar, ele para. O comportamento do núcleo é
comprovado pelos nove testes do host
(`bash native/tests/run_host_test.sh`), inclusive o renderizador headless, a ida e volta dos três
formatos de modelo, o desfazer/refazer e a reabertura de um projeto salvo. A fase também tratou dos
limites: a **tela do compositor não passa do `GL_MAX_TEXTURE_SIZE`** que o driver informou (as
resoluções acima ficam apagadas no painel), a **memória retida** aparece na aba Cena somada por
origem — arquivo, malhas, camadas com a imagem composta, a sessão de UV e o que o histórico ainda
segura —, as chamadas caras do JNI
respondem com erro em vez de deixar uma `std::bad_alloc` atravessar para a pilha do Java e a **cena
de um arquivo pesado é limitada pelo orçamento de memória que o telefone informa**: um glTF de 4 MB
que apontava 2000 nós para 4 malhas chegou a expandir em 28,7 milhões de vértices e um gigabyte e
meio, o que fecha o processo antes de qualquer aviso; as referências repetidas no mesmo lugar agora
são unidas e o que passaria do orçamento fica de fora, com o número dito na mensagem. O layout
novo, o salvamento e a leitura via SAF ainda não foram validados em telefone.

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
