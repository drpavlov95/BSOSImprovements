#include "features/slider_data_filter.h"

#include <commctrl.h>

#include <algorithm>
#include <cwctype>

#include "core/host.h"
#include "core/log.h"
#include "core/theme.h"
#include "core/ui_thread.h"
#include "features/registry.h"
#include "win32/winfind.h"

namespace {

constexpr int kScopeId = 0xBF04;
constexpr int kFilterId = 0xBF05;
constexpr UINT_PTR kPendingSubclassId = 0xB51A;
constexpr UINT_PTR kDialogSubclassId = 0xB51B;

HHOOK g_hook = nullptr;
HWND g_frame = nullptr;
HWND g_dialog = nullptr;
HWND g_nativeFilter = nullptr;
HWND g_source = nullptr;
HWND g_status = nullptr;
HWND g_list = nullptr;
HWND g_scope = nullptr;
HWND g_filter = nullptr;
std::vector<HWND> g_buttons;
std::vector<SliderDataFilterRow> g_rows;
int g_sortColumn = -1;
bool g_sortAscending = true;
bool g_dark = false;
UINT g_refreshMessage = 0;

std::wstring Lower(std::wstring text) {
	for (wchar_t& c : text)
		c = static_cast<wchar_t>(std::towlower(c));
	return text;
}

int CompareInsensitive(const std::wstring& a, const std::wstring& b) {
	const int result = CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()),
		b.c_str(), static_cast<int>(b.size()), TRUE);
	if (result == CSTR_LESS_THAN)
		return -1;
	if (result == CSTR_GREATER_THAN)
		return 1;
	return 0;
}

std::wstring WindowText(HWND window) {
	const int length = GetWindowTextLengthW(window);
	if (length <= 0)
		return std::wstring();
	std::wstring text(static_cast<size_t>(length) + 1, L'\0');
	const int written = GetWindowTextW(window, text.data(), static_cast<int>(text.size()));
	text.resize(written > 0 ? static_cast<size_t>(written) : 0);
	return text;
}

std::wstring ComboText(HWND combo, int index) {
	const LRESULT length = SendMessageW(combo, CB_GETLBTEXTLEN, index, 0);
	if (length == CB_ERR || length <= 0)
		return std::wstring();
	std::wstring text(static_cast<size_t>(length) + 1, L'\0');
	const LRESULT written = SendMessageW(combo, CB_GETLBTEXT, index,
		reinterpret_cast<LPARAM>(text.data()));
	text.resize(written > 0 ? static_cast<size_t>(written) : 0);
	return text;
}

std::wstring HeaderText(HWND header, int column) {
	wchar_t buffer[256] = {};
	HDITEMW item = {};
	item.mask = HDI_TEXT;
	item.pszText = buffer;
	item.cchTextMax = static_cast<int>(std::size(buffer));
	if (!Header_GetItem(header, column, &item))
		return std::wstring();
	return buffer;
}

std::wstring ListText(HWND list, int row, int column) {
	std::vector<wchar_t> buffer(256);
	for (;;) {
		LVITEMW item = {};
		item.iSubItem = column;
		item.pszText = buffer.data();
		item.cchTextMax = static_cast<int>(buffer.size());
		const int written = static_cast<int>(SendMessageW(list, LVM_GETITEMTEXTW,
			static_cast<WPARAM>(row), reinterpret_cast<LPARAM>(&item)));
		if (written < static_cast<int>(buffer.size()) - 1 || buffer.size() >= 8192)
			return std::wstring(buffer.data(), written > 0 ? static_cast<size_t>(written) : 0);
		buffer.resize(buffer.size() * 2);
	}
}

RECT RectIn(HWND window, HWND parent) {
	RECT rect = {};
	GetWindowRect(window, &rect);
	MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&rect), 2);
	return rect;
}

