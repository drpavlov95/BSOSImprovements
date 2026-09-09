// Reordenacao dos blocos <Slider> dentro de um .osp.
#include "test_util.h"

#include <string>

#include "core/osp.h"

namespace {

// Um .osp reduzido, com a mesma forma do arquivo de verdade: BOM, cabecalho,
// shapes, e os sliders com <Data> dentro.
const char* kOsp =
	"\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	"<SliderSetInfo version=\"1\">\n"
	"    <SliderSet name=\"Corpo\">\n"
	"        <DataFolder>Pasta</DataFolder>\n"
	"        <Shape target=\"CBBE\">CBBE</Shape>\n"
	"        <Slider name=\"Alfa\" invert=\"false\" small=\"0\" big=\"0\">\n"
	"            <Data name=\"CBBEAlfa\" target=\"CBBE\">arquivo\\CBBEAlfa</Data>\n"
	"        </Slider>\n"
	"        <Slider name=\"Beta\" invert=\"false\" small=\"100\" big=\"0\">\n"
	"            <Data name=\"CBBEBeta\" target=\"CBBE\">arquivo\\CBBEBeta</Data>\n"
	"            <Data name=\"PantsBeta\" target=\"Pants\">arquivo\\PantsBeta</Data>\n"
	"        </Slider>\n"
	"        <Slider name=\"Gama\" invert=\"true\" small=\"0\" big=\"100\" uv=\"true\"/>\n"
	"    </SliderSet>\n"
	"</SliderSetInfo>\n";

size_t PositionOf(const std::string& text, const std::string& needle) {
	return text.find(needle);
}

} // namespace

TEST(ReadsTheSliderOrderOutOfAnOsp) {
	const std::vector<std::string> order = ReadOspSliderOrder(kOsp, "Corpo");
	TEST_ASSERT(order.size() == 3);
	TEST_ASSERT(order[0] == "Alfa");
	TEST_ASSERT(order[1] == "Beta");
	TEST_ASSERT(order[2] == "Gama");

	// "<SliderSet" tambem comeca com "<Slider". Sem checar o caractere
	// seguinte, o conjunto inteiro entraria na lista como se fosse um slider.
	for (const std::string& name : order)
		TEST_ASSERT(name != "Corpo");

	// Conjunto que nao existe devolve vazio, e nao o primeiro que aparecer.
	TEST_ASSERT(ReadOspSliderOrder(kOsp, "Outro").empty());
	TEST_ASSERT(ReadOspSliderOrder("", "Corpo").empty());
	return true;
}

TEST(ReordersTheBlocksAndLeavesTheRestAlone) {
	const std::string out = ReorderOspSliders(kOsp, "Corpo", {"Gama", "Alfa", "Beta"});
	TEST_ASSERT(!out.empty());

	// A ordem nova esta la.
	const std::vector<std::string> after = ReadOspSliderOrder(out, "Corpo");
	TEST_ASSERT(after.size() == 3);
	TEST_ASSERT(after[0] == "Gama");
	TEST_ASSERT(after[1] == "Alfa");
	TEST_ASSERT(after[2] == "Beta");

	// Os <Data> viajaram junto com o slider deles. Se ficassem para tras, o
	// arquivo continuaria valido e o projeto sairia com a morfologia trocada --
	// o pior tipo de estrago, o silencioso.
	TEST_ASSERT(PositionOf(out, "CBBEAlfa") < PositionOf(out, "CBBEBeta"));
	TEST_ASSERT(PositionOf(out, "PantsBeta") > PositionOf(out, "CBBEBeta"));
	TEST_ASSERT(PositionOf(out, "CBBEAlfa") > PositionOf(out, "name=\"Gama\""));

	// Tudo que nao e slider fica intacto, inclusive o BOM e o cabecalho.
	TEST_ASSERT(out.compare(0, 3, "\xEF\xBB\xBF") == 0);
	TEST_ASSERT(out.find("<DataFolder>Pasta</DataFolder>") != std::string::npos);
	TEST_ASSERT(out.find("<Shape target=\"CBBE\">CBBE</Shape>") != std::string::npos);
	TEST_ASSERT(out.find("</SliderSetInfo>") != std::string::npos);

	// E o tamanho e o mesmo: nada foi perdido nem duplicado, so movido.
	TEST_ASSERT(out.size() == std::string(kOsp).size());

	// A indentacao viaja com o bloco.
	TEST_ASSERT(out.find("        <Slider name=\"Gama\"") != std::string::npos);
	TEST_ASSERT(out.find("            <Data name=\"CBBEAlfa\"") != std::string::npos);
	return true;
}

