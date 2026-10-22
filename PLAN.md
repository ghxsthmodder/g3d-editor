G3D Editor

Editor 3D para Android com foco em UV Unwrapping, edição de ilhas UV e composição de texturas.

O projeto busca oferecer um fluxo de trabalho prático para preparar modelos 3D e suas texturas diretamente em dispositivos Android, priorizando bibliotecas open source, desempenho e simplicidade de implementação.

«Status: em planejamento e desenvolvimento. As funcionalidades descritas neste documento representam os objetivos do projeto, não uma declaração de que já estão implementadas.»

Objetivos

- Editar e visualizar modelos 3D em dispositivos Android.
- Gerar UVs automaticamente utilizando o xatlas.
- Selecionar, transformar e reorganizar ilhas UV.
- Importar imagens para montar texturas por camadas.
- Visualizar texturas aplicadas ao modelo.
- Exportar modelos com UVs atualizados e texturas finais.
- Manter projetos editáveis para continuar o trabalho posteriormente.
- Reutilizar bibliotecas existentes para reduzir o tempo de desenvolvimento.

O projeto não tem foco comercial. A prioridade é construir uma ferramenta funcional sem desenvolver sistemas complexos ou reinventar funcionalidades já disponíveis em projetos open source.

Formatos suportados

Modelos 3D

Formato| Extensão| Finalidade
Wavefront OBJ| ".obj"| Importação e exportação de geometria e UVs
glTF| ".gltf"| Importação e exportação de modelos estruturados
GLB| ".glb"| Importação e exportação de modelos em formato binário

O suporte efetivo a cada operação dependerá da implementação dos importadores, exportadores e dos recursos disponíveis no formato de destino.

O OBJ possui limitações em relação a hierarquia, materiais e outros dados estruturados. O editor deverá informar quando informações não puderem ser preservadas durante a exportação.

Imagens e texturas

O formato inicial de exportação da textura final será PNG.

A importação deverá utilizar os formatos de imagem suportados pela biblioteca de decodificação integrada. PNG e JPEG são os formatos iniciais prioritários.

Formatos adicionais poderão ser incorporados posteriormente, conforme a necessidade.

G3D

Repositório: https://github.com/ghxsthmodder/g3d

O formato G3D ainda está em desenvolvimento. Portanto, não haverá importação nem exportação G3D nesta versão do editor.

O repositório será mantido como referência para uma integração futura. O Coder (Qoder) poderá consultar suas especificações e implementação quando necessário, sem presumir que o formato esteja finalizado.

A arquitetura interna deverá permitir a adição futura de G3D sem exigir a reescrita das ferramentas de UV, do compositor de texturas ou do renderizador.

Dependências

As dependências devem ser selecionadas e integradas de acordo com a necessidade real do projeto. Priorizar versões estáveis, licenças compatíveis e compilação viável para Android.

1. xatlas — geração de UVs

Repositório: https://github.com/jpcy/xatlas

Biblioteca C++ para geração automática de coordenadas UV, criação de charts e empacotamento de ilhas em um atlas.

É a dependência prioritária do projeto.

Responsabilidades:

- Gerar UVs automaticamente.
- Criar ilhas a partir da geometria.
- Empacotar as ilhas no atlas.
- Permitir configurar os parâmetros disponíveis na biblioteca.
- Retornar os resultados para a estrutura interna do editor.

O xatlas não implementa a interface de edição UV nem a composição de imagens. Essas funcionalidades serão desenvolvidas no G3D Editor.

2. Assimp — importação e exportação de modelos

Repositório: https://github.com/assimp/assimp

Biblioteca para importação e processamento de modelos 3D.

Será avaliada para importar OBJ, glTF e GLB e, quando adequado, exportar os modelos editados.

A configuração Android deverá incluir apenas os formatos e recursos necessários, quando a arquitetura de compilação permitir.

Não presumir que todos os recursos disponíveis na importação estejam disponíveis na exportação.

3. stb — processamento de imagens

Repositório: https://github.com/nothings/stb

Biblioteca de cabeçalho único que oferece ferramentas para leitura e gravação de imagens.

Componentes a avaliar:

- "stb_image.h": leitura de imagens.
- "stb_image_write.h": gravação de imagens.
- "stb_image_resize2.h": redimensionamento, caso necessário e disponível na versão selecionada.

Será utilizada para importar imagens ao compositor e gerar a textura final em PNG.

4. GLM — matemática 3D

Repositório: https://github.com/g-truc/glm

Biblioteca matemática para C++ que fornece vetores, matrizes, transformações e operações necessárias para câmeras e geometria 3D.

