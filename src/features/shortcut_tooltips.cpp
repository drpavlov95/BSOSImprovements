#include "features/shortcut_tooltips.h"

#include <commctrl.h>

#include <vector>

#include "core/host.h"
#include "core/log.h"
#include "core/ui_thread.h"
#include "features/registry.h"
#include "win32/menu.h"
#include "win32/winfind.h"
#include "xrcmap.h"

namespace {

const UINT_PTR kSubclassId = 0xB50C;

HWND g_frame = nullptr;
std::map<UINT, std::wstring> g_accel;
std::vector<HWND> g_subclassed;

// O texto reescrito precisa sobreviver ao retorno da notificacao: o controle de
// tooltip le a string DEPOIS que o tratamento termina. Por isso ele mora aqui e
// nao numa variavel local.
//
// Tambem nao da para reaproveitar o szText embutido da notificacao: ele tem 80
// caracteres, e as descricoes do Outfit Studio passam disso com folga -- "Masks
// all vertices that do not have any of the selected asymmetries." Escrever la
// truncaria o texto do proprio programa.
std::wstring g_text;

bool g_warnedAnsi = false;

std::wstring CurrentText(const NMTTDISPINFOW* info) {
	const wchar_t* text = info->lpszText;
	if (!text)
		return std::wstring(info->szText);
	if (text == LPSTR_TEXTCALLBACKW || IS_INTRESOURCE(text))
		return std::wstring(); // callback ou id de recurso: nao da para ler daqui
	return std::wstring(text);
}

void RewriteTooltip(NMTTDISPINFOW* info) {
	if (!info)
		return;

	// Com TTF_IDISHWND o idFrom e um HWND e nao um id de comando -- e o caso
	// dos tooltips que o wx poe em controles avulsos. Ali nao ha ferramenta de
	// barra nenhuma para casar.
	if (info->uFlags & TTF_IDISHWND)
		return;

	auto found = g_accel.find(static_cast<UINT>(info->hdr.idFrom));
	if (found == g_accel.end() || found->second.empty())
		return;

	const std::wstring original = CurrentText(info);
	const std::wstring composed = ComposeTooltip(original, found->second);
	if (composed == original)
		return;

	g_text = composed;
	info->lpszText = g_text.data();
	// Com hinst preenchido o controle trataria lpszText como id de recurso.
	info->hinst = nullptr;
}

LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR) {
	if (msg == WM_NCDESTROY)
		RemoveWindowSubclass(hwnd, SubclassProc, id);

	if (msg != WM_NOTIFY)
		return DefSubclassProc(hwnd, msg, wParam, lParam);

	auto* header = reinterpret_cast<NMHDR*>(lParam);
	if (!header)
		return DefSubclassProc(hwnd, msg, wParam, lParam);

	if (header->code == TTN_GETDISPINFOA) {
		// O wx e Unicode, entao isto nao deveria acontecer. Se acontecer, o log
		// diz por que os tooltips nao mudaram, em vez de ficar em branco.
		if (!g_warnedAnsi) {
			g_warnedAnsi = true;
			LogF("tooltips: chegou TTN_GETDISPINFO em ANSI -- esse eu nao mexo");
		}
		return DefSubclassProc(hwnd, msg, wParam, lParam);
	}

	if (header->code != TTN_GETDISPINFOW)
		return DefSubclassProc(hwnd, msg, wParam, lParam);

	// O texto normal e escrito pelo wx durante esta chamada; so depois dela ha
	// o que reescrever.
	const LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
	RewriteTooltip(reinterpret_cast<NMTTDISPINFOW*>(lParam));
	return result;
}

void SubclassHere(void*) {
	for (HWND target : g_subclassed) {
		if (!SetWindowSubclass(target, SubclassProc, kSubclassId, 0))
			LogF("tooltips: nao consegui subclassar %p", static_cast<void*>(target));
	}
}

void UnsubclassHere(void*) {
	for (HWND target : g_subclassed) {
		if (target && IsWindow(target))
			RemoveWindowSubclass(target, SubclassProc, kSubclassId);
	}
}

