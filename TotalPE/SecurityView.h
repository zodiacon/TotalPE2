#pragma once

#include "ViewBase.h"
#include "resource.h"
#include <VirtualListView.h>
#include <CustomSplitterWindow.h>

class PEFile;

class CSecurityView :
	public CViewBase<CSecurityView>,
	public CVirtualListView<CSecurityView> {
public:
	CSecurityView(IMainFrame* frame, PEFile const& pe) : CViewBase(frame), m_PE(pe) {}
	~CSecurityView();

	CString GetTitle() const override;
	CString GetColumnText(HWND h, int row, int col) const;
	void DoSort(SortInfo const* si);
	void OnStateChanged(HWND h, int from, int to, DWORD oldState, DWORD newState);
	bool OnRightClickList(HWND h, int row, int col, POINT const& pt) const;
	void UpdateUI(bool first = false);

	BEGIN_MSG_MAP(CSecurityView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CSecurityView>)
		CHAIN_MSG_MAP(CViewBase<CSecurityView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_SECURITY_VIEWCERTIFICATE, OnViewCertificate)
		CHAIN_MSG_MAP_ALT(CViewBase<CSecurityView>, 1)
	END_MSG_MAP()

	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;
	LRESULT OnViewCertificate(WORD, WORD, HWND, BOOL&) const;

private:
	void BuildItems();
	void PopulateCertDetails(std::vector<BYTE> const& certData);
	void AddProperty(std::wstring_view name, std::wstring_view value);
	void FreeCertContext();

	static std::wstring FileTimeToString(FILETIME const& ft);
	static std::wstring BytesToHex(BYTE const* data, DWORD len, wchar_t sep = L' ');

	CListViewCtrl            m_List, m_DetailList;
	CCustomHorSplitterWindow m_Splitter;
	std::vector<PESecurity>  m_Items;
	PCCERT_CONTEXT           m_CurrentCert{};
	HCERTSTORE               m_CurrentStore{};
	HCRYPTMSG                m_CurrentMsg{};
	PEFile const&            m_PE;
};
