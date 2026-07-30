#pragma once

#include <windows.h>

// Ao carregar uma roupa pela primeira vez, selecionar o reference em vez do
// primeiro mesh da lista.
namespace RefAutoSelect {

bool Install(HWND frame);
void Uninstall();

} // namespace RefAutoSelect

// Maquina de estados do ciclo de repopulacao da RefreshGUIFromProj(), exposta
// para teste sem depender de janela.
//
// A funcao e chamada de ~35 lugares, mas o auto-select do primeiro item
// (OutfitStudio.cpp:3920) so roda no branch em que NENHUMA selecao anterior
// sobreviveu -- que e exatamente "acabei de carregar". Deletar ou renomear um
// shape restaura a selecao e passa pelo outro branch.
class PopulationTracker {
public:
	void OnDelete();
	void OnInsert();
	void OnSelect();
	void OnExpand();

	// Verdadeiro so quando o ciclo terminou sem restaurar selecao.
	bool ShouldRetarget() const;
	void Reset();

private:
	bool sawDelete_ = false;
	bool sawSelect_ = false;
	bool sawExpand_ = false;
	int insertCount_ = 0;
};
