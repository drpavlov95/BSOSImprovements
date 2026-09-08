// Como o nome do osso do outro lado e deduzido, e como os sinais viram.
#include "test_util.h"

#include <commctrl.h>

#include "features/mirror_pose.h"
#include "features/pose_panel.h"

TEST(MirrorsSkyrimBoneNames) {
	// A convencao do esqueleto do Skyrim: o lado aparece duas vezes, solto e
	// dentro do apelido entre colchetes. As duas tem que virar juntas, senao o
	// nome resultante nao existe na lista.
	TEST_ASSERT(MirrorBoneName(L"NPC L Thigh [LThg]") == L"NPC R Thigh [RThg]");
	TEST_ASSERT(MirrorBoneName(L"NPC R Thigh [RThg]") == L"NPC L Thigh [LThg]");
	TEST_ASSERT(MirrorBoneName(L"NPC L Clavicle [LClv]") == L"NPC R Clavicle [RClv]");
	TEST_ASSERT(MirrorBoneName(L"NPC L Finger01 [LF01]") == L"NPC R Finger01 [RF01]");
	TEST_ASSERT(MirrorBoneName(L"NPC L Forearm [LLar]") == L"NPC R Forearm [RLar]");

	// Ossos de corpo de mod, que costumam vir sem colchetes.
	TEST_ASSERT(MirrorBoneName(L"L Breast01") == L"R Breast01");
	TEST_ASSERT(MirrorBoneName(L"NPC L Breast") == L"NPC R Breast");

	// Lado colado no comeco.
	TEST_ASSERT(MirrorBoneName(L"LArm") == L"RArm");
	TEST_ASSERT(MirrorBoneName(L"RThigh") == L"LThigh");

	// Convencao de quem exporta do Blender.
	TEST_ASSERT(MirrorBoneName(L"Thigh_L") == L"Thigh_R");
	TEST_ASSERT(MirrorBoneName(L"Thigh.R") == L"Thigh.L");
	TEST_ASSERT(MirrorBoneName(L"Hand-L") == L"Hand-R");
	TEST_ASSERT(MirrorBoneName(L"NPC_L_Thigh") == L"NPC_R_Thigh");

	// Por extenso.
	TEST_ASSERT(MirrorBoneName(L"Left Hand") == L"Right Hand");
	TEST_ASSERT(MirrorBoneName(L"Right Hand") == L"Left Hand");
	return true;
}

TEST(LeavesCenterBonesAlone) {
	// Osso sem lado devolve vazio, e a feature nao faz nada. E o caso mais
	// importante: inventar um lado aqui escreveria pose num osso errado.
	TEST_ASSERT(MirrorBoneName(L"NPC Root [Root]").empty());
	TEST_ASSERT(MirrorBoneName(L"NPC Spine [Spn0]").empty());
	TEST_ASSERT(MirrorBoneName(L"NPC Head [Head]").empty());
	TEST_ASSERT(MirrorBoneName(L"NPC Pelvis [Pelv]").empty());
	TEST_ASSERT(MirrorBoneName(L"NPC Neck [Neck]").empty());
	TEST_ASSERT(MirrorBoneName(L"CME Body [Bdy]").empty());
	TEST_ASSERT(MirrorBoneName(L"").empty());

	// O ponto fino: "Root" comeca com R, e "[Root]" tambem. So a exigencia de
	// maiuscula ou digito logo depois impede "Loot".
	TEST_ASSERT(MirrorBoneName(L"Root").empty());
	TEST_ASSERT(MirrorBoneName(L"[Root]").empty());

	// "Lower" comeca com L pelo mesmo motivo.
	TEST_ASSERT(MirrorBoneName(L"Lower").empty());
	return true;
}

TEST(MirrorRoundTripsBackToTheOriginal) {
	// Espelhar duas vezes tem que voltar ao mesmo nome. Se nao voltar, alguma
	// regra esta consumindo o lado em vez de troca-lo.
	const wchar_t* names[] = {
		L"NPC L Thigh [LThg]",
		L"NPC R Hand [RHnd]",
		L"L Breast01",
		L"LArm",
		L"Thigh_L",
		L"Left Hand",
		L"NPC_L_Thigh",
	};

	for (const wchar_t* name : names) {
		const std::wstring once = MirrorBoneName(name);
		TEST_ASSERT(!once.empty());
		TEST_ASSERT(MirrorBoneName(once) == name);
	}
	return true;
}

TEST(AppliesTheConfiguredSigns) {
	const int in[7] = {10, 20, 30, 40, 50, 60, 70};
	int out[7] = {};

	// Defaults: nega rotacao Y e Z, e deslocamento X.
	MirrorPoseSigns defaults;
	MirrorValues(in, out, defaults);
	TEST_ASSERT(out[0] == 10);  // rotacao X mantem
	TEST_ASSERT(out[1] == -20); // rotacao Y nega
	TEST_ASSERT(out[2] == -30); // rotacao Z nega
	TEST_ASSERT(out[3] == -40); // deslocamento X nega
	TEST_ASSERT(out[4] == 50);
	TEST_ASSERT(out[5] == 60);
	TEST_ASSERT(out[6] == 70); // escala mantem

	// A tabela e configuravel justamente porque a convencao muda de esqueleto
	// para esqueleto: com tudo desligado, espelhar copia igual.
	MirrorPoseSigns none;
	none.rotationY = false;
	none.rotationZ = false;
	none.offsetX = false;
	MirrorValues(in, out, none);
	for (int i = 0; i < 7; ++i)
		TEST_ASSERT(out[i] == in[i]);

	// Zero espelhado continua zero, nos dois sentidos.
	const int zeros[7] = {};
	MirrorValues(zeros, out, defaults);
	for (int i = 0; i < 7; ++i)
		TEST_ASSERT(out[i] == 0);
	return true;
}

