#include "features/zero_sliders.h"

#include <commctrl.h>

#include <vector>

#include "core/log.h"
#include "features/pose_panel.h"
#include "win32/winfind.h"

namespace {

HWND g_frame = nullptr;
HWND g_posePanel = nullptr;

struct HostCount {
	HWND host = nullptr;
	int sliders = 0;
	bool scrolls = false;
};

std::vector<HWND> VisibleTrackbarChildren(HWND host) {
	std::vector<HWND> out;
	for (HWND child : ChildrenOf(host)) {
		if (_wcsicmp(ClassOf(child).c_str(), TRACKBAR_CLASSW) == 0 && IsWindowVisible(child))
			out.push_back(child);
	}
	return out;
}

// Escreve na barra pelo mesmo caminho de um arrasto de verdade: a posicao entra
// no controle e o dono recebe o WM_HSCROLL. Sem o aviso, o controle mostraria
// zero e o programa continuaria com o valor antigo por dentro.
void WriteSlider(HWND host, HWND slider, int value) {
	SendMessageW(slider, TBM_SETPOS, TRUE, static_cast<LPARAM>(value));
	SendMessageW(host, WM_HSCROLL, MAKEWPARAM(TB_THUMBPOSITION, static_cast<WORD>(value)),
				 reinterpret_cast<LPARAM>(slider));
	SendMessageW(host, WM_HSCROLL, MAKEWPARAM(TB_ENDTRACK, 0), reinterpret_cast<LPARAM>(slider));
}

} // namespace

int ZeroTargetFor(int minimum, int maximum) {
	if (minimum > maximum)
		return minimum;
	if (0 < minimum)
		return minimum;
	if (0 > maximum)
		return maximum;
	return 0;
}

HWND PickSliderHost(HWND frame, HWND exclude) {
	if (!frame)
		return nullptr;

	std::vector<HostCount> hosts;

	for (HWND slider : FindDescendantsByClass(frame, TRACKBAR_CLASSW)) {
		if (!IsWindowVisible(slider))
			continue; // painel recolhido: as barras existem mas ninguem as ve

		HWND host = GetParent(slider);
		if (!host || host == exclude)
			continue;

		bool known = false;
		for (HostCount& entry : hosts) {
			if (entry.host == host) {
				++entry.sliders;
				known = true;
				break;
			}
		}
		if (known)
			continue;

		HostCount entry;
		entry.host = host;
		entry.sliders = 1;
		entry.scrolls = (GetWindowLongW(host, GWL_STYLE) & WS_VSCROLL) != 0;
		hosts.push_back(entry);
	}

	// Painel que rola ganha de painel que nao rola, mesmo com menos barras.
	//
	// E o que separa o painel de sliders dos outros nos dois programas: tanto o
	// SliderScrollWindow do BodySlide quanto o sliderScroll do Outfit Studio
	// rolam, enquanto o painel de luzes e o de recorte, que tambem tem barras,
	// nao rolam. Sem isso, um projeto com poucos sliders perderia a disputa
	// para o painel de luzes aberto.
	const HostCount* best = nullptr;
	for (const HostCount& entry : hosts) {
		if (entry.sliders < 2)
			continue; // uma barra solta e um controle avulso, nao um painel
		if (!best || (entry.scrolls && !best->scrolls) ||
			(entry.scrolls == best->scrolls && entry.sliders > best->sliders))
			best = &entry;
	}

	return best ? best->host : nullptr;
}

namespace ZeroSliders {

bool Install(HWND frame) {
	g_frame = frame;

	// O painel de pose so existe no Outfit Studio; no BodySlide isto devolve
	// nullptr e nada e excluido.
	const PosePanel pose = FindPosePanel(frame);
	g_posePanel = pose.ok ? pose.panel : nullptr;

	LogF("zerar sliders: pronto (painel de pose fora da conta: %p)",
		 static_cast<void*>(g_posePanel));
	return true;
}

void Uninstall() {
	g_frame = nullptr;
	g_posePanel = nullptr;
}

bool Run() {
	// O painel e procurado a cada uso, e nao guardado na instalacao: ele so
	// existe depois que um outfit e carregado, e e refeito a cada troca de
	// outfit ou de preset.
	HWND host = PickSliderHost(g_frame, g_posePanel);
	if (!host) {
		LogF("zerar sliders: nenhum painel de sliders na tela, tecla ignorada");
		return false;
	}

	const std::vector<HWND> sliders = VisibleTrackbarChildren(host);
	int changed = 0;

	// Um painel cheio sao dezenas de barras, e cada uma redesenha ao mudar.
	// Sem segurar o desenho, zerar tudo vira uma cascata visivel de barras
	// caindo uma a uma.
	SendMessageW(host, WM_SETREDRAW, FALSE, 0);

	for (HWND slider : sliders) {
		const int minimum = static_cast<int>(SendMessageW(slider, TBM_GETRANGEMIN, 0, 0));
		const int maximum = static_cast<int>(SendMessageW(slider, TBM_GETRANGEMAX, 0, 0));
		const int target = ZeroTargetFor(minimum, maximum);

		if (static_cast<int>(SendMessageW(slider, TBM_GETPOS, 0, 0)) == target)
			continue; // ja esta la; avisar de novo so gastaria um recalculo

		WriteSlider(host, slider, target);
		++changed;
	}

	SendMessageW(host, WM_SETREDRAW, TRUE, 0);
	if (changed > 0)
		RedrawWindow(host, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);

	LogF("zerar sliders: painel %p, %d barras, %d zeradas",
		 static_cast<void*>(host), static_cast<int>(sliders.size()), changed);
	return true;
}

} // namespace ZeroSliders
