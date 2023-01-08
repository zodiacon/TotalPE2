// View.cpp : implementation of the CView class
//
/////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "resource.h"
#include "PEImageView.h"
#include "PEFile.h"
#include "PEStrings.h"
#include <SortHelper.h>
#include <ClipboardHelper.h>

LRESULT CPEImageView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_hWndClient = m_List.Create(*this, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_OWNERDATA, 0);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	auto cm = GetColumnManager(m_List);

	cm->AddColumn(L"Name", LVCFMT_LEFT, 150);
	cm->AddColumn(L"Value", LVCFMT_LEFT, 200);
	cm->AddColumn(L"Details", LVCFMT_LEFT, 550);

	BuildItems();

	return 0;
}

CString CPEImageView::GetTitle() const {
	auto& path = m_PE.GetPath();
	return path.substr(path.rfind(L'\\') + 1).c_str();
}

CPEImageView::CPEImageView(IMainFrame* frame, PEFile const& pe) : CViewBase(frame), m_PE(pe) {
}

CString CPEImageView::GetColumnText(HWND, int row, int col) const {
	auto& item = m_Items[row];
	switch (col) {
		case 0: return item.Name.c_str();
		case 1: return item.Value.c_str();
		case 2: return item.Details.c_str();
	}
	return CString();
}

bool CPEImageView::IsSortable(HWND, int col) const {
	return col == 0;		// sort on Name column only
}

void CPEImageView::OnStateChanged(HWND, int from, int to, DWORD oldState, DWORD newState) {
	UpdateUI();
}

LRESULT CPEImageView::OnCopy(WORD, WORD, HWND, BOOL&) const {
	auto text = ListViewHelper::GetSelectedRowsAsString(m_List);
	ClipboardHelper::CopyText(m_hWnd, text);
	return 0;
}

void CPEImageView::DoSort(SortInfo const* si) {
	if (si == nullptr)
		return;

	auto compare = [&](auto& item1, auto& item2) {
		return SortHelper::Sort(item1.Name, item2.Name, si->SortAscending);
	};

	std::ranges::sort(m_Items, compare);
}

