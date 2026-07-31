#pragma once

#include <windows.h>

// Ao carregar uma roupa pela primeira vez, selecionar o reference em vez do
// primeiro mesh da lista.
namespace RefAutoSelect {

bool Install(HWND frame);
void Uninstall();

} // namespace RefAutoSelect

// Detecta o fim de um ciclo de repopulacao da RefreshGUIFromProj().
//
// Duas tentativas anteriores de criterio nao funcionaram, e vale registrar
// por que:
//
//  * Exigir deletar antes de inserir. As deleces so acontecem dentro de
//    "if (outfitRoot.IsOk())" (OutfitStudio.cpp:3850), ou seja, apenas quando
//    ja havia um projeto aberto. No primeiro carregamento da sessao -- o caso
//    mais comum -- nao ha o que deletar, e o ciclo nunca era reconhecido.
//
//  * Usar TVM_SELECTITEM como sinal de "a selecao foi restaurada, nao mexa".
//    O proprio auto-select do vanilla (SelectItem(firstItem), linha 3920)
//    tambem emite TVM_SELECTITEM, entao os dois casos ficam identicos.
//
// O criterio atual e so "houve insercoes e depois um expand". Quem decide se
// vale trocar a selecao e o estado final da arvore, nao a sequencia: so age
// quando o item selecionado e o PRIMEIRO shape, que e exatamente o que o
// Outfit Studio escolhe sozinho ao carregar.
class PopulationTracker {
public:
	void OnInsert();
	void OnExpand();

	// Verdadeiro quando um ciclo de repopulacao acabou de terminar.
	bool CycleFinished() const;
	void Reset();

private:
	bool sawExpand_ = false;
	int insertCount_ = 0;
};
