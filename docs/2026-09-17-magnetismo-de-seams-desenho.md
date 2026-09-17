# Magnetismo de seams entre meshes -- desenho

Estado: **desenho aceito, bloqueador respondido.** O ponto de hook existe, foi
achado e esta mapeado abaixo. O solver continua especificado e sem dependencia
dele.

## O problema

Esculpir o pescoco de um personagem exige mexer em duas meshes: metade do
pescoco esta no corpo, metade na cabeca. Selecionar as duas nao resolve -- elas
nao respondem de forma unificada, os seams deixam de se tocar e abre um gap.

Mascarar o anel do seam nao resolve, e o motivo importa: mascara **atenua**, nao
**restringe**. "Nao se mexa" nao e "se mexam juntos", e o resultado e a malha
inflando em volta do anel mascarado e deixando um buraco onde estava a mascara.

## O que foi pedido, e o que foi recusado

O pedido, em tres passos, nas palavras do usuario:

1. Um botao que deixa escolher **quais meshes** entram.
2. O programa varre as meshes escolhidas e acha as tangencialidades -- vertice
   com vertice **e vertice com aresta** (o Merge by Distance do Blender so faz o
   primeiro).
3. Enquanto o magnetismo esta ligado, esses vertices **nao se separam**. Eles
   sofrem escultura normalmente -- inflam, deflacionam -- mas continuam se
   tocando "como se fossem vertices de um plano unico".

Dois esclarecimentos que vieram depois e que mudam o escopo:

- A varredura **nao procura seams**, procura **contato**. Nao importa se o anel
  e uma borda aberta da topologia; importa que um vertice de A ocupa a mesma
  posicao que um vertice de B, ou que cai sobre uma aresta de B. O
  `FindSeamVertices` de `mesh/seams.h` responde a pergunta errada -- ele acha
  bordas *dentro* de uma mesh.
- A generalidade que isso cria (o sutia encostado no corpo tambem e contato) nao
  e problema, porque **o usuario escolhe as meshes**. O outro botao que sobra e a
  **tolerancia**: zero absoluto nao funciona em malha vinda de .nif.

