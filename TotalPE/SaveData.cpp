#include "pch.h"
#include "resource.h"
#include "SaveData.h"
#include "ResourceContent.h"
#include <WTLHelper.h>

namespace {
	void AppendUtf8(std::string& out, CString const& text) {
		if (text.IsEmpty())
			return;
		int len = ::WideCharToMultiByte(CP_UTF8, 0, text, text.GetLength(), nullptr, 0, nullptr, nullptr);
		auto start = out.size();
		out.resize(start + len);
		::WideCharToMultiByte(CP_UTF8, 0, text, text.GetLength(), out.data() + start, len, nullptr, nullptr);
	}

	// A CSV field is quoted if it holds a separator, a quote or a line break, or has spaces around it
	CString CsvField(CString text) {
		if (text.FindOneOf(L",\"\r\n") < 0 && (text.IsEmpty() || (text[0] != L' ' && text[text.GetLength() - 1] != L' ')))
			return text;
		text.Replace(L"\"", L"\"\"");
		return L"\"" + text + L"\"";
	}

	void ReportFailure(HWND hOwner, PCWSTR path) {
		AtlMessageBox(hOwner, (L"Failed to save " + CString(path)).GetString(), IDR_MAINFRAME, MB_ICONERROR);
	}

	// "Bitmap_#101_English (United States)" for a resource in a folder of resources
	CString ResourceFileName(FlatResource const& res, bool withType) {
		CString name = withType ? std::format(L"{}_{}_{}", res.Type, res.Name, res.Language).c_str() : res.Name.c_str();
		name.Remove(L'#');
		return ToFileName(name);
	}
}

HWND FindViewControl(HWND hView, PCWSTR className) {
	auto isMatch = [&](HWND h) {
		WCHAR name[64];
		return h && ::IsWindowVisible(h) && ::GetClassName(h, name, _countof(name)) && _wcsicmp(name, className) == 0;
	};
	if (auto hFocus = ::GetFocus(); isMatch(hFocus) && ::IsChild(hView, hFocus))
		return hFocus;

	struct Search {
		decltype(isMatch)& Match;
		HWND Found{ nullptr };
	} search{ isMatch };
	::EnumChildWindows(hView, [](HWND h, LPARAM lp) -> BOOL {
		auto search = (Search*)lp;
		if (!search->Match(h))
			return TRUE;
		search->Found = h;
		return FALSE;
	}, (LPARAM)&search);
	return search.Found;
}

CString ToFileName(CString name) {
	for (PCWSTR bad = L"\\/:*?\"<>|"; *bad; bad++)
		name.Replace(*bad, L'-');
	return name.Trim();
}

CString AskSaveFile(HWND hOwner, PCWSTR title, PCWSTR defExt, PCWSTR fileName, PCWSTR filter) {
	CSimpleFileDialog dlg(FALSE, defExt, fileName, OFN_EXPLORER | OFN_ENABLESIZING | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST, filter, hOwner);
	dlg.m_ofn.lpstrTitle = title;
	WTLHelper::SuspendHook();
	auto path = IDOK == dlg.DoModal() ? dlg.m_szFileName : L"";
	WTLHelper::ResumeHook();
	return path;
}