void Remember(HWND candidate) {
	if (!candidate)
		return;
	for (HWND existing : g_subclassed) {
		if (existing == candidate)
			return;
	}
	g_subclassed.push_back(candidate);
}

// Sobrescreve o acelerador de um comando identificado pelo name= do XRC.
void OverrideAccelerator(HMENU bar, const std::wstring& xrc, const RemapEntry& entry,
						 const char* origin) {
	const MenuTrail trail = ResolveMenuTrail(xrc.c_str(), entry.xrcName.c_str());
	if (trail.empty()) {
		LogF("tooltips: '%s' (%s) nao existe no XRC, ignorado", entry.xrcName.c_str(), origin);
		return;
	}

	const UINT id = CommandIdAtLabeledPath(bar, trail.path, trail.labels);
	if (id == 0) {
		LogF("tooltips: '%s' (%s) nao tem id no menu vivo, ignorado", entry.xrcName.c_str(), origin);
		return;
	}

	const std::wstring text = FormatHotkey(entry.key);
	if (text.empty())
		return;

	g_accel[id] = text;
	LogF("tooltips: '%s' (%s) passa a mostrar '%ls'", entry.xrcName.c_str(), origin, text.c_str());
}

// Registra quais ferramentas de barra ficaram com atalho e quais nao.
//
// E o que permite descobrir pelo log o que vale a pena por em
// [TooltipShortcuts]: as teclas que o Outfit Studio trata no proprio codigo --
// os numeros que trocam de brush -- nao aparecem em recurso nenhum que de para
// ler de fora.
void LogToolCoverage(HWND toolbar) {
	const int count = static_cast<int>(SendMessageW(toolbar, TB_BUTTONCOUNT, 0, 0));
	int withAccel = 0;
	int without = 0;

	for (int i = 0; i < count; ++i) {
		TBBUTTON button = {};
		if (!SendMessageW(toolbar, TB_GETBUTTON, static_cast<WPARAM>(i),
						  reinterpret_cast<LPARAM>(&button)))
			continue;
		if (button.idCommand == 0)
			continue; // separador

		auto found = g_accel.find(static_cast<UINT>(button.idCommand));
		if (found != g_accel.end() && !found->second.empty())
			++withAccel;
		else
			++without;
	}

	LogF("tooltips: barra %p tem %d ferramentas -- %d com atalho, %d sem",
		 static_cast<void*>(toolbar), count, withAccel, without);
}

bool Enabled(const Config& cfg) {
	return cfg.shortcutTooltips;
}

} // namespace

std::wstring AcceleratorFromMenuLabel(const std::wstring& rawLabel) {
	const size_t tab = rawLabel.find(L'\t');
	if (tab == std::wstring::npos)
		return std::wstring();

	std::wstring accel = rawLabel.substr(tab + 1);

	// O rotulo pode trazer mais de um \t: o Windows usa o primeiro para separar
	// o acelerador, e o que vier depois e alinhamento.
	const size_t another = accel.find(L'\t');
	if (another != std::wstring::npos)
		accel.resize(another);

	const size_t begin = accel.find_first_not_of(L" \r\n");
	if (begin == std::wstring::npos)
		return std::wstring();
	const size_t end = accel.find_last_not_of(L" \r\n");
	return accel.substr(begin, end - begin + 1);
}

std::wstring FormatHotkey(const Hotkey& key) {
	if (!key.IsValid())
		return std::wstring();

	std::wstring text;
	if (key.ctrl)
		text += L"Ctrl+";
	if (key.alt)
		text += L"Alt+";
	if (key.shift)
		text += L"Shift+";

	// ParseHotkey so aceita A-Z e 0-9, entao o vk ja e o proprio caractere.
	text.push_back(static_cast<wchar_t>(key.vk));
	return text;
}

