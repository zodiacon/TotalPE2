// View.cpp : implementation of the CView class
//
/////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "resource.h"
#include "LoadConfigView.h"
#include "PEFile.h"
#include "PEStrings.h"
#include <SortHelper.h>
#include <ClipboardHelper.h>

LRESULT CLoadConfigView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_hWndClient = m_List.Create(*this, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | 
		LVS_REPORT | LVS_OWNERDATA | LVS_SHAREIMAGELISTS, 0);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	auto cm = GetColumnManager(m_List);

	cm->AddColumn(L"Name", LVCFMT_LEFT, 260);
	cm->AddColumn(L"Value", LVCFMT_LEFT, 250);
	cm->AddColumn(L"Details", LVCFMT_LEFT, 550);

	BuildItems();

	return 0;
}

CString CLoadConfigView::GetTitle() const {
	return L"Load Config";
}

CLoadConfigView::CLoadConfigView(IMainFrame* frame, PEFile const& pe) : CViewBase(frame), m_PE(pe) {
}

CString CLoadConfigView::GetColumnText(HWND, int row, int col) const {
	auto& item = m_Items[row];
	switch (col) {
		case 0: return item.Name.c_str();
		case 1: return item.Value.c_str();
		case 2: return item.Details.c_str();
	}
	return CString();
}

bool CLoadConfigView::IsSortable(HWND, int col) const {
	return col == 0;		// sort on Name column only
}

void CLoadConfigView::OnStateChanged(HWND, int from, int to, DWORD oldState, DWORD newState) {
	UpdateUI();
}

LRESULT CLoadConfigView::OnCopy(WORD, WORD, HWND, BOOL&) const {
	auto text = ListViewHelper::GetSelectedRowsAsString(m_List);
	ClipboardHelper::CopyText(m_hWnd, text);
	return 0;
}

void CLoadConfigView::DoSort(SortInfo const* si) {
	if (si == nullptr)
		return;

	auto compare = [&](auto& item1, auto& item2) {
		return SortHelper::Sort(item1.Name, item2.Name, si->SortAscending);
	};

	std::ranges::sort(m_Items, compare);
}

