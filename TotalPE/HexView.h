#pragma once

#include "ViewBase.h"
#include <SimpleHexControl.h>
#include <CustomSplitterWindow.h>
#include "resource.h"
#include "HexInspector.h"
#include "HexFind.h"
#include "PERegions.h"

class PEFile;

class CHexView : public CViewBase<CHexView> {
public:
	explicit CHexView(IMainFrame* frame, CString const& title = L"");

	CString GetTitle() const override {
		return m_Title;
	}

	void UpdateUI(bool first = false) const;

	CHexControl& Hex();

	bool SetData(PEFile const& pe, uint32_t offset, uint32_t size);
	bool SetData(std::span<const std::byte> data);
	bool SetData(PVOID address, uint32_t size, bool copy = false);

	// Marks the structure of a PE file (headers, sections, directories...) with colors and enables
	// RVA and symbol information in the inspector. The data must be (part of) the file starting at file offset 'bias'.
	void SetPE(PEFile const& pe, std::vector<HexRegion> regions);
	void ShowInspector(bool show);

	enum {
		ID_DATASIZE_1BYTE = 0x1000,
		ID_DATASIZE_2BYTES,
		ID_DATASIZE_4BYTES,
		ID_DATASIZE_8BYTES,
		ID_EXPORT,
		ID_BYTES_PER_LINE,
	};
	static constexpr UINT FirstBookmarkCommand = 0x7000;
	static constexpr int MaxBookmarkCommands = 64;

	void ClearData();

	int64_t GetNavigationPosition() const override;
	void SetNavigationPosition(int64_t position) override;

	BEGIN_MSG_MAP(CHexView)
		COMMAND_RANGE_HANDLER(ID_DATASIZE_1BYTE, ID_DATASIZE_8BYTES, OnChangeDataSize)
		COMMAND_ID_HANDLER(ID_HEX_INSPECTOR, OnToggleInspector)
		COMMAND_ID_HANDLER(ID_HEX_BIGENDIAN, OnToggleBigEndian)
		NOTIFY_CODE_HANDLER(NMHX_SELECTION_CHANGED, OnSelectionChanged)
		NOTIFY_CODE_HANDLER(NMHX_CARET_CHANGED, OnCaretChanged)
		NOTIFY_CODE_HANDLER(TBN_DROPDOWN, OnDropDown)
		NOTIFY_CODE_HANDLER(NM_RCLICK, OnRightClick)
		COMMAND_ID_HANDLER(ID_EXPORT, OnSave)
		MESSAGE_HANDLER(::RegisterWindowMessage(L"WTLHelperUpdateTheme"), OnUpdateTheme)
		MESSAGE_HANDLER(WM_UPDATE_DARKMODE, OnUpdateTheme)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		CHAIN_MSG_MAP(CViewBase<CHexView>)
	ALT_MSG_MAP(1)
		COMMAND_RANGE_HANDLER(ID_BYTESPERLINE_8, ID_BYTESPERLINE_64, OnChangeBytesPerLine)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_RANGE_HANDLER(ID_HEX_COPYAS_HEX, ID_HEX_COPYAS_BASE64, OnCopyAs)
		COMMAND_ID_HANDLER(ID_HEX_BOOKMARK_TOGGLE, OnBookmarkToggle)
		COMMAND_ID_HANDLER(ID_HEX_BOOKMARK_NEXT, OnBookmarkNext)
		COMMAND_ID_HANDLER(ID_HEX_BOOKMARK_PREV, OnBookmarkPrev)
		COMMAND_ID_HANDLER(ID_HEX_BOOKMARK_CLEAR, OnBookmarkClear)
		COMMAND_RANGE_HANDLER(FirstBookmarkCommand, FirstBookmarkCommand + MaxBookmarkCommands - 1, OnBookmarkGoto)
		COMMAND_ID_HANDLER(ID_HEX_INSPECTOR, OnToggleInspector)
		COMMAND_ID_HANDLER(ID_HEX_BIGENDIAN, OnToggleBigEndian)
		COMMAND_ID_HANDLER(ID_EDIT_FIND, OnFind)
		COMMAND_ID_HANDLER(ID_EDIT_FIND_NEXT, OnFindNext)
		COMMAND_ID_HANDLER(ID_EDIT_FIND_PREVIOUS, OnFindPrevious)
		CHAIN_MSG_MAP_ALT(CViewBase<CHexView>, 1)
	END_MSG_MAP()

private:
	void UpdateColors();
	void RebuildHighlights();
	void UpdateInspector();
	void BuildCopyAsMenu(CMenuHandle menu) const;
	void BuildBookmarksMenu(CMenuHandle menu) const;
	void GotoBookmark(int64_t offset);
	bool DoFind(bool forward, bool fromDialog);
	std::wstring RegionAt(int64_t fileOffset) const;
	COLORREF RegionTextColor() const;
	COLORREF RegionBackColor(int color) const;

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;
	LRESULT OnCopyAs(WORD, WORD id, HWND, BOOL&);
	LRESULT OnDropDown(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/);
	LRESULT OnSave(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnChangeDataSize(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnChangeBytesPerLine(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnUpdateTheme(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSelectionChanged(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/);
	LRESULT OnCaretChanged(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/);
	LRESULT OnRightClick(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/);
	LRESULT OnToggleInspector(WORD, WORD, HWND, BOOL&);
	LRESULT OnToggleBigEndian(WORD, WORD, HWND, BOOL&);
	LRESULT OnBookmarkToggle(WORD, WORD, HWND, BOOL&);
	LRESULT OnBookmarkNext(WORD, WORD, HWND, BOOL&);
	LRESULT OnBookmarkPrev(WORD, WORD, HWND, BOOL&);
	LRESULT OnBookmarkClear(WORD, WORD, HWND, BOOL&);
	LRESULT OnBookmarkGoto(WORD, WORD id, HWND, BOOL&);
	LRESULT OnFind(WORD, WORD, HWND, BOOL&);
	LRESULT OnFindNext(WORD, WORD, HWND, BOOL&);
	LRESULT OnFindPrevious(WORD, WORD, HWND, BOOL&);

	CCustomSplitterWindow m_Splitter;
	CHexControl m_Hex;
	CHexInspector m_Inspector;
	CString m_Title;
	wil::unique_mapview_ptr<BYTE> m_Ptr;
	std::unique_ptr<IBufferManager> m_Buffer;
	CToolBarCtrl m_tb;

	PEFile const* m_PE{ nullptr };
	std::vector<HexRegion> m_Regions;
	std::vector<int64_t> m_Bookmarks;	// buffer offsets, sorted
	HexFindOptions m_FindOptions;
	HexPattern m_Pattern;
	bool m_InspectorVisible{ false };
	bool m_ColorRegions{ false };	// color the hex view by PE structure (set by SetPE)
};
