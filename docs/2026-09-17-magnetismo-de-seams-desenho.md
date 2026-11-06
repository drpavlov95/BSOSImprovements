# Magnetismo de seams entre meshes -- desenho

Estado: **construido inteiro, e nunca rodado.** O bloqueador foi respondido no
Ghidra, o solver esta de pe e testado, e a ponte com o host esta escrita e
compilando. O que falta e a unica coisa que este documento nao pode dar:
abrir o Outfit Studio, ligar o item de menu e esculpir um pescoco.

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

Feitas no Outfit Studio 5.8.2 rodando, com instrumentacao descartavel que foi
removida depois que as respostas entraram na implementacao.

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
| `TB_Inflate` vftable | `0x197CBF8` | RTTI |
| `TB_Inflate::queryPoints` | `0x6100A0` | slot `+0xD8` da vftable |
| `TB_Inflate::brushAction` | `0x603960` | slot `+0xE0` da vftable |
| thunk de `TweakStroke::updateStroke` | `0x02E7B2` | destino dos quatro call sites |
| corpo de `TweakStroke::updateStroke` | `0x611F40` | thunk e prologo conferidos no executavel instalado |

A releitura foi feita contra o SHA-256 exato que `HostBuild` aceita, compilando
tambem o fonte oficial 5.8.2 para recuperar os simbolos. Essa build usa thunks
de link incremental; os RVAs medidos antes pertenciam a outro layout do mesmo
codigo. O fonte atual tambem tem um virtual a mais: `queryPoints` esta em
`+0xD8` e `brushAction`, que mexe nos vertices, em `+0xE0`.

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
helper do host (thunk `0x00C0E0`, corpo `0x5E2A20`), que devolve o no e deixa o
`Vector3` em `no+0x14`.

### O laco que hospeda a correcao

`updateStroke(TweakStroke* this, TweakPickInfo* pick)`, campos que interessam:

| offset | conteudo |
| --- | --- |
| `+0x00` / `+0x08` | `vector<mesh*>` das meshes do traco |
| `+0x18` | `TweakBrush*` ativo |
| `+0x20` | flag "primeiro update" (pula a checagem de espacamento) |
| `+0x24` | ultimo ponto de pick, `Vector3` |
| `+0xF0` | `UndoStateProject*` -- o vetor de `UndoStateShape` comeca em `*(+0x08)`, **passo `0x290`, indexado na mesma ordem do vector de meshes** |

A checagem de espacamento acontece uma vez; por mesh vem a coleta de pontos
(`queryPoints`, slot `+0xD8`) e a acao (`brushAction`, slot `+0xE0`). Se o
pincel e espelhado, coleta e acao se repetem com o pick espelhado. Normais,
BVH e buffers seguem o fluxo normal do host.

### O que isso muda no desenho

1. **O hook e um so, e nao um por pincel.** `updateStroke` da acesso as meshes
   todas *e* ao `UndoStateShape` de cada uma -- que e exatamente o que o solver
   precisa, porque corrigir o pescoco mexe na cabeca enquanto o pincel esta no
   corpo. Remendar o slot `+0xE0` de cada vftable `TB_*` instala mais limpo (sem
   patch de codigo) mas entrega uma mesh por chamada, sem as outras
   `UndoStateShape` -- nao serve.
2. **Persistencia e undo saem pelo mesmo cano do pincel**: escrever no mapa de
   fim (`undo+0x60`) e, se o vertice for novo naquele traco, tambem no de inicio
   (`undo+0x20`). Era a duvida que mantinha o desenho travado.
3. A correcao entra depois de `updateStroke` original e marca
   `Mesh::queueUpdate[Position]`; o paint seguinte sobe o buffer corrigido.
4. `updateStroke` nao e virtual. O mod troca somente os quatro `call rel32` que
   chegam ao thunk original e valida separadamente thunk, corpo, prologo e
   destinos antes de escrever qualquer byte.

### As medicoes de 2026-09-17, relidas com o codigo na mao

- **"o pincel le as normais uma vez, no mouse-down" estava certo, e agora esta
  provado.** O `strokeInit` copia `mesh+0xC0` para um buffer por
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
- o resto do layout do `UndoStateShape` (`0x290` bytes; so os mapas em `+0x20` e
  `+0x60` foram lidos)
- o slot `+0xD0` (coleta de pontos) por dentro -- so a assinatura, vista de fora
- os pinceis de mascara (tipos 2 e 7) tomam outro ramo do laco, com outra
  chamada; se entram no escopo, precisam de leitura propria

## A ponte com o host

**Rodada dentro do Outfit Studio instalado em 2026-10-07.** Compila limpo, os
125 testes passam, o item foi acionado por automacao e o log confirmou a
instalacao do hook nos quatro call sites. Ainda falta um ensaio manual de
escultura com duas meshes e Ctrl+Z para validar o resultado visual e o undo.

