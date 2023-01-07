#pragma once

#include <FrameView.h>
#include "Interfaces.h"

template<typename T, typename TBase = CWindow>
class CViewBase abstract : 
	public CFrameView<T, IMainFrame, TBase>, 
	public CAutoUpdateUI<T>,
	public CIdleHandler,
	public CMessageFilter,
	public IView {
public:
	using BaseFrame = CFrameView<T, IMainFrame, TBase>;
	explicit CViewBase(IMainFrame* frame) : BaseFrame(frame) {}

	BOOL OnIdle() override {
		auto p = static_cast<T*>(this);
		p->UIUpdateToolBar();
		return FALSE;
	}

	BOOL PreTranslateMessage(MSG* pMsg) {
		if (BaseFrame::PreTranslateMessage(pMsg))
			return TRUE;

		return FALSE;
	}

	HWND GetHwnd() const override {
		return static_cast<const T*>(this)->m_hWnd;
	}

	HWND DoCreate(HWND hParent) {
		auto p = static_cast<T*>(this);
		auto hWnd = p->Create(hParent, p->rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN);
		if (hWnd) {
			p->UpdateUI(true);
		}
		return hWnd;
	}

	BEGIN_MSG_MAP(CViewBase)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_ACTIVATE, OnActivate)
		CHAIN_MSG_MAP(CAutoUpdateUI<T>)
		CHAIN_MSG_MAP(BaseFrame)
	END_MSG_MAP()

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM active, LPARAM lParam, BOOL& /*bHandled*/) {
		m_Handlers = true;
		_Module.GetMessageLoop()->AddIdleHandler(this);
		_Module.GetMessageLoop()->AddMessageFilter(this);
		return 0;
	}

	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM active, LPARAM lParam, BOOL& bHandled) {
		if (m_Handlers) {
			_Module.GetMessageLoop()->RemoveIdleHandler(this);
			_Module.GetMessageLoop()->RemoveMessageFilter(this);
		}
		bHandled = FALSE;
		return 0;
	}

	LRESULT OnActivate(UINT /*uMsg*/, WPARAM active, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
		auto p = static_cast<T*>(this);
		p->Activate(active);
		if (active)
			p->UpdateUI(false);
		return 0;
	}

	void Activate(bool) {}
	void UpdateUI(bool = false) {}

	bool m_Handlers{ false };
};
