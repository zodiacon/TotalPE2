#pragma once

#include "ViewBase.h"

class CBitmapView :
	public CViewBase<CBitmapView>,
	public CZoomScrollImpl<CBitmapView> {
public:
	CBitmapView(IMainFrame* frame, PCWSTR title);

	CString GetTitle() const override;
	bool CanSave() const override;

	bool SetData(std::span<const std::byte> data);
	bool SetImage(std::span<const std::byte> data);
	void DoPaint(CDCHandle);

	BEGIN_MSG_MAP(CBitmapView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CZoomScrollImpl<CBitmapView>)
		CHAIN_MSG_MAP(CViewBase)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_FILE_SAVE, OnSave)
		CHAIN_MSG_MAP_ALT(CViewBase<CBitmapView>, 1)
	END_MSG_MAP()

private:
	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	// the image as a file: a bitmap resource gets its file header, other images are saved as they are
	LRESULT OnSave(WORD, WORD, HWND, BOOL&);

	UINT m_Width{ 0 }, m_Height{ 0 };
	bool m_Alpha{ false };
	std::vector<std::byte> m_Data;	// what the image was made from
	bool m_Dib{ false };			// a bitmap resource (a DIB without its file header), not an image file
	CBitmap m_bmp;
	CString m_Title;
};


