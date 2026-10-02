#include "pch.h"
#include "VersionView.h"
#include "PEStrings.h"
#include "SortHelper.h"
#include <ClipboardHelper.h>
#include "VersionInfo.h"

CVersionView::CVersionView(IMainFrame* frame, PCWSTR title) : CViewBase(frame), m_Title(title) {
}

CString CVersionView::GetTitle() const {
	return m_Title;
}

void CVersionView::SetData(std::span<const std::byte> data) {
	m_Data = data;

	BuildItems();
}

void CVersionView::UpdateUI(bool first) {
	auto& ui = Frame()->GetUI();
	ui.UIEnable(ID_EDIT_COPY, m_List.GetSelectedCount() > 0);
}

CString CVersionView::GetColumnText(HWND, int row, int col) const {
	auto& item = m_Items[row];
	switch (col) {
		case 0: return item.Name.c_str();
		case 1: return item.Value.c_str();
		case 2: return item.Block.c_str();
	}
	return CString();
}

void CVersionView::DoSort(SortInfo const* si) {
	if (si == nullptr)
		return;

	auto compare = [&](auto& item1, auto& item2) {
		return si->SortColumn == 2 ? SortHelper::Sort(item1.Block, item2.Block, si->SortAscending) : SortHelper::Sort(item1.Name, item2.Name, si->SortAscending);
	};
	std::stable_sort(m_Items.begin(), m_Items.end(), compare);
}

bool CVersionView::IsSortable(HWND, int col) const {
	return col == 0 || col == 2;
}

LRESULT CVersionView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_List.Create(*this, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHAREIMAGELISTS);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	auto cm = GetColumnManager(m_List);

	cm->AddColumn(L"Name", LVCFMT_LEFT, 160);
	cm->AddColumn(L"Value", LVCFMT_LEFT, 420);
	cm->AddColumn(L"Block", LVCFMT_LEFT, 280);

	return 0;
}

void CVersionView::BuildItems() {
	m_Items.clear();
	auto info = ParseVersionInfo(m_Data);
	if (!info.Valid) {
		m_Items.push_back({ L"Error", L"The resource is not a valid version resource", L"" });
		m_List.SetItemCount((int)m_Items.size());
		return;
	}

	if (info.HasFixedInfo) {
		auto const& fi = info.Fixed;
		const std::wstring block = L"Fixed file information";
		Item items[] = {
			{ L"Struct Version", std::format(L"{}.{}", HIWORD(fi.dwStrucVersion), LOWORD(fi.dwStrucVersion)), block },
			{ L"File Version", std::format(L"{}.{}.{}.{}", HIWORD(fi.dwFileVersionMS), LOWORD(fi.dwFileVersionMS), HIWORD(fi.dwFileVersionLS), LOWORD(fi.dwFileVersionLS)), block },
			{ L"Product Version", std::format(L"{}.{}.{}.{}", HIWORD(fi.dwProductVersionMS), LOWORD(fi.dwProductVersionMS), HIWORD(fi.dwProductVersionLS), LOWORD(fi.dwProductVersionLS)), block },
			{ L"OS File", PEStrings::VersionFileOSToString(fi.dwFileOS), block },
			{ L"File Type", PEStrings::FileTypeToString(fi.dwFileType), block },
			{ L"File Subtype", PEStrings::FileSubTypeToString(fi.dwFileType, fi.dwFileSubtype), block },
			{ L"File Flags", PEStrings::FileFlagsToString(fi.dwFileFlags & fi.dwFileFlagsMask), block },
		};
		m_Items.insert(m_Items.end(), std::begin(items), std::end(items));
	}

	// StringFileInfo: the strings, a table for each language
	for (auto const& table : info.Tables) {
		auto language = LanguageName(table.Language);
		auto block = std::format(L"Strings: {}, {}", language.empty() ? std::format(L"language 0x{:04X}", table.Language) : language, CodePageName(table.CodePage));
		if (table.Strings.empty())
			m_Items.push_back({ L"(no strings)", L"", block });
		for (auto const& [name, value] : table.Strings)
			m_Items.push_back({ name, value, block });
	}

	// VarFileInfo: the languages the resource says it supports
	for (auto const& t : info.Translations) {
		auto language = LanguageName(t.Language);
		m_Items.push_back({ L"Translation", std::format(L"{}, {}  ({:04X} {:04X})", language.empty() ? L"Unknown language" : language, CodePageName(t.CodePage), t.Language, t.CodePage), L"VarFileInfo" });
	}

	m_List.SetItemCount((int)m_Items.size());
}

LRESULT CVersionView::OnCopy(WORD, WORD, HWND, BOOL&) const {
	auto text = ListViewHelper::GetSelectedRowsAsString(m_List, L",");
	ClipboardHelper::CopyText(m_hWnd, text);
	return 0;
}
