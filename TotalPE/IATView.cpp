#include "pch.h"
#include "IATView.h"
#include "resource.h"
#include <SortHelper.h>
#include <ClipboardHelper.h>
#include <ListViewHelper.h>

CIATView::CIATView(IMainFrame* frame, PEFile const& pe) : CViewBase(frame), m_PE(pe) {}

CString CIATView::GetTitle() const {
	return L"Import Address Table";
}

CString CIATView::GetColumnText(HWND h, int row, int col) const {
	if (h == m_ModList) {
		auto& m = m_Modules[row];
		switch (GetColumnManager(h)->GetColumnTag<ModCol>(col)) {
			case ModCol::Module:     return m.ModuleName.c_str();
			case ModCol::IatRVA:    return std::format(L"0x{:08X}", m.IatBaseRVA).c_str();
			case ModCol::FileOffset: return std::format(L"0x{:08X}", m.FileOffset).c_str();
			case ModCol::Count:     return std::to_wstring(m.Count).c_str();
		}
	}
	else {
		auto& f = m_Functions[row];
		switch (GetColumnManager(h)->GetColumnTag<FuncCol>(col)) {
			case FuncCol::Index:     return std::to_wstring(f.Index).c_str();
			case FuncCol::Name:      return f.IsOrdinal ? L"" : (PCWSTR)CString(f.FuncName.c_str());
			case FuncCol::HintOrd:   return f.IsOrdinal
				? std::format(L"Ord: {}", f.Ordinal).c_str()
				: std::format(L"0x{:04X}", f.Hint).c_str();
			case FuncCol::SlotRVA:   return std::format(L"0x{:08X}", f.SlotRVA).c_str();
			case FuncCol::SlotOffset: return std::format(L"0x{:08X}", f.SlotOffset).c_str();
			case FuncCol::RawValue:  return m_Is64
				? std::format(L"0x{:016X}", f.RawValue).c_str()
				: std::format(L"0x{:08X}", (uint32_t)f.RawValue).c_str();
		}
	}
	return {};
}

int CIATView::GetRowImage(HWND h, int row, int) const {
	if (h == m_ModList)
		return Frame()->GetIconIndex(m_Modules[row].ModuleName.starts_with("api-ms-win") ? IDI_INTERFACE : IDI_DLL_IMPORT);
	return Frame()->GetIconIndex(IDI_FUNCTION);
}

void CIATView::DoSort(SortInfo const* si) {
	auto asc = si->SortAscending;
	if (si->hWnd == m_ModList) {
		auto col = GetColumnManager(si->hWnd)->GetColumnTag<ModCol>(si->SortColumn);
		std::ranges::sort(m_Modules, [&](auto& a, auto& b) {
			switch (col) {
				case ModCol::Module:     return SortHelper::Sort(a.ModuleName,  b.ModuleName,  asc);
				case ModCol::IatRVA:    return SortHelper::Sort(a.IatBaseRVA, b.IatBaseRVA, asc);
				case ModCol::FileOffset: return SortHelper::Sort(a.FileOffset,  b.FileOffset,  asc);
				case ModCol::Count:     return SortHelper::Sort(a.Count,       b.Count,       asc);
			}
			return false;
		});
	}
	else {
		auto col = GetColumnManager(si->hWnd)->GetColumnTag<FuncCol>(si->SortColumn);
		std::ranges::sort(m_Functions, [&](auto& a, auto& b) {
			switch (col) {
				case FuncCol::Index:      return SortHelper::Sort(a.Index,      b.Index,      asc);
				case FuncCol::Name:       return SortHelper::Sort(a.FuncName,   b.FuncName,   asc);
				case FuncCol::HintOrd:    return SortHelper::Sort(
					a.IsOrdinal ? a.Ordinal : (DWORD)a.Hint,
					b.IsOrdinal ? b.Ordinal : (DWORD)b.Hint, asc);
				case FuncCol::SlotRVA:    return SortHelper::Sort(a.SlotRVA,    b.SlotRVA,    asc);
				case FuncCol::SlotOffset: return SortHelper::Sort(a.SlotOffset, b.SlotOffset, asc);
				case FuncCol::RawValue:   return SortHelper::Sort(a.RawValue,   b.RawValue,   asc);
			}
			return false;
		});
	}
}

void CIATView::OnStateChanged(HWND hWnd, int from, int to, DWORD oldState, DWORD newState) {
	if ((newState & LVIS_SELECTED) || (oldState & LVIS_SELECTED)) {
		if (hWnd == m_ModList) {
			if (m_ModList.GetSelectedCount() == 1)
				PopulateFunctions(m_Modules[m_ModList.GetNextItem(-1, LVNI_SELECTED)].ImportIndex);
			else {
				m_Functions.clear();
				m_FuncList.SetItemCount(0);
			}
		}
		UpdateUI();
	}
}

void CIATView::UpdateUI(bool) const {
	auto& ui = Frame()->GetUI();
	auto hFocus = ::GetFocus();
	CListViewCtrl lv;
	if (hFocus == m_ModList)       lv = m_ModList;
	else if (hFocus == m_FuncList) lv = m_FuncList;
	if (lv)
		ui.UIEnable(ID_EDIT_COPY, lv.GetSelectedCount() > 0);
}

