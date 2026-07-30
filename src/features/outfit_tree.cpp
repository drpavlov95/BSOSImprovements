#include "features/outfit_tree.h"

#include <vector>

#include "core/log.h"
#include "win32/tree.h"
#include "win32/winfind.h"

namespace {

HWND g_cachedFrame = nullptr;
HWND g_cachedTree = nullptr;

// O Outfit Studio tem quatro SysTreeView32 -- outfitShapes, outfitBones,
// segmentTree e partitionTree -- e identifica-las de fora nao e obvio:
//
//   * Todas usam wxTR_HIDE_ROOT, e nesse modo o wx nao cria item nativo de
//     raiz. Nao ha texto de raiz para comparar.
//   * Tres delas ficam em abas inativas de um notebook e compartilham o mesmo
//     retangulo default, entao geometria nao separa.
//   * Ordem de criacao depende de z-order de irmaos, que nao e garantia.
//
// O que separa de verdade: SetItemBold e chamado SO em outfitShapes, nas duas
// unicas ocorrencias do codigo do Outfit Studio. Entao a arvore que tiver um
// item em negrito e ela, e esse item e o proprio reference.
//
// Antes de um projeto carregar nao ha negrito em lugar nenhum. Ai cai no
// fallback: a primeira arvore com state image list (so shapes e bones tem),
// que segue a ordem de declaracao do XRC. Esse caso so importa para a Task 8,
// que precisa do handle antes de haver reference; para selecionar, nao havendo
// negrito nao ha o que selecionar.

bool HasStateImageList(HWND tree) {
	return SendMessageW(tree, TVM_GETIMAGELIST, TVSIL_STATE, 0) != 0;
}

std::vector<HWND> AllTrees(HWND frame) {
	std::vector<HWND> trees;
	for (int nth = 0; nth < 16; ++nth) {
		HWND tree = FindDescendantByClass(frame, WC_TREEVIEWW, nth);
		if (!tree)
			break;
		trees.push_back(tree);
	}
	return trees;
}

} // namespace

HWND FindOutfitShapesTree(HWND frame) {
	if (g_cachedFrame == frame && g_cachedTree && IsWindow(g_cachedTree))
		return g_cachedTree;

	if (!frame)
		return nullptr;

	const std::vector<HWND> trees = AllTrees(frame);

	// Sinal exato: quem tem negrito e outfitShapes.
	for (HWND tree : trees) {
		if (FindBoldDescendant(tree)) {
			g_cachedFrame = frame;
			g_cachedTree = tree;
			LogF("outfit_tree: outfitShapes = %p (identificada pelo negrito)", static_cast<void*>(tree));
			return tree;
		}
	}

	// Fallback sem projeto carregado. Nao cacheia: assim que houver um
	// reference, a identificacao exata assume.
	for (HWND tree : trees) {
		if (HasStateImageList(tree))
			return tree;
	}

	LogF("outfit_tree: nenhuma das %d arvores serve", static_cast<int>(trees.size()));
	return nullptr;
}

HTREEITEM FindReferenceItem(HWND tree) {
	return FindBoldDescendant(tree);
}

bool SelectReference(HWND frame) {
	HWND tree = FindOutfitShapesTree(frame);
	if (!tree)
		return false;

	HTREEITEM reference = FindReferenceItem(tree);
	if (!reference) {
		// Projeto sem reference: nao fazer nada e o comportamento correto.
		return false;
	}

	SelectItem(tree, reference);
	return true;
}
