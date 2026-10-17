#pragma once

//
// A strip of tabs that paints itself, in the colors of the light or the dark mode (a tab control's owner drawing is left to
// its parent in the dark mode). A tab's text can be in italics: a preview, which the next one previewed replaces. The
// selected tab, and the one under the mouse, have a close button (if the tabs can be closed); the middle button closes
// one too. Tabs too many to fit are narrowed (to a minimum), then scrolled: by the wheel, and to keep the selected one in
// view. What the user does is told through the callbacks.
//
class CTabStrip : public CWindowImpl<CTabStrip> {
public:
	DECLARE_WND_CLASS_EX(L"Win32ApiExplorerTabStrip", CS_DBLCLKS, -1)

	std::function<void(int)> OnSelected;			// the user selected a tab
	std::function<void(int)> OnCloseRequested;		// the close button, the middle button
	std::function<void(int)> OnDoubleClicked;
	std::function<void(int, CPoint)> OnContextMenu;	// the tab (-1: none), where (screen)

	void SetClosable(bool closable);
	void SetImageList(HIMAGELIST images);

	int Add(std::wstring text, int image = -1, bool italic = false);
	int Insert(int index, std::wstring text, int image = -1, bool italic = false);
	void Remove(int index);
	void RemoveAll();
	int GetCount() const;
	int GetSelected() const;
	// (no callback)
	void Select(int index);
	void SetText(int index, std::wstring text);
	std::wstring const& GetText(int index) const;
	void SetItalic(int index, bool italic);
	bool IsItalic(int index) const;
	void SetImage(int index, int image);
	// the height it needs
	int GetIdealHeight() const;
	void UpdateColors();
	// the font of the texts (an italic one is made of it); null: the system's (of messages)
	void SetFont(LOGFONT const* font);

	BEGIN_MSG_MAP(CTabStrip)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
		MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLButtonDblClk)
		MESSAGE_HANDLER(WM_MBUTTONUP, OnMButtonUp)
		MESSAGE_HANDLER(WM_RBUTTONUP, OnRButtonUp)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
		MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
		MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
	END_MSG_MAP()

private:
	struct Tab {
		std::wstring Text;
		int Image{ -1 };
		bool Italic{ false };
		int Width{ 0 };		// as laid out
		int Left{ 0 };
	};

	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) {
		return 1;
	}
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonUp(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLButtonDblClk(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMButtonUp(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnRButtonUp(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseMove(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseLeave(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnMouseWheel(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSettingChange(UINT, WPARAM, LPARAM, BOOL&);

	void CreateFonts();
	void Layout();
	void EnsureVisible(int index);
	int HitTest(CPoint pt, bool* onClose = nullptr) const;
	CRect TabRect(int index) const;
	CRect CloseRect(int index) const;
	bool HasCloseButton(int index) const;

	std::vector<Tab> m_Tabs;
	int m_Selected{ -1 };
	int m_Hot{ -1 };
	bool m_HotClose{ false };
	int m_Pressed{ -1 };		// a close button pressed
	int m_Scroll{ 0 };			// how far the tabs are scrolled to the left
	int m_TotalWidth{ 0 };
	bool m_Closable{ true };
	bool m_Tracking{ false };
	HIMAGELIST m_Images{ nullptr };
	CFont m_Font, m_ItalicFont;
	int m_FontHeight{ 16 };
	std::optional<LOGFONT> m_CustomFont;
};
