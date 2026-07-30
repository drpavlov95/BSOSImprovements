# BodySlide and OutfitStudio Improvements

Cinco melhorias de interface para o **BodySlide e Outfit Studio 5.6.3**, entregues
como um DLL standalone. Nenhum arquivo original é modificado ou redistribuído.

## O que faz

### BodySlide

**Busca no diálogo "Choose Groups".** Com centenas de grupos instalados, achar um na
lista é doloroso. Agora há uma caixa de busca que filtra a lista enquanto você digita,
mais botões **Marcar visíveis** e **Limpar tudo** e um contador.

Grupos marcados que somem por causa do filtro **continuam marcados**. O filtro é só de
visualização — dá para buscar "3BA", marcar, limpar a busca, procurar "HIMBO", marcar
mais, e no OK todos vão junto.

### Outfit Studio

**O reference já vem selecionado.** Ao carregar uma roupa, o Outfit Studio seleciona o
primeiro mesh da lista. Agora seleciona o shape *reference* — aquele em verde e negrito.
Vale também quando o projeto é aberto pelo BodySlide ou por duplo clique num `.osp`.

Só age quando não havia seleção anterior, ou seja, numa carga nova. Deletar, renomear ou
adicionar um shape num projeto já aberto **não** mexe na sua seleção.

| Atalho | O que faz |
|---|---|
| `B` | Seleciona o shape reference |
| `Shift+E` | Export Slider Data ▸ Export OBJ |
| `Shift+I` | Import Slider Data ▸ Import OBJ |
| `F` | Redimensionar brush estilo Blender |
| `K` | Transform (movido do `F`) |

**`Shift+E` e `Shift+I`** só funcionam com algum slider em **Edit mode** — a mesma
condição que o próprio Outfit Studio usa para habilitar esses itens de menu. Fora do
edit mode a tecla passa adiante normalmente.

**`F` redimensiona o brush** como no Blender: aperta `F`, move o mouse na horizontal
para crescer ou encolher o círculo, clica com o esquerdo para confirmar. `Esc` ou botão
direito cancela e devolve o tamanho anterior. Precisa de um brush ativo (teclas `1`–`9`);
com a ferramenta Select não faz nada.

Nenhum atalho dispara enquanto o foco está numa caixa de texto — digitar "B" num filtro
escreve "b".

## Instalação

**Mod Organizer 2 / Vortex:** instale como qualquer outro mod e ative.

**Manual:** copie a pasta `CalienteTools` para dentro da pasta onde está o
`BodySlide x64.exe`, de forma que o `msimg32.dll` fique ao lado do executável.

Requer os executáveis **x64** (`BodySlide x64.exe` e `OutfitStudio x64.exe`). Os de 32
bits continuam abrindo normalmente, mas sem as melhorias.

Para desinstalar, apague `msimg32.dll` e `BSOSImprovements.ini`.

## Configuração

Tudo fica em `BSOSImprovements.ini`, ao lado do DLL. Cada feature liga e desliga
sozinha, e todo atalho é trocável.

A seção `[Remap]` liga **qualquer** comando do Outfit Studio a **qualquer** tecla. É
assim que o `F` fica livre para o brush: o `Transform` é movido para `K`. Se você quiser
o `R` para o reference, por exemplo:

```ini
[Hotkeys]
SelectReference=R

[Remap]
btnRecalcNormals=N
```

Os nomes de comando saem de `CalienteTools\BodySlide\res\xrc\OutfitStudio.xrc`: procure
o texto do menu e olhe o `name=` do `<object>` em volta.

## Compatibilidade

- **BodySlide e Outfit Studio 5.6.3.** Nada depende de endereços de memória ou
  assinaturas de bytes — só de mensagens padrão do Windows — então versões próximas
  devem funcionar. Se algo não funcionar numa versão nova, o mod se desliga em silêncio
  em vez de quebrar o programa.
- **Convive com outros mods que usam `version.dll`**, como os de drape. Este mod usa o
  slot `msimg32.dll`, deliberadamente diferente.
- Traduções do BodySlide são suportadas: nada é identificado por texto de interface.

## Problemas

Ligue o log e reproduza:

```ini
[Debug]
LogFile=1
```

Os arquivos saem em `%TEMP%\BSOSImprovements_BodySlide.log` e
`%TEMP%\BSOSImprovements_OutfitStudio.log`.

## Checklist de teste

Estado da verificação até aqui. Os itens marcados foram exercitados de forma
automatizada contra o programa real; os demais precisam de um par de mãos.

- [x] Os dois executáveis x64 abrem com o DLL presente
- [x] O executável de 32 bits continua abrindo
- [x] Convivência com o `version.dll` de outro mod no mesmo processo
- [x] Tecla de reference seleciona o shape correto, com projeto carregado
- [x] Tecla de reference não faz nada em projeto sem reference
- [x] Os três atalhos e os remaps registram na inicialização
- [x] Nome de comando inválido no `[Remap]` é ignorado sem derrubar os outros
- [x] BodySlide abre com a busca de grupos instalada
- [ ] Choose Groups: busca filtra, marcados sobrevivem ao filtro, OK aplica
- [ ] Auto-select do reference numa carga onde o reference não é o primeiro shape
- [ ] Deletar/renomear shape não rouba a seleção
- [ ] `Shift+E` / `Shift+I` abrem os diálogos de OBJ em edit mode
- [ ] `F` redimensiona o brush; clique confirma sem pintar; `Esc` restaura
- [ ] `K` aciona o Transform

O teste do Choose Groups precisa rodar **pelo Mod Organizer**: os grupos vêm do VFS, e
fora dele a lista aparece quase vazia.

## Build

Precisa do Visual Studio 2022 Build Tools com o workload de C++.

```
build.bat            compila dist\msimg32.dll
tests\build_tests.bat  compila e roda os testes
package.bat          monta a pasta do mod pronta para zipar
deploy.bat           copia o DLL para uma instalação de teste
clean-deploy.bat     remove os arquivos de teste
```

## Créditos

BodySlide e Outfit Studio são de **ousnius**. Este mod não inclui nenhum código nem
arquivo deles; apenas conversa com o programa por mensagens padrão do Windows.