LRESULT CIATView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN);

	m_ModList.Create(m_Splitter, rcDefault, nullptr,
		WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS, 0);
	m_ModList.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	m_ModList.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);

	m_FuncList.Create(m_Splitter, rcDefault, nullptr,
		WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS, 0);
	m_FuncList.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	m_FuncList.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);

	auto cm = GetColumnManager(m_ModList);
	cm->AddColumn(L"Module",      LVCFMT_LEFT,  240, ModCol::Module);
	cm->AddColumn(L"IAT RVA",     LVCFMT_RIGHT, 100, ModCol::IatRVA);
	cm->AddColumn(L"File Offset", LVCFMT_RIGHT, 100, ModCol::FileOffset);
	cm->AddColumn(L"Count",       LVCFMT_RIGHT,  60, ModCol::Count);

	cm = GetColumnManager(m_FuncList);
	cm->AddColumn(L"#",           LVCFMT_RIGHT,  45, FuncCol::Index);
	cm->AddColumn(L"Name",        LVCFMT_LEFT,  230, FuncCol::Name);
	cm->AddColumn(L"Hint/Ord",    LVCFMT_RIGHT,  80, FuncCol::HintOrd);
	cm->AddColumn(L"Slot RVA",    LVCFMT_RIGHT, 100, FuncCol::SlotRVA);
	cm->AddColumn(L"File Offset", LVCFMT_RIGHT, 100, FuncCol::SlotOffset);
	cm->AddColumn(L"Raw Value",   LVCFMT_RIGHT, 140, FuncCol::RawValue);

	BuildItems();

	m_Splitter.SetSplitterPanes(m_ModList, m_FuncList);
	m_Splitter.SetSplitterPosPct(35);
	return 0;
}

LRESULT CIATView::OnCopy(WORD, WORD, HWND, BOOL&) const {
	ClipboardHelper::CopyText(m_hWnd, ListViewHelper::GetSelectedRowsAsString(::GetFocus(), L"\t"));
	return 0;
}

void CIATView::BuildItems() {
	m_Is64 = m_PE.GetFileInfo()->IsPE64;

	auto const* imports = m_PE.GetImport();
	int idx = 0;
	for (auto const& imp : *imports) {
		ModuleItem mi{};
		mi.ModuleName  = imp.ModuleName;
		mi.IatBaseRVA  = imp.ImportDesc.Name;  // stored as import_address_table_rva() in BuildCaches
		mi.Count       = (uint32_t)imp.ImportFunc.size();
		mi.FileOffset  = mi.IatBaseRVA ? m_PE.GetOffsetFromRVA(mi.IatBaseRVA) : 0;
		mi.ImportIndex = idx++;
		m_Modules.push_back(mi);
	}

	// Status bar: overall IAT data directory
	auto const* dirs = m_PE.GetDataDirs();
	if (dirs && dirs->size() > 12) {
		auto const& iatDir = dirs->at(12);
		if (iatDir.DataDir.VirtualAddress)
			Frame()->SetStatusText(1, std::format(L"IAT RVA: 0x{:08X}   Size: {} bytes",
				iatDir.DataDir.VirtualAddress, iatDir.DataDir.Size).c_str());
	}

	m_ModList.SetItemCount((int)m_Modules.size());
}

void CIATView::PopulateFunctions(int importIndex) {
	m_Functions.clear();

	auto const& imp = m_PE.GetImport()->at(importIndex);
	DWORD ptrSize  = m_Is64 ? 8 : 4;
	DWORD iatBase  = imp.ImportDesc.Name;
	auto const* raw = m_PE.GetData();
	uint32_t fileSize = m_PE.GetFileSize();

	int idx = 0;
	for (auto const& fn : imp.ImportFunc) {
		FuncItem fi{};
		fi.Index     = idx++;
		fi.FuncName  = fn.FuncName;
		fi.Hint      = fn.ImpByName.Hint;
		fi.IsOrdinal = (fn.ImpByName.Name[0] == '\0');
		if (fi.IsOrdinal)
			fi.Ordinal = m_Is64
				? (DWORD)(fn.unThunk.Thunk64.u1.Ordinal & ~IMAGE_ORDINAL_FLAG64)
				: (DWORD)(fn.unThunk.Thunk32.u1.Ordinal & ~IMAGE_ORDINAL_FLAG32);

		fi.SlotRVA    = iatBase + (DWORD)(fi.Index * ptrSize);
		fi.SlotOffset = fi.SlotRVA ? m_PE.GetOffsetFromRVA(fi.SlotRVA) : 0;

		if (fi.SlotOffset && fi.SlotOffset + ptrSize <= fileSize) {
			if (m_Is64)
				fi.RawValue = *reinterpret_cast<const uint64_t*>(raw + fi.SlotOffset);
			else
				fi.RawValue = *reinterpret_cast<const uint32_t*>(raw + fi.SlotOffset);
		}
		m_Functions.push_back(fi);
	}

	Sort(m_FuncList);
	m_FuncList.SetItemCount((int)m_Functions.size());
}
