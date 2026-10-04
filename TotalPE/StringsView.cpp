#include "pch.h"
#include "StringsView.h"
#include "PEStrings.h"
#include <PEFile.h>
#include <SortHelper.h>
#include <ClipboardHelper.h>
#include <ListViewHelper.h>
#include <ToolbarHelper.h>

namespace {
	const uint32_t MinLengths[] = { 3, 4, 5, 6, 8, 10, 16, 32 };
	static_assert(_countof(MinLengths) == ID_STRINGS_MINLENGTH_LAST - ID_STRINGS_MINLENGTH_FIRST + 1);

	enum Column { Offset, Rva, Section, Encoding, Length, Text };
}

CStringsView::CStringsView(IMainFrame* frame, PEFile const& pe) : CViewBase(frame), m_PE(pe) {
}

CString CStringsView::GetTitle() const {
	return L"Strings";
}

CString CStringsView::GetColumnText(HWND, int row, int col) const {
	auto const& item = m_Items[row];
	switch (col) {
		case Column::Offset: return std::format(L"0x{:X}", item.Offset).c_str();
		case Column::Rva: return item.Rva ? std::format(L"0x{:X}", item.Rva).c_str() : L"";
		case Column::Section:
			return item.Section >= 0 ? m_SectionNames[item.Section].c_str() : item.Section == -1 ? L"(Headers)" : L"(Overlay)";
		case Column::Encoding: return StringEncodingToString(item.Encoding);
		case Column::Length: return std::to_wstring(item.Length).c_str();
		case Column::Text: return item.Text.c_str();
	}
	return CString();
}

void CStringsView::DoSort(SortInfo const* si) {
	if (si == nullptr)
		return;

	auto asc = si->SortAscending;
	m_Items.Sort([&](Item const& a, Item const& b) {
		switch (si->SortColumn) {
			case Column::Offset: return SortHelper::Sort(a.Offset, b.Offset, asc);
			case Column::Rva: return SortHelper::Sort(a.Rva, b.Rva, asc);
			case Column::Section: return SortHelper::Sort(a.Section, b.Section, asc);
			case Column::Encoding: return SortHelper::Sort((int)a.Encoding, (int)b.Encoding, asc);
			case Column::Length: return SortHelper::Sort(a.Length, b.Length, asc);
			case Column::Text: return SortHelper::Sort(a.Text, b.Text, asc);
		}
		return false;
	});
}

bool CStringsView::OnDoubleClickList(HWND, int row, int, CPoint const&) const {
	if (row < 0 || row >= (int)m_Items.size())
		return false;
	return Frame()->GoToFileOffset(m_Items[row].Offset, false);
}

void CStringsView::OnStateChanged(HWND, int, int, UINT oldState, UINT newState) {
	if ((newState & LVIS_SELECTED) || (oldState & LVIS_SELECTED))
		UpdateUI();
}

bool CStringsView::OnRightClickList(HWND, int row, int, POINT const& pt) const {
	if (row < 0)
		return false;
	CMenu menu;
	menu.LoadMenu(IDR_CONTEXT);
	return Frame()->ShowContextMenu(menu.GetSubMenu(10), 0, pt.x, pt.y);
}

void CStringsView::UpdateUI(bool first) {
	auto& ui = Frame()->GetUI();
	auto selected = m_List.GetSelectedCount();
	ui.UIEnable(ID_EDIT_COPY, selected > 0);
	ui.UIEnable(ID_STRINGS_HEX, selected == 1);
	auto item = GetSelectedItem();
	ui.UIEnable(ID_STRINGS_XREFS, item && item->Rva != 0);
	Frame()->SetStatusText(1, (m_Items.size() == m_Items.TotalSize() ? std::format(L"Strings: {}", m_Items.size()) :
		std::format(L"Strings: {} of {}", m_Items.size(), m_Items.TotalSize())).c_str());
}

CStringsView::Item const* CStringsView::GetSelectedItem() const {
	if (m_List.GetSelectedCount() != 1)
		return nullptr;
	return &m_Items[m_List.GetNextItem(-1, LVNI_SELECTED)];
}