5. Material Components for Android — interface

Repositório: https://github.com/material-components/material-components-android

Biblioteca de componentes de interface Android alinhados ao Material Design.

A versão do Material Design não é um requisito rígido. A implementação deverá escolher uma versão compatível com o ambiente do projeto, sem migrações desnecessárias.

Se o projeto existente não utilizar Gradle, avaliar o custo de integração antes de alterar seu sistema de compilação.

6. OpenGL ES — renderização

Tecnologia gráfica utilizada para visualizar modelos e texturas.

OpenGL ES 3.0 será a referência inicial, respeitando as capacidades do dispositivo e as limitações de compatibilidade.

O renderizador será desenvolvido especificamente para as necessidades do editor, sem incorporar um motor de jogo completo.

7. meshoptimizer — otimização opcional

Repositório: https://github.com/zeux/meshoptimizer

Biblioteca para otimização de malhas e melhoria da organização de dados gráficos.

Sua integração é opcional e deverá ocorrer somente após a conclusão do fluxo principal de edição.

Seleção de dependências

Antes de adicionar uma dependência, verificar:

- Compatibilidade com Android e as ABIs necessárias.
- Licença e requisitos de atribuição.
- Tamanho adicionado ao aplicativo.
- Complexidade de compilação.
- Possibilidade de compilar apenas os módulos necessários.
- Existência de uma alternativa mais simples já disponível no projeto.

Não adicionar bibliotecas pesadas sem uma justificativa técnica concreta.

Arquitetura

O aplicativo será dividido em três partes principais: interface Android, núcleo C++ e renderizador.

G3D Editor
├── app/
│   ├── ui/
│   │   ├── ProjectScreen
│   │   ├── ModelEditorScreen
│   │   ├── UVEditorScreen
│   │   ├── TextureComposerScreen
│   │   └── ExportScreen
│   └── file_access/
│
├── native/
│   ├── core/
│   │   ├── Document
│   │   ├── Scene
│   │   ├── Mesh
│   │   ├── Material
│   │   └── UndoRedo
│   ├── importers/
│   │   └── AssimpImporter
│   ├── uv/
│   │   ├── UVService
│   │   ├── UVSelection
│   │   ├── UVTransform
│   │   └── UVValidation
│   ├── textures/
│   │   ├── Image
│   │   ├── TextureLayer
│   │   ├── TextureComposer
│   │   └── TextureExporter
│   ├── renderer/
│   └── bridge/
│
└── third_party/
    ├── xatlas/
    ├── assimp/
    ├── stb/
    └── glm/

Essa árvore é uma referência arquitetural, não uma exigência de criar antecipadamente todos os arquivos e diretórios. A estrutura definitiva deverá respeitar o repositório existente e o sistema de build escolhido.

Núcleo C++

Responsável pela estrutura interna de documentos, geometria, UVs, materiais, camadas e operações de edição.

As ferramentas deverão trabalhar sobre uma representação interna comum, independente do formato do arquivo importado.

Interface Android

Responsável pela navegação, menus, controles, gestos, seleção de arquivos e exibição do estado das operações.

A interface não deverá implementar algoritmos de processamento de malhas ou composição de imagens.

Renderizador

Responsável por visualizar a geometria e os materiais, mostrar a textura aplicada e permitir a inspeção do resultado das edições.

Comunicação JNI

Quando a interface utilizar Java e o núcleo utilizar C++, a comunicação será feita por uma API JNI pequena e explícita.

Evitar chamadas frequentes por vértice ou por pixel. Operações pesadas deverão ser executadas no núcleo nativo.

Interface do usuário

Orientação e tema

- Orientação padrão: paisagem.
- Estilo: técnico e escuro.
- Design: Material Design.
- Prioridade: funcionalidade, clareza e aproveitamento do espaço.

O aplicativo não deverá reproduzir a interface complexa de um editor desktop. As ferramentas devem ser organizadas em áreas específicas, com painéis recolhíveis.

Tela de projetos

Deverá permitir:

- Criar ou abrir um projeto editável.
- Importar um modelo.
- Acessar arquivos recentes.
- Consultar informações básicas do documento.

O usuário deverá poder abrir diretamente um modelo sem ser obrigado a criar manualmente um projeto antes.

Editor 3D

Elementos principais:

- Barra superior com arquivo, desfazer, refazer e salvar.
- Área central de visualização.
- Ferramentas de seleção e transformação.
- Painel recolhível de hierarquia e propriedades.
- Acesso ao editor UV, compositor e exportação.

Gestos previstos:

