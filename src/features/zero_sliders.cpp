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

bool IsUnder(HWND window, HWND ancestor) {
	if (!ancestor)
		return false;
	for (HWND walk = window; walk; walk = GetParent(walk)) {
		if (walk == ancestor)
			return true;
	}
	return false;
}

// Todas as barras debaixo do painel, e nao so as filhas diretas: no Outfit
// Studio cada linha e um painel proprio e a barra fica um nivel abaixo.
std::vector<HWND> TrackbarsUnder(HWND host, HWND exclude) {
	std::vector<HWND> out;
	for (HWND slider : FindDescendantsByClass(host, TRACKBAR_CLASSW)) {
		// Se o painel escolhido por acaso contiver o de pose, as sete barras
		// dele continuam de fora: zera-las apagaria a pose do usuario.
		if (!IsUnder(slider, exclude))
			out.push_back(slider);
	}
	return out;
}

// Por que nao houve painel, em detalhe.
//
// Um "nao achei" sozinho nao distingue as tres causas possiveis -- nenhuma
// barra na arvore, todas em painel escondido, ou nenhum painel com duas -- e
// foi exatamente essa ambiguidade que fez a primeira versao parecer que a
// tecla nao chegava.
void LogWhyNothingWasFound(HWND frame, HWND exclude) {
	const std::vector<HWND> all = FindDescendantsByClass(frame, TRACKBAR_CLASSW);

	int visibleSelf = 0;
	int visibleHost = 0;
	int excluded = 0;
	for (HWND slider : all) {
		if (IsWindowVisible(slider))
			++visibleSelf;
		HWND host = GetParent(slider);
		if (host == exclude)
			++excluded;
		else if (host && IsWindowVisible(host))
			++visibleHost;
	}

	LogF("zerar sliders: %d barras na arvore -- %d visiveis por si, %d com painel visivel, %d no painel de pose",
		 static_cast<int>(all.size()), visibleSelf, visibleHost, excluded);
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

namespace {

// O ancestral `levels` niveis acima, ou nullptr se a arvore acabar antes.
HWND AncestorOf(HWND window, int levels) {
	for (int i = 0; i < levels && window; ++i)
		window = GetParent(window);
	return window;
}

// Procura os paineis num nivel so.
HWND PickAtLevel(HWND frame, HWND exclude, int level) {
	std::vector<HostCount> hosts;

	for (HWND slider : FindDescendantsByClass(frame, TRACKBAR_CLASSW)) {
		HWND host = AncestorOf(slider, level);
		if (!host || host == exclude || host == frame)
			continue;

		// A visibilidade e perguntada ao PAINEL, nao a barra.
		//
		// A primeira versao perguntava a barra e nao achava nada: das
		// quarenta e quatro barras do Outfit Studio, o dump mostrou UMA
		// respondendo visivel -- a de Field of View, que mora na barra de
		// ferramentas. As do painel de sliders respondem que nao, mesmo
		// desenhadas na tela.
		//
		// Perguntar ao painel serve igual para o que aquela regra queria
		// resolver: painel recolhido esta escondido, e as barras dentro dele
		// saem da conta junto.
		if (!IsWindowVisible(host))
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

} // namespace

HWND PickSliderHost(HWND frame, HWND exclude) {
	if (!frame)
		return nullptr;

	// Sobe de nivel ate achar um painel que junte barras.
	//
	// No BodySlide as barras sao filhas diretas da area que rola, e o nivel 1
	// resolve. No Outfit Studio NAO: cada linha de slider e um painel proprio,
	// com o lapis, a caixa, o nome, a barra e a porcentagem dentro. Ali cada
	// barra esta sozinha no pai dela, e agrupar por pai direto dava cento e
	// trinta e um paineis de uma barra cada -- nenhum com as duas que a regra
	// exige. Era por isso que a tecla nao achava nada.
	for (int level = 1; level <= 4; ++level) {
		if (HWND host = PickAtLevel(frame, exclude, level))
			return host;
	}
	return nullptr;
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
		LogWhyNothingWasFound(g_frame, g_posePanel);
		return false;
	}

	const std::vector<HWND> sliders = TrackbarsUnder(host, g_posePanel);
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

		// O aviso vai para o PAI da barra, que e quem o trata. Com cada linha
		// num painel proprio, mandar para o painel de cima nao chegaria a
		// ninguem.
		WriteSlider(GetParent(slider), slider, target);
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
