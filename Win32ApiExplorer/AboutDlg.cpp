#include "pch.h"
#include "AboutDlg.h"
#include "AppSettings.h"
#include <VersionResourceHelper.h>

LRESULT CAboutDlg::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
	SetDialogIcon(IDR_MAINFRAME);
	CenterWindow(GetParent());

	VersionResourceHelper vh;
	SetDlgItemText(IDC_VERSION, vh.GetValue(L"ProductName") + L" v" + vh.GetValue(L"ProductVersion"));
	SetDlgItemText(IDC_COPYRIGHT, vh.GetValue(L"LegalCopyright"));

	// the metadata: its version (if it was downloaded here), else where it is; what it has
	auto& settings = AppSettings::Get();
	auto version = settings.MetadataVersion(), wdk = settings.WdkMetadataVersion();
	std::wstring metadata = L"Metadata: ";
	if (!version.empty())
		metadata += L"v" + version + (wdk.empty() ? L"" : L" (WDK v" + wdk + L")");
	else if (!settings.MetadataFolder().empty())
		metadata += L"in " + settings.MetadataFolder();
	else
		metadata += L"next to Win32ApiExp.exe";
	if (m_Api) {
		// (the count in the user's way: 161,229)
		WCHAR count[32]{};
		NUMBERFMT format{ 0, 0, 3, (PWSTR)L".", (PWSTR)L",", 0 };
		WCHAR separator[8]{};
		if (::GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_STHOUSAND, separator, _countof(separator)))
			format.lpThousandSep = separator;
		::GetNumberFormatEx(LOCALE_NAME_USER_DEFAULT, 0, std::to_wstring(m_Api->Entries().size()).c_str(), &format, count, _countof(count));
		metadata += std::format(L"\n{} functions, types and constants", count);
	}
	else
		metadata += L"\n(loading...)";
	SetDlgItemText(IDC_METADATA, metadata.c_str());
	return TRUE;
}

// (each link has its address)
LRESULT CAboutDlg::OnClickLink(int, LPNMHDR hdr, BOOL&) {
	if (hdr->idFrom == IDC_LINK)
		::ShellExecute(nullptr, L"open", ((NMLINK*)hdr)->item.szUrl, nullptr, nullptr, SW_SHOWDEFAULT);
	return 0;
}
