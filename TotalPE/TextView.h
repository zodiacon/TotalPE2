#pragma once

#include "ViewBase.h"

class CTextView :
	public CViewBase<CTextView> {
public:
	CTextView(IMainFrame* frame, PCWSTR title);

	void SetXmlText(PCWSTR text);

	CString GetTitle() const override;

	BEGIN_MSG_MAP(CTextView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CViewBase)
	ALT_MSG_MAP(1)
	END_MSG_MAP()

private:
	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

	CString m_Title;
	CEdit m_Edit;
};

