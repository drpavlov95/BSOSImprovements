#pragma once

#include <windows.h>

#include <string>
#include <vector>

std::vector<HWND> ChildrenOf(HWND parent);
std::wstring ClassOf(HWND hwnd);

// nth == 0 devolve a primeira ocorrencia.
HWND FindChildByClass(HWND parent, const wchar_t* cls, int nth = 0);
HWND FindDescendantByClass(HWND root, const wchar_t* cls, int nth = 0);

// Se a JANELA esta marcada como visivel, sem perguntar pelos ancestrais dela.
//
// IsWindowVisible mente neste programa. O diagnostico mediu: cento e quarenta e
// quatro barras deslizantes desenhadas na tela, e IsWindowVisible respondendo
// "nao" para cento e quarenta e tres delas -- porque exige que TODA a cadeia de
// pais tenha WS_VISIBLE, e algum painel intermediario do Outfit Studio nao tem,
// mesmo com o conteudo a mostra.
//
// Para "o usuario esta vendo isto?", que e o que quase sempre se quer perguntar
// aqui, o estilo da propria janela e a resposta certa. Duas features foram
// construidas sobre a pergunta errada antes disso ficar claro.
bool HasVisibleStyle(HWND window);

// Todos os descendentes com essa classe, em profundidade e pre-ordem.
std::vector<HWND> FindDescendantsByClass(HWND root, const wchar_t* cls);

// O maior descendente visivel com essa classe.
//
// Existe porque as janelas que interessam aparecem repetidas: ha mais de um
// wxGLCanvas no processo -- a janela de preview tem o seu -- e a view 3D
// principal e sempre a maior das visiveis.
HWND FindLargestVisibleByClass(HWND root, const wchar_t* cls);

// Verdadeiro se o foco esta num campo de texto editavel. Os atalhos de tecla
// unica precisam disso: sem essa guarda, digitar "R" num filtro viraria atalho.
bool IsTextInputFocused();
