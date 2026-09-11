#include "features/slider_reorder.h"

#include <commctrl.h>

#include <algorithm>

#include "core/host.h"
#include "core/log.h"
#include "core/theme.h"
#include "features/pose_panel.h"
#include "features/zero_sliders.h" // PickSliderHost
#include "win32/menu_toggle.h"
#include "win32/winfind.h"

namespace {

// A alca fica ENTRE a caixa de marcacao e o nome, e nao antes do lapis.
//
// A medida saiu da propria linha, registrada no log: lapis em 0..22, caixa em
// 27..42, nome em 47..149, barra em 154..715, valor em 722..762. Nao ha um pixel
// livre ali, entao alguem tem que andar. Poe-la a esquerda obrigaria a empurrar
// o lapis e a caixa junto; entre a caixa e o nome so o nome e a barra andam, e
// os dois botoes que ja existiam ficam exatamente onde o usuario aprendeu a
// procura-los.
const int kGripWidth = 14;
const int kGripId = 0xBF03;
const UINT_PTR kGripSubclassId = 0xB515;
const UINT_PTR kHostSubclassId = 0xB516;
const UINT_PTR kSlotSubclassId = 0xB517;

// Pedido de "olhe a lista de novo", mandado pelo painel para ele mesmo.
const UINT kRecheckMsg = WM_APP + 0x1F;

HWND g_frame = nullptr;
HWND g_posePanel = nullptr;
bool g_installed = false;

// Uma linha da lista, com o nome que a identifica.
struct Row {
	HWND window = nullptr;
	std::wstring name;
	int top = 0;
};

// A ordem que o usuario escolheu, por NOME.
//
// Nao por HWND e nao por coordenada: o wx refaz o layout quando quer, e as
// janelas de ontem nao existem mais depois de trocar de outfit. O nome e a unica
// coisa do slider que sobrevive a isso.
std::vector<std::wstring> g_desiredOrder;

// As linhas que a lista tinha da ultima vez que olhamos.
//
// Guardadas inteiras, e nao so a contagem: um outfit de cento e vinte sliders
// trocado por outro de cento e vinte recria todas as janelas sem mudar o numero,
// e contar nao perceberia.
std::vector<HWND> g_knownRows;
HWND g_knownHost = nullptr;
DWORD g_lastCheck = 0;
bool g_recheckPending = false;

bool g_gripsOn = true;
bool g_dark = false;

// O arrasto em curso.
struct Drag {
	bool active = false;
	HWND host = nullptr;
	HWND slot = nullptr;        // o vao desenhado no lugar de destino
	std::vector<HWND> order;    // as linhas na ordem de agora
	std::vector<HWND> original; // como estavam quando o arrasto comecou
	std::vector<int> slotTops;
	int rowHeight = 0;
	int index = -1;
	int grabOffset = 0;
};

Drag g_drag;

RECT RectIn(HWND window, HWND parent) {
	RECT rc = {};
	GetWindowRect(window, &rc);
	MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&rc), 2);
	return rc;
}

HWND GripOf(HWND row) {
	return GetDlgItem(row, kGripId);
}

std::wstring TextOf(HWND window) {
	const int len = GetWindowTextLengthW(window);
	if (len <= 0 || len > 512)
		return std::wstring();
	std::wstring text(static_cast<size_t>(len) + 1, L'\0');
	const int written = GetWindowTextW(window, text.data(), static_cast<int>(text.size()));
	text.resize(written > 0 ? static_cast<size_t>(written) : 0);
	return text;
}

bool HasTrackbar(HWND window) {
	return !FindDescendantsByClass(window, TRACKBAR_CLASSW).empty();
}

// Onde estao a barra e o nome dentro da linha.
//
// O nome e o controle mais a DIREITA entre os que ficam a esquerda da barra --
// ou seja, depois do lapis e da caixa de marcacao. E ali que a alca entra, e e
// dali para a direita que tudo anda.
struct RowParts {
	HWND trackbar = nullptr;
	HWND name = nullptr;
	RECT trackbarRect = {};
	RECT nameRect = {};
	bool ok = false;
};

RowParts FindParts(HWND row) {
	RowParts parts;
	HWND grip = GripOf(row);

	for (HWND child : ChildrenOf(row)) {
		if (child == grip)
			continue;
		if (_wcsicmp(ClassOf(child).c_str(), TRACKBAR_CLASSW) != 0)
			continue;
		parts.trackbar = child;
		parts.trackbarRect = RectIn(child, row);
	}
	if (!parts.trackbar)
		return parts; // sem barra nao e linha de slider

	for (HWND child : ChildrenOf(row)) {
		if (child == grip || child == parts.trackbar)
			continue;
		const RECT rc = RectIn(child, row);
		if (rc.left >= parts.trackbarRect.left)
			continue; // a porcentagem, que fica a direita da barra
		if (!parts.name || rc.left > parts.nameRect.left) {
			parts.name = child;
			parts.nameRect = rc;
		}
	}

	parts.ok = parts.name != nullptr;
	return parts;
}