void CPEImageView::BuildItems() {
	auto header = m_PE->GetNTHeader();
	//
	// file header is bitness agnostic
	//
	auto fheader = header->NTHdr32.FileHeader;
	auto is64 = m_PE->GetFileInfo()->IsPE64;
	auto opt64 = header->NTHdr64.OptionalHeader;
	auto opt32 = header->NTHdr32.OptionalHeader;

	auto subSystem = is64 ? opt64.Subsystem : opt32.Subsystem;
	auto magic = is64 ? opt64.Magic : opt32.Magic;
	auto dllchar = is64 ? opt64.DllCharacteristics : opt32.DllCharacteristics;
	auto file = Frame()->GetSymbols().GetSymbolFile();

	m_Items = std::vector<DataItem> {
		{ L"File Name", m_PE.GetPath().substr(m_PE.GetPath().rfind(L'\\') + 1), m_PE.GetPath() },
		{ L"File Size", PEStrings::ToMemorySize(m_PE.GetFileSize()) },
		{ L"File Alignment", std::format(L"0x{:X}", is64 ? opt64.FileAlignment : opt32.FileAlignment) },
		{ L"Section Alignment", std::format(L"0x{:X}", is64 ? opt64.SectionAlignment : opt32.SectionAlignment) },
		{ L"Checksum", std::format(L"0x{:X}", is64 ? opt64.CheckSum : opt32.CheckSum) },
		{ L"Time/Date Stamp", std::format(L"0x{:08X}", fheader.TimeDateStamp) },
		{ L"Machine", std::format(L"{} (0x{:X})", fheader.Machine, fheader.Machine), std::format(L"{} ({})", 
			PEStrings::MachineTypeToString(fheader.Machine), libpe::MapFileHdrMachine.at(fheader.Machine)) },
		{ L"Subsystem", std::to_wstring(subSystem), std::format(L"{} ({})", PEStrings::SubsystemToString(subSystem), libpe::MapOptHdrSubsystem.at(subSystem)) },
		{ L"Number of Sections", std::to_wstring(fheader.NumberOfSections) },
		{ L"Size of Optional Header", std::format(L"{} bytes", fheader.SizeOfOptionalHeader) },
		{ L"Size of Headers", std::format(L"{} bytes", is64 ? opt64.SizeOfHeaders : opt32.SizeOfHeaders) },
		{ L"Number of Symbols", std::to_wstring(fheader.NumberOfSymbols) },
		{ L"Characteristics", std::format(L"0x{:08X}", fheader.Characteristics), PEStrings::CharacteristicsToString(fheader.Characteristics) },
		{ L"Magic", std::format(L"{} (0x{:X})", magic, magic), std::format(L"{} ({})", PEStrings::MagicToString(magic), libpe::MapOptHdrMagic.at(magic)) },
		{ L"DLL Characteristics", std::format(L"0x{:04X}", dllchar), PEStrings::DllCharacteristicsToString(dllchar) },
		{ L"Image Base", std::format(L"0x{:X}", is64 ? opt64.ImageBase : opt32.ImageBase) },
		{ L"Image Size", PEStrings::ToMemorySize(is64 ? opt64.SizeOfImage : opt32.SizeOfImage) },
		{ L"Stack Reserve", PEStrings::ToMemorySize(is64 ? opt64.SizeOfStackReserve : opt32.SizeOfStackReserve) },
		{ L"Stack Commit", PEStrings::ToMemorySize(is64 ? opt64.SizeOfStackCommit : opt32.SizeOfStackCommit) },
		{ L"Heap Reserve", PEStrings::ToMemorySize(is64 ? opt64.SizeOfHeapReserve : opt32.SizeOfHeapReserve) },
		{ L"Heap Commit", PEStrings::ToMemorySize(is64 ? opt64.SizeOfHeapCommit : opt32.SizeOfHeapCommit) },
		{ L"Size of Code", PEStrings::ToMemorySize(is64 ? opt64.SizeOfCode : opt32.SizeOfCode) },
		{ L"Size of Initialized Data", PEStrings::ToMemorySize(is64 ? opt64.SizeOfInitializedData : opt32.SizeOfInitializedData) },
		{ L"Size of Uninitialized Data", PEStrings::ToMemorySize(is64 ? opt64.SizeOfUninitializedData : opt32.SizeOfUninitializedData) },
		{ L"Is Managed?", m_PE->GetFileInfo()->HasCOMDescr ? L"Yes" : L"No" },
		{ L"Entry Point", std::format(L"0x{:X}", is64 ? opt64.AddressOfEntryPoint : opt32.AddressOfEntryPoint) },
		{ L"Base of Code", std::format(L"0x{:X}", is64 ? opt64.BaseOfCode : opt32.BaseOfCode) },
		{ L"OS Version", std::format(L"{}.{}", is64 ? opt64.MajorOperatingSystemVersion : opt32.MajorOperatingSystemVersion,
			is64 ? opt64.MinorOperatingSystemVersion : opt32.MinorOperatingSystemVersion) },
		{ L"Image Version", std::format(L"{}.{}", is64 ? opt64.MajorImageVersion: opt32.MajorImageVersion,
			is64 ? opt64.MinorImageVersion : opt32.MinorImageVersion) },
		{ L"Subsystem Version", std::format(L"{}.{}", is64 ? opt64.MajorSubsystemVersion : opt32.MajorSubsystemVersion,
			is64 ? opt64.MinorSubsystemVersion : opt32.MinorSubsystemVersion) },
		{ L"Linker Version", std::format(L"{}.{}", is64 ? opt64.MajorLinkerVersion : opt32.MajorLinkerVersion,
			is64 ? opt64.MinorLinkerVersion : opt32.MinorLinkerVersion) },
		{ L"Loader Flags", std::format(L"0x{:X}", is64 ? opt64.LoaderFlags : opt32.LoaderFlags) },
		{ L"Symbols", file.empty() ? L"(None)" : L"Loaded", file },
	};

	if (!is64) {
		m_Items.push_back({ L"Base of Data", std::format(L"0x{:X}", opt32.BaseOfData) });
	}

	m_List.SetItemCount((int)m_Items.size());
}

void CPEImageView::UpdateUI(bool first) {
	Frame()->GetUI().UIEnable(ID_EDIT_COPY, m_List.GetSelectedCount() > 0);
}

LRESULT CPEImageView::OnFind(UINT, WPARAM, LPARAM, BOOL&) {
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
