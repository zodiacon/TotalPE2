#pragma once

#include "ViewBase.h"
#include <SimpleHexControl.h>

class PEFile;

class CHexView : public CViewBase<CHexView> {
public:
	explicit CHexView(IMainFrame* frame, CString const& title = L"");

	CString GetTitle() const override {
		return m_Title;
	}

	CHexControl& Hex();

	bool SetData(PEFile const& pe, uint32_t offset, uint32_t size);
	void ClearData();

	BEGIN_MSG_MAP(CHexView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CViewBase<CHexView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
	END_MSG_MAP()

private:
	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;

	CHexControl m_Hex;
	CString m_Title;
	wil::unique_mapview_ptr<BYTE> m_Ptr;
	std::unique_ptr<IBufferManager> m_Buffer;
};