std::wstring NameOf(HWND row) {
	const RowParts parts = FindParts(row);
	return parts.ok ? TextOf(parts.name) : std::wstring();
}

// TODAS as linhas do painel, escondidas pelo filtro ou nao.
//
// Serve para duas coisas que precisam do conjunto inteiro: reconstruir a ordem
// completa por nome, e saber quais janelas existem.
std::vector<HWND> RowWindows(HWND host) {
	std::vector<HWND> out;
	for (HWND child : ChildrenOf(host)) {
		if (HasTrackbar(child))
			out.push_back(child);
	}
	return out;
}

// So as linhas que estao de fato na tela agora.
//
// O Outfit Studio tem um filtro de sliders PROPRIO -- a caixa "Slider Filter"
// no alto do painel -- e ele esconde linha por linha. Este mod nao sabia disso:
// mexia em todas, inclusive nas escondidas, e reposicionava a lista inteira
// usando as posicoes delas. Dai vinham as duas queixas: as linhas apareciam e
// sumiam sem regra durante a busca, e entrar no modo de edicao com a busca ativa
// levava tudo embora -- porque o modo de edicao dispara um relayout, o relayout
// chamava RefreshRows, e RefreshRows reordenava as cento e tantas linhas por
// cima do que o filtro tinha acabado de montar.
//
// A pergunta e feita a PROPRIA linha, com HasVisibleStyle, e nao com
// IsWindowVisible -- pela mesma razao de sempre neste programa: IsWindowVisible
// exige a arvore inteira visivel e mente aqui. O bit da propria janela responde
// exatamente o que interessa: "o filtro escondeu esta linha?".
std::vector<HWND> VisibleRowWindows(HWND host) {
	std::vector<HWND> out;
	for (HWND window : RowWindows(host)) {
		if (HasVisibleStyle(window))
			out.push_back(window);
	}
	return out;
}

// As linhas visiveis de cima para baixo, com nome e altura.
//
// Ordenadas pela posicao na TELA e nao pela z-order: a z-order nao promete
// acompanhar a ordem em que as linhas aparecem, e e a da tela que o usuario esta
// arrastando. Depois do primeiro arrasto as duas ja divergem, porque a linha
// carregada vai para o topo da z-order.
std::vector<Row> FindRows(HWND host) {
	std::vector<Row> rows;
	for (HWND window : VisibleRowWindows(host)) {
		Row row;
		row.window = window;
		row.name = NameOf(window);
		row.top = static_cast<int>(RectIn(window, host).top);
		rows.push_back(row);
	}

	std::sort(rows.begin(), rows.end(),
			  [](const Row& a, const Row& b) { return a.top < b.top; });
	return rows;
}

// A ordem completa por nome, escondidas incluidas.
//
// Reconstruida a partir da ordem persistida, e NAO das coordenadas: a posicao Y
// de uma linha escondida pelo filtro nao segue regra nenhuma -- ela fica parada
// onde estava enquanto as visiveis se compactam por cima. Ordenar por Y
// misturaria as duas coisas.
std::vector<std::wstring> FullNameOrder(HWND host) {
	std::vector<std::wstring> present;
	for (HWND window : RowWindows(host))
		present.push_back(NameOf(window));

	std::vector<std::wstring> out;
	out.reserve(present.size());
	for (int index : ApplyDesiredOrder(g_desiredOrder, present))
		out.push_back(present[static_cast<size_t>(index)]);
	return out;
}

std::vector<HWND> WindowsOf(const std::vector<Row>& rows) {
	std::vector<HWND> out;
	out.reserve(rows.size());
	for (const Row& row : rows)
		out.push_back(row.window);
	return out;
}

bool BeginDrag(HWND row);
void DragToCursor();
void FinishDrag(bool cancelled);
void RefreshRows();

// Se o ponteiro esta em cima desta alca.
//
// Guardado na propria janela, e nao num global: sao mais de cem alcas na tela e
// o realce e de UMA. Um global faria a lista inteira acender junto.
bool GripIsHot(HWND grip) {
	return GetWindowLongPtrW(grip, GWLP_USERDATA) != 0;
}

// Manda repintar a alca, e a faixa do PAI debaixo dela.
//
// As duas, porque a alca e WS_EX_TRANSPARENT: ela nao tem fundo proprio, entao
// quem apaga o desenho velho e a linha. Invalidar so a alca deixaria os pontos
// antigos por baixo dos novos.
void RepaintGrip(HWND grip) {
	HWND row = GetParent(grip);
	if (!row)
		return;
	const RECT rc = RectIn(grip, row);
	InvalidateRect(row, &rc, TRUE);
	InvalidateRect(grip, nullptr, FALSE);
}

