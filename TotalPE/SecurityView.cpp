#include "pch.h"
#include "SecurityView.h"
#include "PEStrings.h"
#include "resource.h"
#include <PEFile.h>
#include <SortHelper.h>
#include <ClipboardHelper.h>
#include <ListViewHelper.h>
#include <wincrypt.h>
#include <cryptuiapi.h>
#include <WTLHelper.h>

#pragma comment(lib, "Crypt32")
#pragma comment(lib, "Cryptui")
#pragma comment(lib, "Bcrypt")

CSecurityView::~CSecurityView() {
	FreeCertContext();
}

CString CSecurityView::GetTitle() const {
	return L"Security";
}

CString CSecurityView::GetColumnText(HWND, int row, int col) const {
	auto& item = m_Items[row];
	switch (col) {
		case 0: return PEStrings::CertificateTypeToString(item.WinCert.wCertificateType);
		case 1: return std::format(L"0x{:04X}", item.WinCert.wRevision).c_str();
		case 2: return std::format(L"{} bytes", item.WinCert.dwLength - sizeof(WIN_CERTIFICATE)).c_str();
	}
	return {};
}

void CSecurityView::DoSort(SortInfo const*) {}

LRESULT CSecurityView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr,
		WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, WS_EX_CLIENTEDGE);

	m_List.Create(m_Splitter, rcDefault, nullptr,
		WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	auto cm = GetColumnManager(m_List);
	cm->AddColumn(L"Certificate Type", LVCFMT_LEFT,  160);
	cm->AddColumn(L"Revision",         LVCFMT_RIGHT,  80);
	cm->AddColumn(L"Data Size",        LVCFMT_RIGHT, 100);
	cm->UpdateColumns();

	m_DetailList.Create(m_Splitter, rcDefault, nullptr,
		WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS);
	m_DetailList.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	m_DetailList.InsertColumn(0, L"Property", LVCFMT_LEFT,  160);
	m_DetailList.InsertColumn(1, L"Value",    LVCFMT_LEFT,  900);

	m_Splitter.SetSplitterPanes(m_List, m_DetailList);
	m_Splitter.SetSplitterPosPct(25);

	BuildItems();
	return 0;
}

void CSecurityView::BuildItems() {
	m_Items = *m_PE.GetSecurity();
	m_List.SetItemCount((int)m_Items.size());

	// decode the signatures, check the hashes and ask the system if they are trusted
	CWaitCursor wait;
	m_Auth = AnalyzeAuthenticode(m_PE);
	if (!m_Items.empty())
		m_List.SelectItem(0);
}

void CSecurityView::OnStateChanged(HWND h, int from, int to, DWORD oldState, DWORD newState) {
	if (h == m_List && (newState & LVIS_SELECTED))
		PopulateCertDetails(from);
	UpdateUI();
}

bool CSecurityView::OnRightClickList(HWND h, int row, int col, POINT const& pt) const {
	if (h != m_List) return false;
	CMenu menu;
	menu.LoadMenu(IDR_CONTEXT);
	return Frame()->ShowContextMenu(menu.GetSubMenu(7), 0, pt.x, pt.y);
}

void CSecurityView::UpdateUI(bool) {
	auto& ui = Frame()->GetUI();
	ui.UIEnable(ID_SECURITY_VIEWCERTIFICATE, m_CurrentCert != nullptr);
	ui.UIEnable(ID_EDIT_COPY, m_List.GetSelectedCount() > 0 || m_DetailList.GetSelectedCount() > 0);
}

LRESULT CSecurityView::OnCopy(WORD, WORD, HWND, BOOL&) const {
	auto hFocus = ::GetFocus();
	if (hFocus == m_List || hFocus == m_DetailList) {
		ClipboardHelper::CopyText(m_hWnd, ListViewHelper::GetSelectedRowsAsString(hFocus, L"\t"));
	}
	return 0;
}

LRESULT CSecurityView::OnViewCertificate(WORD, WORD, HWND, BOOL&) const {
	if (m_CurrentCert) {
		WTLHelper::SuspendHook();
		::CryptUIDlgViewContext(CERT_STORE_CERTIFICATE_CONTEXT, m_CurrentCert, m_hWnd, nullptr, 0, nullptr);
		WTLHelper::ResumeHook();
	}
	return 0;
}

// ── helpers ───────────────────────────────────────────────────────────────────

void CSecurityView::FreeCertContext() {
	if (m_CurrentCert)  { CertFreeCertificateContext(m_CurrentCert); m_CurrentCert = nullptr; }
	if (m_CurrentStore) { CertCloseStore(m_CurrentStore, 0);         m_CurrentStore = nullptr; }
	if (m_CurrentMsg)   { CryptMsgClose(m_CurrentMsg);               m_CurrentMsg = nullptr; }
}

void CSecurityView::AddProperty(std::wstring_view name, std::wstring_view value) {
	int row = m_DetailList.GetItemCount();
	m_DetailList.InsertItem(row, CString(name.data(), (int)name.size()));
	m_DetailList.SetItemText(row, 1, CString(value.data(), (int)value.size()));
}

// ── the details of a signature ────────────────────────────────────────────────

void CSecurityView::PopulateCertDetails(int index) {
	m_DetailList.DeleteAllItems();
	FreeCertContext();

	if (index < 0 || index >= (int)m_Items.size())
		return;
	auto const& certData = m_Items[index].CertData;
	if (certData.empty())
		return;

	// the signing certificate, for "View Certificate"
	CERT_BLOB blob{ (DWORD)certData.size(), const_cast<BYTE*>(certData.data()) };
	DWORD dwEncoding{}, dwContentType{}, dwFormatType{};
	if (CryptQueryObject(CERT_QUERY_OBJECT_BLOB, &blob, CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED, CERT_QUERY_FORMAT_FLAG_BINARY,
		0, &dwEncoding, &dwContentType, &dwFormatType, &m_CurrentStore, &m_CurrentMsg, nullptr)) {
		DWORD size{};
		CryptMsgGetParam(m_CurrentMsg, CMSG_SIGNER_INFO_PARAM, 0, nullptr, &size);
		std::vector<BYTE> buffer(size);
		if (size && CryptMsgGetParam(m_CurrentMsg, CMSG_SIGNER_INFO_PARAM, 0, buffer.data(), &size)) {
			auto si = reinterpret_cast<CMSG_SIGNER_INFO*>(buffer.data());
			CERT_INFO ci{};
			ci.Issuer = si->Issuer;
			ci.SerialNumber = si->SerialNumber;
			m_CurrentCert = CertFindCertificateInStore(m_CurrentStore, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, CERT_FIND_SUBJECT_CERT, &ci, nullptr);
		}
	}

	if (index >= (int)m_Auth.Signatures.size())
		return;
	// what the system thinks of the file as a whole
	if (!m_Auth.Trust.empty())
		AddProperty(L"Trust", m_Auth.Trust + (m_Auth.TrustCode ? std::format(L"  (0x{:08X})", (uint32_t)m_Auth.TrustCode) : L""));
	for (auto const& [name, value] : DescribeSignature(m_Auth.Signatures[index]))
		AddProperty(name, value);
}
