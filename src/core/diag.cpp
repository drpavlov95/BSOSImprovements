#include "core/diag.h"

#include <commctrl.h>

#include <string>
#include <vector>

#include "core/log.h"
#include "win32/menu.h"
#include "win32/winfind.h"

namespace {

std::wstring TextOf(HWND window) {
	const int len = GetWindowTextLengthW(window);
	if (len <= 0 || len > 200)
		return std::wstring();

	std::wstring text(static_cast<size_t>(len) + 1, L'\0');
	const int written = GetWindowTextW(window, text.data(), static_cast<int>(text.size()));
	text.resize(written > 0 ? static_cast<size_t>(written) : 0);

	// Quebra de linha no meio de uma linha de log embaralharia a arvore.
	for (wchar_t& c : text) {
		if (c == L'\r' || c == L'\n')
			c = L' ';
	}
	return text;
}

void DumpBranch(HWND window, int depth, int& budget) {
	if (depth > 10)
		return;

	for (HWND child : ChildrenOf(window)) {
		// Teto de linhas: a arvore do wx tem milhares de janelas quando um
		// projeto grande esta aberto, e um log de dezenas de megabytes nao
		// ajuda ninguem a ler nada.
		if (--budget < 0)
			return;

		RECT rc = {};
		GetWindowRect(child, &rc);

		const std::wstring indent(static_cast<size_t>(depth) * 2, L' ');
		// Duas visibilidades, e nao uma.
		//
		// `visivel` e a pergunta do Windows, que exige a arvore inteira acesa.
		// `estilo` e o bit da PROPRIA janela, que e no que o resto do mod se
		// apoia -- e as duas discordam neste programa o tempo todo. Reportar so
		// a primeira ja quase me fez ler um viveiro de linhas escondidas como se
		// fossem linhas de verdade.
		LogF("  %ls%ls hwnd=%p id=%d %ldx%ld em (%ld,%ld) visivel=%d estilo=%d '%ls'",
			 indent.c_str(), ClassOf(child).c_str(), static_cast<void*>(child),
			 GetDlgCtrlID(child), rc.right - rc.left, rc.bottom - rc.top, rc.left, rc.top,
			 IsWindowVisible(child) ? 1 : 0, HasVisibleStyle(child) ? 1 : 0,
			 TextOf(child).c_str());

		DumpBranch(child, depth + 1, budget);
	}
}

void DumpMenuBranch(HMENU menu, const std::wstring& path, int depth) {
	if (!menu || depth > 6)
		return;

	const int count = GetMenuItemCount(menu);
	for (int i = 0; i < count; ++i) {
		const std::wstring raw = MenuRawTextAt(menu, i);

		if (HMENU sub = GetSubMenu(menu, i)) {
			LogF("  menu %ls%d: submenu '%ls'", path.c_str(), i, raw.c_str());
			DumpMenuBranch(sub, path + std::to_wstring(i) + L">", depth + 1);
			continue;
		}

		const UINT id = CommandIdAt(menu, i);
		if (id == 0)
			continue; // separador

		LogF("  menu %ls%d: id=%u '%ls'", path.c_str(), i, id, raw.c_str());
	}
}

} // namespace

namespace Diag {

void DumpWindowTree(HWND root, const char* what) {
	if (!root || !IsWindow(root)) {
		LogF("dump: %s -- janela invalida", what);
		return;
	}

	RECT rc = {};
	GetWindowRect(root, &rc);
	LogF("dump: arvore de %s -- raiz %p classe '%ls' %ldx%ld '%ls'", what,
		 static_cast<void*>(root), ClassOf(root).c_str(), rc.right - rc.left,
		 rc.bottom - rc.top, TextOf(root).c_str());

	int budget = 1500;
	DumpBranch(root, 1, budget);
	if (budget < 0)
		LogF("dump: (cortado no teto de linhas)");
	LogF("dump: fim da arvore de %s", what);
}

void DumpToolbars(HWND root) {
	const std::vector<HWND> bars = FindDescendantsByClass(root, TOOLBARCLASSNAMEW);
	LogF("dump: %d barras de ferramentas", static_cast<int>(bars.size()));

	for (HWND bar : bars) {
		RECT rc = {};
		GetWindowRect(bar, &rc);
		const int count = static_cast<int>(SendMessageW(bar, TB_BUTTONCOUNT, 0, 0));
		LogF("dump: barra %p %ldx%ld em (%ld,%ld), %d botoes", static_cast<void*>(bar),
			 rc.right - rc.left, rc.bottom - rc.top, rc.left, rc.top, count);

		for (int i = 0; i < count; ++i) {
			TBBUTTON button = {};
			if (!SendMessageW(bar, TB_GETBUTTON, static_cast<WPARAM>(i),
							  reinterpret_cast<LPARAM>(&button))) {
				LogF("  botao %d: TB_GETBUTTON falhou", i);
				continue;
			}
			LogF("  botao %d: id=%d estilo=0x%02X estado=0x%02X bitmap=%d", i,
				 button.idCommand, static_cast<unsigned>(button.fsStyle),
				 static_cast<unsigned>(button.fsState), button.iBitmap);
		}
	}
}

void DumpMenuIds(HWND frame) {
	HMENU bar = frame ? GetMenu(frame) : nullptr;
	if (!bar) {
		LogF("dump: o frame nao tem menubar");
		return;
	}
	LogF("dump: ids da menubar");
	DumpMenuBranch(bar, std::wstring(), 0);
	LogF("dump: fim dos ids da menubar");
}

void DumpEverything(HWND frame) {
	LogF("=== DUMP DE DIAGNOSTICO ===");
	DumpToolbars(frame);
	DumpMenuIds(frame);
	DumpWindowTree(frame, "janela principal");
	LogF("=== FIM DO DUMP ===");
}

} // namespace Diag
