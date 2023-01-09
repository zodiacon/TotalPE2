#pragma once

#include "ViewBase.h"
#include <SimpleHexControl.h>
#include "resource.h"

class PEFile;

class CHexView : public CViewBase<CHexView> {
public:
	explicit CHexView(IMainFrame* frame, CString const& title = L"");

	CString GetTitle() const override {
		return m_Title;
	}

	CHexControl& Hex();

	bool SetData(PEFile const& pe, uint32_t offset, uint32_t size);
	bool SetData(std::span<const std::byte> data);
	bool SetData(PVOID address, uint32_t size, bool copy = false);

	enum {
		ID_DATASIZE_1BYTE = 0x1000,
		ID_DATASIZE_2BYTES,
		ID_DATASIZE_4BYTES,
		ID_DATASIZE_8BYTES,
		ID_EXPORT,
		ID_BYTES_PER_LINE,
	};

	void ClearData();

	BEGIN_MSG_MAP(CHexView)
		COMMAND_RANGE_HANDLER(ID_DATASIZE_1BYTE, ID_DATASIZE_8BYTES, OnChangeDataSize)
		NOTIFY_CODE_HANDLER(TBN_DROPDOWN, OnDropDown)
		COMMAND_ID_HANDLER(ID_EXPORT, OnSave)
		MESSAGE_HANDLER(::RegisterWindowMessage(L"WTLHelperUpdateTheme"), OnUpdateTheme)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		CHAIN_MSG_MAP(CViewBase<CHexView>)
	ALT_MSG_MAP(1)
		COMMAND_RANGE_HANDLER(ID_BYTESPERLINE_8, ID_BYTESPERLINE_64, OnChangeBytesPerLine)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		CHAIN_MSG_MAP_ALT(CViewBase<CHexView>, 1)
	END_MSG_MAP()

private:
	void UpdateColors();

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;
	LRESULT OnDropDown(int /*idCtrl*/, LPNMHDR hdr, BOOL& /*bHandled*/);
	LRESULT OnSave(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnChangeDataSize(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnChangeBytesPerLine(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnUpdateTheme(UINT, WPARAM, LPARAM, BOOL&);

	CHexControl m_Hex;
	CString m_Title;
	wil::unique_mapview_ptr<BYTE> m_Ptr;
	std::unique_ptr<IBufferManager> m_Buffer;
	CToolBarCtrl m_tb;
};

