#pragma once

#include <FrameView.h>
#include "Interfaces.h"

template<typename T>
class CViewBase abstract : 
	public CFrameView<T, IMainFrame>, 
	public IView {
public:
	using BaseFrame = CFrameView<T, IMainFrame>;
	explicit CViewBase(IMainFrame* frame) : BaseFrame(frame) {}

	HWND GetHwnd() const override {
		return static_cast<const T*>(this)->m_hWnd;
	}

	HWND DoCreate(HWND hParent) {
		auto p = static_cast<T*>(this);
		auto hWnd = p->Create(hParent, p->rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN);
		if (hWnd)
			p->UpdateUI(true);
		return hWnd;
	}

	BEGIN_MSG_MAP(CViewBase)
		MESSAGE_HANDLER(WM_ACTIVATE, OnActivate)
		CHAIN_MSG_MAP(BaseFrame)
	END_MSG_MAP()

	LRESULT OnActivate(UINT /*uMsg*/, WPARAM active, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
		auto p = static_cast<T*>(this);
		p->Activate(active);
		if (active)
			p->UpdateUI();
		return 0;
	}

	void Activate(bool) {}
	void UpdateUI(bool = false) {}

};
