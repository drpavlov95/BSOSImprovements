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

// Texto original do tooltip -> atalho a mostrar. E o mapa que a reescrita
// consulta, e a unica chave que o wx nos deixa: o id de comando da ferramenta
// de barra nao e o do item de menu de mesmo nome.
std::map<std::wstring, std::wstring> g_byTooltip;

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

	// O casamento e pelo TEXTO, e nao pelo id de comando.
	//
	// A primeira versao usava o id, na suposicao de que o wx daria o mesmo
	// XRCID para a ferramenta de barra e o item de menu de mesmo nome. Nao da:
	// das quarenta e cinco ferramentas do Outfit Studio, ZERO casaram. O texto
	// do tooltip, esse, vem do XRC intacto.
	const std::wstring original = CurrentText(info);
	if (original.empty())
		return;

	auto found = g_byTooltip.find(original);
	if (found == g_byTooltip.end() || found->second.empty())
		return;

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
	auto* info = reinterpret_cast<NMTTDISPINFOW*>(lParam);

	// Uma linha na primeira notificacao da sessao, e so uma.
	//
	// Sem ela nao ha como separar "a notificacao nunca chega" de "chega e o
	// texto nao casa", e as duas se parecem exatamente igual na tela: o
	// tooltip sai sem o atalho. Foi essa ambiguidade que fez a versao anterior
	// falhar em silencio.
	static bool logged = false;
	if (!logged && info) {
		logged = true;
		LogF("tooltips: primeira notificacao -- janela %p, idFrom=%u, flags=0x%04X, texto '%ls'",
			 static_cast<void*>(hwnd), static_cast<unsigned>(info->hdr.idFrom),
			 static_cast<unsigned>(info->uFlags), CurrentText(info).c_str());
	}

	RewriteTooltip(info);
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

// Aplica as sobrescritas do INI por cima dos aceleradores lidos do XRC.
//
// [Remap] muda a tecla de verdade, entao o tooltip tem que mostrar a nova;
// [TooltipShortcuts] so rotula, e por isso vem depois e e a palavra final.
void ApplyOverrides(std::map<std::string, std::wstring>& accel) {
	for (const RemapEntry& entry : Cfg().remaps) {
		const std::wstring text = FormatHotkey(entry.key);
		if (!text.empty())
			accel[entry.xrcName] = text;
	}
	for (const RemapEntry& entry : Cfg().tooltipShortcuts) {
		const std::wstring text = FormatHotkey(entry.key);
		if (!text.empty())
			accel[entry.xrcName] = text;
	}
}

bool Enabled(const Config& cfg) {
	return cfg.shortcutTooltips;
}

} // namespace

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

namespace ShortcutTooltips {

bool Install(HWND frame) {
	Uninstall();
	g_frame = frame;

	const std::wstring xrc = AppDir() + L"res\\xrc\\OutfitStudio.xrc";
	const XrcShortcuts shortcuts = ResolveXrcShortcuts(xrc.c_str());
	if (shortcuts.toolByTooltip.empty()) {
		LogF("tooltips: nenhuma ferramenta com tooltip no XRC");
		return false;
	}

	std::map<std::string, std::wstring> accel = shortcuts.acceleratorByName;
	ApplyOverrides(accel);

	for (const auto& tool : shortcuts.toolByTooltip) {
		// Nome vazio e tooltip repetido em duas ferramentas: sem saber qual e
		// qual, melhor nenhum atalho que o atalho da outra.
		if (tool.second.empty())
			continue;

		auto found = accel.find(tool.second);
		if (found == accel.end() || found->second.empty())
			continue;

		g_byTooltip[tool.first] = found->second;
	}

	LogF("tooltips: %d ferramentas com tooltip no XRC, %d comandos com acelerador, %d casadas",
		 static_cast<int>(shortcuts.toolByTooltip.size()),
		 static_cast<int>(accel.size()), static_cast<int>(g_byTooltip.size()));

	if (g_byTooltip.empty())
		return false;

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
	g_byTooltip.clear();
	g_text.clear();
	g_frame = nullptr;
}

} // namespace ShortcutTooltips

BSOS_REGISTER_FEATURE(tooltips, "atalho no tooltip", HostApp::OutfitStudio, Enabled,
					  ShortcutTooltips::Install, ShortcutTooltips::Uninstall)
