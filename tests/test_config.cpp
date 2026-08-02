// Parser de INI, de hotkey e da secao [Remap].
#include "test_util.h"

#include "core/config.h"

namespace {

// A API de perfil do Windows resolve caminho relativo contra o diretorio do
// Windows, entao os testes precisam usar caminho absoluto.
std::wstring TempPath(const wchar_t* name) {
	wchar_t dir[MAX_PATH] = {};
	GetTempPathW(MAX_PATH, dir);
	return std::wstring(dir) + name;
}

bool WriteIni(const std::wstring& path, const char* content) {
	HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	DWORD written = 0;
	BOOL ok = WriteFile(h, content, static_cast<DWORD>(strlen(content)), &written, nullptr);
	CloseHandle(h);
	return ok != FALSE;
}

} // namespace

TEST(ParseHotkeyHandlesSpecsAndGarbage) {
	Hotkey r = ParseHotkey("R");
	TEST_ASSERT(r.vk == 'R' && !r.shift && !r.ctrl && !r.alt);

	Hotkey se = ParseHotkey("Shift+E");
	TEST_ASSERT(se.vk == 'E' && se.shift && !se.ctrl && !se.alt);

	Hotkey lower = ParseHotkey("shift+e");
	TEST_ASSERT(lower.vk == 'E' && lower.shift);

	Hotkey spaced = ParseHotkey("  Ctrl + Alt + K ");
	TEST_ASSERT(spaced.vk == 'K' && spaced.ctrl && spaced.alt && !spaced.shift);

	Hotkey digit = ParseHotkey("Shift+7");
	TEST_ASSERT(digit.vk == '7' && digit.shift);

	// Tudo abaixo e invalido e o chamador deve manter o default.
	TEST_ASSERT(!ParseHotkey("").IsValid());
	TEST_ASSERT(!ParseHotkey("   ").IsValid());
	TEST_ASSERT(!ParseHotkey("Shift+").IsValid());
	TEST_ASSERT(!ParseHotkey("Shift+EE").IsValid());
	TEST_ASSERT(!ParseHotkey("Meta+E").IsValid());
	TEST_ASSERT(!ParseHotkey("+").IsValid());
	TEST_ASSERT(!ParseHotkey("Ctrl").IsValid());
	return true;
}

TEST(ConfigUsesDefaultsWhenFileMissing) {
	std::wstring missing = TempPath(L"bsos_nao_existe_xyz.ini");
	DeleteFileW(missing.c_str());

	Config c = LoadConfig(missing.c_str());
	TEST_ASSERT(c.groupSearch);
	TEST_ASSERT(c.referenceAutoSelect);
	TEST_ASSERT(c.sliderObjHotkeys);
	TEST_ASSERT(c.referenceHotkey);
	TEST_ASSERT(!c.logFile);
	TEST_ASSERT(c.selectReference.vk == 'B' && !c.selectReference.shift);
	TEST_ASSERT(c.exportSliderObj.vk == 'E' && c.exportSliderObj.shift);
	TEST_ASSERT(c.importSliderObj.vk == 'I' && c.importSliderObj.shift);
	return true;
}

TEST(ConfigReadsValuesAndSurvivesGarbage) {
	std::wstring path = TempPath(L"bsos_test_cfg.ini");
	TEST_ASSERT(WriteIni(path,
						 "[Features]\r\n"
						 "GroupSearch=0\r\n"
						 "ReferenceAutoSelect=1\r\n"
						 "[Hotkeys]\r\n"
						 "SelectReference=Ctrl+B\r\n"
						 "ExportSliderOBJ=LIXO++\r\n"
						 "[Debug]\r\n"
						 "LogFile=1\r\n"));

	Config c = LoadConfig(path.c_str());
	TEST_ASSERT(!c.groupSearch);
	TEST_ASSERT(c.referenceAutoSelect);
	TEST_ASSERT(c.sliderObjHotkeys); // ausente no arquivo => default
	TEST_ASSERT(c.logFile);
	TEST_ASSERT(c.selectReference.vk == 'B' && c.selectReference.ctrl && !c.selectReference.shift);

	// O spec invalido nao pode zerar a hotkey; tem que sobrar o default.
	TEST_ASSERT(c.exportSliderObj.vk == 'E' && c.exportSliderObj.shift);

	DeleteFileW(path.c_str());
	return true;
}

TEST(ReadsRemapSection) {
	std::wstring path = TempPath(L"bsos_remap.ini");
	TEST_ASSERT(WriteIni(path,
						 "[Remap]\r\n"
						 "btnTransform=K\r\n"
						 "btnRecalcNormals=N\r\n"
						 "btnQuebrado=LIXO++\r\n"
						 "=SemNome\r\n"));

	auto remaps = ReadRemapSection(path.c_str());
	TEST_ASSERT(remaps.size() == 2);
	TEST_ASSERT(remaps[0].xrcName == "btnTransform");
	TEST_ASSERT(remaps[0].key.vk == 'K');
	TEST_ASSERT(remaps[1].xrcName == "btnRecalcNormals");
	TEST_ASSERT(remaps[1].key.vk == 'N');

	TEST_ASSERT(ReadRemapSection(TempPath(L"bsos_sem_remap.ini").c_str()).empty());

	DeleteFileW(path.c_str());
	return true;
}

TEST(ReadsTuningAndBrushKeys) {
	std::wstring path = TempPath(L"bsos_tuning.ini");
	TEST_ASSERT(WriteIni(path,
						 "[Features]\r\nBrushResizeDrag=0\r\n"
						 "[Hotkeys]\r\nBrushResize=Shift+F\r\n"
						 "[Tuning]\r\nBrushResizeSensitivity=2.5\r\n"));

	Config c = LoadConfig(path.c_str());
	TEST_ASSERT(!c.brushResizeDrag);
	TEST_ASSERT(c.brushResize.vk == 'F' && c.brushResize.shift);
	TEST_ASSERT(c.brushResizeSensitivity > 2.4f && c.brushResizeSensitivity < 2.6f);

	Config d = LoadConfig(TempPath(L"bsos_sem_tuning.ini").c_str());
	TEST_ASSERT(d.brushResizeDrag);
	TEST_ASSERT(d.brushResize.vk == 'F' && !d.brushResize.shift);
	TEST_ASSERT(d.brushResizeSensitivity > 0.9f && d.brushResizeSensitivity < 1.1f);
	TEST_ASSERT(d.remaps.empty());

	// Sensibilidade invalida cai no default em vez de zerar a feature.
	std::wstring bad = TempPath(L"bsos_bad_tuning.ini");
	TEST_ASSERT(WriteIni(bad, "[Tuning]\r\nBrushResizeSensitivity=abc\r\n"));
	Config e = LoadConfig(bad.c_str());
	TEST_ASSERT(e.brushResizeSensitivity > 0.9f && e.brushResizeSensitivity < 1.1f);

	DeleteFileW(path.c_str());
	DeleteFileW(bad.c_str());
	return true;
}
