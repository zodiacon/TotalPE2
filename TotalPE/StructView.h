#pragma once

#include "ViewBase.h"
#include "TreeListView.h"
#include <CustomSplitterWindow.h>
#include "HexView.h"

class CStructView : public CViewBase<CStructView> {
public:
	CStructView(IMainFrame* frame, DiaSymbol const& sym, CString const& title = L"");

	CString GetTitle() const override {
		return m_Title;
	}

	void SetValue(PVOID address);

	BEGIN_MSG_MAP(CStructView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CViewBase<CStructView>)
		ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
	END_MSG_MAP()

private:
	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;

	CCustomHorSplitterWindow m_Splitter;
	CTreeListView m_TL;
	CHexView m_HexView;
	CString m_Title;
	DiaSymbol const& m_Object;
};
