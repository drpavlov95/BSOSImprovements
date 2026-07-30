#pragma once

#include <windows.h>

#include <vector>

#include "core/config.h"
#include "core/host.h"

// Cada feature se registra sozinha num inicializador estatico. Assim o host
// nunca precisa conhecer a lista -- adicionar uma feature nao mexe no host.
struct FeatureDef {
	const char* name;
	HostApp app;
	bool (*enabled)(const Config&);
	bool (*install)(HWND frame);
	void (*uninstall)();
};

void RegisterFeature(const FeatureDef& def);
const std::vector<FeatureDef>& RegisteredFeatures();

// Usar no .cpp da feature:
//   BSOS_REGISTER_FEATURE(hotkeys, "hotkeys", HostApp::OutfitStudio,
//                         EnabledFn, Hotkeys::Install, Hotkeys::Uninstall)
//
// TAG e um identificador simples e unico por arquivo. Ele existe porque
// INSTALL costuma ser qualificado (Hotkeys::Install), e "::" nao sobrevive
// ao operador de colagem de tokens do pre-processador.
#define BSOS_REGISTER_FEATURE(TAG, NAME, APP, ENABLED, INSTALL, UNINSTALL)                 \
	namespace {                                                                            \
	struct BsosReg_##TAG {                                                                 \
		BsosReg_##TAG() { RegisterFeature({NAME, APP, ENABLED, INSTALL, UNINSTALL}); }      \
	} g_bsosReg_##TAG;                                                                     \
	}
