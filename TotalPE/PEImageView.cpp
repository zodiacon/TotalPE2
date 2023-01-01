// View.cpp : implementation of the CView class
//
/////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "resource.h"
#include "PEImageView.h"
#include "PEFile.h"
#include "PEStrings.h"
#include "SortHelper.h"
#include "ClipboardHelper.h"

LRESULT CPEImageView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_hWndClient = m_List.Create(*this, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_OWNERDATA, 0);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);
	auto cm = GetColumnManager(m_List);

	cm->AddColumn(L"Name", LVCFMT_LEFT, 150);
	cm->AddColumn(L"Value", LVCFMT_LEFT, 200);
	cm->AddColumn(L"Details", LVCFMT_LEFT, 450);

	BuildItems();

	return 0;
}

CString CPEImageView::GetTitle() const {
	auto& path = m_PE.GetPath();
	return path.substr(path.rfind(L'\\') + 1).c_str();
}

HWND CPEImageView::GetHwnd() const {
	return m_hWnd;
}

CPEImageView::CPEImageView(IMainFrame* frame, PEFile const& pe) : CFrameView(frame), m_PE(pe) {
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
	if (!text.IsEmpty())
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

	DWORD fileSize = 0;
	HANDLE hFile = ::CreateFile(m_PE.GetPath().c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hFile != INVALID_HANDLE_VALUE) {
		fileSize = ::GetFileSize(hFile, nullptr);
		::CloseHandle(hFile);
	}

	m_Items = std::vector<DataItem>{
		{ L"File Name", m_PE.GetPath().substr(m_PE.GetPath().rfind(L'\\') + 1), m_PE.GetPath() },
		{ L"File Size", PEStrings::ToMemorySize(fileSize) },
		{ L"Alignment", std::format(L"0x{:X}", is64 ? opt64.FileAlignment : opt32.FileAlignment) },
		{ L"Checksum", std::format(L"0x{:X}", is64 ? opt64.CheckSum : opt32.CheckSum) },
		{ L"Time/Date Stamp", std::format(L"0x{:08X}", fheader.TimeDateStamp) },
		{ L"Machine", std::format(L"{} (0x{:X})", fheader.Machine, fheader.Machine), PEStrings::MachineTypeToString(fheader.Machine) },
		//{ L"Subsystem", std::to_wstring(header.get_sub_system()), PEStrings::SubsystemToString(header.get_sub_system()) },
		//{ L"Sections", std::to_wstring(header.get_sections_number()) },
		//{ L"Characteristics", std::format(L"0x{:08X}", header.get_characteristics()), PEStrings::CharacteristicsToString(header.get_characteristics()) },
		//{ L"Magic", std::format(L"{} (0x{:X})", header.get_magic(), header.get_magic()), PEStrings::MagicToString(header.get_magic()) },
		//{ L"DLL Characteristics", std::format(L"0x{:04X}", header.get_characteristics_dll()), PEStrings::DllCharacteristicsToString(header.get_characteristics_dll()) },
		//{ L"Image Base", std::format(L"0x{:X}", header.get_image_base()) },
		//{ L"Image Size", PEStrings::ToMemorySize(header.get_image_size()) },
		//{ L"Stack Reserve", PEStrings::ToMemorySize(header.get_stack_reserve_size()) },
		//{ L"Stack Commit", PEStrings::ToMemorySize(header.get_stack_commit_size()) },
		//{ L"Heap Reserve", PEStrings::ToMemorySize(header.get_heap_reserve_size()) },
		//{ L"Heap Commit", PEStrings::ToMemorySize(header.get_heap_commit_size()) },
		//{ L"Is Managed?", header.has_directory(IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR) ? L"Yes" : L"No" },
		//{ L"Code Size", PEStrings::ToMemorySize(header.get_size_of_code()) },
		//{ L"Entry Point", std::format(L"0x{:X}", header.get_entry_point()) },
		//{ L"OS Version", std::format(L"{}.{}", header.get_os_ver_major(), header.get_os_ver_minor()) },
		//{ L"Image Version", std::format(L"{}.{}", header.get_image_ver_major(), header.get_image_ver_minor()) },
		//{ L"Linker Version", std::format(L"{}.{}", header.get_major_linker(), header.get_minor_linker()) },
		//{ L"Loader Flags", std::format(L"0x{:X}", header.get_loader_flags()) },
	};

	m_List.SetItemCount((int)m_Items.size());
}

void CPEImageView::UpdateUI() {
	Frame()->GetUI().UIEnable(ID_EDIT_COPY, m_List.GetSelectedCount() > 0);
}
