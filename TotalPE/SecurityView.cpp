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

#pragma comment(lib, "Crypt32")
#pragma comment(lib, "Cryptui")

// OID strings are ASCII; convert to wide for display
static std::wstring OidToWide(const char* s) {
	if (!s || !*s) return {};
	int n = ::MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
	std::wstring w(n, L'\0');
	::MultiByteToWideChar(CP_ACP, 0, s, -1, w.data(), n);
	if (!w.empty() && w.back() == L'\0') w.pop_back();
	return w;
}

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
	m_DetailList.InsertColumn(1, L"Value",    LVCFMT_LEFT,  500);

	m_Splitter.SetSplitterPanes(m_List, m_DetailList);
	m_Splitter.SetSplitterPosPct(25);

	BuildItems();
	return 0;
}

void CSecurityView::BuildItems() {
	m_Items = *m_PE.GetSecurity();
	m_List.SetItemCount((int)m_Items.size());
}

void CSecurityView::OnStateChanged(HWND h, int from, int to, DWORD oldState, DWORD newState) {
	if (h == m_List && (newState & LVIS_SELECTED))
		PopulateCertDetails(m_Items[from].CertData);
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
	if (hFocus == m_List || hFocus == m_DetailList)
		ClipboardHelper::CopyText(m_hWnd, ListViewHelper::GetSelectedRowsAsString(hFocus, L"\t"));
	return 0;
}

LRESULT CSecurityView::OnViewCertificate(WORD, WORD, HWND, BOOL&) const {
	if (m_CurrentCert)
		CryptUIDlgViewContext(CERT_STORE_CERTIFICATE_CONTEXT, m_CurrentCert, m_hWnd, nullptr, 0, nullptr);
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

std::wstring CSecurityView::FileTimeToString(FILETIME const& ft) {
	FILETIME local{};
	SYSTEMTIME st{};
	FileTimeToLocalFileTime(&ft, &local);
	FileTimeToSystemTime(&local, &st);

	WCHAR date[64]{}, time[64]{};
	GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, date, _countof(date));
	GetTimeFormatW(LOCALE_USER_DEFAULT, 0,              &st, nullptr, time, _countof(time));
	return std::wstring(date) + L"  " + time;
}

std::wstring CSecurityView::BytesToHex(BYTE const* data, DWORD len, wchar_t sep) {
	std::wstring s;
	s.reserve(len * 3);
	for (DWORD i = 0; i < len; ++i) {
		s += std::format(L"{:02X}", data[i]);
		if (sep && i + 1 < len) s += sep;
	}
	return s;
}

// ── certificate decoding ──────────────────────────────────────────────────────

