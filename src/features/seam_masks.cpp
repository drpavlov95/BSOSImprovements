#include "features/seam_masks.h"

#include <commctrl.h>

#include <string>

#include "core/log.h"
#include "core/ui_thread.h"
#include "features/native_outfit_mesh.h"
#include "mesh/seams.h"
#include "win32/menu.h"
#include "win32/winfind.h"

namespace {

constexpr UINT_PTR kFrameSubclassId = 0xB540;

HWND g_frame = nullptr;
HWND g_canvas = nullptr;
HMENU g_shapeMenu = nullptr;
HMENU g_masksMenu = nullptr;
HWND g_tooltip = nullptr;
TOOLINFOW g_toolInfo = {};
UINT g_geometryId = 0;
UINT g_uvId = 0;
UINT g_nonManifoldId = 0;
UINT g_humanoidId = 0;
bool g_installed = false;

std::wstring LocalMenuTextAt(HMENU menu, int position) {
	wchar_t text[256] = {};
	GetMenuStringW(menu, position, text, static_cast<int>(std::size(text)), MF_BYPOSITION);
	std::wstring out(text);
	const size_t tab = out.find(L'\t');
	if (tab != std::wstring::npos)
		out.resize(tab);
	while (!out.empty() && out.back() == L'.')
		out.pop_back();
	return out;
}

bool MenuUsesId(HMENU menu, UINT id) {
	if (!menu)
		return false;
	for (int i = 0; i < GetMenuItemCount(menu); ++i) {
		if (HMENU child = GetSubMenu(menu, i)) {
			if (MenuUsesId(child, id))
				return true;
		}
		else if (GetMenuItemID(menu, i) == id)
			return true;
	}
	return false;
}

UINT FreeId(HWND frame, UINT first) {
	for (UINT id = first; id < first + 32; ++id)
		if (!MenuUsesId(GetMenu(frame), id))
			return id;
	return 0;
}

HMENU FindShapeMenu(HWND frame) {
	HMENU bar = GetMenu(frame);
	if (!bar)
		return nullptr;
	for (int i = 0; i < GetMenuItemCount(bar); ++i)
		if (_wcsicmp(LocalMenuTextAt(bar, i).c_str(), L"Shape") == 0)
			return GetSubMenu(bar, i);
	return nullptr;
}

int InsertPosition(HMENU shape) {
	for (int i = 0; i < GetMenuItemCount(shape); ++i)
		if (_wcsicmp(LocalMenuTextAt(shape, i).c_str(), L"Mask Symmetric Vertices") == 0)
			return i;
	return GetMenuItemCount(shape);
}

void ShowResultError(NativeOutfitMesh::Result result) {
	const wchar_t* text = L"The selected mesh could not be accessed safely.";
	if (result == NativeOutfitMesh::Result::UnsupportedBinary)
		text = L"Mask Seams supports only the verified Outfit Studio 5.8.2 build.";
	else if (result == NativeOutfitMesh::Result::NoSelectedMesh)
		text = L"Select exactly one shape and try again.";
	MessageBoxW(g_frame, text, L"Mask Seams", MB_OK | MB_ICONINFORMATION);
}

void CreateMask(SeamKind kind) {
	NativeOutfitMesh::Snapshot snapshot;
	const NativeOutfitMesh::Result read = NativeOutfitMesh::SnapshotSelected(snapshot);
	if (read != NativeOutfitMesh::Result::Ok) {
		ShowResultError(read);
		return;
	}
	const std::vector<uint16_t> vertices = FindSeamVertices(snapshot.mesh, kind);
	if (vertices.empty()) {
		MessageBoxW(g_frame, L"No matching seam vertices were found in the selected shape.",
					L"Mask Seams", MB_OK | MB_ICONINFORMATION);
		return;
	}
	const NativeOutfitMesh::Result write = NativeOutfitMesh::ReplaceMask(snapshot, vertices, g_canvas);
	if (write != NativeOutfitMesh::Result::Ok) {
		ShowResultError(write);
		return;
	}
	LogF("seams nativo: %zu vertices mascarados", vertices.size());
}

const wchar_t* HelpFor(UINT id) {
	if (id == g_humanoidId)
		return L"Masks only the neck, wrist and ankle seam rings of an upright humanoid body.";
	if (id == g_geometryId)
		return L"Masks coincident boundary vertices that are separate in the mesh topology.";
	if (id == g_uvId)
		return L"Masks coincident boundary vertices whose UV coordinates split.";
	if (id == g_nonManifoldId)
		return L"Masks vertices on open edges or edges shared by more than two triangles.";
	return nullptr;
}

void ShowMenuTooltip(const wchar_t* text) {
	if (!g_tooltip)
		return;
	if (!text) {
		SendMessageW(g_tooltip, TTM_TRACKACTIVATE, FALSE,
					 reinterpret_cast<LPARAM>(&g_toolInfo));
		return;
	}
	g_toolInfo.lpszText = const_cast<wchar_t*>(text);
	SendMessageW(g_tooltip, TTM_UPDATETIPTEXTW, 0,
				 reinterpret_cast<LPARAM>(&g_toolInfo));
	POINT cursor = {};
	GetCursorPos(&cursor);
	SendMessageW(g_tooltip, TTM_TRACKPOSITION, 0, MAKELPARAM(cursor.x + 18, cursor.y + 24));
	SendMessageW(g_tooltip, TTM_TRACKACTIVATE, TRUE,
				 reinterpret_cast<LPARAM>(&g_toolInfo));
}

LRESULT CALLBACK FrameProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
						   UINT_PTR id, DWORD_PTR) {
	if (msg == WM_COMMAND && HIWORD(wParam) == 0 && lParam == 0) {
		ShowMenuTooltip(nullptr);
		const UINT command = LOWORD(wParam);
		if (command == g_geometryId) { CreateMask(SeamKind::Geometry); return 0; }
		if (command == g_uvId) { CreateMask(SeamKind::UV); return 0; }
		if (command == g_nonManifoldId) { CreateMask(SeamKind::NonManifold); return 0; }
		if (command == g_humanoidId) { CreateMask(SeamKind::HumanoidOpenings); return 0; }
	}
	if (msg == WM_MENUSELECT) {
		const wchar_t* help = HelpFor(LOWORD(wParam));
		ShowMenuTooltip(help);
		if (help)
			if (HWND status = FindDescendantByClass(hwnd, STATUSCLASSNAMEW))
				SetWindowTextW(status, help);
	}
	if (msg == WM_EXITMENULOOP)
		ShowMenuTooltip(nullptr);
	if (msg == WM_NCDESTROY)
		RemoveWindowSubclass(hwnd, FrameProc, id);
	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void InstallHere(void*) {
	g_shapeMenu = FindShapeMenu(g_frame);
	if (!g_shapeMenu)
		return;
	g_geometryId = FreeId(g_frame, 0xBF60);
	g_uvId = g_geometryId ? FreeId(g_frame, g_geometryId + 1) : 0;
	g_nonManifoldId = g_uvId ? FreeId(g_frame, g_uvId + 1) : 0;
	g_humanoidId = g_nonManifoldId ? FreeId(g_frame, g_nonManifoldId + 1) : 0;
	if (!g_geometryId || !g_uvId || !g_nonManifoldId || !g_humanoidId)
		return;

	g_masksMenu = CreatePopupMenu();
	if (!g_masksMenu)
		return;
	if (!AppendMenuW(g_masksMenu, MF_STRING, g_humanoidId, L"Anatomical Seams") ||
		!AppendMenuW(g_masksMenu, MF_SEPARATOR, 0, nullptr) ||
		!AppendMenuW(g_masksMenu, MF_STRING, g_geometryId, L"Geometric Seams") ||
		!AppendMenuW(g_masksMenu, MF_STRING, g_uvId, L"UV Seams") ||
		!AppendMenuW(g_masksMenu, MF_STRING, g_nonManifoldId, L"Non-Manifold Borders") ||
		!InsertMenuW(g_shapeMenu, InsertPosition(g_shapeMenu), MF_BYPOSITION | MF_POPUP,
					 reinterpret_cast<UINT_PTR>(g_masksMenu), L"Masks")) {
		DestroyMenu(g_masksMenu);
		g_masksMenu = nullptr;
		return;
	}
	if (!SetWindowSubclass(g_frame, FrameProc, kFrameSubclassId, 0)) {
		for (int i = 0; i < GetMenuItemCount(g_shapeMenu); ++i)
			if (GetSubMenu(g_shapeMenu, i) == g_masksMenu) {
				RemoveMenu(g_shapeMenu, i, MF_BYPOSITION);
				break;
			}
		DestroyMenu(g_masksMenu);
		g_masksMenu = nullptr;
		return;
	}
	g_tooltip = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TRANSPARENT, TOOLTIPS_CLASSW, nullptr,
		WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
		CW_USEDEFAULT, CW_USEDEFAULT, g_frame, nullptr, GetModuleHandleW(nullptr), nullptr);
	if (g_tooltip) {
		g_toolInfo = {};
		g_toolInfo.cbSize = sizeof(g_toolInfo);
		g_toolInfo.uFlags = TTF_TRACK | TTF_ABSOLUTE;
		g_toolInfo.hwnd = g_frame;
		g_toolInfo.uId = 1;
		g_toolInfo.lpszText = const_cast<wchar_t*>(L"");
		SendMessageW(g_tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&g_toolInfo));
		SendMessageW(g_tooltip, TTM_SETMAXTIPWIDTH, 0, 420);
		SetWindowPos(g_tooltip, HWND_TOPMOST, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	}
	DrawMenuBar(g_frame);
	g_installed = true;
}

} // namespace

