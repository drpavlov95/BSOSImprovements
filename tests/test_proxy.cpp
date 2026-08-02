// O proxy msimg32 precisa repassar os exports para o System32.
#include "test_util.h"

using AlphaBlendFn = BOOL(WINAPI*)(HDC, int, int, int, int, HDC, int, int, int, int, BLENDFUNCTION);

// Desenha um alpha-blend conhecido e devolve o pixel resultante.
// Branco sobre preto a 50% precisa dar o mesmo valor nas duas implementacoes.
static COLORREF BlendOneAndSample(AlphaBlendFn fn, BLENDFUNCTION bf) {
	HDC screen = GetDC(nullptr);
	HDC dstDC = CreateCompatibleDC(screen);
	HDC srcDC = CreateCompatibleDC(screen);

	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = 4;
	bi.bmiHeader.biHeight = -4; // top-down
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;

	void* dstBits = nullptr;
	void* srcBits = nullptr;
	HBITMAP dstBmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &dstBits, nullptr, 0);
	HBITMAP srcBmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &srcBits, nullptr, 0);

	HGDIOBJ oldDst = SelectObject(dstDC, dstBmp);
	HGDIOBJ oldSrc = SelectObject(srcDC, srcBmp);

	for (int i = 0; i < 16; ++i) {
		static_cast<DWORD*>(dstBits)[i] = 0x00000000; // destino preto
		static_cast<DWORD*>(srcBits)[i] = 0x00FFFFFF; // origem branca
	}

	COLORREF sampled = CLR_INVALID;
	if (fn && fn(dstDC, 0, 0, 4, 4, srcDC, 0, 0, 4, 4, bf))
		sampled = GetPixel(dstDC, 1, 1);

	SelectObject(dstDC, oldDst);
	SelectObject(srcDC, oldSrc);
	DeleteObject(dstBmp);
	DeleteObject(srcBmp);
	DeleteDC(dstDC);
	DeleteDC(srcDC);
	ReleaseDC(nullptr, screen);
	return sampled;
}

TEST(ProxyForwardsAlphaBlendToSystem) {
	HMODULE proxy = LoadLibraryW(L"dist\\msimg32.dll");
	if (!proxy) {
		std::printf("\n      PULADO: rode build.bat antes\n      ");
		return true;
	}

	HMODULE real = LoadLibraryW(L"C:\\Windows\\System32\\msimg32.dll");
	TEST_ASSERT(real != nullptr);

	// Se o loader tivesse devolvido o nosso proprio modulo por causa do nome
	// de base igual, os dois handles seriam o mesmo e o proxy nao funcionaria.
	TEST_ASSERT(proxy != real);

	auto viaProxy = reinterpret_cast<AlphaBlendFn>(GetProcAddress(proxy, "AlphaBlend"));
	auto viaReal = reinterpret_cast<AlphaBlendFn>(GetProcAddress(real, "AlphaBlend"));
	TEST_ASSERT(viaProxy != nullptr);
	TEST_ASSERT(viaReal != nullptr);
	TEST_ASSERT(viaProxy != viaReal); // precisa ser o nosso wrapper

	BLENDFUNCTION bf = {AC_SRC_OVER, 0, 128, 0};
	COLORREF fromProxy = BlendOneAndSample(viaProxy, bf);
	COLORREF fromReal = BlendOneAndSample(viaReal, bf);

	TEST_ASSERT(fromProxy != CLR_INVALID);
	TEST_ASSERT(fromProxy == fromReal);

	FreeLibrary(proxy);
	FreeLibrary(real);
	return true;
}

TEST(ProxyExportsAllFiveNames) {
	HMODULE proxy = LoadLibraryW(L"dist\\msimg32.dll");
	if (!proxy) {
		std::printf("\n      PULADO: rode build.bat antes\n      ");
		return true;
	}

	// Os cinco exports do msimg32, sem decoracao de nome.
	TEST_ASSERT(GetProcAddress(proxy, "AlphaBlend") != nullptr);
	TEST_ASSERT(GetProcAddress(proxy, "GradientFill") != nullptr);
	TEST_ASSERT(GetProcAddress(proxy, "TransparentBlt") != nullptr);
	TEST_ASSERT(GetProcAddress(proxy, "DllInitialize") != nullptr);
	TEST_ASSERT(GetProcAddress(proxy, "vSetDdrawflag") != nullptr);

	FreeLibrary(proxy);
	return true;
}
