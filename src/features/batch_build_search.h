#pragma once

#include <windows.h>

#include <string>
#include <vector>

// Acrescenta uma caixa de busca ao dialogo "Batch Build" do BodySlide.
//
// Diferente do Choose Groups, aqui o dialogo NAO e substituido. O resultado do
// Batch Build e lido de dentro do wx (batchBuildList->GetCheckedItems), e o
// estado das caixinhas de um wxCheckListBox nao e legivel nem gravavel de
// fora -- o wx desenha e guarda por conta propria. Substituir o dialogo
// deixaria a selecao sem como voltar.
//
// Por isso a lista original continua ali, intacta: a busca so rola ate os
// itens que casam, e o "Toggle matching" alterna a marcacao simulando a tecla
// que o proprio wx trata, o que mantem o estado interno consistente.
namespace BatchBuildSearch {

bool Install(HWND frame);
void Uninstall();

} // namespace BatchBuildSearch

// Devolve a lista do dialogo se ele for o Batch Build, ou nullptr.
//
// Exposto para teste: dlgBatchBuild e o Choose Groups tem exatamente a mesma
// estrutura -- uma lista de marcacao e dois botoes -- e ja houve um bug de um
// ser confundido com o outro.
HWND FindBatchBuildListBox(HWND dlg);

// Indices dos itens que casam com a consulta, em ordem.
std::vector<int> MatchingItems(const std::vector<std::wstring>& items, const std::wstring& query);