namespace SeamMasks {

bool Install(HWND frame) {
	Uninstall();
	if (!frame || !NativeOutfitMesh::SupportsRunningBinary())
		return false;
	g_frame = frame;
	g_canvas = FindLargestVisibleByClass(frame, L"wxGLCanvas");
	if (!RunOnUiThread(frame, InstallHere, nullptr) || !g_installed) {
		Uninstall();
		return false;
	}
	return true;
}

void Uninstall() {
	if (g_tooltip && IsWindow(g_tooltip))
		DestroyWindow(g_tooltip);
	if (g_frame && IsWindow(g_frame))
		RemoveWindowSubclass(g_frame, FrameProc, kFrameSubclassId);
	if (g_shapeMenu && g_masksMenu) {
		for (int i = GetMenuItemCount(g_shapeMenu) - 1; i >= 0; --i)
			if (GetSubMenu(g_shapeMenu, i) == g_masksMenu) {
				RemoveMenu(g_shapeMenu, i, MF_BYPOSITION);
				break;
			}
		DestroyMenu(g_masksMenu);
	}
	g_frame = nullptr;
	g_canvas = nullptr;
	g_shapeMenu = nullptr;
	g_masksMenu = nullptr;
	g_tooltip = nullptr;
	g_toolInfo = {};
	g_geometryId = g_uvId = g_nonManifoldId = g_humanoidId = 0;
	g_installed = false;
}

} // namespace SeamMasks