**Recusado explicitamente:** um comando pos-fato -- congelar os contatos,
esculpir, e apertar uma tecla que solda depois. Era a alternativa mais barata e
foi rejeitada sem meio termo ("nao quero isso, odeiei. queria esculpir sem
separar os seams"). Facilidade de implementacao nao negocia com isso. Nao
reofereca, nem como entrega intermediaria.

## Medicoes (2026-09-17)

Feitas no Outfit Studio 5.8.2 rodando, com a instrumentacao descartavel em
`src/features/weld_probe.cpp`, atras de `[Debug] WeldProbe=1`.

**Normais em `Mesh+192`.** Provado calculando a normal geometrica ponderada por
area a partir dos triangulos e correlacionando com cada array candidato do
objeto:

| offset | unitario | dot medio | o que e |
| --- | --- | --- | --- |
| +192 | 100% | **+1.000** | normais |
| +200 | 100% | -0.002 | tangentes |
| +208 | 100% | +0.000 | bitangentes |

O `+200` e o `+208` sao 100% unitarios e teriam passado num teste de "parece uma
normal". Sem o gabarito geometrico havia 1 chance em 3 de escrever no array
errado.

**Escrever `Mesh::verts` nao persiste.** Deslocamento de +2 em X, um rebuild do
host, e releitura: **0 de 664** vertices mantiveram o valor escrito, **664**
voltaram ao anterior. E buffer de trabalho, reconstruido de base + diffs.

Ressalva que importa: o pincel tambem escreve nesse array e a edicao dele
persiste. Existe um commit em algum lugar. O que a medicao derruba nao e "da pra
chegar no modelo", e "da pra chegar no modelo escrevendo verts e pronto".

**O pincel nao tira a direcao de `Mesh+192`.** Com o array replantado para +Z a
cada 10 ms -- **788 ticks, 0 falhas de escrita** -- apenas **0 de 28** vertices
deslocados seguiram +Z (dot medio +0.372), enquanto o deslocamento acompanhou a
normal propria de cada vertice (dot medio +0.683). O **sombreamento mudou**, o
que prova que o array alimenta o renderizador.

Melhor explicacao que encaixa nas tres medicoes de uma vez: o pincel le as
normais **uma vez, no mouse-down**, para uma copia propria, e o host recalcula as
normais a partir da geometria imediatamente antes disso -- o recalculo de <109 ms
que a vigia registrou.

## Abordagens descartadas

| | resultado |
| --- | --- |
| **A** -- corrigir `Mesh::verts` no fim da pincelada | morta: a escrita nao persiste |
| **D** -- unificar as normais no contato para o Inflate nao divergir | morta por mecanismo: ganhamos a corrida do replantio e o pincel ignorou |
| **B** -- soldar por round-trip de OBJ (`Import Slider Data`) | tecnicamente viavel, **recusada pelo usuario** |

## O desenho aceito: hospedar a correcao dentro do traco

O ponto de hook e a **aplicacao do traco do pincel**, nao o commit do diff.

Entrando na funcao que o pincel usa para deslocar vertices, a correcao dos pares
acontece dentro do traco do proprio host. Duas propriedades saem de graca, e sao
exatamente os dois requisitos que estavam em aberto:

- **persistencia** -- o resultado sai pelo mesmo caminho do trabalho normal do
  pincel, que comprovadamente persiste
- **undo** -- o `Ctrl+Z` desfaz a pincelada *com* a correcao dentro, porque e um
  traco so. Corrigir por fora divergiria: o undo desfaria o traco e nao a
  correcao, reabrindo o seam.

E e o unico desenho que entrega o passo 3 do pedido: os vertices nao se separam
**enquanto** se esculpe, porque quem os move e a mesma passada que move os
outros.

**Custo assumido.** E a coisa mais invasiva que este mod faria, bem acima do
`SetPendingChanges` de `native_project_state.cpp`. Fica travada por hash na 5.8.2
como as outras features nativas, e quebra a cada versao nova do BodySlide ate
alguem remedir os RVAs. O usuario aceitou esse custo depois de as duas rotas
baratas terem sido medidas e caido.

O precedente de disciplina ja existe no repositorio: `native_project_state.cpp`
chama funcao do host por RVA validando o alvo por assinatura de bytes antes de
tocar, observa call sites dentro do `SaveProject`, e guarda o `OutfitProject` em
`frame+0x3C0`.

## O solver

Estado: **construido e testado**, em `src/mesh/contact.h` e `.cpp`, com 13 testes
em `tests/test_contact.cpp` que rodam sem o host. Falta so o que depende do
hospedeiro: o hook, a escolha das meshes e amarrar os aneis ao raio do pincel.

Esta parte e geometria pura e **nao depende do bloqueio**. Ela e identica
qualquer que seja o destino da escrita, e e testavel no chicote de testes que
roda sem o host.

### Grafo de contato congelado

Montado uma vez, quando o usuario liga o magnetismo. **Congelado** e o ponto: nada
e redetectado durante a escultura. Foi requisito explicito -- um snap arbitrario
e continuo criaria conexoes novas conforme o trabalho avanca, o que nao e o que
"os vertices que estao se tocando AGORA" significa.

Indice espacial com celula do tamanho da tolerancia, sobre os vertices das meshes
escolhidas.

### Deteccao, em dois passes

**Passe 1 -- vertice x vertice.** Coincidencia dentro da tolerancia, agrupada em
**clusters** por transitividade, nao em pares: tres meshes podem se encontrar no
mesmo ponto, e um par nao representa isso.

**Passe 2 -- vertice x aresta.** Para cada vertice sem par no passe 1, a aresta
mais proxima de outra mesh. Vale se a distancia perpendicular estiver dentro da
tolerancia e o parametro cair em `(0, 1)` aberto -- as pontas ja sao caso do
passe 1.

### As restricoes

**Cluster:** todos os membros na mesma posicao. O alvo e a posicao do membro que
se moveu; se mais de um se moveu, a media dos que se moveram. Isso da de graca o
comportamento "estou esculpindo o corpo, a cabeca segue" sem configuracao.

**Vertice em aresta:** guardar a **razao ao longo da aresta**, nao a posicao.

```text
A = B0 + t * (B1 - B0)
```

reavaliada conforme `B0` e `B1` se movem. Um snap de posicao faria o T-junction
deslizar quando a aresta se estica ou gira; a razao o mantem colado.

### Falloff

Mover so o anel corrigido da mesh que segue cria uma **dobra**. A correcao tem de
propagar para o interior dela com peso 1 no anel decaindo a 0 ao longo de N
aneis de vizinhanca, com N amarrado ao raio do pincel. E isso que faz parecer
uma superficie unica em vez de um anel remendado. A adjacencia sai dos
triangulos, que a ponte ja le.

### A assimetria do vertice em T

Um vertice preso a uma aresta pode seguir a aresta. O contrario e **mal posto** --
um ponto sozinho nao determina para onde dois vao. Na pratica: **esculpa no lado
denso, o lado esparso segue.** Decidir isso de proposito, e documentar, em vez de
o usuario descobrir usando.

### Dois tipos de contato

Dentro do par escolhido existem dois casos com comportamento bem diferente:

- **borda contra borda** (o pescoco: as duas malhas *continuam* uma na outra)
- **superficie sobre superficie** (o sutia: uma malha *repousa* sobre a outra)

Grudar os dois funciona, mas o segundo se comporta de outro jeito quando o corpo
infla. Nao resolvido, e nao urgente -- a selecao de meshes do passo 1 permite ao
usuario simplesmente nao incluir o sutia.

## O bloqueador -- respondido: sim, a funcao existe

Feito no Ghidra, no `OutfitStudio x64.exe` 5.8.2 da mesma instalacao de onde
saem os RVAs que o mod ja usa. O RTTI da MSVC entregou os nomes de classe, como
esperado, entao a busca foi por nome e nao rastreamento a mao.

### O mapa

| alvo | RVA | como foi identificado |
| --- | --- | --- |
| `TweakBrush` vftable | `0xFB49F0` | RTTI |
| `TB_Inflate` vftable | `0xFB45B0` | RTTI |
| slot `+0xD8` na vftable base | aponta pro `_purecall` (`0xB4959C`) | prova de que o slot e **virtual puro** |
| `TB_Inflate::brushAction` | `0x158640` | slot `+0xD8` da vftable de TB_Inflate |
| `TweakStroke::updateStroke` | `0x1569C0` | unica funcao que chama `+0xB8`, `+0xD0` e `+0xD8` em sequencia, num laco sobre meshes |
| `strokeInit` (slot `+0xB0`) | `0x157990` | copia as normais no inicio do traco |

O `TB_Inflate` so sobrescreve tres slots da base: `+0x18`, `+0x40` e `+0xD8`. O
`+0xD8` e o unico virtual puro da classe, e o `0x158640` que o preenche e o que
mexe em vertice. Os outros pinceis repetem o padrao -- `0x158b80`, `0x158fe0`,
`0x15c880`, `0x160870`, `0x160d20`, `0x161210`, `0x1615c0` sao as outras
implementacoes do mesmo slot.

### A acao do traco, por dentro

```c
void brushAction(TweakBrush* this, mesh* m, TweakPickInfo* pick,
                 const int* points, int nPoints, UndoStateShape* undo);
```

Na ordem, por ponto da pincelada:

1. le a posicao atual em `m->verts` (`mesh+0xB8`)
2. se o vertice ainda nao estiver no **mapa de inicio** (`undo+0x20`), grava ali a
   posicao de partida
3. calcula o deslocamento: direcao x forca x falloff (slot `+0xC8`) x
   `(1 - mascara)`, com a mascara em `mesh+0xF0`
4. **escreve a posicao nova em `m->verts`**
5. grava a posicao nova no **mapa de fim** (`undo+0x60`)
6. marca a mesh como suja (byte em `mesh+0`)

Os dois mapas sao `unordered_map<int, Vector3>` e as insercoes passam pelo mesmo
helper do host (`0x164a00`), que devolve o no e deixa o `Vector3` em `no+0x14`.

### O laco que hospeda a correcao

`updateStroke(TweakStroke* this, TweakPickInfo* pick)`, campos que interessam:

| offset | conteudo |
| --- | --- |
| `+0x00` / `+0x08` | `vector<mesh*>` das meshes do traco |
| `+0x18` | `TweakBrush*` ativo |
| `+0x20` | flag "primeiro update" (pula a checagem de espacamento) |
| `+0x24` | ultimo ponto de pick, `Vector3` |
| `+0xF0` | `UndoStateProject*` -- o vetor de `UndoStateShape` comeca em `*(+0x08)`, **passo `0x118`, indexado na mesma ordem do vector de meshes** |

Por mesh: espacamento (`+0xB8`) -> coleta de pontos (`+0xD0`) -> acao (`+0xD8`)
-> se o pincel e espelhado, repete coleta e acao com o pick espelhado -> empurra
o estado pra fila global de undo. No fim do laco, recalcula normais e sobe os
buffers de video.

### O que isso muda no desenho

1. **O hook e um so, e nao um por pincel.** `updateStroke` da acesso as meshes
   todas *e* ao `UndoStateShape` de cada uma -- que e exatamente o que o solver
   precisa, porque corrigir o pescoco mexe na cabeca enquanto o pincel esta no
   corpo. Remendar o slot `+0xD8` de cada vftable `TB_*` instala mais limpo (sem
   patch de codigo) mas entrega uma mesh por chamada, sem as outras
   `UndoStateShape` -- nao serve.
2. **Persistencia e undo saem pelo mesmo cano do pincel**: escrever no mapa de
   fim (`undo+0x60`) e, se o vertice for novo naquele traco, tambem no de inicio
   (`undo+0x20`). Era a duvida que mantinha o desenho travado.
3. A correcao tem de entrar **antes da cauda** de `updateStroke` (normais +
   buffers), ou refazer essa cauda para a tela nao ficar um tick atras.
4. `updateStroke` nao e virtual: precisa de desvio nos bytes de entrada. A
   primeira instrucao e `mov [rsp+0x10], rdx` -- **5 bytes exatos**, entao um
   `jmp rel32` cabe sem partir instrucao, e o trampolim so reexecuta ela.
   Assinatura pra validar antes de tocar:
   `48 ? ? ? ? 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC`

### As medicoes de 2026-09-17, relidas com o codigo na mao

- **"o pincel le as normais uma vez, no mouse-down" estava certo, e agora esta
  provado.** O `strokeInit` (`0x157990`) copia `mesh+0xC0` para um buffer por
  mesh guardado no mapa em `brush+0x48`; o `brushAction` tira a direcao de la, e
  nao do array vivo. Replantar `+0xC0` no meio do traco nao tinha como mudar a
  direcao -- e mudou o sombreamento porque o array continua alimentando o
  renderizador. As duas metades da medicao batem.
- **"escrever `Mesh::verts` nao persiste" continua verdade, e agora tem
  mecanismo.** Quem persiste e o par de mapas do `UndoStateShape`, preenchido
  dentro do `brushAction`. Escrita em `verts` por fora do traco nao deixa rastro
  nenhum neles, e o rebuild seguinte reconstroi de base + diffs por cima.

### O que ainda nao foi olhado

- o que o fim do traco faz com o `UndoStateProject` -- onde o diff vira projeto
- o resto do layout do `UndoStateShape` (`0x118` bytes; so os mapas em `+0x20` e
  `+0x60` foram lidos)
- o slot `+0xD0` (coleta de pontos) por dentro -- so a assinatura, vista de fora
- os pinceis de mascara (tipos 2 e 7) tomam outro ramo do laco, com outra
  chamada; se entram no escopo, precisam de leitura propria

## Instrumentacao

`src/features/weld_probe.cpp` e `.h`, atras de `[Debug] WeldProbe=1`, em
`Shape > Weld Probe (diagnostico)`. Guardas de memoria duplicadas de proposito
das de `native_outfit_mesh.cpp`: instrumentacao descartavel nao deve poder
quebrar o caminho de producao.

Os itens 0 (achar as normais), 7 e 8 (replantar e medir a direcao do
deslocamento) continuam uteis para validar o hook quando ele existir -- o 8 mede
para onde a escultura foi, que e exatamente a pergunta que um hook novo precisa
responder. Apagar quando a feature estiver de pe.