void CStringsView::Scan() {
	CWaitCursor wait;
	auto strings = FindStrings(m_PE.GetSpan(0, m_PE.GetFileSize()), m_Options);

	// where each string is: the headers end where the first section starts, the overlay after the last section's data
	struct Range {
		uint32_t Start, End, Rva;
		int Section;
	};
	std::vector<Range> ranges;
	uint32_t headersEnd = m_PE.GetFileSize(), dataEnd = 0;
	m_SectionNames.clear();
	if (auto sections = m_PE.GetSecHeaders()) {
		int i = 0;
		for (auto const& sec : *sections) {
			auto const& hdr = sec.SecHdr;
			CString name = sec.SectionName.c_str();
			if (name.IsEmpty())
				name = CString((PCSTR)hdr.Name, 8);
			m_SectionNames.push_back((PCWSTR)name);
			if (hdr.SizeOfRawData && hdr.PointerToRawData) {
				auto size = hdr.Misc.VirtualSize ? std::min(hdr.SizeOfRawData, hdr.Misc.VirtualSize) : hdr.SizeOfRawData;
				ranges.push_back({ hdr.PointerToRawData, hdr.PointerToRawData + size, hdr.VirtualAddress, i });
				headersEnd = std::min<uint32_t>(headersEnd, hdr.PointerToRawData);
				dataEnd = std::max<uint32_t>(dataEnd, hdr.PointerToRawData + hdr.SizeOfRawData);
			}
			i++;
		}
	}
	if (dataEnd == 0)
		dataEnd = m_PE.GetFileSize();

	std::vector<Item> items;
	items.reserve(strings.size());
	for (auto& s : strings) {
		Item item{ std::move(s) };
		item.Section = item.Offset < headersEnd ? -1 : -2;
		item.Rva = item.Offset < headersEnd ? item.Offset : 0;	// the headers are mapped at the start of the image
		for (auto const& r : ranges) {
			if (item.Offset >= r.Start && item.Offset < r.End) {
				item.Section = r.Section;
				item.Rva = r.Rva + (item.Offset - r.Start);
				break;
			}
		}
		// between sections (alignment padding) or in a section's data that is not mapped
		if (item.Section == -2 && item.Offset < dataEnd)
			continue;
		items.push_back(std::move(item));
	}
	m_Items.Set(std::move(items));
	ApplyFilter();
}

void CStringsView::ApplyFilter() {
	if (m_FilterText.IsEmpty())
		m_Items.Filter(nullptr);
	else {
		CString filter(m_FilterText);
		filter.MakeLower();
		m_Items.Filter([&](Item const& item, size_t) {
			CString text(item.Text.c_str());
			text.MakeLower();
			return text.Find(filter) >= 0;
		});
	}
	m_List.SetItemCountEx((int)m_Items.size(), LVSICF_NOSCROLL);
	Sort(m_List, true);
	UpdateUI();
}

LRESULT CStringsView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	ToolBarButtonInfo const buttons[] = {
		{ ID_STRINGS_ASCII, 0, BTNS_CHECK | BTNS_SHOWTEXT, L"ASCII" },
		{ ID_STRINGS_UTF16, 0, BTNS_CHECK | BTNS_SHOWTEXT, L"UTF-16" },
		{ 0 },
		{ ID_STRINGS_MINLENGTH, 0, BTNS_DROPDOWN | BTNS_WHOLEDROPDOWN | BTNS_SHOWTEXT, L"Minimum Length" },
	};
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	m_tb = ToolbarHelper::CreateAndInitToolBar(m_hWnd, buttons, _countof(buttons), 16);
	AddSimpleReBarBand(m_tb);
	UIAddToolBar(m_tb);
	UISetCheck(ID_STRINGS_ASCII, m_Options.Ascii);
	UISetCheck(ID_STRINGS_UTF16, m_Options.Utf16);

	// the rebar passes the filter's notifications on to this window
	CRect rc(0, 0, 250, 20);
	m_Filter.Create(m_hWnd, rc, nullptr, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL);
	m_Filter.SetFont(AtlGetDefaultGuiFont());
	m_Filter.SetWatermark(L"Filter (Esc to clear)");
	AddSimpleReBarBand(m_Filter, nullptr, FALSE, 250);

	// the band of the toolbar is as wide as all of its buttons (the dropdown included), so the filter comes after them
	CRect rcLast;
	m_tb.GetItemRect(m_tb.GetButtonCount() - 1, &rcLast);
	CReBarCtrl rebar(m_hWndToolBar);
	REBARBANDINFO band{ sizeof(band) };
	band.fMask = RBBIM_CHILDSIZE;
	rebar.GetBandInfo(0, &band);
	band.fMask = RBBIM_CHILDSIZE | RBBIM_SIZE | RBBIM_IDEALSIZE;
	band.cxMinChild = band.cxIdeal = rcLast.right;
	band.cx = rcLast.right + 16;	// the gripper and the band's borders
	rebar.SetBandInfo(0, &band);

	m_hWndClient = m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	auto cm = GetColumnManager(m_List);
	cm->AddColumn(L"Offset", LVCFMT_RIGHT, 90);
	cm->AddColumn(L"RVA", LVCFMT_RIGHT, 90);
	cm->AddColumn(L"Section", LVCFMT_LEFT, 80);
	cm->AddColumn(L"Encoding", LVCFMT_LEFT, 70);
	cm->AddColumn(L"Length", LVCFMT_RIGHT, 60);
	cm->AddColumn(L"Text", LVCFMT_LEFT, 700);

	Scan();
	return 0;
}