`src/features/seam_magnetism.{h,cpp}`.

### Onde entra

Remendo de 5 bytes nos **quatro call sites** de `updateStroke` -- `0x898D98`,
`0x89FBEB`, `0x8A41C3`, `0x8A6BA6` -- e nao um desvio na entrada da funcao. Os
calls originais apontam para o thunk `0x02E7B2`, que leva ao corpo em
`0x611F40`. Como o DLL pode cair alem do alcance de +/- 2 GiB de um `call rel32`,
uma pagina executavel perto do host guarda um relay de 14 bytes (`jmp [rip+0]`
seguido do endereco absoluto do hook). Ele nao altera registrador nenhum.

Antes de escrever qualquer byte, tres perguntas, todas obrigatorias:

1. o SHA-256 do executavel e o da 5.8.2 medida (`HostBuild`)
2. o thunk `0x02E7B2` leva ao corpo `0x611F40`, cujo prologo e o que medimos
3. o thunk `0x00C0E0` leva ao helper de undo `0x5E2A20`, tambem com assinatura
   conferida
4. cada call site e mesmo um `E8` cujo destino calculado da no thunk de
   `updateStroke`

A terceira e o que protege contra outro mod ter chegado antes no mesmo lugar.

### A ordem dentro do hook

```text
le o flag de "primeiro update" (+0x20)   <- antes, porque o original o zera
se comeca traco e o grafo nao existe: CONGELA o grafo
chama updateStroke original              <- o traco do usuario, com ou sem nos
le o que o pincel mexeu
resolve o contato
escreve vertices, mapas de undo e a flag de buffer sujo
```

Congelar **antes** do original e o ponto fino: montar o grafo depois da primeira
passada leria um seam que o pincel acabou de abrir, e congelaria justamente a
ausencia de contato que a feature existe para evitar.

### Como se sabe o que o pincel mexeu

Pelo mapa de fim do `UndoStateShape`, que e a lista dos vertices que o traco
tocou. Mas nos tambem escrevemos nesse mapa, e um vertice nosso lido de volta
como "movido" faria o lado que segue virar o lado que manda. A separacao e
guardar o que escrevemos e comparar: se o valor no mapa e exatamente o que
deixamos la, o pincel nao voltou nele.

### A guarda que vale por todas

Um pincel de mascara, de peso ou de cor tambem escreve nos mapas do
`UndoStateShape`. Tratar um peso como se fosse posicao deformaria a malha.

A invariante que separa os dois e barata e exata: **num pincel de geometria, o
que esta no mapa de fim e igual ao que esta no array de vertices**, porque
`brushAction` escreve os dois com o mesmo valor. Se um unico par discordar, o
tick inteiro e descartado sem escrever nada.

Ela cobre de graca o caso que mais assusta: um offset errado em qualquer lugar
da cadeia (`+0xF0`, `+0x08`, passo `0x290`, `+0x60`) produz numeros que nao tem
como bater com posicao de vertice.

### O que a tela ve

Escrever no array de vertices nao sobe nada para o OpenGL sozinho. A correcao
marca `Mesh::queueUpdate[Position]`, o mesmo byte que o `brushAction` marca, e o
paint seguinte manda o buffer novo. Ficou de fora: a **normal** do vertice
corrigido so e recalculada no tick seguinte, entao a ultima pincelada pode
terminar com o sombreamento um tick atrasado ate a proxima interacao.

### Superficie para o usuario

`Shape > Seam Magnetism`, um item marcavel que entra sempre desmarcado -- so
criar o item quando a feature ja estivesse ligada faria a opcao aparecer apenas
para quem ja soubesse que ela existe. `[Features] SeamMagnetism` esconde o item;
`[Tuning] SeamMagnetismTolerance` e `SeamMagnetismRings` afinam o resto.

As meshes que entram sao as do primeiro traco depois de ligar, ou seja: **as que
o usuario tem selecionadas**. Isso resolve o passo 1 do pedido sem um segundo
dialogo, e mantem o congelamento.

### O que falta

- fazer um ensaio manual completo com duas meshes e Ctrl+Z
- amarrar `falloffRings` ao raio do pincel, que e o que o desenho pede; hoje e
  numero fixo no INI. Falta medir o offset do raio em `TweakBrush`
- a normal do vertice corrigido, um tick atras
- o que o fim do traco faz com o `UndoStateProject`: a persistencia depende
  disso e ainda nao foi lida

## Instrumentacao

As sondas `Weld Probe` e `Structure Probe` foram removidas em 2026-10-07. As
medicoes relevantes ficaram registradas neste documento; nenhum menu ou codigo
capaz de alterar vertices para diagnostico permanece no DLL distribuido.
