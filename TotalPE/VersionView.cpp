#include "pch.h"
#include "VersionView.h"
#include "PEStrings.h"
#include "SortHelper.h"
#include <ClipboardHelper.h>

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
	}
	return CString();
}

void CVersionView::DoSort(SortInfo const* si) {
	if (si == nullptr)
		return;

	auto compare = [&](auto& item1, auto& item2) {
		return SortHelper::Sort(item1.Name, item2.Name, si->SortAscending);
	};
	std::sort(m_Items.begin(), m_Items.end(), compare);
}

bool CVersionView::IsSortable(HWND, int col) const {
	return col == 0;
}

LRESULT CVersionView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_List.Create(*this, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHAREIMAGELISTS);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	auto cm = GetColumnManager(m_List);

	cm->AddColumn(L"Name", LVCFMT_LEFT, 140);
	cm->AddColumn(L"Value", LVCFMT_LEFT, 330);

	return 0;
}

void CVersionView::BuildItems() {
	m_Items.clear();
	m_Items.reserve(16);

	struct VS_VERSIONINFO {
		WORD             wLength;
		WORD             wValueLength;
		WORD             wType;
		WCHAR            szKey[16];	//L"VS_VERSION_INFO";
		WORD             Padding1;
		VS_FIXEDFILEINFO Value;
		WORD             Padding2;
		WORD             Children;
	};
	auto ver = (VS_VERSIONINFO*)m_Data.data();
	if (ver->wValueLength) {
		auto& fi = (VS_FIXEDFILEINFO&)ver->Value;
		Item items[] = {
			{ L"Struct Version", std::format(L"{}.{}", HIWORD(fi.dwStrucVersion), LOWORD(fi.dwStrucVersion)) },
			{ L"File Version", std::format(L"{}.{}.{}.{}", HIWORD(fi.dwFileVersionMS), LOWORD(fi.dwFileVersionMS), HIWORD(fi.dwFileVersionLS), LOWORD(fi.dwFileVersionLS)) },
			{ L"Product Version", std::format(L"{}.{}.{}.{}", HIWORD(fi.dwProductVersionMS), LOWORD(fi.dwProductVersionMS), HIWORD(fi.dwProductVersionLS), LOWORD(fi.dwProductVersionLS)) },
			{ L"OS File", PEStrings::VersionFileOSToString(fi.dwFileOS) },
			{ L"File Type", PEStrings::FileTypeToString(fi.dwFileType) },
			{ L"File Subtype", PEStrings::FileSubTypeToString(fi.dwFileType, fi.dwFileSubtype) },
			{ L"File Flags", PEStrings::FileFlagsToString(fi.dwFileFlags & fi.dwFileFlagsMask) },
		};
		m_Items.insert(m_Items.begin(), std::begin(items), std::end(items));
	}

	// TODO: parse Children (StringFileInfo / VarFileInfo)

	m_List.SetItemCount((int)m_Items.size());
}

LRESULT CVersionView::OnCopy(WORD, WORD, HWND, BOOL&) const {
	auto text = ListViewHelper::GetSelectedRowsAsString(m_List, L",");
	ClipboardHelper::CopyText(m_hWnd, text);
	return 0;
}