void CLoadConfigView::BuildItems() {
	auto& lc32 = m_PE->GetLoadConfig()->LCD32;
	auto& lc64 = m_PE->GetLoadConfig()->LCD64;
	auto is64 = m_PE->GetFileInfo()->IsPE64;
	auto symbols = Frame()->GetSymbols();

	ULONGLONG guardCFFunction = is64 ? lc64.GuardCFCheckFunctionPointer : lc32.GuardCFCheckFunctionPointer;
	ULONGLONG guardCFDispFunction = is64 ? lc64.GuardCFDispatchFunctionPointer : lc32.GuardCFDispatchFunctionPointer;

	m_Items = std::vector<DataItem>{
		{ L"Size", std::format(L"{} Bytes", lc32.Size) },
		{ L"Time Date Stamp", std::format(L"0x{:X}", lc32.TimeDateStamp) },
		{ L"Version", std::format(L"{}.{}", lc32.MajorVersion, lc32.MinorVersion) },
		{ L"Global Flags Clear", std::format(L"0x{:08X}", lc32.GlobalFlagsClear) },
		{ L"Global Flags Set", std::format(L"0x{:08X}", lc32.GlobalFlagsSet) },
		{ L"Critical Section Default Timeout", std::format(L"0x{:X}", lc32.CriticalSectionDefaultTimeout) },
		{ L"Decommit Free Block Threshold", std::format(L"0x{:X}", is64 ? lc64.DeCommitFreeBlockThreshold : lc32.DeCommitFreeBlockThreshold) },
		{ L"Decommit Total Free Threshold", std::format(L"0x{:X}", is64 ? lc64.DeCommitTotalFreeThreshold : lc64.DeCommitTotalFreeThreshold) },
		{ L"Lock Prefix Table", std::format(L"0x{:X}", is64 ? lc64.LockPrefixTable : lc32.LockPrefixTable) },
		{ L"Maximum Allocation Size", std::format(L"0x{:X}", is64 ? lc64.MaximumAllocationSize : lc32.MaximumAllocationSize) },
		{ L"Virtual Memory Threshold", std::format(L"0x{:X}", is64 ? lc64.VirtualMemoryThreshold : lc32.VirtualMemoryThreshold) },
		{ L"Process Affinity Mask", std::format(L"0x{:08X}", is64 ? lc64.ProcessAffinityMask : lc32.ProcessAffinityMask) },
		{ L"Process Heap Flags", std::format(L"0x{:08X}", is64 ? lc64.ProcessHeapFlags : lc32.ProcessHeapFlags) },
		{ L"CSD Version", std::format(L"0x{:X}", is64 ? lc64.CSDVersion : lc32.CSDVersion) },
		{ L"Dependent Load Flags", std::format(L"0x{:08X}", is64 ? lc64.DependentLoadFlags : lc32.DependentLoadFlags) },
		{ L"Edit List", std::format(L"0x{:X}", is64 ? lc64.EditList : lc32.EditList) },
		{ L"Security Cookie", std::format(L"0x{:X}", is64 ? lc64.SecurityCookie : lc32.SecurityCookie) },
		{ L"SE Handler Table", std::format(L"0x{:X}", is64 ? lc64.SEHandlerTable : lc32.SEHandlerTable) },
		{ L"SE Handler Count", std::format(L"0x{:X}", is64 ? lc64.SEHandlerCount : lc32.SEHandlerCount) },
		{ L"Guard CF Check Function", std::format(L"0x{:X}", guardCFFunction) },
		{ L"Guard CF Dispatch Function", std::format(L"0x{:X}", guardCFDispFunction) },
		{ L"Guard CF Function Table", std::format(L"0x{:X}", is64 ? lc64.GuardCFFunctionTable : lc32.GuardCFFunctionTable) },
		{ L"Guard CF Function Count", std::format(L"0x{:X}", is64 ? lc64.GuardCFFunctionCount : lc32.GuardCFFunctionCount) },
		{ L"Guard Flags", std::format(L"0x{:08X}", is64 ? lc64.GuardFlags : lc32.GuardFlags) },
		{ L"Guard Address Taken IAT Entry Table", std::format(L"0x{:X}", is64 ? lc64.GuardAddressTakenIatEntryTable : lc32.GuardAddressTakenIatEntryTable) },
		{ L"Guard Address Taken IAT Entry Count", std::format(L"0x{:X}", is64 ? lc64.GuardAddressTakenIatEntryCount : lc32.GuardAddressTakenIatEntryCount) },
		{ L"Guard Long Jump Target Table", std::format(L"0x{:X}", is64 ? lc64.GuardLongJumpTargetTable : lc32.GuardLongJumpTargetTable) },
		{ L"Guard Long Jump Target Count", std::format(L"0x{:X}", is64 ? lc64.GuardLongJumpTargetCount : lc32.GuardLongJumpTargetCount) },
		{ L"Dynamic Value Reloc Table", std::format(L"0x{:X}", is64 ? lc64.DynamicValueRelocTable : lc32.DynamicValueRelocTable) },
		{ L"CHPE Metadata Pointer", std::format(L"0x{:X}", is64 ? lc64.CHPEMetadataPointer : lc32.CHPEMetadataPointer) },
		{ L"Guard RF Failure Routine", std::format(L"0x{:X}", is64 ? lc64.GuardRFFailureRoutine : lc32.GuardRFFailureRoutine) },
		{ L"Guard RF Failure Routine Function Pointer", std::format(L"0x{:X}", is64 ? lc64.GuardRFFailureRoutineFunctionPointer : lc32.GuardRFFailureRoutineFunctionPointer) },
		{ L"Dynamic Value Relocation Table Offset", std::format(L"0x{:X}", is64 ? lc64.DynamicValueRelocTableOffset : lc32.DynamicValueRelocTableOffset) },
		{ L"Guard RF Verify Stack Pointer Function Pointer", std::format(L"0x{:X}", is64 ? lc64.DynamicValueRelocTableSection : lc32.DynamicValueRelocTableSection) },
		{ L"Dynamic Value Relocation Table Section", std::format(L"0x{:X}", is64 ? lc64.GuardRFVerifyStackPointerFunctionPointer : lc32.GuardRFVerifyStackPointerFunctionPointer) },
		{ L"Hot Patch Table Offset", std::format(L"0x{:X}", is64 ? lc64.HotPatchTableOffset : lc32.HotPatchTableOffset) },
		{ L"Enclave Configuration Pointer", std::format(L"0x{:X}", is64 ? lc64.EnclaveConfigurationPointer : lc32.EnclaveConfigurationPointer) },
		{ L"Volatile Metadata Pointer", std::format(L"0x{:X}", is64 ? lc64.VolatileMetadataPointer : lc32.VolatileMetadataPointer) },
		{ L"Guard EH Continuation Table", std::format(L"0x{:X}", is64 ? lc64.GuardEHContinuationTable : lc32.GuardEHContinuationTable) },
		{ L"Guard EH Continuation Count", std::format(L"0x{:X}", is64 ? lc64.GuardEHContinuationCount : lc32.GuardEHContinuationCount) },
	};

	m_List.SetItemCount((int)m_Items.size());
}

void CLoadConfigView::UpdateUI(bool first) const {
	Frame()->GetUI().UIEnable(ID_EDIT_COPY, m_List.GetSelectedCount() > 0);
}

LRESULT CLoadConfigView::OnFind(UINT, WPARAM, LPARAM, BOOL&) {
	auto findDlg = Frame()->GetFindDialog();
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
