#pragma once

#include <functional>
#include <string>
#include <vector>
#include <Win32Api.h>

//
// Quick Open (Ctrl+P): a box to type in and the best matches below it, at the top of the frame. A name matches
// exactly, by its start, by the initials of its words (CFW: CreateFileW; sa: SECURITY_ATTRIBUTES), by a part of it, or by
// its letters in order; the better and shorter ones first. Enter (or a double-click) opens the one selected; Esc, or
// going elsewhere, closes it.
//
class CQuickOpen : public CWindowImpl<CQuickOpen> {
public:
	DECLARE_WND_CLASS_EX(L"Win32ApiUIQuickOpen", CS_DROPSHADOW, -1)

	std::function<void(size_t entry)> OnOpen;

	// (another machine: its entries)
	void SetApi(Win32Api& api);
	void SetImageList(HIMAGELIST images);
	void SetUIFont(HFONT font);
	// shown at the top of the owner's client area, empty
	void Show();
	BOOL PreTranslateMessage(MSG* pMsg);

	BEGIN_MSG_MAP(CQuickOpen)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_ACTIVATE, OnActivate)
		COMMAND_CODE_HANDLER(EN_CHANGE, OnTextChanged)
		NOTIFY_CODE_HANDLER(LVN_GETDISPINFO, OnGetDispInfo)
		NOTIFY_CODE_HANDLER(NM_DBLCLK, OnDoubleClick)
	END_MSG_MAP()

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnActivate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTextChanged(WORD, WORD, HWND, BOOL&);
	LRESULT OnGetDispInfo(int, LPNMHDR, BOOL&);
	LRESULT OnDoubleClick(int, LPNMHDR, BOOL&);

	void Search();
	void Accept();
	void UpdateLayout();
	// the names of the entries in lowercase, and the initials of their words (made when they are first searched)
	void Index();

	Win32Api* m_Api{ nullptr };
	std::vector<std::string> m_Lower, m_Initials;
	std::vector<size_t> m_Results;
	CEdit m_Edit;
	CListViewCtrl m_List;
	HFONT m_Font{ nullptr };
};
