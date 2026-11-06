#pragma once

#include <windows.h>

#include <string>
#include <vector>

// Uma copia de uma linha do wxListCtrl nativo. itemData e o indice que o
// proprio Outfit Studio usa para ligar a linha de volta ao modelo; ele precisa
// sobreviver a filtro e ordenacao para Make Local/External continuarem agindo
// sobre a entrada certa.
struct SliderDataFilterRow {
	std::vector<std::wstring> columns;
	LPARAM itemData = 0;
	UINT state = 0;
};

// -1 procura em todas as colunas; 0..N procura apenas naquela coluna.
std::vector<size_t> MatchingSliderDataRows(
	const std::vector<SliderDataFilterRow>& rows, int column,
	const std::wstring& query);

void SortSliderDataRows(std::vector<SliderDataFilterRow>& rows, int column,
						bool ascending);

struct SliderDataDialogControls {
	HWND filter = nullptr;
	HWND source = nullptr;
	HWND status = nullptr;
	HWND list = nullptr;
	std::vector<HWND> buttons;

	bool ok() const {
		return filter && source && status && list && buttons.size() == 4;
	}
};

// Reconhece o dialogo pela estrutura, nunca pelo titulo ou pelos textos, para
// continuar funcionando nas traducoes do Outfit Studio.
SliderDataDialogControls FindSliderDataDialogControls(HWND dialog);

namespace SliderDataFilter {

bool Install(HWND frame);
void Uninstall();

} // namespace SliderDataFilter
