#pragma once

#include <windows.h>

#include <string>
#include <vector>

// Substitui o diálogo "Choose Groups" do BodySlide por um com busca.
//
// O original e um wxMultiChoiceDialog cujo contrato inteiro e: le a caixa de
// filtro de grupos, mostra uma lista de marcacao, e no OK escreve os marcados
// de volta separados por virgula e repopula a lista de outfits. Como isso e
// tudo texto, da para substituir o dialogo sem tocar em nada interno do wx.
namespace GroupSearch {

bool Install(HWND frame);
void Uninstall();

} // namespace GroupSearch

// Logica pura, exposta para teste.

// Reproduz o parsing do BodySlide: tokeniza em ',' e ';' e faz trim.
std::vector<std::wstring> ParseFilterTokens(const std::wstring& text);

// Junta com ", ", como o OnChooseGroups original.
std::wstring JoinFilterTokens(const std::vector<std::wstring>& names);

// Substring case-insensitive. Consulta vazia casa tudo.
bool MatchesFilter(const std::wstring& name, const std::wstring& query);

// IDs dos controles do nosso dialogo. No header porque o teste os verifica.
enum GroupSearchControlId {
	kGroupSearchEdit = 2001,
	kGroupSearchList = 2002,
	kGroupSearchCheckVisible = 2003,
	kGroupSearchClearAll = 2004,
	kGroupSearchCounter = 2005,
};

// Monta o DLGTEMPLATE em memoria. Exposto para teste: um template malformado
// so se manifesta na hora de criar o dialogo, e isso precisa falhar no CI, nao
// na mao do usuario.
std::vector<BYTE> BuildGroupsDialogTemplate();

// Devolve a lista do dialogo se ele for o Choose Groups, ou nullptr.
//
// Exposto para teste porque errar aqui significa sequestrar o dialogo errado
// do BodySlide -- ja aconteceu com o de Batch Build.
HWND FindChooseGroupsListBox(HWND dlg);

// Qual das caixas de busca da janela e a de filtro de GRUPOS.
//
// Exposto para teste pelo mesmo motivo: errar aqui escreve os nomes dos grupos
// na caixa errada. O 5.8.2 acrescentou uma segunda wxSearchCtrl irma para
// filtrar outfits, e a escolha por ordem de enumeracao passou a pegar essa.
HWND FindGroupFilterEdit(HWND frame, const std::vector<std::wstring>& knownGroups);