void SetGripHot(HWND grip, bool hot) {
	if (GripIsHot(grip) == hot)
		return;
	SetWindowLongPtrW(grip, GWLP_USERDATA, hot ? 1 : 0);
	RepaintGrip(grip);
}

// Desenha os seis pontos da alca, e nada mais.
//
// Desenhados, e nao um caractere numa fonte. A versao anterior punha o simbolo
// "identico a" num Static, e o diagnostico provou que ele ficava na posicao
// certa e mesmo assim nada aparecia: um glifo depende de a fonte te-lo, do corpo
// dela e de como o controle o alinha. Seis retangulos nao dependem de nada
// disso.
//
// E sem fundo NENHUM. A versao anterior pedia o pincel ao pai por
// WM_CTLCOLORSTATIC, supondo que um painel do wx respondesse como um Static
// comum -- e o que voltava era o branco do DefWindowProc. Na tela isso virou um
// quadradinho branco no meio de uma linha escura. A alca e WS_EX_TRANSPARENT: o
// pai pinta o fundo dele por baixo, e aqui so ficam os pontos.
void PaintGrip(HWND grip, HDC dc) {
	RECT rc = {};
	GetClientRect(grip, &rc);

	// Cinza parado, azul sob o ponteiro e durante o arrasto. Um cinza discreto
	// e o que faz a alca ler como parte da tipografia da linha em vez de mais um
	// controle; o azul e o que confirma que ela e clicavel.
	const COLORREF tone = GripIsHot(grip)
							  ? GetSysColor(COLOR_HIGHLIGHT)
							  : (g_dark ? RGB(150, 150, 150) : GetSysColor(COLOR_GRAYTEXT));

	HBRUSH ink = CreateSolidBrush(tone);
	if (!ink)
		return;

	const int dot = 2;
	const int gapX = 2;
	const int gapY = 2;
	const int width = dot * 2 + gapX;
	const int height = dot * 3 + gapY * 2;
	const int originX = (static_cast<int>(rc.right) - width) / 2;
	const int originY = (static_cast<int>(rc.bottom) - height) / 2;

	for (int line = 0; line < 3; ++line) {
		for (int column = 0; column < 2; ++column) {
			RECT bit = {};
			bit.left = originX + column * (dot + gapX);
			bit.top = originY + line * (dot + gapY);
			bit.right = bit.left + dot;
			bit.bottom = bit.top + dot;
			FillRect(dc, &bit, ink);
		}
	}
	DeleteObject(ink);
}

