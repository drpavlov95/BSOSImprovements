# Magnetismo de seams entre meshes -- desenho

Estado: **desenho aceito, bloqueado numa incognita.** O solver esta especificado
e nao depende do bloqueio; o caminho de gravacao depende.

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

## O bloqueador

**A funcao de aplicacao do traco existe num lugar que da para achar e hookar?**

Isso e Ghidra, e e o proximo passo. Nada mais do desenho depende de resposta
nenhuma; tudo depende desta.

Se a resposta for nao, a feature morre -- porque a alternativa que salvaria o
codigo do solver e justamente a que foi recusada. Construir o solver antes da
resposta e uma aposta consciente na existencia do hook.

Setup: **GhidrAssistMCP** servindo `localhost:8080/sse` de dentro de um Ghidra
aberto, com o `OutfitStudio x64.exe` ja importado e analisado -- o mesmo projeto
de onde sairam os RVAs que o mod ja usa.
A Symbol Tree tem `Classes` e `Namespaces` populados, ou seja o RTTI foi digerido
-- num binario C++ da MSVC isso entrega nomes de classe de verdade mesmo sem PDB,
entao a busca provavelmente e por nome e nao um rastreamento a mao a partir do
handler de mouse.

## Instrumentacao

`src/features/weld_probe.cpp` e `.h`, atras de `[Debug] WeldProbe=1`, em
`Shape > Weld Probe (diagnostico)`. Guardas de memoria duplicadas de proposito
das de `native_outfit_mesh.cpp`: instrumentacao descartavel nao deve poder
quebrar o caminho de producao.

Os itens 0 (achar as normais), 7 e 8 (replantar e medir a direcao do
deslocamento) continuam uteis para validar o hook quando ele existir -- o 8 mede
para onde a escultura foi, que e exatamente a pergunta que um hook novo precisa
responder. Apagar quando a feature estiver de pe.
