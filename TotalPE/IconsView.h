#pragma once

#include "ViewBase.h"

// An icon or a cursor (RT_ICON, RT_CURSOR), or the images of a group (RT_GROUP_ICON, RT_GROUP_CURSOR).
// Click an image of a group to select it. Save (and Export) saves the selected image as an .ico or .cur file;
// with no image selected, Save saves the whole group as one file.
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
	bool CanSave() const override;

	BEGIN_MSG_MAP(CIconsView)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLeftButtonDown)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		CHAIN_MSG_MAP(CScrollImpl<CIconsView>)
		CHAIN_MSG_MAP(CViewBase)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_ICON_EXPORT, OnExportIcon)
		COMMAND_ID_HANDLER(ID_FILE_SAVE, OnSave)
		CHAIN_MSG_MAP_ALT(CViewBase<CIconsView>, 1)
	END_MSG_MAP()

private:
	struct IconData {
		CIconHandle Icon;
		int Size;
		UINT Id;
		UINT Colors;
		CRect Rect;
		std::span<const std::byte> Data;	// the image's resource (RT_ICON or RT_CURSOR)
	};
	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnLeftButtonDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnExportIcon(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnSave(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	// the image of a group (or the icon itself) under a point of the client area; -1 if none
	int HitTest(CPoint pt) const;
	void Select(int index);
	// saves the resource data of an icon or a cursor image as a file of its own
	void SaveImage(std::span<const std::byte> data);
	void SaveGroup();
	CString FileName() const;

	// a single icon or cursor (RT_ICON, RT_CURSOR): its resource data is what Save and Export save
	CIconHandle m_Icon;
	std::vector<std::byte> m_IconData;
	CRect m_IconRect;
	int m_IconSize{ 0 };
	// a group
	std::vector<std::byte> m_GroupData;
	std::vector<IconData> m_Icons;
	int m_SelectedIcon{ -1 };

	bool m_IsIcon{ true };
	CString m_Title;
};
