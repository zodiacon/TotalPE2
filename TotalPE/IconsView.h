#pragma once

#include "ViewBase.h"

class CIconsView :
	public CViewBase<CIconsView>,
	public CScrollImpl<CIconsView> {
public:
	CIconsView(IMainFrame* frame, PCWSTR title);

	void SetGroupIconData(std::span<const std::byte> data);
	void SetIconData(std::span<const std::byte> data, bool icon);
	void DoPaint(CDCHandle);
	void UpdateUI(bool first = false) const;

	CString GetTitle() const override;

	BEGIN_MSG_MAP(CIconsView)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		//MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		CHAIN_MSG_MAP(CScrollImpl<CIconsView>)
		CHAIN_MSG_MAP(CViewBase)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_ICON_EXPORT, OnExportIcon)
	END_MSG_MAP()

private:
	struct IconData {
		CIconHandle Icon;
		int Size;
		UINT Id;
		UINT Colors;
		CRect Rect;
	};
	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnEraseBkgnd(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnExportIcon(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// a single icon or cursor (RT_ICON, RT_CURSOR): its resource data is what Export saves
	CIconHandle m_Icon;
	std::vector<std::byte> m_IconData;
	bool m_IsIcon{ true };
	CRect m_IconRect;
	CString m_Title;
	int m_IconSize{ 0 };
	std::vector<IconData> m_Icons;
	int m_SelectedIcon{ -1 };

};

