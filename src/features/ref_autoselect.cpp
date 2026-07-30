#include "features/ref_autoselect.h"

#include <commctrl.h>

#include "core/host.h"
#include "core/log.h"
#include "core/ui_thread.h"
#include "features/outfit_tree.h"
#include "features/registry.h"
#include "win32/tree.h"

namespace {

const UINT_PTR kSubclassId = 0xB505;

HWND g_frame = nullptr;
HWND g_tree = nullptr;
UINT g_deferredMsg = 0;
PopulationTracker g_tracker;

LRESULT CALLBACK TreeSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
								  UINT_PTR, DWORD_PTR) {
	// Mensagem adiada: a selecao acontece depois que a pilha de repopulacao do
	// wx desenrola. Fazer isso dentro do proprio TVM_EXPAND arriscaria pegar o
	// LockShapeSelect ainda ativo.
	if (g_deferredMsg && msg == g_deferredMsg) {
		bool selected = SelectReference(g_frame);
		LogF("auto-select do reference: %s", selected ? "selecionado" : "sem reference, mantido o vanilla");
		return 0;
	}

	switch (msg) {
		case TVM_DELETEITEM:
			g_tracker.OnDelete();
			break;
		case TVM_INSERTITEMA:
		case TVM_INSERTITEMW:
			g_tracker.OnInsert();
			break;
		case TVM_SELECTITEM:
			g_tracker.OnSelect();
			break;
		case TVM_EXPAND:
			g_tracker.OnExpand();
			if (g_tracker.ShouldRetarget()) {
				// Reset antes de postar: a nossa propria selecao gera
				// TVM_SELECTITEM e nao pode realimentar o ciclo.
				g_tracker.Reset();
				if (g_deferredMsg)
					PostMessageW(hwnd, g_deferredMsg, 0, 0);
			}
			break;
		default:
			break;
	}

	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void InstallHere(void*) {
	g_tree = FindOutfitShapesTree(g_frame);
	if (!g_tree) {
		LogF("auto-select: arvore de shapes nao encontrada");
		return;
	}
	if (!SetWindowSubclass(g_tree, TreeSubclassProc, kSubclassId, 0)) {
		LogF("auto-select: SetWindowSubclass falhou");
		return;
	}

	// Projeto passado na linha de comando -- que e como o BodySlide abre o
	// Outfit Studio, e o que acontece ao dar duplo clique num .osp -- ja
	// terminou de carregar antes desta janela existir. Nesse caso a
	// repopulacao aconteceu sem nos, entao vale um retarget unico agora.
	HTREEITEM reference = FindReferenceItem(g_tree);
	if (!reference)
		return;

	HTREEITEM selected = reinterpret_cast<HTREEITEM>(SendMessageW(g_tree, TVM_GETNEXTITEM, TVGN_CARET, 0));
	LogF("auto-select na abertura: selecionado='%ls' reference='%ls' -> %s",
		 ItemText(g_tree, selected).c_str(), ItemText(g_tree, reference).c_str(),
		 selected == reference ? "ja correto, nada a fazer" : "corrigindo");

	if (selected != reference && g_deferredMsg)
		PostMessageW(g_tree, g_deferredMsg, 0, 0);
}

void UninstallHere(void*) {
	if (g_tree && IsWindow(g_tree))
		RemoveWindowSubclass(g_tree, TreeSubclassProc, kSubclassId);
	g_tree = nullptr;
}

bool Enabled(const Config& cfg) {
	return cfg.referenceAutoSelect;
}

} // namespace

void PopulationTracker::OnDelete() {
	sawDelete_ = true;
}

void PopulationTracker::OnInsert() {
	++insertCount_;
}

void PopulationTracker::OnSelect() {
	sawSelect_ = true;
}

void PopulationTracker::OnExpand() {
	sawExpand_ = true;
}

bool PopulationTracker::ShouldRetarget() const {
	// Precisa do ciclo completo: deletou os filhos antigos, inseriu novos,
	// expandiu -- e nenhuma selecao foi restaurada no meio.
	return sawDelete_ && insertCount_ > 0 && !sawSelect_ && sawExpand_;
}

void PopulationTracker::Reset() {
	sawDelete_ = false;
	sawSelect_ = false;
	sawExpand_ = false;
	insertCount_ = 0;
}

namespace RefAutoSelect {

bool Install(HWND frame) {
	Uninstall();
	g_frame = frame;
	g_tracker.Reset();

	if (!g_deferredMsg)
		g_deferredMsg = RegisterWindowMessageW(L"BSOSImprovements_SelectReference");

	// Subclasse exige a thread dona da janela.
	if (!RunOnUiThread(frame, InstallHere, nullptr))
		return false;

	return g_tree != nullptr;
}

void Uninstall() {
	if (g_tree && g_frame)
		RunOnUiThread(g_frame, UninstallHere, nullptr);
	g_tree = nullptr;
	g_frame = nullptr;
}

} // namespace RefAutoSelect

BSOS_REGISTER_FEATURE(refauto, "auto-select do reference", HostApp::OutfitStudio, Enabled,
					  RefAutoSelect::Install, RefAutoSelect::Uninstall)