std::wstring ComposeTooltip(const std::wstring& original, const std::wstring& accel) {
	// Sem tooltip nao ha o que anotar. Devolver so "(K)" inventaria um tooltip
	// para uma ferramenta que de proposito nao tem nenhum.
	if (original.empty() || accel.empty())
		return original;

	const std::wstring suffix = L" (" + accel + L")";
	if (original.size() >= suffix.size() &&
		original.compare(original.size() - suffix.size(), suffix.size(), suffix) == 0)
		return original;

	return original + suffix;
}

std::map<UINT, std::wstring> CollectMenuAccelerators(HMENU bar) {
	std::map<UINT, std::wstring> out;
	if (!bar)
		return out;

	// Pilha explicita em vez de recursao. Um HMENU pode conter a si mesmo --
	// nada na API impede -- e a recursao ingenua nunca voltaria.
	std::vector<HMENU> pending{bar};
	std::vector<HMENU> seen;

	while (!pending.empty()) {
		HMENU menu = pending.back();
		pending.pop_back();

		bool repeated = false;
		for (HMENU already : seen) {
			if (already == menu) {
				repeated = true;
				break;
			}
		}
		if (repeated)
			continue;
		seen.push_back(menu);

		const int count = GetMenuItemCount(menu);
		for (int i = 0; i < count; ++i) {
			if (HMENU sub = GetSubMenu(menu, i)) {
				pending.push_back(sub);
				continue;
			}

			const UINT id = CommandIdAt(menu, i);
			if (id == 0)
				continue; // separador

			const std::wstring accel = AcceleratorFromMenuLabel(MenuRawTextAt(menu, i));
			if (!accel.empty())
				out[id] = accel;
		}
	}
	return out;
}

namespace ShortcutTooltips {

bool Install(HWND frame) {
	Uninstall();
	g_frame = frame;

	HMENU bar = GetMenu(frame);
	if (!bar) {
		LogF("tooltips: o frame nao tem menubar, e e dela que os atalhos saem");
		return false;
	}

	g_accel = CollectMenuAccelerators(bar);
	LogF("tooltips: %d comandos tem acelerador no menu", static_cast<int>(g_accel.size()));

	const std::wstring xrc = AppDir() + L"res\\xrc\\OutfitStudio.xrc";
	// A ordem importa: [Remap] troca a tecla de verdade, e [TooltipShortcuts] e
	// a palavra final do usuario sobre o que exibir.
	for (const RemapEntry& entry : Cfg().remaps)
		OverrideAccelerator(bar, xrc, entry, "remap");
	for (const RemapEntry& entry : Cfg().tooltipShortcuts)
		OverrideAccelerator(bar, xrc, entry, "tooltip");

	const std::vector<HWND> toolbars = FindDescendantsByClass(frame, TOOLBARCLASSNAMEW);
	if (toolbars.empty()) {
		LogF("tooltips: nenhuma barra de ferramentas encontrada");
		return false;
	}

	// A notificacao de tooltip de uma barra nativa e encaminhada para a janela
	// PAI dela, mas o wx tambem a trata na propria barra, dependendo de como
	// roteia o WM_NOTIFY. Em vez de apostar num dos dois caminhos, os dois sao
	// subclassados -- ComposeTooltip e idempotente justamente porque assim a
	// mensagem pode passar pelos dois.
	for (HWND toolbar : toolbars) {
		LogToolCoverage(toolbar);
		Remember(toolbar);
		Remember(GetParent(toolbar));
	}
	Remember(frame);

	if (!RunOnUiThread(frame, SubclassHere, nullptr)) {
		g_subclassed.clear();
		return false;
	}

	LogF("tooltips: %d janelas subclassadas", static_cast<int>(g_subclassed.size()));
	return true;
}

void Uninstall() {
	if (!g_subclassed.empty() && g_frame)
		RunOnUiThread(g_frame, UnsubclassHere, nullptr);
	g_subclassed.clear();
	g_accel.clear();
	g_text.clear();
	g_frame = nullptr;
}

} // namespace ShortcutTooltips

BSOS_REGISTER_FEATURE(tooltips, "atalho no tooltip", HostApp::OutfitStudio, Enabled,
					  ShortcutTooltips::Install, ShortcutTooltips::Uninstall)
