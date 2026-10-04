#pragma once

#include "ViewBase.h"

class CBitmapView :
	public CViewBase<CBitmapView>,
	public CZoomScrollImpl<CBitmapView> {
public:
	CBitmapView(IMainFrame* frame, PCWSTR title);

	CString GetTitle() const override;

	bool SetData(std::span<const std::byte> data);
	bool SetImage(std::span<const std::byte> data);
	void DoPaint(CDCHandle);

	BEGIN_MSG_MAP(CBitmapView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CZoomScrollImpl<CBitmapView>)
		CHAIN_MSG_MAP(CViewBase)
	ALT_MSG_MAP(1)

	END_MSG_MAP()

private:
	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	UINT m_Width{ 0 }, m_Height{ 0 };
	bool m_Alpha{ false };
	CBitmap m_bmp;
	CString m_Title;
};