- Arrastar para orbitar a câmera, quando nenhuma ferramenta de transformação estiver ativa.
- Pinçar para aproximar ou afastar.
- Dois dedos para movimentar a visualização.
- Controles visíveis para selecionar, mover, rotacionar e escalar.

As ações importantes não deverão depender exclusivamente de gestos ocultos.

Editor UV

O editor UV será uma área própria do aplicativo.

Modos de visualização:

- 3D + UV: modelo e atlas simultaneamente.
- Somente 3D: inspeção da geometria e da textura.
- Somente UV: área ampliada para edição do atlas.

Ferramentas:

- Selecionar ilhas.
- Selecionar múltiplas ilhas.
- Mover, rotacionar e escalar ilhas.
- Reempacotar ilhas.
- Bloquear ilhas.
- Mostrar limites do atlas.
- Detectar e destacar sobreposições.
- Mostrar a textura como referência.
- Desfazer e refazer operações.

A edição deverá atualizar as coordenadas UV reais da malha.

Sobreposições podem ser intencionais, como em modelos simétricos. O editor deverá avisar o usuário, sem impedir automaticamente todas as sobreposições.

Geração automática de UVs

O painel de geração deverá oferecer:

- Gerar UVs.
- Configurar a resolução do atlas.
- Configurar a margem entre ilhas.
- Exibir opções compatíveis com os recursos reais do xatlas.
- Pré-visualizar o resultado.
- Aplicar ou cancelar.

Os UVs existentes deverão ser preservados até o usuário confirmar a aplicação do resultado.

Compositor de texturas

O compositor permitirá criar uma textura final usando imagens e camadas.

Funcionalidades iniciais:

- Importar imagens.
- Criar camadas de imagem.
- Mover, rotacionar e redimensionar imagens.
- Controlar opacidade e visibilidade.
- Reordenar camadas.
- Duplicar e remover camadas.
- Definir a cor de fundo.
- Exportar o resultado em PNG.
- Mostrar ou ocultar as linhas UV.

A sobreposição UV será somente visual e não deverá ser incorporada à imagem exportada por padrão.

Edição por ilhas UV

O usuário deverá poder selecionar uma ou mais ilhas e utilizá-las como referência para as operações de edição.

É necessário distinguir claramente:

1. Transformação UV: altera as coordenadas da malha.
2. Transformação de imagem: altera a posição ou escala dos pixels no atlas.

Transformar uma ilha não deverá mover automaticamente as camadas de imagem sem uma decisão explícita. Da mesma forma, mover uma imagem não deverá alterar as coordenadas UV.

Na primeira versão, a composição será realizada no espaço 2D do atlas. Pintura projetada diretamente sobre o modelo 3D ficará para uma versão posterior.

Pré-visualização 3D

O usuário deverá conseguir visualizar a composição atual aplicada ao modelo.

Recursos:

- Visualização texturizada.
- Textura quadriculada de referência.
- Inspeção de emendas e distorções.
- Alternância entre visualização texturizada e simples.
- Atualização após mudanças na composição.

Exportação

A tela de exportação deverá permitir:

- Exportar para OBJ, glTF ou GLB, conforme os exportadores implementados.
- Exportar a textura final em PNG.
- Selecionar a resolução da textura.
- Escolher o destino pelo sistema Android.
- Exibir avisos sobre dados que possam ser perdidos.

O aplicativo deverá informar quando o formato de destino não conseguir representar algum recurso do documento.

Projeto editável

O formato do projeto editável será separado dos formatos de modelo.

O projeto deverá guardar:

- Referência ao modelo original.
- Estado atual dos UVs.
- Camadas de textura.
- Imagens utilizadas.
- Ordem, visibilidade, opacidade e transformações das camadas.
- Configurações relevantes para a composição e exportação.

A extensão e a estrutura definitivas deverão ser decididas antes de consolidar a implementação de salvamento.

O projeto deverá poder ser reaberto para continuar a edição, sem depender somente da textura final rasterizada.

As referências a arquivos externos deverão ser tratadas de maneira robusta, evitando presumir que o caminho original continuará disponível.

Desempenho e compatibilidade

O aplicativo deverá considerar memória limitada e diferenças entre GPUs móveis.

Requisitos:

- Executar operações pesadas fora da thread principal.
- Exibir progresso em operações demoradas.
- Permitir cancelar operações quando tecnicamente possível.
- Validar dimensões de imagens antes de alocar memória.
- Evitar cópias desnecessárias de grandes buffers.
- Tratar erros de carregamento e falta de memória.
- Permitir trabalhar com diferentes resoluções de textura.
- Começar com opções de 1024, 2048 e 4096 pixels, respeitando os limites do dispositivo.

