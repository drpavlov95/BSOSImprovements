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
		// So troca quando o item selecionado e o primeiro shape -- que e o que
		// o Outfit Studio escolhe sozinho ao carregar. Se o usuario tem outro
		// shape selecionado, a selecao e dele e nao se mexe.
		HTREEITEM selected = SelectedItem(hwnd);
		HTREEITEM first = FirstShapeItem(hwnd);
		HTREEITEM reference = FindReferenceItem(hwnd);

		if (!reference) {
			LogF("auto-select: projeto sem reference, mantido o vanilla");
		} else if (selected != first) {
			LogF("auto-select: a selecao nao e o primeiro shape, deixando como esta");
		} else if (selected == reference) {
			LogF("auto-select: o primeiro shape ja e o reference, nada a fazer");
		} else {
			bool ok = SelectReference(g_frame);
			LogF("auto-select: %s", ok ? "trocado para o reference" : "falhou ao selecionar");
		}
		return 0;
	}

	switch (msg) {
		case TVM_INSERTITEMA:
		case TVM_INSERTITEMW:
			g_tracker.OnInsert();
			break;
		case TVM_EXPAND: {
			g_tracker.OnExpand();
			const bool finished = g_tracker.CycleFinished();

			// Zera SEMPRE no expand, tenha disparado ou nao. Sem isso as
			// insercoes de qualquer operacao ficam acumuladas, e um expand
			// avulso mais tarde -- o usuario abrindo um no na arvore --
			// fecharia um "ciclo" que nunca existiu. Zerando aqui, um ciclo
			// so vale se as insercoes vierem imediatamente antes do expand,
			// que e como a repopulacao de fato acontece.
			g_tracker.Reset();

			if (finished && g_deferredMsg)
				PostMessageW(hwnd, g_deferredMsg, 0, 0);
			break;
		}
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

void PopulationTracker::OnInsert() {
	++insertCount_;
}

void PopulationTracker::OnExpand() {
	sawExpand_ = true;
}

bool PopulationTracker::CycleFinished() const {
	return insertCount_ > 0 && sawExpand_;
}

void PopulationTracker::Reset() {
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
