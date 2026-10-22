# Material Icons

Ícones da família **Material Icons**, versão **3.0.1**, licença **Apache License 2.0**
(o texto está em [`LICENSE`](LICENSE), copiado da tag). Origem:
<https://github.com/google/material-design-icons/tree/3.0.1>.

`svg/` guarda os arquivos exatamente como vieram do repositório upstream, no tamanho de 24 px.
Os drawables do app (`app/src/main/res/drawable/ic_*.xml`) são uma conversão mecânica do traçado:
cada `<path d="…">` virou um `<path android:pathData="…">` com `viewport` de 24×24, e o preenchimento
preto virou `@android:color/white` porque o glifo é tingido pelo `iconTint` do botão
(`res/color/icon_tint.xml`, `res/color/mode_icon.xml`), não pelo traçado. Nenhum desenho foi
editado, simplificado ou reamostrado.

## De onde vem cada glifo

| drawable no app | upstream (`<categoria>/svg/production/`) | para que serve no editor |
| --- | --- | --- |
| `ic_import` | `file/ic_file_upload_24px.svg` | importar modelo 3D |
| `ic_image` | `image/ic_image_24px.svg` | importar imagem como camada |
| `ic_export` | `file/ic_file_download_24px.svg` | exportar o PNG composto |
| `ic_frame` | `image/ic_center_focus_strong_24px.svg` | reenquadrar cena ou atlas |
| `ic_view_three_d` | `action/ic_3d_rotation_24px.svg` | vista só 3D |
| `ic_view_split` | `action/ic_view_column_24px.svg` | vista 3D + atlas |
| `ic_view_atlas` | `image/ic_grid_on_24px.svg` | vista só do atlas |
| `ic_uv` | `navigation/ic_apps_24px.svg` | abrir o editor de UV |
| `ic_layers` | `maps/ic_layers_24px.svg` | abrir o compositor de texturas |
| `ic_shade_solid` | `action/ic_change_history_24px.svg` | sombreado sólido |
| `ic_shade_normals` | `image/ic_gradient_24px.svg` | sombreado por normais |
| `ic_shade_texture` | `image/ic_texture_24px.svg` | sombreado texturizado |
| `ic_shade_checker` | `editor/ic_border_all_24px.svg` | grade de referência |
| `ic_eye` | `action/ic_visibility_24px.svg` | mostrar a camada |
| `ic_eye_off` | `action/ic_visibility_off_24px.svg` | ocultar a camada |
| `ic_duplicate` | `content/ic_content_copy_24px.svg` | duplicar camada |
| `ic_delete` | `action/ic_delete_24px.svg` | remover camada |
| `ic_add` | `content/ic_add_24px.svg` | adicionar |
| `ic_arrow_up` | `navigation/ic_arrow_upward_24px.svg` | mover para cima, abrir o log, mostrar a gaveta do inspetor |
| `ic_arrow_down` | `navigation/ic_arrow_downward_24px.svg` | mover para baixo, esconder o log, esconder a gaveta do inspetor |
| `ic_arrow_back` | `navigation/ic_arrow_back_24px.svg` | mover para a esquerda |
| `ic_arrow_forward` | `navigation/ic_arrow_forward_24px.svg` | mover para a direita |
| `ic_diagnostic` | `action/ic_bug_report_24px.svg` | diagnóstico de UV |
| `ic_canvas` | `action/ic_aspect_ratio_24px.svg` | tamanho da tela |
| `ic_background` | `image/ic_color_lens_24px.svg` | cor de fundo |
| `ic_flip` | `image/ic_flip_24px.svg` | espelhar ilhas |
| `ic_undo` | `content/ic_undo_24px.svg` | desfazer a última edição de UV |
| `ic_redo` | `content/ic_redo_24px.svg` | refazer o que foi desfeito |

Os nomes dos arquivos em `svg/` são os upstream, então a tabela acima é o único lugar onde vive a
correspondência com os drawables; um glifo novo entra baixando o SVG da mesma tag e acrescentando a
linha.
