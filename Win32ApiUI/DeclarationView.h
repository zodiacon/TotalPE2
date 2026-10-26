#pragma once

#include <functional>
#include <string>
#include <vector>
#include <Win32Api.h>

//
// The C declaration of a function, type or constant of the Win32 metadata (in Scintilla, colored as C), with the history of
// what it showed: following the type a line refers to (Enter, a double-click, F12) shows that type here, and Back returns.
// The lines that refer to a type have its name underlined. With Ctrl, the owner is asked to open the type in a tab of its
// own (OnFollow). The mouse resting on a name shows what it is: the declaration of a type, function or constant (its first
// lines), the value of a value of an enum. One for each tab of the browser (CApiBrowser).
//
class CDeclarationView : public CWindowImpl<CDeclarationView> {
public:
	DECLARE_WND_CLASS_EX(L"Win32ApiUIDeclaration", 0, -1)

	explicit CDeclarationView(Win32Api& api);

	// a line's type followed: here (newTab false: the owner calls Navigate) or in a tab of its own
	std::function<void(std::string const& name, bool newTab)> OnFollow;
	// what it shows changed (its title, its documentation)
	std::function<void()> OnShownChanged;
	// its menu, before it is shown: the owner's commands (and their states) to it
	std::function<void(CMenu& menu)> OnExtendMenu;

	// What it shows of a struct or union: its declaration; its layout (the fields of the structs and unions in it too, with
	// their offsets from its start, and the padding); its layout on each machine (x86, x64, ARM64; the lines that differ
	// marked). Of the others: their declarations.
	enum class Mode { Declaration, Layout, Compare };
	void SetMode(Mode mode);
	// functions, methods and callbacks declared on a line each, without their parameters' notes (in the tips too)
	void SetSingleLine(bool singleLine);
	// the entry shown (an index into the API's entries; -1: none), the history unchanged
	void Show(int entry);
	// another entry shown, the one shown remembered (Back returns to it); false if there is no such entry
	bool Navigate(std::string const& name);
	bool CanGoBack() const;
	bool CanGoForward() const;
	void GoBack();
	void GoForward();
	// (a preview shows another entry: its history is not that one's)
	void ClearHistory();
	int Entry() const;
	// of the entry shown (empty: none)
	std::string Name() const;
	// the API of another machine: the entry of the name (the history is by name)
	void SetApi(Win32Api& api);
	// the colors and the font (the code font)
	void UpdateColors();
	// the code font (its height at 96 DPI): the host's (SetCodeFont), else Consolas of 10 points
	static LOGFONT CodeFont();
	// (an empty name: the default; the views there are, updated by UpdateColors)
	static void SetCodeFont(LOGFONT const& font);
	// the type the line of the caret refers to (empty: none)
	std::string ReferredAtCaret() const;
	// follows the type of the caret's line (false if it refers to none)
	bool FollowCaret(bool newTab);
	// selects a name in the declaration (a value of an enum), as a word of its own; false if it is not in it
	bool HighlightName(std::string const& name);
	void Copy();
	void SelectAll();
	BOOL PreTranslateMessage(MSG* pMsg);

	BEGIN_MSG_MAP(CDeclarationView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DPICHANGED_AFTERPARENT, OnDpiChanged)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		NOTIFY_CODE_HANDLER(SCN_DOUBLECLICK, OnDoubleClick)
		NOTIFY_CODE_HANDLER(SCN_DWELLSTART, OnDwellStart)
		NOTIFY_CODE_HANDLER(SCN_DWELLEND, OnDwellEnd)
		NOTIFY_CODE_HANDLER(SCN_INDICATORRELEASE, OnIndicatorRelease)
		MESSAGE_HANDLER(WM_FOLLOWLINE, OnFollowLine)
	END_MSG_MAP()

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnContextMenu(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDoubleClick(int, LPNMHDR, BOOL&);
	LRESULT OnDwellStart(int, LPNMHDR, BOOL&);
	LRESULT OnDwellEnd(int, LPNMHDR, BOOL&);
	// Ctrl+click on a link: its type followed (Ctrl+Shift: in a tab of its own), after the click
	LRESULT OnIndicatorRelease(int, LPNMHDR, BOOL&);
	static constexpr UINT WM_FOLLOWLINE = WM_APP + 10;	// wParam: the line, lParam: in a tab of its own
	LRESULT OnFollowLine(UINT, WPARAM wp, LPARAM lp, BOOL&) {
		FollowLine((int)wp, lp != 0);
		return 0;
	}
	// what the tip of a name says (empty: none)
	std::string TipOf(std::string const& name) const;

	LRESULT Sci(UINT msg, WPARAM wp = 0, LPARAM lp = 0) const {
		return ::SendMessage(m_Sci, msg, wp, lp);
	}
	void SetText(std::string const& text);
	void MarkLinks();
	// the text of the entry shown, as the mode says (its lines' types, the lines that differ)
	void Render();
	std::string LayoutText(Win32Api::Entry const& entry);
	std::string CompareText(Win32Api::Entry const& entry);
	bool FollowLine(int line, bool newTab);
	void Remember(std::string name);

	Win32Api* m_Api;
	HWND m_Sci{ nullptr };
	int m_Entry{ -1 };
	std::vector<std::string> m_LineTypes;	// what each line refers to
	std::vector<int> m_DifferentLines;		// (of a comparison)
	Mode m_Mode{ Mode::Declaration };
	bool m_SingleLine{ false };

	std::vector<std::string> m_Back, m_Forward;
};