TEST(ReorderingToTheSameOrderChangesNothing) {
	// Salvar sem ter arrastado nada nao pode mexer um byte do arquivo do
	// usuario.
	const std::string out = ReorderOspSliders(kOsp, "Corpo", {"Alfa", "Beta", "Gama"});
	TEST_ASSERT(out == kOsp);
	return true;
}

TEST(RefusesInsteadOfGuessing) {
	// Um nome a mais faria um slider sumir do arquivo; um a menos duplicaria
	// outro. Nos dois casos o usuario perde trabalho sem ser avisado, entao a
	// resposta e nao mexer.
	TEST_ASSERT(ReorderOspSliders(kOsp, "Corpo", {"Alfa", "Beta"}).empty());
	TEST_ASSERT(ReorderOspSliders(kOsp, "Corpo", {"Alfa", "Beta", "Gama", "Delta"}).empty());
	TEST_ASSERT(ReorderOspSliders(kOsp, "Corpo", {"Alfa", "Beta", "Delta"}).empty());
	TEST_ASSERT(ReorderOspSliders(kOsp, "Corpo", {"Alfa", "Alfa", "Beta"}).empty());
	TEST_ASSERT(ReorderOspSliders(kOsp, "Corpo", {}).empty());

	// Conjunto que nao existe.
	TEST_ASSERT(ReorderOspSliders(kOsp, "Outro", {"Alfa", "Beta", "Gama"}).empty());

	// Arquivo numa linha so: os intervalos de linha se cruzariam, e trocar
	// pedacos cruzados de lugar destruiria o texto.
	const std::string oneLine =
		"<SliderSetInfo><SliderSet name=\"Corpo\">"
		"<Slider name=\"Alfa\"></Slider><Slider name=\"Beta\"></Slider>"
		"</SliderSet></SliderSetInfo>";
	TEST_ASSERT(ReorderOspSliders(oneLine, "Corpo", {"Beta", "Alfa"}).empty());
	return true;
}

TEST(TouchesOnlyTheNamedSliderSet) {
	// Um .osp costuma trazer varios conjuntos -- corpo, calca, bota -- e
	// reordenar um nao pode mexer nos outros.
	const std::string two =
		"<SliderSetInfo version=\"1\">\n"
		"    <SliderSet name=\"Um\">\n"
		"        <Slider name=\"A\"/>\n"
		"        <Slider name=\"B\"/>\n"
		"    </SliderSet>\n"
		"    <SliderSet name=\"Dois\">\n"
		"        <Slider name=\"C\"/>\n"
		"        <Slider name=\"D\"/>\n"
		"    </SliderSet>\n"
		"</SliderSetInfo>\n";

	const std::string out = ReorderOspSliders(two, "Dois", {"D", "C"});
	TEST_ASSERT(!out.empty());

	const std::vector<std::string> first = ReadOspSliderOrder(out, "Um");
	TEST_ASSERT(first.size() == 2 && first[0] == "A" && first[1] == "B");

	const std::vector<std::string> second = ReadOspSliderOrder(out, "Dois");
	TEST_ASSERT(second.size() == 2 && second[0] == "D" && second[1] == "C");

	// E o conjunto de cima segue antes do de baixo: os blocos nao pularam de
	// conjunto.
	TEST_ASSERT(PositionOf(out, "name=\"Um\"") < PositionOf(out, "name=\"Dois\""));
	TEST_ASSERT(PositionOf(out, "name=\"A\"") < PositionOf(out, "name=\"C\""));
	return true;
}
