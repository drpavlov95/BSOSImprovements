#include "features/pose_panel.h"

#include <commctrl.h>

#include <algorithm>

#include "core/log.h"
#include "win32/winfind.h"

namespace {

const int kPoseRows = 7;

void CollectAll(HWND root, std::vector<HWND>& out, int depth) {
	// Teto de profundidade: a arvore de janelas do wx e funda, e uma recursao
	// sem limite aqui rodaria em cima de qualquer arvore estranha que aparecer.
	if (depth > 12)
		return;
	for (HWND child : ChildrenOf(root)) {
		out.push_back(child);
		CollectAll(child, out, depth + 1);
	}
}

int TopOf(HWND window) {
	RECT rc = {};
	GetWindowRect(window, &rc);
	return static_cast<int>(rc.top);
}

// O wxChoice do Windows e um ComboBox sem campo de digitacao. O wxComboBox de
// nome de pose, que fica no mesmo painel, tem um -- e escrever nele em vez de
// na lista de ossos trocaria o nome da pose do usuario.
bool IsDropDownList(HWND combo) {
	const LONG style = GetWindowLongW(combo, GWL_STYLE);
	return (style & 0x3) == CBS_DROPDOWNLIST;
}

} // namespace

std::vector<PoseRow> PairPoseRows(const std::vector<HWND>& sliders, const std::vector<HWND>& texts) {
	std::vector<PoseRow> rows;
	if (sliders.size() != texts.size())
		return rows;

	std::vector<HWND> orderedSliders = sliders;
	std::vector<HWND> orderedTexts = texts;
	auto byTop = [](HWND a, HWND b) { return TopOf(a) < TopOf(b); };
	std::sort(orderedSliders.begin(), orderedSliders.end(), byTop);
	std::sort(orderedTexts.begin(), orderedTexts.end(), byTop);

	for (size_t i = 0; i < orderedSliders.size(); ++i) {
		PoseRow row;
		row.slider = orderedSliders[i];
		row.text = orderedTexts[i];
		row.top = TopOf(orderedSliders[i]);
		rows.push_back(row);
	}
	return rows;
}

PosePanel FindPosePanel(HWND frame) {
	PosePanel out;
	if (!frame)
		return out;

	std::vector<HWND> all;
	CollectAll(frame, all, 0);

	for (HWND candidate : all) {
		std::vector<HWND> sliders;
		std::vector<HWND> texts;
		std::vector<HWND> combos;

		// So filhos DIRETOS. Contando descendentes, qualquer painel acima
		// deste na arvore casaria com a mesma assinatura.
		for (HWND child : ChildrenOf(candidate)) {
			const std::wstring cls = ClassOf(child);
			if (_wcsicmp(cls.c_str(), TRACKBAR_CLASSW) == 0)
				sliders.push_back(child);
			else if (_wcsicmp(cls.c_str(), L"Edit") == 0)
				texts.push_back(child);
			else if (_wcsicmp(cls.c_str(), L"ComboBox") == 0)
				combos.push_back(child);
		}

		if (sliders.size() != kPoseRows || texts.size() != kPoseRows)
			continue;

		const std::vector<PoseRow> rows = PairPoseRows(sliders, texts);
		if (rows.size() != kPoseRows)
			continue;

		for (HWND combo : combos) {
			if (IsDropDownList(combo)) {
				out.boneChoice = combo;
				break;
			}
		}

		for (HWND child : ChildrenOf(candidate)) {
			if (_wcsicmp(ClassOf(child).c_str(), L"Button") != 0)
				continue;
			const LONG style = GetWindowLongW(child, GWL_STYLE);
			const LONG type = style & BS_TYPEMASK;
			// wxMSW pode criar wxCheckBox como BS_CHECKBOX ou
			// BS_AUTOCHECKBOX, conforme a versao/tema do wxWidgets. Os dois
			// representam o Show Pose e ambos sao a ancora valida.
			if (type == BS_CHECKBOX || type == BS_AUTOCHECKBOX) {
				out.showPose = child;
				break;
			}
		}

		if (!out.boneChoice) {
			LogF("pose: achei o painel %p mas nenhuma lista de ossos dentro dele",
				 static_cast<void*>(candidate));
			continue;
		}
		if (!out.showPose) {
			LogF("pose: achei o painel %p mas nao a caixa Show Pose",
				 static_cast<void*>(candidate));
			continue;
		}

		out.panel = candidate;
		for (int i = 0; i < kPoseRows; ++i) {
			out.sliders[i] = rows[static_cast<size_t>(i)].slider;
			out.texts[i] = rows[static_cast<size_t>(i)].text;
		}
		out.ok = true;

		LogF("pose: painel %p, lista de ossos %p, primeira linha em y=%d",
			 static_cast<void*>(out.panel), static_cast<void*>(out.boneChoice), rows[0].top);
		return out;
	}

	// Esperado no BodySlide, que nao tem pose nenhum. No Outfit Studio isto
	// significa que o mirror de pose nao vai funcionar.
	LogF("pose: nenhum painel com sete barras e sete campos");
	return out;
}
