#include "pch.h"
#include "DpiSupport.h"

namespace {
	// the buttons a tool bar was made with, and the size of their images (at 96 DPI)
	struct ToolBarData {
		std::vector<ToolBarButtonInfo> Buttons;
		int Size;
		int Dpi;	// the tool bar's, as it was laid out
	};

	// the band of the rebar the tool bar is in (-1: none)
	int FindBand(CToolBarCtrl tb) {
		CReBarCtrl rebar(tb.GetParent());
		WCHAR name[32];
		if (!rebar || !::GetClassName(rebar, name, _countof(name)) || _wcsicmp(name, REBARCLASSNAME) != 0)
			return -1;
		for (UINT i = 0; i < rebar.GetBandCount(); i++) {
			REBARBANDINFO rbbi{ sizeof(rbbi), RBBIM_CHILD };
			if (rebar.GetBandInfo(i, &rbbi) && rbbi.hwndChild == tb)
				return (int)i;
		}
		return -1;
	}

	// the band as it was (its sizes, and where the buttons ended), to be carried over to the tool bar's new size
	struct BandState {
		int Band{ -1 };
		REBARBANDINFO Info{ sizeof(REBARBANDINFO), RBBIM_CHILDSIZE | RBBIM_SIZE | RBBIM_IDEALSIZE };
		int First{ 0 }, Last{ 0 };	// the right ends of the first and the last buttons
	};

	BandState GetBandState(CToolBarCtrl tb) {
		BandState state;
		int count = tb.GetButtonCount();
		if (count == 0 || (state.Band = FindBand(tb)) < 0)
			return state;
		CReBarCtrl(tb.GetParent()).GetBandInfo(state.Band, &state.Info);
		CRect rc;
		tb.GetItemRect(0, &rc);
		state.First = rc.right;
		tb.GetItemRect(count - 1, &rc);
		state.Last = rc.right;
		return state;
	}

	//
	// The band of the rebar the tool bar is in, for the tool bar's new size. Its widths are as the view made them: one that was
	// the width of the first button or of all of them is that again, one wider than all of them (room for the gripper) as
	// much wider at the new DPI.
	//
	void UpdateBand(CToolBarCtrl tb, BandState const& before, UINT dpi, UINT oldDpi) {
		int count = tb.GetButtonCount();
		if (before.Band < 0 || count == 0)
			return;
		CRect first, last;
		tb.GetItemRect(0, &first);
		tb.GetItemRect(count - 1, &last);
		auto width = [&](UINT value) -> UINT {
			if ((int)value == before.First)
				return first.right;
			if ((int)value >= before.Last)
				return last.right + DpiHelper::Scale((int)value - before.Last, dpi, oldDpi);
			return DpiHelper::Scale((int)value, dpi, oldDpi);
		};
		auto rbbi = before.Info;
		rbbi.cx = width(rbbi.cx);
		rbbi.cxIdeal = width(rbbi.cxIdeal);
		rbbi.cxMinChild = width(rbbi.cxMinChild);
		rbbi.cyMinChild = last.Height();
		CReBarCtrl rebar(tb.GetParent());
		rebar.SetBandInfo(before.Band, &rbbi);
		// (the frame the rebar is in lays itself out again: the rebar may be of another height)
		auto frame = rebar.GetParent();
		CRect client;
		frame.GetClientRect(&client);
		frame.SendMessage(WM_SIZE, SIZE_RESTORED, MAKELPARAM(client.Width(), client.Height()));
	}

	LRESULT CALLBACK ToolBarProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR refData) {
		auto data = reinterpret_cast<ToolBarData*>(refData);
		switch (msg) {
			case WM_DPICHANGED_AFTERPARENT:
			{
				// (the band measured before the buttons change: at the old DPI)
				auto before = GetBandState(hWnd);
				UINT oldDpi = data->Dpi;
				auto result = ::DefSubclassProc(hWnd, msg, wp, lp);
				data->Dpi = DpiHelper::GetDpi(hWnd);
				ToolbarHelper::UpdateToolBarImages(hWnd, data->Buttons.data(), (int)data->Buttons.size(), data->Size);
				UpdateBand(hWnd, before, data->Dpi, oldDpi);
				return result;
			}
			case WM_NCDESTROY:
				::RemoveWindowSubclass(hWnd, ToolBarProc, id);
				delete data;
				break;
		}
		return ::DefSubclassProc(hWnd, msg, wp, lp);
	}

	void ResetImageList(HWND hWnd, HIMAGELIST images) {
		WCHAR name[32];
		if (!::GetClassName(hWnd, name, _countof(name)))
			return;
		if (_wcsicmp(name, WC_LISTVIEW) == 0) {
			for (int kind : { LVSIL_NORMAL, LVSIL_SMALL, LVSIL_STATE })
				if (ListView_GetImageList(hWnd, kind) == images)
					ListView_SetImageList(hWnd, images, kind);
		}
		else if (_wcsicmp(name, WC_TREEVIEW) == 0) {
			for (int kind : { TVSIL_NORMAL, TVSIL_STATE })
				if (TreeView_GetImageList(hWnd, kind) == images)
					TreeView_SetImageList(hWnd, images, kind);
		}
	}

	std::vector<Dpi::ImageSet*> s_ImageSets;

	// a list view or tree view whose DPI changed: the images of its sets for its new DPI
	LRESULT CALLBACK ImageSetProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR tree) {
		switch (msg) {
			case WM_DPICHANGED_AFTERPARENT:
			{
				auto result = ::DefSubclassProc(hWnd, msg, wp, lp);
				if (s_ImageSets.empty())
					return result;
				auto dpi = DpiHelper::GetDpi(hWnd);
				if (tree) {
					for (int kind : { TVSIL_NORMAL, TVSIL_STATE })
						if (auto set = Dpi::ImageSet::Of(TreeView_GetImageList(hWnd, kind)))
							TreeView_SetImageList(hWnd, set->For(dpi), kind);
				}
				else {
					for (int kind : { LVSIL_NORMAL, LVSIL_SMALL, LVSIL_STATE })
						if (auto set = Dpi::ImageSet::Of(ListView_GetImageList(hWnd, kind)))
							ListView_SetImageList(hWnd, set->For(dpi), kind);
				}
				return result;
			}
			case WM_NCDESTROY:
				::RemoveWindowSubclass(hWnd, ImageSetProc, id);
				break;
		}
		return ::DefSubclassProc(hWnd, msg, wp, lp);
	}

	void CALLBACK OnCreate(HWINEVENTHOOK, DWORD, HWND hWnd, LONG object, LONG child, DWORD, DWORD) {
		if (object != OBJID_WINDOW || child != CHILDID_SELF || !hWnd || !::IsWindow(hWnd))
			return;
		WCHAR name[32];
		if (!::GetClassName(hWnd, name, _countof(name)))
			return;
		bool tree = _wcsicmp(name, WC_TREEVIEW) == 0;
		if (tree || _wcsicmp(name, WC_LISTVIEW) == 0)
			::SetWindowSubclass(hWnd, ImageSetProc, 1, tree);
	}
}

