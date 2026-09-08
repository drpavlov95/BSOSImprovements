// Como o valor de <AppearanceMode> vira modo claro ou escuro.
#include "test_util.h"

#include "core/theme.h"

TEST(ResolvesAppearanceLikeBodySlide) {
	// "Dark" e escuro, independente do Windows.
	TEST_ASSERT(ResolveAppearance("Dark", false) == Appearance::Dark);
	TEST_ASSERT(ResolveAppearance("Dark", true) == Appearance::Dark);

	// "Light" e claro, independente do Windows.
	TEST_ASSERT(ResolveAppearance("Light", true) == Appearance::Light);

	// So "System" olha para o tema do Windows.
	TEST_ASSERT(ResolveAppearance("System", true) == Appearance::Dark);
	TEST_ASSERT(ResolveAppearance("System", false) == Appearance::Light);

	// O default do BodySlide e "Light", NAO "System": a funcao dele testa
	// "System" e "Dark" e devolve Light para todo o resto. Chave ausente,
	// vazia ou com lixo tem que cair em claro -- errar aqui deixaria o
	// dialogo escuro dentro de um BodySlide claro, que e pior que o contrario.
	TEST_ASSERT(ResolveAppearance("", true) == Appearance::Light);
	TEST_ASSERT(ResolveAppearance("   ", true) == Appearance::Light);
	TEST_ASSERT(ResolveAppearance("Escuro", true) == Appearance::Light);
	TEST_ASSERT(ResolveAppearance("system-ish", true) == Appearance::Light);

	// O BodySlide compara com CmpNoCase, entao a caixa nao importa.
	TEST_ASSERT(ResolveAppearance("dark", false) == Appearance::Dark);
	TEST_ASSERT(ResolveAppearance("DARK", false) == Appearance::Dark);
	TEST_ASSERT(ResolveAppearance("system", true) == Appearance::Dark);

	// O valor vem de dentro de uma tag XML, entao pode vir com espaco em volta.
	TEST_ASSERT(ResolveAppearance("  Dark  ", false) == Appearance::Dark);
	TEST_ASSERT(ResolveAppearance("\n\tSystem\n", true) == Appearance::Dark);

	// Prefixo nao conta: "Darker" nao e "Dark".
	TEST_ASSERT(ResolveAppearance("Darker", false) == Appearance::Light);
	return true;
}

TEST(FallsBackToLightWithoutConfig) {
	// Sem Config.xml legivel nao da para saber, e claro e o default do
	// BodySlide -- alem de ser o que a maioria das instalacoes usa.
	TEST_ASSERT(DetectAppearance(L"Z:\\pasta\\que\\nao\\existe\\") == Appearance::Light);

	// Os pinceis sao os mesmos objetos a cada chamada. Criar um por
	// WM_CTLCOLOR vazaria um objeto GDI por mensagem de pintura.
	TEST_ASSERT(DarkBackgroundBrush() != nullptr);
	TEST_ASSERT(DarkBackgroundBrush() == DarkBackgroundBrush());
	TEST_ASSERT(EditBackgroundBrush() == EditBackgroundBrush());
	TEST_ASSERT(DarkBackgroundBrush() != EditBackgroundBrush());

	// Chamar com janela nula nao pode explodir: o dialogo pode nao ter todos
	// os controles se algo falhar na criacao.
	ApplyDarkControlTheme(nullptr, true);
	ApplyDarkControlTheme(nullptr, false);
	return true;
}

namespace {

// Escreve um Config.xml de mentira numa pasta temporaria e devolve a pasta,
// com a barra final que DetectAppearance espera.
std::wstring WriteFakeConfig(const char* body) {
	wchar_t temp[MAX_PATH] = {};
	if (!GetTempPathW(MAX_PATH, temp))
		return std::wstring();

	std::wstring dir = std::wstring(temp) + L"bsos_theme_test\\";
	CreateDirectoryW(dir.c_str(), nullptr);

	HANDLE h = CreateFileW((dir + L"Config.xml").c_str(), GENERIC_WRITE, 0, nullptr,
						   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE)
		return std::wstring();

	DWORD written = 0;
	WriteFile(h, body, static_cast<DWORD>(strlen(body)), &written, nullptr);
	CloseHandle(h);
	return dir;
}

} // namespace

TEST(ReadsAppearanceModeOutOfConfigXml) {
	std::wstring dir = WriteFakeConfig(
		"\xEF\xBB\xBF<Config>\r\n  <TargetGame>4</TargetGame>\r\n"
		"  <AppearanceMode>Dark</AppearanceMode>\r\n</Config>\r\n");
	TEST_ASSERT(!dir.empty());
	// BOM e CRLF no arquivo, como o BodySlide grava.
	TEST_ASSERT(DetectAppearance(dir) == Appearance::Dark);

	WriteFakeConfig("<Config><AppearanceMode>Light</AppearanceMode></Config>");
	TEST_ASSERT(DetectAppearance(dir) == Appearance::Light);

	// Chave ausente e o caso da instalacao recem-atualizada: o BodySlide so
	// grava a chave quando o usuario mexe nas Settings.
	WriteFakeConfig("<Config><TargetGame>4</TargetGame></Config>");
	TEST_ASSERT(DetectAppearance(dir) == Appearance::Light);

	// Tag aberta e nao fechada nao pode fazer o leitor sair do arquivo.
	WriteFakeConfig("<Config><AppearanceMode>Dark</Config>");
	TEST_ASSERT(DetectAppearance(dir) == Appearance::Light);

	DeleteFileW((dir + L"Config.xml").c_str());
	RemoveDirectoryW(dir.c_str());
	return true;
}
