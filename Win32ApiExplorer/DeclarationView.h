#pragma once

#include <Win32Api.h>

//
// The C declaration of a function, type or constant of the Win32 metadata (in Scintilla, colored as C), with the history of
// what it showed: following the type a line refers to (Enter, a double-click, F12) shows that type here, and Back returns.
// The lines that refer to a type have its name underlined. With Ctrl, the owner is asked to open the type in a tab of its
// own (OnFollow). One for each tab of the main window.
//
class CDeclarationView : public CWindowImpl<CDeclarationView> {
public:
	DECLARE_WND_CLASS_EX(L"Win32ApiExplorerDeclaration", 0, -1)

	explicit CDeclarationView(Win32Api& api);

	// a line's type followed: here (newTab false: the owner calls Navigate) or in a tab of its own
	std::function<void(std::string const& name, bool newTab)> OnFollow;
	// what it shows changed (its title, its documentation)
	std::function<void()> OnShownChanged;

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
	void UpdateColors();
	// the type the line of the caret refers to (empty: none)
	std::string ReferredAtCaret() const;
	// follows the type of the caret's line (false if it refers to none)
	bool FollowCaret(bool newTab);
	void Copy();
	void SelectAll();
	BOOL PreTranslateMessage(MSG* pMsg);

	BEGIN_MSG_MAP(CDeclarationView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		NOTIFY_CODE_HANDLER(SCN_DOUBLECLICK, OnDoubleClick)
	END_MSG_MAP()

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnContextMenu(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnDoubleClick(int, LPNMHDR, BOOL&);

	LRESULT Sci(UINT msg, WPARAM wp = 0, LPARAM lp = 0) const {
		return ::SendMessage(m_Sci, msg, wp, lp);
	}
	void SetText(std::string const& text);
	void MarkLinks();
	bool FollowLine(int line, bool newTab);
	void Remember(std::string name);

	Win32Api* m_Api;
	HWND m_Sci{ nullptr };
	int m_Entry{ -1 };
	std::vector<std::string> m_LineTypes;	// what each line refers to
	std::vector<std::string> m_Back, m_Forward;
};