// A alca e dona do gesto.
//
// Ela captura o mouse no aperto, entao o movimento e o soltar chegam AQUI mesmo
// que o ponteiro saia da linha, do painel ou da janela. A versao anterior
// dependia de o clique atravessar um controle por hit-test e de o hook global
// adivinhar em que janela ele tinha caido; e como o alvo real da mensagem era a
// alca e nao a linha, a linha nunca era encontrada.
LRESULT CALLBACK GripProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR) {
	switch (msg) {
		case WM_NCDESTROY:
			RemoveWindowSubclass(hwnd, GripProc, id);
			break;

		case WM_ERASEBKGND:
			return 1; // o fundo sai no WM_PAINT, junto com os pontos

		case WM_PAINT: {
			PAINTSTRUCT paint = {};
			if (HDC dc = BeginPaint(hwnd, &paint))
				PaintGrip(hwnd, dc);
			EndPaint(hwnd, &paint);
			return 0;
		}

		// Seta vertical, e nao a de mover em quatro direcoes: a linha so anda
		// para cima e para baixo. A de quatro pontas prometia um movimento
		// lateral que nao existe.
		case WM_SETCURSOR:
			SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
			return TRUE;

		// So captura o mouse se o arrasto REALMENTE comecou.
		//
		// BeginDrag desiste em varios casos -- painel sumido, uma linha so, a
		// linha nao encontrada -- e capturar antes de saber disso prendia o
		// mouse para sempre: o WM_LBUTTONUP nao soltava, porque ele so solta
		// quando ha um arrasto para encerrar.
		case WM_LBUTTONDOWN:
			SetGripHot(hwnd, true);
			if (BeginDrag(GetParent(hwnd)))
				SetCapture(hwnd);
			return 0;

		case WM_MOUSEMOVE:
			if (g_drag.active) {
				DragToCursor();
				return 0;
			}
			if (!GripIsHot(hwnd)) {
				SetGripHot(hwnd, true);
				// Sem pedir o aviso de saida, a alca acenderia e nunca mais
				// apagaria: WM_MOUSELEAVE nao chega sozinho.
				TRACKMOUSEEVENT track = {};
				track.cbSize = sizeof(track);
				track.dwFlags = TME_LEAVE;
				track.hwndTrack = hwnd;
				TrackMouseEvent(&track);
			}
			return 0;

		case WM_MOUSELEAVE:
			if (!g_drag.active)
				SetGripHot(hwnd, false);
			return 0;

		case WM_LBUTTONUP:
			if (g_drag.active) {
				FinishDrag(false);
				ReleaseCapture();
			}
			SetGripHot(hwnd, false);
			return 0;

		// Perder a captura -- alt-tab, outra janela roubando -- encerra o
		// arrasto onde ele estiver. Sem isto o estado ficaria aberto e a lista
		// seguiria um mouse que ja nao esta arrastando nada.
		case WM_CAPTURECHANGED:
			if (g_drag.active)
				FinishDrag(false);
			return 0;

		default:
			break;
	}
	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// O vao: uma linha fina no lugar para onde a linha vai cair.
//
// Sem ele o arrasto e ambiguo. As outras linhas se acomodam e deixam um buraco,
// mas um buraco no meio de uma lista de barras cinzentas nao se le como "e aqui
// que ela entra" -- le-se como um erro de desenho.
//
// Uma LINHA, e nao uma moldura do tamanho da linha. A moldura desenhava uma
// caixa vazia onde vai entrar conteudo, o que parece ferramenta de depuracao; o
// tracinho horizontal e o que as listas arrastaveis usam, e diz a mesma coisa
// sem competir com o resto da tela.
LRESULT CALLBACK SlotProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR) {
	switch (msg) {
		case WM_NCDESTROY:
			RemoveWindowSubclass(hwnd, SlotProc, id);
			break;

		case WM_ERASEBKGND:
			return 1;

		case WM_PAINT: {
			PAINTSTRUCT paint = {};
			HDC dc = BeginPaint(hwnd, &paint);
			if (dc) {
				RECT rc = {};
				GetClientRect(hwnd, &rc);

				// A janela continua ocupando a linha inteira -- e o espaco que a
				// linha arrastada vai reocupar -- mas so o meio dela e pintado.
				RECT line = rc;
				line.top = rc.top + (rc.bottom - rc.top) / 2 - 1;
				line.bottom = line.top + 2;
				line.left += 5;
				line.right -= 5;

				if (HBRUSH edge = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT))) {
					FillRect(dc, &line, edge);
					DeleteObject(edge);
				}
			}
			EndPaint(hwnd, &paint);
			return 0;
		}

		default:
			break;
	}
	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// Poe a linha no formato com alca, quantas vezes for chamada.
//
// Idempotente de proposito: o wx refaz o layout da linha por conta propria e
// devolve o nome e a barra para as posicoes originais, entao esta funcao precisa
// distinguir "ainda nao mexi" de "ja esta como eu quero". Quem diz e a distancia
// entre a alca e o nome: se e exatamente a largura da alca, o layout esta feito;
// se nao e, o wx acabou de desfaze-lo.
//
// Uma versao que so somasse a largura a cada chamada empurraria o nome para fora
// da linha na segunda vez.
void LayoutRow(HWND row) {
	HWND grip = GripOf(row);
	if (!grip)
		return;

	const RowParts parts = FindParts(row);
	if (!parts.ok)
		return;

	const RECT gripRect = RectIn(grip, row);
	if (parts.nameRect.left - gripRect.left == kGripWidth) {
		// So mantem a alca alinhada com o nome, caso a altura da linha mude.
		if (gripRect.top != parts.nameRect.top ||
			gripRect.bottom - gripRect.top != parts.nameRect.bottom - parts.nameRect.top) {
			SetWindowPos(grip, nullptr, static_cast<int>(gripRect.left),
						 static_cast<int>(parts.nameRect.top), kGripWidth,
						 static_cast<int>(parts.nameRect.bottom - parts.nameRect.top),
						 SWP_NOZORDER | SWP_NOACTIVATE);
		}
		return;
	}

	// A alca ocupa o lugar onde o nome comecava; o nome e a barra andam para a
	// direita pela largura dela. So esses dois: o lapis, a caixa e a porcentagem
	// ficam onde estavam, e por isso a barra encolhe pelo mesmo tanto que anda
	// -- senao passaria por cima da porcentagem.
	SetWindowPos(grip, nullptr, static_cast<int>(parts.nameRect.left),
				 static_cast<int>(parts.nameRect.top), kGripWidth,
				 static_cast<int>(parts.nameRect.bottom - parts.nameRect.top),
				 SWP_NOZORDER | SWP_NOACTIVATE);

	SetWindowPos(parts.name, nullptr, static_cast<int>(parts.nameRect.left) + kGripWidth,
				 static_cast<int>(parts.nameRect.top), 0, 0,
				 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

	SetWindowPos(parts.trackbar, nullptr, static_cast<int>(parts.trackbarRect.left) + kGripWidth,
				 static_cast<int>(parts.trackbarRect.top),
				 static_cast<int>(parts.trackbarRect.right - parts.trackbarRect.left) - kGripWidth,
				 static_cast<int>(parts.trackbarRect.bottom - parts.trackbarRect.top),
				 SWP_NOZORDER | SWP_NOACTIVATE);
}

// Poe a alca na linha, se ela ainda nao tem uma.
//
// A janela e criada ANTES de qualquer controle sair do lugar. Na ordem contraria
// -- que era a de antes -- uma falha na criacao deixava a linha deslocada e sem
// alca nenhuma para explicar por que.
void AddGrip(HWND row) {
	if (GripOf(row)) {
		LayoutRow(row);
		return;
	}

	const RowParts parts = FindParts(row);
	if (!parts.ok)
		return;

	// WS_EX_TRANSPARENT: o pai pinta o fundo por baixo, e a alca so poe os
	// pontos em cima. E o que tira o quadradinho branco sem precisar adivinhar a
	// cor de fundo da linha.
	HWND grip = CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"",
								WS_CHILD | WS_VISIBLE | SS_NOTIFY,
								static_cast<int>(parts.nameRect.left),
								static_cast<int>(parts.nameRect.top), kGripWidth,
								static_cast<int>(parts.nameRect.bottom - parts.nameRect.top), row,
								reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kGripId)),
								reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(row, GWLP_HINSTANCE)),
								nullptr);
	if (!grip)
		return; // a linha continua intacta

	SetWindowSubclass(grip, GripProc, kGripSubclassId, 0);
	LayoutRow(row);
}