void CaptureRows() {
	g_rows.clear();
	if (!g_list || !IsWindow(g_list))
		return;

	HWND header = ListView_GetHeader(g_list);
	const int columns = header ? Header_GetItemCount(header) : 0;
	const int count = ListView_GetItemCount(g_list);
	if (columns <= 0 || count < 0)
		return;

	g_rows.reserve(static_cast<size_t>(count));
	for (int row = 0; row < count; ++row) {
		SliderDataFilterRow copy;
		copy.columns.reserve(static_cast<size_t>(columns));
		for (int column = 0; column < columns; ++column)
			copy.columns.push_back(ListText(g_list, row, column));

		LVITEMW item = {};
		item.mask = LVIF_PARAM;
		item.iItem = row;
		if (ListView_GetItem(g_list, &item))
			copy.itemData = item.lParam;
		copy.state = ListView_GetItemState(g_list, row,
			LVIS_SELECTED | LVIS_FOCUSED);
		g_rows.push_back(std::move(copy));
	}
}

void ApplyRows() {
	if (!g_list || !g_filter || !g_scope)
		return;

	const int scope = static_cast<int>(SendMessageW(g_scope, CB_GETCURSEL, 0, 0));
	const int column = scope <= 0 ? -1 : scope - 1;
	const std::vector<size_t> visible = MatchingSliderDataRows(
		g_rows, column, WindowText(g_filter));

	SendMessageW(g_list, WM_SETREDRAW, FALSE, 0);
	ListView_DeleteAllItems(g_list);
	for (size_t rowIndex : visible) {
		if (rowIndex >= g_rows.size() || g_rows[rowIndex].columns.empty())
			continue;
		const SliderDataFilterRow& row = g_rows[rowIndex];
		LVITEMW item = {};
		item.mask = LVIF_TEXT | LVIF_PARAM;
		item.iItem = ListView_GetItemCount(g_list);
		item.pszText = const_cast<wchar_t*>(row.columns[0].c_str());
		item.lParam = row.itemData;
		const int inserted = ListView_InsertItem(g_list, &item);
		if (inserted < 0)
			continue;
		for (int columnIndex = 1;
			 columnIndex < static_cast<int>(row.columns.size()); ++columnIndex)
			ListView_SetItemText(g_list, inserted, columnIndex,
				const_cast<wchar_t*>(row.columns[static_cast<size_t>(columnIndex)].c_str()));
		if (row.state)
			ListView_SetItemState(g_list, inserted, row.state,
				LVIS_SELECTED | LVIS_FOCUSED);
	}
	SendMessageW(g_list, WM_SETREDRAW, TRUE, 0);
	RedrawWindow(g_list, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void SyncVisibleSelection() {
	for (SliderDataFilterRow& row : g_rows)
		row.state = 0;
	const int count = g_list ? ListView_GetItemCount(g_list) : 0;
	for (int itemIndex = 0; itemIndex < count; ++itemIndex) {
		LVITEMW item = {};
		item.mask = LVIF_PARAM;
		item.iItem = itemIndex;
		if (!ListView_GetItem(g_list, &item))
			continue;
		for (SliderDataFilterRow& row : g_rows) {
			if (row.itemData != item.lParam)
				continue;
			row.state = ListView_GetItemState(g_list, itemIndex,
				LVIS_SELECTED | LVIS_FOCUSED);
			break;
		}
	}
}

void SortAndApply(int column) {
	if (column < 0)
		return;
	SyncVisibleSelection();
	if (g_sortColumn == column)
		g_sortAscending = !g_sortAscending;
	else {
		g_sortColumn = column;
		g_sortAscending = true;
	}
	SortSliderDataRows(g_rows, g_sortColumn, g_sortAscending);
	ApplyRows();
}

void RefreshAfterNativeChange() {
	CaptureRows();
	if (g_sortColumn >= 0)
		SortSliderDataRows(g_rows, g_sortColumn, g_sortAscending);
	ApplyRows();
}

void LayoutControls() {
	if (!g_dialog || !g_nativeFilter || !g_scope || !g_filter)
		return;
	const RECT native = RectIn(g_nativeFilter, g_dialog);
	const int width = native.right - native.left;
	const int height = native.bottom - native.top;
	if (width < 80 || height < 12)
		return;

	UINT dpi = GetDpiForWindow(g_dialog);
	if (!dpi)
		dpi = 96;
	int scopeWidth = MulDiv(125, static_cast<int>(dpi), 96);
	const int gap = MulDiv(5, static_cast<int>(dpi), 96);
	if (scopeWidth > width / 2)
		scopeWidth = width / 2;

	SetWindowPos(g_scope, nullptr, native.left, native.top, scopeWidth, height * 10,
		SWP_NOZORDER | SWP_NOACTIVATE);
	SetWindowPos(g_filter, nullptr, native.left + scopeWidth + gap, native.top,
		width - scopeWidth - gap, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

void ResetDialogState() {
	g_dialog = nullptr;
	g_nativeFilter = nullptr;
	g_source = nullptr;
	g_status = nullptr;
	g_list = nullptr;
	g_scope = nullptr;
	g_filter = nullptr;
	g_buttons.clear();
	g_rows.clear();
	g_sortColumn = -1;
	g_sortAscending = true;
}

LRESULT CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
							UINT_PTR id, DWORD_PTR) {
	if (g_refreshMessage && msg == g_refreshMessage) {
		RefreshAfterNativeChange();
		return 0;
	}

	if (msg == WM_COMMAND) {
		HWND control = reinterpret_cast<HWND>(lParam);
		const int notification = HIWORD(wParam);
		if (control == g_filter && notification == EN_CHANGE) {
			ApplyRows();
			return 0;
		}
		if (control == g_scope && notification == CBN_SELCHANGE) {
			ApplyRows();
			return 0;
		}
		if ((control == g_source || control == g_status) &&
			notification == CBN_SELCHANGE) {
			const LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
			RefreshAfterNativeChange();
			return result;
		}
		if (notification == BN_CLICKED &&
			std::find(g_buttons.begin(), g_buttons.end(), control) != g_buttons.end()) {
			const LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
			if (IsWindow(hwnd) && g_refreshMessage)
				PostMessageW(hwnd, g_refreshMessage, 0, 0);
			return result;
		}
	}

	if (msg == WM_NOTIFY) {
		auto* header = reinterpret_cast<NMHDR*>(lParam);
		if (header && header->hwndFrom == g_list && header->code == LVN_COLUMNCLICK) {
			auto* click = reinterpret_cast<NMLISTVIEW*>(lParam);
			SortAndApply(click->iSubItem);
			return 0;
		}
	}

	if (msg == WM_CTLCOLOREDIT && g_dark &&
		reinterpret_cast<HWND>(lParam) == g_filter) {
		SetTextColor(reinterpret_cast<HDC>(wParam), kDarkText);
		SetBkColor(reinterpret_cast<HDC>(wParam), kDarkControlBackground);
		return reinterpret_cast<LRESULT>(EditBackgroundBrush());
	}

	if (msg == WM_SHOWWINDOW || msg == WM_SIZE) {
		const LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
		LayoutControls();
		return result;
	}

	if (msg == WM_NCDESTROY) {
		RemoveWindowSubclass(hwnd, DialogProc, id);
		ResetDialogState();
	}
	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

bool EnhanceDialog(HWND dialog, const SliderDataDialogControls& controls) {
	if (g_dialog || !controls.ok())
		return false;

	g_dialog = dialog;
	g_nativeFilter = controls.filter;
	g_source = controls.source;
	g_status = controls.status;
	g_list = controls.list;
	g_buttons = controls.buttons;
	g_dark = DetectAppearance(AppDir()) == Appearance::Dark;

	HFONT font = reinterpret_cast<HFONT>(SendMessageW(g_nativeFilter, WM_GETFONT, 0, 0));
	HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(dialog, GWLP_HINSTANCE));
	g_scope = CreateWindowExW(0, L"COMBOBOX", L"",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
		0, 0, 10, 100, dialog,
		reinterpret_cast<HMENU>(static_cast<INT_PTR>(kScopeId)), instance, nullptr);
	g_filter = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
		0, 0, 10, 10, dialog,
		reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFilterId)), instance, nullptr);
	if (!g_scope || !g_filter) {
		LogF("slider_data: nao consegui criar os controles de filtro (erro %lu)",
			GetLastError());
		if (g_scope)
			DestroyWindow(g_scope);
		if (g_filter)
			DestroyWindow(g_filter);
		ResetDialogState();
		return false;
	}

	if (font) {
		SendMessageW(g_scope, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
		SendMessageW(g_filter, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
	}

	std::wstring all = ComboText(g_source, 0);
	if (all.empty())
		all = L"All";
	SendMessageW(g_scope, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(all.c_str()));
	HWND header = ListView_GetHeader(g_list);
	const int columns = header ? Header_GetItemCount(header) : 0;
	for (int column = 0; column < columns; ++column) {
		const std::wstring text = HeaderText(header, column);
		SendMessageW(g_scope, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
	}
	SendMessageW(g_scope, CB_SETCURSEL, 0, 0);

	SetWindowTextW(g_nativeFilter, L"");
	ShowWindow(g_nativeFilter, SW_HIDE);
	if (g_dark) {
		ApplyDarkControlTheme(g_scope, false);
		ApplyDarkControlTheme(g_filter, true);
	}

	CaptureRows();
	if (!SetWindowSubclass(dialog, DialogProc, kDialogSubclassId, 0)) {
		ShowWindow(g_nativeFilter, SW_SHOW);
		DestroyWindow(g_scope);
		DestroyWindow(g_filter);
		ResetDialogState();
		return false;
	}
	LayoutControls();
	LogF("slider_data: filtro por coluna e ordenacao instalados (%d linhas, %d colunas)",
		static_cast<int>(g_rows.size()), columns);
	return true;
}

LRESULT CALLBACK PendingDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
								   UINT_PTR id, DWORD_PTR) {
	if (msg == WM_WINDOWPOSCHANGING || msg == WM_WINDOWPOSCHANGED ||
		msg == WM_SHOWWINDOW) {
		const SliderDataDialogControls controls = FindSliderDataDialogControls(hwnd);
		if (controls.ok()) {
			RemoveWindowSubclass(hwnd, PendingDialogProc, id);
			EnhanceDialog(hwnd, controls);
		}
	}
	if (msg == WM_NCDESTROY) {
		RemoveWindowSubclass(hwnd, PendingDialogProc, id);
	}
	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK CbtProc(int code, WPARAM wParam, LPARAM lParam) {
	HWND candidate = reinterpret_cast<HWND>(wParam);
	if (code == HCBT_CREATEWND && candidate) {
		auto* created = reinterpret_cast<CBT_CREATEWNDW*>(lParam);
		if (created && created->lpcs && created->lpcs->hwndParent == g_frame)
			SetWindowSubclass(candidate, PendingDialogProc, kPendingSubclassId, 0);
	}
	else if (code == HCBT_ACTIVATE && candidate && !g_dialog) {
		// wxDialog nao usa obrigatoriamente a classe Win32 #32770. Na ativacao
		// todos os filhos ja existem, entao a estrutura exata do Slider Data e
		// uma identificacao mais forte e independente da classe nativa.
		HWND owner = GetWindow(candidate, GW_OWNER);
		if (owner == g_frame || GetAncestor(candidate, GA_ROOTOWNER) == g_frame) {
			const SliderDataDialogControls controls = FindSliderDataDialogControls(candidate);
			if (controls.ok()) {
				RemoveWindowSubclass(candidate, PendingDialogProc, kPendingSubclassId);
				EnhanceDialog(candidate, controls);
			}
		}
	}
	return CallNextHookEx(g_hook, code, wParam, lParam);
}

void InstallHere(void*) {
	g_hook = SetWindowsHookExW(WH_CBT, CbtProc, SelfModule(), GetCurrentThreadId());
	if (!g_hook)
		LogF("slider_data: SetWindowsHookEx(WH_CBT) falhou (erro %lu)", GetLastError());
}

void UninstallHere(void*) {
	if (g_dialog && IsWindow(g_dialog)) {
		RemoveWindowSubclass(g_dialog, DialogProc, kDialogSubclassId);
		if (g_nativeFilter && IsWindow(g_nativeFilter))
			ShowWindow(g_nativeFilter, SW_SHOW);
		if (g_scope && IsWindow(g_scope))
			DestroyWindow(g_scope);
		if (g_filter && IsWindow(g_filter))
			DestroyWindow(g_filter);
	}
	ResetDialogState();
}

bool Enabled(const Config& config) {
	return config.sliderDataFilter;
}

} // namespace

std::vector<size_t> MatchingSliderDataRows(
	const std::vector<SliderDataFilterRow>& rows, int column,
	const std::wstring& query) {
	std::vector<size_t> matches;
	const std::wstring needle = Lower(query);
	for (size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
		const auto& columns = rows[rowIndex].columns;
		bool match = needle.empty();
		if (!match && column >= 0 && column < static_cast<int>(columns.size()))
			match = Lower(columns[static_cast<size_t>(column)]).find(needle) !=
				std::wstring::npos;
		else if (!match && column < 0) {
			for (const std::wstring& value : columns) {
				if (Lower(value).find(needle) != std::wstring::npos) {
					match = true;
					break;
				}
			}
		}
		if (match)
			matches.push_back(rowIndex);
	}
	return matches;
}

void SortSliderDataRows(std::vector<SliderDataFilterRow>& rows, int column,
						bool ascending) {
	if (column < 0)
		return;
	std::stable_sort(rows.begin(), rows.end(), [column, ascending](
		const SliderDataFilterRow& a, const SliderDataFilterRow& b) {
		const std::wstring empty;
		const std::wstring& left = column < static_cast<int>(a.columns.size()) ?
			a.columns[static_cast<size_t>(column)] : empty;
		const std::wstring& right = column < static_cast<int>(b.columns.size()) ?
			b.columns[static_cast<size_t>(column)] : empty;
		const int result = CompareInsensitive(left, right);
		return ascending ? result < 0 : result > 0;
	});
}

SliderDataDialogControls FindSliderDataDialogControls(HWND dialog) {
	SliderDataDialogControls out;
	if (!dialog)
		return out;

	std::vector<HWND> edits;
	std::vector<HWND> combos;
	std::vector<HWND> lists;
	for (HWND child : ChildrenOf(dialog)) {
		const std::wstring cls = ClassOf(child);
		if (_wcsicmp(cls.c_str(), L"Edit") == 0)
			edits.push_back(child);
		else if (_wcsicmp(cls.c_str(), L"ComboBox") == 0)
			combos.push_back(child);
		else if (_wcsicmp(cls.c_str(), WC_LISTVIEWW) == 0)
			lists.push_back(child);
		else if (_wcsicmp(cls.c_str(), L"Button") == 0)
			out.buttons.push_back(child);
	}
	if (edits.size() != 1 || combos.size() != 2 || lists.size() != 1 ||
		out.buttons.size() != 4) {
		out.buttons.clear();
		return out;
	}
	HWND header = ListView_GetHeader(lists[0]);
	if (!header || Header_GetItemCount(header) != 8) {
		out.buttons.clear();
		return out;
	}
	if (SendMessageW(combos[0], CB_GETCOUNT, 0, 0) != 3 ||
		SendMessageW(combos[1], CB_GETCOUNT, 0, 0) != 3) {
		out.buttons.clear();
		return out;
	}
	const RECT editRect = RectIn(edits[0], dialog);
	const RECT listRect = RectIn(lists[0], dialog);
	if (editRect.bottom > listRect.top) {
		out.buttons.clear();
		return out;
	}
	for (HWND button : out.buttons) {
		if (RectIn(button, dialog).top < listRect.bottom) {
			out.buttons.clear();
			return out;
		}
	}

	std::sort(combos.begin(), combos.end(), [dialog](HWND a, HWND b) {
		return RectIn(a, dialog).left < RectIn(b, dialog).left;
	});
	out.filter = edits[0];
	out.source = combos[0];
	out.status = combos[1];
	out.list = lists[0];
	return out;
}

namespace SliderDataFilter {

bool Install(HWND frame) {
	Uninstall();
	g_frame = frame;
	if (!g_refreshMessage)
		g_refreshMessage = RegisterWindowMessageW(L"BSOSImprovements_SliderDataRefresh");
	if (!RunOnUiThread(frame, InstallHere, nullptr))
		return false;
	return g_hook != nullptr;
}

void Uninstall() {
	if (g_hook) {
		UnhookWindowsHookEx(g_hook);
		g_hook = nullptr;
	}
	if (g_frame)
		RunOnUiThread(g_frame, UninstallHere, nullptr);
	g_frame = nullptr;
}

} // namespace SliderDataFilter

BSOS_REGISTER_FEATURE(sliderdata, "filtro de slider data", HostApp::OutfitStudio,
	Enabled, SliderDataFilter::Install, SliderDataFilter::Uninstall)