Dpi::ImageSet::ImageSet(int size, std::function<void(CImageList&)> fill) : m_Size(size), m_Fill(std::move(fill)) {
	s_ImageSets.push_back(this);
}

Dpi::ImageSet::~ImageSet() {
	std::erase(s_ImageSets, this);
	for (auto& [dpi, list] : m_Lists)
		list.Destroy();
}

HIMAGELIST Dpi::ImageSet::For(UINT dpi) {
	auto& list = m_Lists[dpi];
	if (!list) {
		auto size = DpiHelper::Scale(m_Size, dpi);
		list.Create(size, size, ILC_COLOR32, 32, 16);
		m_Fill(list);
	}
	return list;
}

bool Dpi::ImageSet::Empty() const {
	return m_Lists.empty();
}

void Dpi::ImageSet::ForEach(std::function<void(CImageList&)> const& change) {
	for (auto& [dpi, list] : m_Lists)
		change(list);
}

Dpi::ImageSet* Dpi::ImageSet::Of(HIMAGELIST images) {
	if (!images)
		return nullptr;
	for (auto set : s_ImageSets)
		for (auto& [dpi, list] : set->m_Lists)
			if (list == images)
				return set;
	return nullptr;
}

void Dpi::Install() {
	// (out of context: the event comes to this thread's message loop, after the control is made)
	static bool installed = false;
	if (installed)
		return;
	installed = true;
	::SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE, nullptr, OnCreate, ::GetCurrentProcessId(), ::GetCurrentThreadId(),
		WINEVENT_OUTOFCONTEXT);
}

HWND Dpi::CreateToolBar(HWND hParent, ToolBarButtonInfo const* buttons, int count, int size) {
	auto hToolBar = ToolbarHelper::CreateAndInitToolBar(hParent, buttons, count, size);
	if (hToolBar) {
		auto data = new ToolBarData{ { buttons, buttons + count }, size, (int)DpiHelper::GetDpi(hToolBar) };
		if (!::SetWindowSubclass(hToolBar, ToolBarProc, 1, (DWORD_PTR)data))
			delete data;
	}
	return hToolBar;
}

void Dpi::ImageListResized(HIMAGELIST images) {
	if (!images)
		return;
	// (the views may be in floating windows of the docking area: all the windows of the thread)
	::EnumThreadWindows(::GetCurrentThreadId(), [](HWND top, LPARAM images) {
		ResetImageList(top, (HIMAGELIST)images);
		::EnumChildWindows(top, [](HWND hWnd, LPARAM images) {
			ResetImageList(hWnd, (HIMAGELIST)images);
			return TRUE;
			}, images);
		return TRUE;
		}, (LPARAM)images);
}

void Dpi::RestorePlacement(HWND hWnd, WINDOWPLACEMENT wp, UINT dpi) {
	auto& rc = wp.rcNormalPosition;
	if (dpi == 0) {
		dpi = ::GetDpiForSystem();
		rc = DpiHelper::Scale(CRect(rc), dpi, USER_DEFAULT_SCREEN_DPI);
	}
	// Placed twice: the first time puts it on its monitor, where it takes the monitor's DPI (and its WM_DPICHANGED sizes it
	// from the DPI it had, not the one the size was saved at); the second gives it the saved size at that DPI
	::SetWindowPlacement(hWnd, &wp);
	auto now = DpiHelper::GetDpi(hWnd);
	if (now == dpi)
		return;
	rc.right = rc.left + DpiHelper::Scale(rc.right - rc.left, now, dpi);
	rc.bottom = rc.top + DpiHelper::Scale(rc.bottom - rc.top, now, dpi);
	::SetWindowPlacement(hWnd, &wp);
}

void Dpi::TakeSuggestedRect(HWND hWnd, LPARAM lParam) {
	auto rc = reinterpret_cast<RECT const*>(lParam);
	::SetWindowPos(hWnd, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top, SWP_NOZORDER | SWP_NOACTIVATE);
}
