#include "win32/menu_toggle.h"

#include <commctrl.h>

#include <string>
#include <vector>

#include "core/host.h"
#include "core/log.h"
#include "core/ui_thread.h"
#include "win32/menu.h"
#include "xrcmap.h"

namespace {

const UINT_PTR kFrameSubclassId = 0xB514;

struct Entry {
	UINT id = 0;
	HMENU menu = nullptr;
	MenuToggle::Callback callback = nullptr;
	bool checked = false;
};

HWND g_frame = nullptr;
bool g_subclassed = false;
std::vector<Entry> g_entries;

// O separador que abre o nosso bloco, guardado por menu para sair junto no fim.
std::vector<HMENU> g_separated;

bool MenuUsesId(HMENU menu, UINT id, int depth) {
	if (!menu || depth > 8)
		return false;

	const int count = GetMenuItemCount(menu);
	for (int i = 0; i < count; ++i) {
		if (HMENU sub = GetSubMenu(menu, i)) {
			if (MenuUsesId(sub, id, depth + 1))
				return true;
			continue;
		}
		if (CommandIdAt(menu, i) == id)
			return true;
	}
	return false;
}

bool IdIsTaken(HWND frame, UINT id) {
	if (MenuUsesId(GetMenu(frame), id, 0))
		return true;
	for (const Entry& entry : g_entries) {
		if (entry.id == id)
			return true;
	}
	return false;
}

UINT FindFreeCommandId(HWND frame) {
	for (UINT candidate = 0xBF20; candidate < 0xBF60; ++candidate) {
		if (!IdIsTaken(frame, candidate))
			return candidate;
	}
	return 0;
}

LRESULT CALLBACK FrameSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR) {
	if (msg == WM_NCDESTROY) {
		RemoveWindowSubclass(hwnd, FrameSubclassProc, id);
		g_subclassed = false;
	}

	// Comando de menu chega com o codigo de notificacao zerado e lParam nulo.
	// Sem essa checagem, um controle com o mesmo id acionaria o interruptor.
	if (msg == WM_COMMAND && HIWORD(wParam) == 0 && lParam == 0) {
		const UINT command = LOWORD(wParam);
		for (Entry& entry : g_entries) {
			if (entry.id != command)
				continue;

			entry.checked = !entry.checked;
			CheckMenuItem(entry.menu, entry.id,
						  MF_BYCOMMAND | (entry.checked ? MF_CHECKED : MF_UNCHECKED));
			if (entry.callback)
				entry.callback(entry.checked);
			return 0; // nosso comando: o wx nao tem o que fazer com ele
		}
	}

	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void SubclassHere(void*) {
	g_subclassed = SetWindowSubclass(g_frame, FrameSubclassProc, kFrameSubclassId, 0) != FALSE;
	if (!g_subclassed)
		LogF("menu toggle: nao consegui subclassar o frame -- os itens nao vao responder");
}

void UnsubclassHere(void*) {
	if (g_frame && IsWindow(g_frame))
		RemoveWindowSubclass(g_frame, FrameSubclassProc, kFrameSubclassId);
	g_subclassed = false;
}

bool AlreadySeparated(HMENU menu) {
	for (HMENU seen : g_separated) {
		if (seen == menu)
			return true;
	}
	return false;
}

} // namespace

namespace MenuToggle {

HMENU FindMenu(HWND frame, const char* xrcName) {
	HMENU bar = frame ? GetMenu(frame) : nullptr;
	if (!bar || !xrcName)
		return nullptr;

	const std::wstring xrc = AppDir() + L"res\\xrc\\OutfitStudio.xrc";
	const MenuTrail trail = ResolveMenuTrail(xrc.c_str(), xrcName);
	if (trail.empty())
		return nullptr;

	// Pelo rotulo, e nao so pela posicao: outro mod que insira um menu antes
	// deste empurraria o indice lido do XRC para o menu errado.
	int index = -1;
	HMENU container = ContainerAtLabeledPath(bar, trail.path, trail.labels, index);
	return container ? SubMenuAt(container, index) : nullptr;
}

UINT Add(HWND frame, HMENU menu, const wchar_t* label, bool checked, Callback onToggle) {
	if (!frame || !menu || !label)
		return 0;

	const UINT id = FindFreeCommandId(frame);
	if (id == 0) {
		LogF("menu toggle: nenhum id livre para '%ls'", label);
		return 0;
	}

	if (!AlreadySeparated(menu)) {
		AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
		g_separated.push_back(menu);
	}

	if (!AppendMenuW(menu, MF_STRING, id, label)) {
		LogF("menu toggle: nao consegui acrescentar '%ls'", label);
		return 0;
	}

	CheckMenuItem(menu, id, MF_BYCOMMAND | (checked ? MF_CHECKED : MF_UNCHECKED));

	Entry entry;
	entry.id = id;
	entry.menu = menu;
	entry.callback = onToggle;
	entry.checked = checked;
	g_entries.push_back(entry);

	g_frame = frame;
	if (!g_subclassed)
		RunOnUiThread(frame, SubclassHere, nullptr);

	DrawMenuBar(frame);
	LogF("menu toggle: '%ls' criado com id %u", label, id);
	return id;
}

void SetChecked(UINT id, bool checked) {
	for (Entry& entry : g_entries) {
		if (entry.id != id)
			continue;
		entry.checked = checked;
		CheckMenuItem(entry.menu, entry.id,
					  MF_BYCOMMAND | (checked ? MF_CHECKED : MF_UNCHECKED));
		return;
	}
}

void RemoveAll() {
	for (const Entry& entry : g_entries)
		DeleteMenu(entry.menu, entry.id, MF_BYCOMMAND);

	// E o separador que abriu o bloco. Deixa-lo para tras faria o menu do
	// usuario ganhar um risco solto no fim a cada ciclo de instalar e sair.
	for (HMENU menu : g_separated) {
		const int last = GetMenuItemCount(menu) - 1;
		if (last >= 0 && MenuTextAt(menu, last).empty() && !GetSubMenu(menu, last))
			DeleteMenu(menu, static_cast<UINT>(last), MF_BYPOSITION);
	}

	if (g_frame && IsWindow(g_frame)) {
		DrawMenuBar(g_frame);
		RunOnUiThread(g_frame, UnsubclassHere, nullptr);
	}

	g_entries.clear();
	g_separated.clear();
	g_frame = nullptr;
	g_subclassed = false;
}

} // namespace MenuToggle
