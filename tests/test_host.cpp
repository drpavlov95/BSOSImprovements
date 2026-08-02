// Deteccao do exe hospedeiro e registro de features.
#include "test_util.h"

#include "core/host.h"
#include "features/registry.h"

TEST(DetectsHostAppFromExeName) {
	TEST_ASSERT(DetectApp(L"C:\\x\\BodySlide x64.exe") == HostApp::BodySlide);
	TEST_ASSERT(DetectApp(L"C:\\x\\OutfitStudio x64.exe") == HostApp::OutfitStudio);

	// Case-insensitive: o caminho vem do sistema, sem garantia de caixa.
	TEST_ASSERT(DetectApp(L"C:\\x\\outfitstudio.EXE") == HostApp::OutfitStudio);
	TEST_ASSERT(DetectApp(L"C:\\x\\BODYSLIDE X64.EXE") == HostApp::BodySlide);

	// O BodySlide 5.8.0 deixou de usar o sufixo x64: os executaveis passaram a
	// se chamar BodySlide.exe e OutfitStudio.exe. Reportado por usuario.
	TEST_ASSERT(DetectApp(L"C:\\x\\BodySlide.exe") == HostApp::BodySlide);
	TEST_ASSERT(DetectApp(L"C:\\x\\OutfitStudio.exe") == HostApp::OutfitStudio);

	// Qualquer outro processo precisa ser ignorado. O msimg32 e importado por
	// muita coisa; injetar em explorer.exe seria um bug grave.
	TEST_ASSERT(DetectApp(L"C:\\x\\explorer.exe") == HostApp::Unknown);
	TEST_ASSERT(DetectApp(L"C:\\Windows\\notepad.exe") == HostApp::Unknown);
	TEST_ASSERT(DetectApp(L"") == HostApp::Unknown);
	TEST_ASSERT(DetectApp(nullptr) == HostApp::Unknown);

	// So o nome do arquivo conta, nao a pasta: um diretorio chamado
	// "BodySlide" nao pode fazer qualquer exe passar por BodySlide.
	TEST_ASSERT(DetectApp(L"E:\\mods\\BodySlide\\OutfitStudio x64.exe") == HostApp::OutfitStudio);
	TEST_ASSERT(DetectApp(L"E:\\BodySlide and Outfit Studio\\qualquer.exe") == HostApp::Unknown);
	return true;
}

namespace {

bool AlwaysOn(const Config&) {
	return true;
}
bool FakeInstall(HWND) {
	return true;
}
void FakeUninstall() {}

} // namespace

TEST(FeaturesSelfRegister) {
	size_t before = RegisteredFeatures().size();

	RegisterFeature({"teste-a", HostApp::BodySlide, AlwaysOn, FakeInstall, FakeUninstall});
	RegisterFeature({"teste-b", HostApp::OutfitStudio, AlwaysOn, FakeInstall, FakeUninstall});

	const auto& all = RegisteredFeatures();
	TEST_ASSERT(all.size() == before + 2);
	TEST_ASSERT(std::string(all[before].name) == "teste-a");
	TEST_ASSERT(all[before].app == HostApp::BodySlide);
	TEST_ASSERT(std::string(all[before + 1].name) == "teste-b");
	TEST_ASSERT(all[before + 1].app == HostApp::OutfitStudio);
	return true;
}
