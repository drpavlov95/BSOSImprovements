// Resolucao de itens de menu por posicao.
#include "test_util.h"

#include "xrcmap.h"

namespace {

std::wstring TempFile(const wchar_t* name) {
	wchar_t dir[MAX_PATH] = {};
	GetTempPathW(MAX_PATH, dir);
	return std::wstring(dir) + name;
}

bool WriteText(const std::wstring& path, const char* content) {
	HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	DWORD written = 0;
	BOOL ok = WriteFile(h, content, static_cast<DWORD>(strlen(content)), &written, nullptr);
	CloseHandle(h);
	return ok != FALSE;
}

} // namespace

// O XRC e um arquivo do proprio BodySlide, que e GPL-3.0, entao ele nao mora
// neste repositorio. Para rodar este teste, copie
//   CalienteTools\BodySlide\res\xrc\OutfitStudio.xrc
// da sua instalacao para tests\data\. Sem ele o teste se declara pulado em vez
// de falhar -- os outros testes do xrcmap usam fixtures proprias e cobrem as
// regras de parsing.
TEST(ResolvesRealOutfitStudioMenuPaths) {
	const wchar_t* xrc = L"tests\\data\\OutfitStudio.xrc";

	if (GetFileAttributesW(xrc) == INVALID_FILE_ATTRIBUTES) {
		std::printf("\n      PULADO: copie OutfitStudio.xrc para tests\\data\\ para rodar\n      ");
		return true;
	}

	MenuPath imp = ResolveMenuPath(xrc, "sliderImportOBJ");
	MenuPath exp = ResolveMenuPath(xrc, "sliderExportOBJ");

	// menubar > ... > submenu Import/Export > item OBJ.
	//
	// A profundidade NAO e fixada aqui de proposito. No 5.6.3 o caminho tinha 3
	// niveis; o 5.8.2 enfiou um submenu "menuSliderImportGroup" no meio e passou
	// a ter 4. Isso nao quebra o mod -- ContainerOf() desce path.size()-1 niveis,
	// seja qual for -- entao cravar o numero aqui so gera falso alarme a cada
	// versao. O que precisa valer e a relacao entre os caminhos.
	TEST_ASSERT(imp.size() >= 2);
	TEST_ASSERT(exp.size() >= 2);
	TEST_ASSERT(imp[0] == exp[0]); // mesmo menu de topo
	TEST_ASSERT(imp != exp);
	TEST_ASSERT(imp.back() == 2); // Import OBJ e o 3o do submenu (NIF, BSD, OBJ)
	TEST_ASSERT(exp.back() == 2);

	// O submenu pai tambem precisa resolver: e dele que se le o estado enabled,
	// que e o reflexo direto do bEditSlider. Ele tem que ser exatamente o
	// caminho do item menos o ultimo indice -- se nao for, IsSliderEditModeActive
	// estaria lendo o estado de outro menu qualquer.
	MenuPath impMenu = ResolveMenuPath(xrc, "menuImportSlider");
	MenuPath expMenu = ResolveMenuPath(xrc, "menuExportSlider");
	TEST_ASSERT(impMenu == MenuPath(imp.begin(), imp.end() - 1));
	TEST_ASSERT(expMenu == MenuPath(exp.begin(), exp.end() - 1));

	// Comandos do brush resize e do remap.
	MenuPath inc = ResolveMenuPath(xrc, "btnIncreaseSize");
	MenuPath dec = ResolveMenuPath(xrc, "btnDecreaseSize");
	MenuPath transform = ResolveMenuPath(xrc, "btnTransform");
	MenuPath normals = ResolveMenuPath(xrc, "btnRecalcNormals");
	TEST_ASSERT(!inc.empty() && !dec.empty());
	TEST_ASSERT(!transform.empty() && !normals.empty());
	TEST_ASSERT(inc != dec);

	TEST_ASSERT(ResolveMenuPath(xrc, "naoExisteEsseNome").empty());
	TEST_ASSERT(ResolveMenuPath(L"arquivo_que_falta.xrc", "sliderImportOBJ").empty());
	return true;
}

TEST(CountsSeparatorsAsPositions) {
	// Separadores ocupam posicao no HMENU nativo. Se o parser nao contasse,
	// todo indice depois do primeiro separador sairia deslocado -- e o mod
	// dispararia o comando errado.
	std::wstring path = TempFile(L"bsos_sep.xrc");
	TEST_ASSERT(WriteText(path,
						  "<resource><object class='wxMenuBar' name='bar'>"
						  "<object class='wxMenu' name='m'>"
						  "  <label>Menu</label>"
						  "  <object class='wxMenuItem' name='i0'><label>Zero</label></object>"
						  "  <object class='separator'/>"
						  "  <object class='wxMenuItem' name='i2'><label>Dois</label></object>"
						  "</object></object></resource>"));

	MenuPath p0 = ResolveMenuPath(path.c_str(), "i0");
	MenuPath p2 = ResolveMenuPath(path.c_str(), "i2");
	TEST_ASSERT(p0.size() == 2 && p0[0] == 0 && p0[1] == 0);
	TEST_ASSERT(p2.size() == 2 && p2[0] == 0 && p2[1] == 2);

	DeleteFileW(path.c_str());
	return true;
}

TEST(IgnoresObjectsBeforeTheMenuBar) {
	// A toolbar do Outfit Studio tem um "tool" com o mesmo name de um item de
	// menu (btnTransform). O parser precisa comecar a contar so no menubar,
	// senao devolveria um caminho de toolbar que nao existe no HMENU.
	std::wstring path = TempFile(L"bsos_toolbar.xrc");
	TEST_ASSERT(WriteText(path,
						  "<resource><object class='wxFrame' name='f'>"
						  "<object class='wxToolBar' name='tb'>"
						  "  <object class='tool' name='btnDup'><label>Dup</label></object>"
						  "</object>"
						  "<object class='wxMenuBar' name='bar'>"
						  "<object class='wxMenu' name='m0'><label>A</label></object>"
						  "<object class='wxMenu' name='m1'>"
						  "  <label>B</label>"
						  "  <object class='wxMenuItem' name='btnDup'><label>Dup</label></object>"
						  "</object>"
						  "</object></object></resource>"));

	MenuPath p = ResolveMenuPath(path.c_str(), "btnDup");
	TEST_ASSERT(p.size() == 2);
	TEST_ASSERT(p[0] == 1 && p[1] == 0);

	DeleteFileW(path.c_str());
	return true;
}