O aplicativo não deverá assumir que todo dispositivo suporta texturas grandes ou os mesmos recursos gráficos.

Escopo da primeira versão

Obrigatório

- Interface Android com Material Design.
- Orientação paisagem.
- Importação de OBJ, glTF e GLB.
- Visualização 3D.
- Seleção de malhas.
- Geração automática de UVs com xatlas.
- Visualização e edição de ilhas UV.
- Importação de imagens.
- Composição básica por camadas.
- Pré-visualização da textura no modelo.
- Exportação de textura em PNG.
- Exportação de modelos para os formatos de saída efetivamente suportados.
- Salvamento e reabertura de projetos editáveis.
- Desfazer e refazer para operações essenciais.
- Tratamento de erros.

Fora do escopo inicial

- Importação e exportação G3D.
- Suporte a FBX, 3DS, Collada e outros formatos não listados.
- Pintura avançada diretamente na superfície 3D.
- Projeção de imagens do espaço 3D para o atlas.
- Editor completo de geometria.
- Ferramentas avançadas de escultura.
- Sistema de plugins para terceiros.
- Serviços de nuvem, autenticação e funcionalidades comerciais.
- Mapas PBR avançados, como normal, roughness e metallic, até que o fluxo básico esteja estável.

Plano de implementação

Fase 1 — Estrutura e compilação

- Inspecionar o repositório atual.
- Identificar o sistema de build e a configuração Android.
- Integrar xatlas.
- Definir as estruturas básicas de malha e UV.
- Validar a compilação do núcleo nativo.

Critério de conclusão: o aplicativo consegue executar uma operação nativa de teste.

Fase 2 — Importação e visualização

- Integrar a importação de OBJ e glTF/GLB.
- Converter os arquivos para a estrutura interna.
- Implementar o renderizador.
- Implementar a seleção de malhas.

Critério de conclusão: o usuário consegue importar e visualizar modelos.

Fase 3 — UVs

- Integrar a geração de UVs com xatlas.
- Desenhar o atlas e suas ilhas.
- Implementar seleção e transformações.
- Implementar aplicação, cancelamento e validação.

Critério de conclusão: o usuário consegue gerar, editar e visualizar UVs no modelo.

Fase 4 — Texturas

- Implementar importação de imagens.
- Implementar camadas e transformações 2D.
- Compor a imagem final.
- Aplicar a textura ao modelo.
- Exportar PNG.

Critério de conclusão: o usuário consegue montar uma textura utilizando imagens importadas e visualizar o resultado no modelo.

Fase 5 — Projetos e exportação

- Implementar salvamento e reabertura do projeto.
- Implementar exportação para os formatos de modelo suportados.
- Implementar desfazer e refazer.
- Tratar erros e limites de memória.
- Testar a reimportação dos modelos exportados.

Critério de conclusão: o fluxo completo pode ser executado sem perder o trabalho entre etapas.

Instruções para o Coder (Qoder)

Antes de modificar o projeto:

1. Inspecionar o repositório e a estrutura existente.
2. Identificar a linguagem, o sistema de build e as restrições atuais.
3. Verificar a compatibilidade Android das dependências.
4. Confirmar as licenças das bibliotecas incorporadas.
5. Implementar em etapas pequenas, mantendo o projeto compilável.

O repositório https://github.com/ghxsthmodder/g3d deverá ser consultado somente quando necessário para referência sobre o formato G3D. Não implementar importação ou exportação G3D nesta versão.

Não reescrever código funcional sem justificativa. Não introduzir sistemas complexos desnecessários. Não criar botões sem funcionalidade real nem declarar implementadas funcionalidades que ainda sejam apenas planejadas.

Ao concluir cada fase, informar:

- Funcionalidades implementadas.
- Dependências adicionadas.
- Comandos para compilar.
- Testes realizados e resultados.
- Limitações conhecidas.
- Próxima etapa recomendada.

Licenças e atribuições

Antes de distribuir o aplicativo, verificar as licenças e obrigações de atribuição de cada versão das
bibliotecas utilizadas, incluindo xatlas, Assimp, stb, GLM, Material Components, os glifos do Material
Icons e meshoptimizer.

Manter os avisos de licença exigidos e registrar as versões efetivamente incorporadas.

Princípio de desenvolvimento

Reutilizar primeiro, implementar apenas o necessário e manter o fluxo de edição simples.

O objetivo não é criar um substituto completo para ferramentas profissionais de modelagem 3D. É oferecer uma ferramenta Android prática para geração de UVs, edição de ilhas e composição de texturas, com suporte a OBJ e glTF/GLB e possibilidade de integração futura com o formato G3D.