void RemoveGrip(HWND row) {
	HWND grip = GripOf(row);
	if (!grip)
		return;

	const RowParts parts = FindParts(row);
	const RECT gripRect = RectIn(grip, row);
	const bool shifted = parts.ok && parts.nameRect.left - gripRect.left == kGripWidth;

	DestroyWindow(grip);
	if (!shifted)
		return;

	SetWindowPos(parts.name, nullptr, static_cast<int>(parts.nameRect.left) - kGripWidth,
				 static_cast<int>(parts.nameRect.top), 0, 0,
				 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

	SetWindowPos(parts.trackbar, nullptr, static_cast<int>(parts.trackbarRect.left) - kGripWidth,
				 static_cast<int>(parts.trackbarRect.top),
				 static_cast<int>(parts.trackbarRect.right - parts.trackbarRect.left) + kGripWidth,
				 static_cast<int>(parts.trackbarRect.bottom - parts.trackbarRect.top),
				 SWP_NOZORDER | SWP_NOACTIVATE);
}

void PlaceRow(HWND host, HWND row, int top) {
	const RECT rc = RectIn(row, host);
	if (rc.top == top)
		return;
	SetWindowPos(row, nullptr, static_cast<int>(rc.left), top, 0, 0,
				 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// Poe a lista na ordem que o usuario escolheu.
void ApplyOrder(HWND host, const std::vector<Row>& rows) {
	if (rows.empty() || g_desiredOrder.empty())
		return;

	std::vector<std::wstring> present;
	present.reserve(rows.size());
	for (const Row& row : rows)
		present.push_back(row.name);

	const std::vector<int> arrangement = ApplyDesiredOrder(g_desiredOrder, present);
	if (arrangement.size() != rows.size())
		return;

	// Os lugares sao os que a lista ja tem; so muda quem ocupa cada um. Assim a
	// altura de cada linha e o espacamento continuam sendo os do wx.
	std::vector<int> slots;
	slots.reserve(rows.size());
	for (const Row& row : rows)
		slots.push_back(row.top);
	std::sort(slots.begin(), slots.end());

	for (size_t i = 0; i < arrangement.size(); ++i) {
		const int from = arrangement[i];
		if (from >= 0 && from < static_cast<int>(rows.size()))
			PlaceRow(host, rows[static_cast<size_t>(from)].window, slots[i]);
	}
}

void RefreshRows() {
	if (g_drag.active)
		return;

	HWND host = PickSliderHost(g_frame, g_posePanel);
	if (!host)
		return;

	const std::vector<Row> rows = FindRows(host);
	if (rows.empty())
		return;

	for (const Row& row : rows) {
		if (g_gripsOn)
			AddGrip(row.window);
		else
			RemoveGrip(row.window);
	}

	// A ordem escolhida e reaplicada AQUI, e nao so no fim do arrasto: este e o
	// momento em que a lista acabou de ser refeita pelo programa, e sem isto a
	// escolha do usuario se perderia na primeira troca de outfit.
	ApplyOrder(host, rows);

	g_knownHost = host;
	g_knownRows = VisibleRowWindows(host);
	RedrawWindow(host, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
}

// O painel avisa quando muda.
//
// Duas coisas interessam. WM_PARENTNOTIFY conta que uma linha nasceu ou morreu,
// que e a troca de outfit ou de preset. WM_SIZE conta que o wx acabou de refazer
// o layout, o que desmancha tanto o deslocamento dentro das linhas quanto a
// ordem delas.
//
// Nos dois casos o trabalho e ADIADO por PostMessage. Mexer nos filhos no meio
// da passagem de layout do wx e pedir para brigar com ela; deixado para a
// proxima mensagem, o wx ja terminou e o campo esta livre. Esta e a mesma licao
// que o painel do Symmetrize ensinou: nao disputar o layout com o wx, deixar que
// ele acabe e corrigir depois.
LRESULT CALLBACK HostProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR) {
	if (msg == kRecheckMsg) {
		g_recheckPending = false;
		RefreshRows();
		return 0;
	}

	if (msg == WM_NCDESTROY) {
		RemoveWindowSubclass(hwnd, HostProc, id);
		if (hwnd == g_knownHost) {
			g_knownHost = nullptr;
			g_knownRows.clear();
		}
	}

	const LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);

	if ((msg == WM_SIZE || msg == WM_PARENTNOTIFY) && !g_recheckPending && !g_drag.active) {
		g_recheckPending = true;
		PostMessageW(hwnd, kRecheckMsg, 0, 0);
	}
	return result;
}

void WatchHost(HWND host) {
	if (host && SetWindowSubclass(host, HostProc, kHostSubclassId, 0))
		LogF("reorder: painel %p sob observacao", static_cast<void*>(host));
}

// Nota quando a lista muda e repoe alcas e ordem.
//
// Uma rede de seguranca por tras do aviso do painel: o painel so pode ser
// subclassado depois de existir, e ele so existe depois de um outfit carregado.
// Esta checagem barata, no movimento do mouse, e o que o acha da primeira vez.
void RecheckOnMouseMove() {
	if (g_drag.active)
		return;

	const DWORD now = GetTickCount();
	if (now - g_lastCheck < 300)
		return;
	g_lastCheck = now;

	if (!g_knownHost || !IsWindow(g_knownHost)) {
		HWND host = PickSliderHost(g_frame, g_posePanel);
		if (!host)
			return;
		WatchHost(host);
		RefreshRows();
		return;
	}

	// Comparando os HANDLES das linhas VISIVEIS, e nao a contagem de todas.
	//
	// Handles e nao contagem porque um outfit de cento e vinte sliders trocado
	// por outro de cento e vinte recria todas as janelas sem mudar o numero.
	// Visiveis e nao todas porque digitar no filtro de sliders do Outfit Studio
	// nao cria nem destroi janela nenhuma -- so acende e apaga -- e olhar so
	// para o conjunto completo nao veria nada acontecer.
	if (VisibleRowWindows(g_knownHost) != g_knownRows)
		RefreshRows();
}

void ShowSlotAt(int index) {
	if (!g_drag.slot || index < 0 || index >= static_cast<int>(g_drag.slotTops.size()))
		return;

	// Onde ele estava, para mandar o painel apagar aquele pedaco. Sem isto o
	// tracinho deixaria rastro: a janela e transparente e nao tem fundo proprio
	// para cobrir o desenho anterior.
	const RECT before = RectIn(g_drag.slot, g_drag.host);

	RECT client = {};
	GetClientRect(g_drag.host, &client);
	SetWindowPos(g_drag.slot, nullptr, 0, g_drag.slotTops[static_cast<size_t>(index)],
				 static_cast<int>(client.right), g_drag.rowHeight,
				 SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);

	InvalidateRect(g_drag.host, &before, TRUE);
	InvalidateRect(g_drag.slot, nullptr, FALSE);
}

bool BeginDrag(HWND row) {
	HWND host = GetParent(row);
	if (!host)
		return false;

	const std::vector<Row> rows = FindRows(host);
	if (rows.size() < 2)
		return false;

	int index = -1;
	for (size_t i = 0; i < rows.size(); ++i) {
		if (rows[i].window == row)
			index = static_cast<int>(i);
	}
	if (index < 0)
		return false;

	const RECT rowRect = RectIn(row, host);

	g_drag = Drag();
	g_drag.active = true;
	g_drag.host = host;
	g_drag.order = WindowsOf(rows);
	g_drag.original = g_drag.order;
	for (const Row& entry : rows)
		g_drag.slotTops.push_back(entry.top);
	g_drag.rowHeight = static_cast<int>(rowRect.bottom - rowRect.top);
	g_drag.index = index;

	POINT cursor = {};
	GetCursorPos(&cursor);
	ScreenToClient(host, &cursor);
	g_drag.grabOffset = static_cast<int>(cursor.y) - g_drag.slotTops[static_cast<size_t>(index)];

	g_drag.slot = CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"", WS_CHILD, 0, 0, 0, 0, host,
								  nullptr,
								  reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(host, GWLP_HINSTANCE)),
								  nullptr);
	if (g_drag.slot) {
		SetWindowSubclass(g_drag.slot, SlotProc, kSlotSubclassId, 0);
		ShowSlotAt(index);
	}

	// A linha na mao vai para o topo da z-order, senao ela passa POR TRAS das
	// vizinhas e do vao enquanto e arrastada, e parece ter sumido.
	SetWindowPos(row, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

	LogF("reorder: peguei a linha %d de %d", index + 1, static_cast<int>(rows.size()));
	return true;
}

void DragToCursor() {
	if (!g_drag.active || !g_drag.host)
		return;

	POINT cursor = {};
	GetCursorPos(&cursor);
	ScreenToClient(g_drag.host, &cursor);
	const int carried = static_cast<int>(cursor.y) - g_drag.grabOffset;

	const int wanted = SlotAt(g_drag.slotTops, carried);
	if (wanted >= 0 && wanted != g_drag.index) {
		MoveInOrder(g_drag.order, g_drag.index, wanted);
		g_drag.index = wanted;

		// As outras acomodam nos lugares; a da mao fica de fora, seguindo o
		// cursor -- e o que faz parecer que ela esta sendo carregada. O lugar
		// que sobra e o do vao.
		for (size_t i = 0; i < g_drag.order.size() && i < g_drag.slotTops.size(); ++i) {
			if (static_cast<int>(i) != g_drag.index)
				PlaceRow(g_drag.host, g_drag.order[i], g_drag.slotTops[i]);
		}
		ShowSlotAt(g_drag.index);
	}

	PlaceRow(g_drag.host, g_drag.order[static_cast<size_t>(g_drag.index)], carried);
}

void FinishDrag(bool cancelled) {
	if (!g_drag.active)
		return;

	g_drag.active = false;

	if (g_drag.slot) {
		DestroyWindow(g_drag.slot);
		g_drag.slot = nullptr;
	}

	if (cancelled)
		g_drag.order = g_drag.original;

	for (size_t i = 0; i < g_drag.order.size() && i < g_drag.slotTops.size(); ++i)
		PlaceRow(g_drag.host, g_drag.order[i], g_drag.slotTops[i]);

	// A escolha e guardada por NOME, que e o que sobrevive a lista ser refeita.
	//
	// E e COSTURADA na ordem completa, nao posta no lugar dela: com o filtro de
	// sliders ligado o arrasto so viu as linhas que sobraram na tela, e trocar a
	// ordem inteira pela desse punhado mandaria todas as escondidas para o fim
	// assim que a busca fosse limpa.
	if (!cancelled) {
		std::vector<std::wstring> subsetOrder;
		for (HWND row : g_drag.order) {
			std::wstring name = NameOf(row);
			if (!name.empty())
				subsetOrder.push_back(std::move(name));
		}
		g_desiredOrder = SpliceOrder(FullNameOrder(g_drag.host), subsetOrder);
	}

	if (g_drag.host && IsWindow(g_drag.host)) {
		RedrawWindow(g_drag.host, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
		g_knownRows = VisibleRowWindows(g_drag.host);
	}

	LogF("reorder: arrasto %s, %d nomes na ordem guardada",
		 cancelled ? "cancelado" : "concluido", static_cast<int>(g_desiredOrder.size()));

	g_drag = Drag();
}

void OnGripsToggled(bool checked) {
	g_gripsOn = checked;
	RefreshRows();
	LogF("reorder: alcas %s pelo menu", checked ? "ligadas" : "desligadas");
}

} // namespace

int SlotAt(const std::vector<int>& slotTops, int y) {
	if (slotTops.empty())
		return -1;

	// Antes do primeiro lugar e depois do ultimo caem nas pontas, e nao em
	// "nenhum": arrastar para fora da lista quer dizer levar a linha para o topo
	// ou para o fim, que e o que o usuario esta pedindo ao fazer isso.
	if (y <= slotTops.front())
		return 0;
	if (y >= slotTops.back())
		return static_cast<int>(slotTops.size()) - 1;

	for (size_t i = 0; i + 1 < slotTops.size(); ++i) {
		const int middle = slotTops[i] + (slotTops[i + 1] - slotTops[i]) / 2;
		if (y < middle)
			return static_cast<int>(i);
	}
	return static_cast<int>(slotTops.size()) - 1;
}

void MoveInOrder(std::vector<HWND>& order, int from, int to) {
	const int count = static_cast<int>(order.size());
	if (from < 0 || to < 0 || from >= count || to >= count || from == to)
		return;

	HWND moved = order[static_cast<size_t>(from)];
	order.erase(order.begin() + from);
	order.insert(order.begin() + to, moved);
}

std::vector<int> ApplyDesiredOrder(const std::vector<std::wstring>& desired,
								   const std::vector<std::wstring>& present) {
	std::vector<int> out;
	out.reserve(present.size());

	std::vector<bool> used(present.size(), false);

	// Primeiro os que o usuario ordenou, na ordem dele. Nome repetido consome um
	// slider de cada vez, e nome que sumiu simplesmente nao entra.
	for (const std::wstring& name : desired) {
		for (size_t i = 0; i < present.size(); ++i) {
			if (used[i] || present[i] != name)
				continue;
			used[i] = true;
			out.push_back(static_cast<int>(i));
			break;
		}
	}

	// Depois o que apareceu e ele nunca ordenou -- outro outfit, outro projeto --
	// na ordem em que o programa os deu. Descarta-los seria sumir com slider.
	for (size_t i = 0; i < present.size(); ++i) {
		if (!used[i])
			out.push_back(static_cast<int>(i));
	}

	return out;
}

std::vector<std::wstring> SpliceOrder(const std::vector<std::wstring>& full,
									  const std::vector<std::wstring>& subsetOrder) {
	// Quais lugares da ordem completa pertencem ao subconjunto.
	//
	// Casados um a um e da esquerda para a direita, e nao por "este nome esta na
	// lista?": dois sliders de mesmo nome marcariam os dois lugares mesmo que so
	// um deles estivesse na tela, e o resultado deixaria de ser uma permutacao.
	std::vector<bool> claimed(subsetOrder.size(), false);
	std::vector<bool> isSlot(full.size(), false);

	for (size_t i = 0; i < full.size(); ++i) {
		for (size_t j = 0; j < subsetOrder.size(); ++j) {
			if (claimed[j] || subsetOrder[j] != full[i])
				continue;
			claimed[j] = true;
			isSlot[i] = true;
			break;
		}
	}

	// So os nomes que acharam lugar entram no preenchimento.
	//
	// Um nome do subconjunto que nao esta na ordem completa nao reclamou lugar
	// nenhum, e enfia-lo mesmo assim expulsaria alguem: ele entraria num lugar
	// que pertence a outro, e esse outro sumiria da lista.
	std::vector<std::wstring> usable;
	for (size_t j = 0; j < subsetOrder.size(); ++j) {
		if (claimed[j])
			usable.push_back(subsetOrder[j]);
	}

	// Os lugares marcados recebem o subconjunto na ordem nova; os outros ficam
	// com quem ja estava neles.
	std::vector<std::wstring> out;
	out.reserve(full.size());
	size_t next = 0;
	for (size_t i = 0; i < full.size(); ++i) {
		if (isSlot[i] && next < usable.size())
			out.push_back(usable[next++]);
		else
			out.push_back(full[i]);
	}
	return out;
}

namespace SliderReorder {

bool Install(HWND frame) {
	Uninstall();
	g_frame = frame;

	const PosePanel pose = FindPosePanel(frame);
	g_posePanel = pose.ok ? pose.panel : nullptr;
	g_gripsOn = Cfg().sliderDragHandles;
	g_dark = DetectAppearance(AppDir()) == Appearance::Dark;

	if (HMENU view = MenuToggle::FindMenu(frame, "menuView"))
		MenuToggle::Add(frame, view, L"Slider drag handles", g_gripsOn, OnGripsToggled);

	g_installed = true;

	// O painel ainda nao existe nesta altura -- ele nasce com o primeiro outfit
	// carregado. Quem o acha e a checagem no movimento do mouse.
	LogF("reorder: pronto, alcas %s", g_gripsOn ? "ligadas" : "desligadas");
	return true;
}

void Uninstall() {
	if (g_drag.active)
		FinishDrag(true);

	if (g_knownHost && IsWindow(g_knownHost))
		RemoveWindowSubclass(g_knownHost, HostProc, kHostSubclassId);

	g_frame = nullptr;
	g_posePanel = nullptr;
	g_installed = false;
	g_knownHost = nullptr;
	g_knownRows.clear();
	g_recheckPending = false;
	g_drag = Drag();
}

bool HandleMouseMessage(MSG* msg) {
	if (!g_installed || !msg)
		return false;

	// O gesto pertence a alca, que capturou o mouse. Aqui sobra o Esc, que ela
	// nao recebe por nao ter foco de teclado, e a checagem barata de a lista ter
	// sido refeita.
	if (msg->message == WM_KEYDOWN && msg->wParam == VK_ESCAPE && g_drag.active) {
		FinishDrag(true);
		ReleaseCapture();
		return true;
	}

	if (msg->message == WM_MOUSEMOVE)
		RecheckOnMouseMove();

	return false;
}

} // namespace SliderReorder