CString AskFolder(HWND hOwner, PCWSTR title) {
	CShellFileOpenDialog dlg(nullptr, FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
	dlg.GetPtr()->SetTitle(title);
	WTLHelper::SuspendHook();
	CString path;
	if (IDOK == dlg.DoModal(hOwner))
		dlg.GetFilePath(path);
	WTLHelper::ResumeHook();
	return path;
}

bool WriteFileData(PCWSTR path, void const* data, size_t size) {
	wil::unique_hfile hFile(::CreateFile(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr));
	if (!hFile)
		return false;
	DWORD written;
	return size <= MAXDWORD && ::WriteFile(hFile.get(), data, (DWORD)size, &written, nullptr) && written == size;
}

bool SaveListView(HWND hOwner, HWND hList, CString const& name) {
	auto path = AskSaveFile(hOwner, L"Export List", L"csv", name + L".csv",
		L"CSV Files (*.csv)\0*.csv\0Text Files (Tab Separated) (*.txt)\0*.txt\0All Files\0*.*\0");
	if (path.IsEmpty())
		return false;

	CWaitCursor wait;
	bool csv = path.Right(4).CompareNoCase(L".csv") == 0;
	auto field = [&](CString const& text) { return csv ? CsvField(text) : text; };

	CListViewCtrl lv(hList);
	auto header = lv.GetHeader();
	// the columns in the order they are shown, without those that are hidden
	std::vector<int> columns(header.GetItemCount());
	if (!columns.empty())
		header.GetOrderArray((int)columns.size(), columns.data());
	std::erase_if(columns, [&](int c) { return lv.GetColumnWidth(c) == 0; });

	std::string out("\xEF\xBB\xBF");	// UTF-8, so that Excel opens the file correctly
	auto addRow = [&](auto&& getText) {
		for (size_t i = 0; i < columns.size(); i++) {
			if (i)
				out += csv ? ',' : '\t';
			AppendUtf8(out, field(getText(columns[i])));
		}
		out += "\r\n";
	};
	addRow([&](int c) {
		WCHAR text[256]{};
		HDITEM hdi{ HDI_TEXT };
		hdi.pszText = text;
		hdi.cchTextMax = _countof(text);
		header.GetItem(c, &hdi);
		return CString(text);
	});
	int count = lv.GetItemCount();
	for (int row = 0; row < count; row++) {
		addRow([&](int c) {
			CString text;
			lv.GetItemText(row, c, text);
			return text;
		});
	}

	if (!WriteFileData(path, out.data(), out.size())) {
		ReportFailure(hOwner, path);
		return false;
	}
	return true;
}

// A resource as a file; an icon or cursor group gets its images
static ResourceFile MakeFile(FlatResource const& res, std::vector<FlatResource> const& all) {
	constexpr uint16_t GroupCursor = 12, GroupIcon = 14;
	if (res.TypeID == GroupIcon || res.TypeID == GroupCursor) {
		uint16_t imageType = res.TypeID == GroupIcon ? 3 : 1;	// RT_ICON, RT_CURSOR
		auto data = MakeIconGroupFile(res.Data, [&](uint16_t id) -> std::span<const std::byte> {
			auto it = std::ranges::find_if(all, [&](auto const& r) { return r.TypeID == imageType && r.NameID == id; });
			return it == all.end() ? std::span<const std::byte>() : it->Data;
		});
		if (!data.empty())
			return { std::move(data), res.TypeID == GroupIcon ? L"ico" : L"cur" };
	}
	return MakeResourceFile(res.Data, res.TypeID, res.TypeStr);
}

bool SaveResourceFiles(HWND hOwner, std::vector<FlatResource const*> const& resources, std::vector<FlatResource> const& all) {
	if (resources.empty())
		return false;

	if (resources.size() == 1) {
		auto const& res = *resources[0];
		auto file = MakeFile(res, all);
		auto filter = std::format(L"{} Files (*.{})|*.{}|All Files|*.*|", CString(file.Extension.c_str()).MakeUpper().GetString(), file.Extension, file.Extension);
		std::replace(filter.begin(), filter.end(), L'|', L'\0');
		auto path = AskSaveFile(hOwner, L"Save Resource", file.Extension.c_str(), ResourceFileName(res, false) + L"." + file.Extension.c_str(), filter.c_str());
		if (path.IsEmpty())
			return false;
		if (!WriteFileData(path, file.Data.data(), file.Data.size())) {
			ReportFailure(hOwner, path);
			return false;
		}
		return true;
	}

	auto folder = AskFolder(hOwner, std::format(L"Save {} Resources To", resources.size()).c_str());
	if (folder.IsEmpty())
		return false;

	CWaitCursor wait;
	int failed = 0;
	for (auto res : resources) {
		auto file = MakeFile(*res, all);
		auto path = folder + L"\\" + ResourceFileName(*res, true) + L"." + file.Extension.c_str();
		if (!WriteFileData(path, file.Data.data(), file.Data.size()))
			failed++;
	}
	if (failed) {
		AtlMessageBox(hOwner, std::format(L"{} of the {} resources could not be saved", failed, resources.size()).c_str(), IDR_MAINFRAME, MB_ICONWARNING);
		return false;
	}
	return true;
}

bool SaveSectionData(HWND hOwner, PEFile const& pe, PESectionHeader const& section) {
	auto const& hdr = section.SecHdr;
	auto fileSize = pe.GetFileSize();
	if (hdr.SizeOfRawData == 0 || hdr.PointerToRawData >= fileSize) {
		AtlMessageBox(hOwner, L"The section has no data in the file", IDR_MAINFRAME, MB_ICONINFORMATION);
		return false;
	}
	CString name = section.SectionName.c_str();
	if (name.IsEmpty())
		name = CString((PCSTR)hdr.Name, 8);
	name.TrimLeft(L'.');
	auto path = AskSaveFile(hOwner, L"Save Section Data", L"bin", ToFileName(name.IsEmpty() ? CString(L"section") : name) + L".bin",
		L"Binary Files (*.bin)\0*.bin\0All Files\0*.*\0");
	if (path.IsEmpty())
		return false;
	auto size = std::min<uint32_t>(hdr.SizeOfRawData, fileSize - hdr.PointerToRawData);
	if (!WriteFileData(path, pe.GetData() + hdr.PointerToRawData, size)) {
		ReportFailure(hOwner, path);
		return false;
	}
	return true;
}
