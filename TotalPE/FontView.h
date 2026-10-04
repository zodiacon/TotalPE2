#pragma once

#include "ViewBase.h"

// A font resource (TrueType, OpenType or a Windows .fnt font): its name and sample text in a few sizes
class CFontView :
	public CViewBase<CFontView>,
	public CScrollImpl<CFontView> {
public:
	CFontView(IMainFrame* frame, PCWSTR title);
	~CFontView();

	CString GetTitle() const override;

	// False if the font cannot be loaded; the view then says so
	bool SetData(std::span<const std::byte> data);
	void DoPaint(CDCHandle dc);

	BEGIN_MSG_MAP(CFontView)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		CHAIN_MSG_MAP(CScrollImpl<CFontView>)
		CHAIN_MSG_MAP(CViewBase<CFontView>)
	ALT_MSG_MAP(1)
		CHAIN_MSG_MAP_ALT(CViewBase<CFontView>, 1)
	END_MSG_MAP()

private:
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) { return 1; }

	// draws the view (or only measures it, without a DC to draw on); returns its size
	CSize Render(CDCHandle dc, bool draw);

	CString m_Title;
	std::wstring m_FaceName, m_Message;
	HANDLE m_hFont{ nullptr };
	DWORD m_FontCount{ 0 };
};