namespace {

HWND MakePosePanelWindow(std::vector<HWND>& sliders, std::vector<HWND>& texts) {
	static bool registered = false;
	if (!registered) {
		WNDCLASSW wc = {};
		wc.lpfnWndProc = DefWindowProcW;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.lpszClassName = L"BSOSPoseHost";
		RegisterClassW(&wc);
		registered = true;
	}

	INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_BAR_CLASSES};
	InitCommonControlsEx(&icc);

	HWND panel = CreateWindowExW(0, L"BSOSPoseHost", L"pose", WS_OVERLAPPEDWINDOW,
								 0, 0, 300, 400, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
	if (!panel)
		return nullptr;

	// De proposito fora de ordem vertical: a linha de baixo e criada primeiro.
	// E exatamente o caso que a ordenacao por posicao existe para cobrir --
	// casar por ordem de enumeracao escreveria a escala no campo da rotacao.
	for (int row = 6; row >= 0; --row) {
		const int y = 10 + row * 30;
		sliders.push_back(CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE,
										  10, y, 150, 20, panel, nullptr,
										  GetModuleHandleW(nullptr), nullptr));
		texts.push_back(CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE,
										170, y, 40, 20, panel, nullptr,
										GetModuleHandleW(nullptr), nullptr));
	}
	return panel;
}

} // namespace

TEST(PairsPoseRowsTopToBottom) {
	std::vector<HWND> sliders;
	std::vector<HWND> texts;
	HWND panel = MakePosePanelWindow(sliders, texts);
	TEST_ASSERT(panel != nullptr);
	TEST_ASSERT(sliders.size() == 7);

	std::vector<PoseRow> rows = PairPoseRows(sliders, texts);
	TEST_ASSERT(rows.size() == 7);

	// De cima para baixo, sempre.
	for (size_t i = 1; i < rows.size(); ++i)
		TEST_ASSERT(rows[i - 1].top <= rows[i].top);

	// E a barra de cada linha tem que estar na mesma altura do campo dela.
	for (const PoseRow& row : rows) {
		RECT slider = {};
		RECT text = {};
		GetWindowRect(row.slider, &slider);
		GetWindowRect(row.text, &text);
		TEST_ASSERT(slider.top == text.top);
	}

	// Listas de tamanhos diferentes nao produzem par nenhum: emparelhar o que
	// sobrou associaria a barra errada ao campo errado.
	std::vector<HWND> shorter(texts.begin(), texts.end() - 1);
	TEST_ASSERT(PairPoseRows(sliders, shorter).empty());

	DestroyWindow(panel);
	return true;
}

TEST(FindsThePosePanelByItsShape) {
	std::vector<HWND> sliders;
	std::vector<HWND> texts;
	HWND panel = MakePosePanelWindow(sliders, texts);
	TEST_ASSERT(panel != nullptr);

	// Sem a lista de ossos o painel nao serve: e dela que sai o nome do osso.
	PosePanel without = FindPosePanel(panel);
	TEST_ASSERT(!without.ok);

	// wxComboBox editavel (nome da pose) e wxChoice (lista de ossos) sao os
	// dois "ComboBox" no Windows. Tem que escolher o segundo.
	HWND poseName = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWN,
									10, 220, 100, 100, panel, nullptr,
									GetModuleHandleW(nullptr), nullptr);
	HWND boneList = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
									10, 250, 100, 100, panel, nullptr,
									GetModuleHandleW(nullptr), nullptr);
	TEST_ASSERT(poseName != nullptr && boneList != nullptr);

	// FindPosePanel procura entre os DESCENDENTES, entao o painel precisa de
	// um pai para ser encontrado.
	HWND frame = CreateWindowExW(0, L"BSOSPoseHost", L"frame", WS_OVERLAPPEDWINDOW,
								 0, 0, 400, 500, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
	TEST_ASSERT(frame != nullptr);
	SetParent(panel, frame);

	PosePanel found = FindPosePanel(frame);
	TEST_ASSERT(found.ok);
	TEST_ASSERT(found.panel == panel);
	TEST_ASSERT(found.boneChoice == boneList);

	// As sete linhas saem em ordem vertical.
	for (int i = 1; i < 7; ++i) {
		RECT above = {};
		RECT below = {};
		GetWindowRect(found.sliders[i - 1], &above);
		GetWindowRect(found.sliders[i], &below);
		TEST_ASSERT(above.top < below.top);
	}

	DestroyWindow(frame);
	return true;
}