void CSecurityView::PopulateCertDetails(std::vector<BYTE> const& certData) {
	m_DetailList.DeleteAllItems();
	FreeCertContext();

	if (certData.empty()) return;

	// Decode the PKCS#7 SignedData blob
	CERT_BLOB blob{ (DWORD)certData.size(), const_cast<BYTE*>(certData.data()) };
	DWORD dwEncoding{}, dwContentType{}, dwFormatType{};
	if (!CryptQueryObject(CERT_QUERY_OBJECT_BLOB, &blob,
		CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED,
		CERT_QUERY_FORMAT_FLAG_BINARY,
		0, &dwEncoding, &dwContentType, &dwFormatType,
		&m_CurrentStore, &m_CurrentMsg, nullptr)) {
		AddProperty(L"Note", L"Certificate blob could not be decoded (not PKCS#7 SignedData)");
		return;
	}

	// Signer info — identifies which certificate to look up
	DWORD cbSignerInfo{};
	CryptMsgGetParam(m_CurrentMsg, CMSG_SIGNER_INFO_PARAM, 0, nullptr, &cbSignerInfo);
	std::vector<BYTE> siBuf(cbSignerInfo);
	if (!CryptMsgGetParam(m_CurrentMsg, CMSG_SIGNER_INFO_PARAM, 0, siBuf.data(), &cbSignerInfo)) {
		AddProperty(L"Note", L"Signer info not found");
		return;
	}
	auto* si = reinterpret_cast<CMSG_SIGNER_INFO*>(siBuf.data());

	// Locate the leaf certificate in the embedded store
	CERT_INFO ci{};
	ci.Issuer       = si->Issuer;
	ci.SerialNumber = si->SerialNumber;
	m_CurrentCert = CertFindCertificateInStore(m_CurrentStore,
		X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
		0, CERT_FIND_SUBJECT_CERT, &ci, nullptr);

	if (!m_CurrentCert) {
		AddProperty(L"Note", L"Signing certificate not found in embedded store");
		return;
	}

	auto* ci2 = m_CurrentCert->pCertInfo;

	// Subject
	WCHAR buf[512]{};
	CertGetNameString(m_CurrentCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, buf, _countof(buf));
	AddProperty(L"Subject", buf);

	// Issuer
	CertGetNameString(m_CurrentCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, CERT_NAME_ISSUER_FLAG, nullptr, buf, _countof(buf));
	AddProperty(L"Issuer", buf);

	// Validity
	AddProperty(L"Valid From", FileTimeToString(ci2->NotBefore));
	AddProperty(L"Valid To",   FileTimeToString(ci2->NotAfter));

	// Serial number (displayed most-significant byte first, as per standards)
	{
		auto& sn = ci2->SerialNumber;
		std::wstring snStr;
		snStr.reserve(sn.cbData * 3);
		for (DWORD i = sn.cbData; i > 0; --i)
			snStr += std::format(L"{:02X}{}", sn.pbData[i - 1], i > 1 ? L" " : L"");
		AddProperty(L"Serial Number", snStr);
	}

	// SHA-1 thumbprint
	{
		BYTE thumb[20]{};
		DWORD thumbLen = sizeof(thumb);
		if (CertGetCertificateContextProperty(m_CurrentCert, CERT_SHA1_HASH_PROP_ID, thumb, &thumbLen))
			AddProperty(L"Thumbprint (SHA-1)", BytesToHex(thumb, thumbLen));
	}

	// Signature algorithm (from the cert itself)
	{
		auto* oidInfo = CryptFindOIDInfo(CRYPT_OID_INFO_OID_KEY,
			ci2->SignatureAlgorithm.pszObjId, 0);
		AddProperty(L"Signature Algorithm",
			oidInfo ? std::wstring(oidInfo->pwszName) : OidToWide(ci2->SignatureAlgorithm.pszObjId));
	}

	// Digest algorithm (from the signer info — what was hashed)
	{
		auto* oidInfo = CryptFindOIDInfo(CRYPT_OID_INFO_OID_KEY, si->HashAlgorithm.pszObjId, 0);
		AddProperty(L"Digest Algorithm",
			oidInfo ? std::wstring(oidInfo->pwszName) : OidToWide(si->HashAlgorithm.pszObjId));
	}

	// Timestamp from counter-signature unauthenticated attribute
	for (DWORD i = 0; i < si->UnauthAttrs.cAttr; ++i) {
		auto& attr = si->UnauthAttrs.rgAttr[i];
		bool isTraditional = (strcmp(attr.pszObjId, szOID_RSA_counterSign) == 0);
		if (!isTraditional) continue;
		if (attr.cValue == 0) continue;

		// Decode counter-signer info
		BYTE* pCSI = nullptr;
		DWORD cbCSI = 0;
		if (CryptDecodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
			PKCS7_SIGNER_INFO,
			attr.rgValue[0].pbData, attr.rgValue[0].cbData,
			CRYPT_DECODE_ALLOC_FLAG, nullptr, &pCSI, &cbCSI)) {
			auto* csi = reinterpret_cast<CMSG_SIGNER_INFO*>(pCSI);
			for (DWORD j = 0; j < csi->AuthAttrs.cAttr; ++j) {
				if (strcmp(csi->AuthAttrs.rgAttr[j].pszObjId, szOID_RSA_signingTime) != 0) continue;
				FILETIME ft{};
				DWORD ftSize = sizeof(ft);
				if (CryptDecodeObjectEx(X509_ASN_ENCODING,
					szOID_RSA_signingTime,
					csi->AuthAttrs.rgAttr[j].rgValue[0].pbData,
					csi->AuthAttrs.rgAttr[j].rgValue[0].cbData,
					0, nullptr, &ft, &ftSize))
					AddProperty(L"Timestamp", FileTimeToString(ft));
				break;
			}
			LocalFree(pCSI);
		}
		break;
	}
}

