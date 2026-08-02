#include "core/config.h"

#include "core/log.h"

#include <cctype>
#include <string>
#include <vector>

namespace {

std::string Trim(const std::string& s) {
	size_t begin = s.find_first_not_of(" \t\r\n");
	if (begin == std::string::npos)
		return std::string();
	size_t end = s.find_last_not_of(" \t\r\n");
	return s.substr(begin, end - begin + 1);
}

std::string Lower(std::string s) {
	for (char& c : s)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

bool ReadBool(const wchar_t* ini, const wchar_t* section, const wchar_t* key, bool fallback) {
	return GetPrivateProfileIntW(section, key, fallback ? 1 : 0, ini) != 0;
}

// Le uma hotkey do INI. Chave ausente ou spec invalido devolvem o fallback --
// nunca uma hotkey invalida, senao a feature ficaria silenciosamente morta.
Hotkey ReadHotkey(const wchar_t* ini, const wchar_t* key, Hotkey fallback) {
	wchar_t wide[128] = {};
	GetPrivateProfileStringW(L"Hotkeys", key, L"", wide, 128, ini);
	if (wide[0] == L'\0')
		return fallback;

	std::string narrow;
	for (const wchar_t* p = wide; *p; ++p)
		narrow.push_back(*p < 128 ? static_cast<char>(*p) : '?');

	Hotkey parsed = ParseHotkey(narrow.c_str());
	return parsed.IsValid() ? parsed : fallback;
}

// GetPrivateProfileInt nao le decimais, entao a sensibilidade vai por string.
float ReadFloat(const wchar_t* ini, const wchar_t* section, const wchar_t* key, float fallback) {
	wchar_t raw[64] = {};
	GetPrivateProfileStringW(section, key, L"", raw, 64, ini);
	if (raw[0] == L'\0')
		return fallback;

	wchar_t* end = nullptr;
	float value = wcstof(raw, &end);
	if (end == raw || value <= 0.0f)
		return fallback; // lixo ou valor sem sentido: mantem o default
	return value;
}

} // namespace

std::vector<RemapEntry> ReadRemapSection(const wchar_t* iniPath) {
	std::vector<RemapEntry> entries;
	if (!iniPath || !*iniPath)
		return entries;

	// GetPrivateProfileSection devolve "chave=valor\0chave=valor\0\0".
	std::vector<wchar_t> buffer(8192);
	DWORD used = GetPrivateProfileSectionW(L"Remap", buffer.data(),
										   static_cast<DWORD>(buffer.size()), iniPath);
	if (used == 0)
		return entries;

	// A API sinaliza truncamento devolvendo o tamanho do buffer menos dois.
	// Descartar tudo calado deixaria o usuario com os remaps sumindo sem
	// explicacao.
	if (used >= buffer.size() - 2) {
		LogF("config: a secao [Remap] passou de %d caracteres e foi ignorada",
			 static_cast<int>(buffer.size()));
		return entries;
	}

	for (const wchar_t* line = buffer.data(); *line; line += wcslen(line) + 1) {
		const wchar_t* eq = wcschr(line, L'=');
		if (!eq || eq == line)
			continue;

		std::string name;
		for (const wchar_t* p = line; p < eq; ++p)
			name.push_back(*p < 128 ? static_cast<char>(*p) : '?');

		std::string spec;
		for (const wchar_t* p = eq + 1; *p; ++p)
			spec.push_back(*p < 128 ? static_cast<char>(*p) : '?');

		RemapEntry entry;
		entry.xrcName = Trim(name);
		entry.key = ParseHotkey(spec.c_str());
		if (entry.xrcName.empty() || !entry.key.IsValid())
			continue; // spec invalido: ignora a linha, nao derruba as outras

		entries.push_back(entry);
	}

	return entries;
}

Hotkey ParseHotkey(const char* spec) {
	Hotkey invalid;
	if (!spec)
		return invalid;

	// Quebra em '+'. O ultimo token e a tecla; os anteriores sao modificadores.
	std::vector<std::string> parts;
	std::string s(spec);
	size_t start = 0;
	for (;;) {
		size_t plus = s.find('+', start);
		if (plus == std::string::npos) {
			parts.push_back(s.substr(start));
			break;
		}
		parts.push_back(s.substr(start, plus - start));
		start = plus + 1;
	}

	Hotkey hk;
	for (size_t i = 0; i + 1 < parts.size(); ++i) {
		std::string mod = Lower(Trim(parts[i]));
		if (mod == "shift")
			hk.shift = true;
		else if (mod == "ctrl" || mod == "control")
			hk.ctrl = true;
		else if (mod == "alt")
			hk.alt = true;
		else
			return invalid; // modificador desconhecido
	}

	std::string key = Trim(parts.back());
	if (key.size() != 1)
		return invalid;

	char c = static_cast<char>(std::toupper(static_cast<unsigned char>(key[0])));
	if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
		return invalid;

	hk.vk = static_cast<UINT>(static_cast<unsigned char>(c));
	return hk;
}

Config LoadConfig(const wchar_t* iniPath) {
	Config c;
	if (!iniPath || !*iniPath)
		return c;

	c.groupSearch = ReadBool(iniPath, L"Features", L"GroupSearch", c.groupSearch);
	c.batchBuildSearch = ReadBool(iniPath, L"Features", L"BatchBuildSearch", c.batchBuildSearch);
	c.referenceAutoSelect = ReadBool(iniPath, L"Features", L"ReferenceAutoSelect", c.referenceAutoSelect);
	c.sliderObjHotkeys = ReadBool(iniPath, L"Features", L"SliderOBJHotkeys", c.sliderObjHotkeys);
	c.referenceHotkey = ReadBool(iniPath, L"Features", L"ReferenceHotkey", c.referenceHotkey);

	c.brushResizeDrag = ReadBool(iniPath, L"Features", L"BrushResizeDrag", c.brushResizeDrag);

	c.selectReference = ReadHotkey(iniPath, L"SelectReference", c.selectReference);
	c.exportSliderObj = ReadHotkey(iniPath, L"ExportSliderOBJ", c.exportSliderObj);
	c.importSliderObj = ReadHotkey(iniPath, L"ImportSliderOBJ", c.importSliderObj);
	c.brushResize = ReadHotkey(iniPath, L"BrushResize", c.brushResize);

	c.brushResizeSensitivity = ReadFloat(iniPath, L"Tuning", L"BrushResizeSensitivity", c.brushResizeSensitivity);
	c.remaps = ReadRemapSection(iniPath);

	c.logFile = ReadBool(iniPath, L"Debug", L"LogFile", c.logFile);
	return c;
}
