#include "pch.h"
#include "SecurityView.h"
#include "PEStrings.h"
#include <SortHelper.h>
#include <cryptuiapi.h>
#include <ImageHlp.h>
#include <PEFile.h>

#pragma comment(lib, "Cryptui")
#pragma comment(lib, "Crypt32")
#pragma comment(lib, "imagehlp")

CString CSecurityView::GetTitle() const {
	return L"Security";
}

CString CSecurityView::GetColumnText(HWND, int row, int col) const {
	auto& item = m_Items[row];

	switch (col) {
		case 0: return PEStrings::CertificateTypeToString(item.WinCert.wCertificateType);
		case 1: return std::format(L"0x{:X}", item.WinCert.wRevision).c_str();
		case 2: return std::format(L"0x{:X}", item.WinCert.dwLength).c_str();
	}
	return CString();
}

void CSecurityView::DoSort(SortInfo const* si) {
}

LRESULT CSecurityView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPSIBLINGS, WS_EX_CLIENTEDGE);
	m_List.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	auto cm = GetColumnManager(m_List);
	cm->AddColumn(L"Certificate Type", LVCFMT_LEFT, 140);
	cm->AddColumn(L"Revision", LVCFMT_RIGHT, 80);
	cm->AddColumn(L"Data Size", LVCFMT_RIGHT, 100);
	cm->UpdateColumns();

	m_HexView.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPSIBLINGS);
	m_HexView.SetStatic(true);
	m_Splitter.SetSplitterPanes(m_List, m_HexView);
	m_Splitter.SetSplitterPosPct(15);

	BuildItems();

	return 0;
}

void CSecurityView::BuildItems() {
	m_Items = *m_PE->GetSecurity();
	m_List.SetItemCount((int)m_Items.size());
}

void CSecurityView::OnStateChanged(HWND h, int from, int to, DWORD oldState, DWORD newState) {
	if (newState & LVIS_SELECTED) {
		m_HexView.SetData(m_Items[from].WinCert.bCertificate, m_Items[from].WinCert.dwLength);
	}
	UpdateUI();
}

void CSecurityView::UpdateUI(bool first) {
}