LRESULT CStringsView::OnFind(UINT, WPARAM, LPARAM, BOOL&) {
	auto findDlg = Frame()->GetFindDialog();
	if (findDlg == nullptr || m_List.GetItemCount() == 0)
		return 0;

	auto index = ListViewHelper::SearchItem(m_List, findDlg->GetFindString(), findDlg->SearchDown(), findDlg->MatchCase());
	if (index >= 0) {
		m_List.SelectItem(index);
		m_List.SetFocus();
	}
	else {
		AtlMessageBox(m_hWnd, L"Finished searching list.", IDR_MAINFRAME, MB_ICONINFORMATION);
	}
	return 0;
}

LRESULT CStringsView::OnDropDown(int, LPNMHDR hdr, BOOL& handled) {
	auto tb = (NMTOOLBAR*)hdr;
	if (tb->iItem != ID_STRINGS_MINLENGTH) {
		handled = FALSE;
		return 0;
	}
	CMenu menu;
	menu.CreatePopupMenu();
	for (int i = 0; i < _countof(MinLengths); i++)
		menu.AppendMenu(MF_STRING | (MinLengths[i] == m_Options.MinLength ? MF_CHECKED : 0), ID_STRINGS_MINLENGTH_FIRST + i,
			std::format(L"{} Characters", MinLengths[i]).c_str());
	// below the button (the point is in screen coordinates)
	auto pt = ToolbarHelper::GetDropdownMenuPoint(m_tb, ID_STRINGS_MINLENGTH);
	auto cmd = (UINT)Frame()->ShowContextMenu(menu, TPM_VERTICAL | TPM_RETURNCMD, pt.x, pt.y);
	if (cmd) {
		BOOL dummy;
		OnMinLength(0, (WORD)cmd, nullptr, dummy);
	}
	return TBDDRET_DEFAULT;
}

LRESULT CStringsView::OnFilterChanged(WORD, WORD, HWND hWnd, BOOL& handled) {
	if (hWnd != m_Filter) {
		handled = FALSE;
		return 0;
	}
	m_Filter.GetWindowText(m_FilterText);
	ApplyFilter();
	return 0;
}

LRESULT CStringsView::OnToggleEncoding(WORD, WORD id, HWND, BOOL&) {
	auto& value = id == ID_STRINGS_ASCII ? m_Options.Ascii : m_Options.Utf16;
	value = !value;
	UISetCheck(id, value);
	Scan();
	return 0;
}

LRESULT CStringsView::OnMinLength(WORD, WORD id, HWND, BOOL&) {
	auto length = MinLengths[id - ID_STRINGS_MINLENGTH_FIRST];
	if (length != m_Options.MinLength) {
		m_Options.MinLength = length;
		Scan();
	}
	return 0;
}

// the text of the selected strings, one per line
LRESULT CStringsView::OnCopy(WORD, WORD, HWND, BOOL&) const {
	std::wstring text;
	for (int i = m_List.GetNextItem(-1, LVNI_SELECTED); i >= 0; i = m_List.GetNextItem(i, LVNI_SELECTED)) {
		if (!text.empty())
			text += L"\r\n";
		text += m_Items[i].Text;
	}
	ClipboardHelper::CopyText(m_hWnd, text.c_str());
	return 0;
}

LRESULT CStringsView::OnShowInHex(WORD, WORD, HWND, BOOL&) const {
	if (auto item = GetSelectedItem())
		Frame()->GoToFileOffset(item->Offset, false);
	return 0;
}

LRESULT CStringsView::OnXrefs(WORD, WORD, HWND, BOOL&) const {
	if (auto item = GetSelectedItem(); item && item->Rva)
		Frame()->ShowXrefs(m_PE.GetImageBase() + item->Rva);
	return 0;
}
