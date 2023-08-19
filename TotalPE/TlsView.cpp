#include "pch.h"
#include "TlsView.h"

CTlsView::CTlsView(IMainFrame* frame, PEFile const& pe) : CViewBase(frame), m_PE(pe) {
}

CString CTlsView::GetTitle() const {
	return L"TLS";
}

CString CTlsView::GetColumnText(HWND h, int row, int col) const {
	if (h == m_List) {
		auto& item = m_Items[row];
		switch (col) {
			case 0: return std::format(L"0x{:X}", item).c_str();
			case 1:
				auto & sym = Frame()->GetSymbols();
				if (sym)
					return sym.GetSymbolByRVA(item).Name().c_str();
				break;
		}
	}
	else {
		auto& item = m_Data[row];
		switch (col) {
			case 0: return item.Name.c_str();
			case 1: return item.Value.c_str();
		}
	}
	return CString();
}

void CTlsView::DoSort(SortInfo const* si) {
}

LRESULT CTlsView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	m_List.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	m_GenList.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS);
	m_GenList.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	auto cm = GetColumnManager(m_List);
	cm->AddColumn(L"dummy", 0, 1);
	cm->AddColumn(L"Offset", LVCFMT_RIGHT, 100);
	cm->AddColumn(L"Symbol", LVCFMT_LEFT, 300);
	cm->DeleteColumn(0);

	cm = GetColumnManager(m_GenList);
	cm->AddColumn(L"Name", LVCFMT_LEFT, 160);
	cm->AddColumn(L"Value", LVCFMT_RIGHT, 180);

	m_Splitter.SetSplitterPanes(m_List, m_GenList);
	m_Splitter.SetSplitterPosPct(50);

	BuildItems();
	return 0;
}

void CTlsView::BuildItems() {
	auto tls = m_PE->GetTLS();

	m_Items = tls->TLSCallbacks;
	m_List.SetItemCount((int)m_Items.size());
	auto& tls32 = tls->unTLS.TLSDir32;
	auto& tls64 = tls->unTLS.TLSDir64;
	auto is64 = m_PE->GetFileInfo()->IsPE64;

	m_Data = std::vector<DataItem>{
		{ L"Start Address of Raw Data", std::format(L"0x{:X}", is64 ? tls64.StartAddressOfRawData : tls32.StartAddressOfRawData) },
		{ L"End Address of Raw Data", std::format(L"0x{:X}", is64 ? tls64.EndAddressOfRawData : tls32.EndAddressOfRawData) },
		{ L"Address of Callbacks", std::format(L"0x{:X}", is64 ? tls64.AddressOfCallBacks : tls32.AddressOfCallBacks) },
		{ L"Address of Index", std::format(L"0x{:X}", is64 ? tls64.AddressOfIndex: tls32.AddressOfIndex) },
		{ L"Characteristics", std::format(L"0x{:X}", is64 ? tls64.Characteristics : tls32.Characteristics) },
		{ L"Alignment", std::format(L"{}", is64 ? tls64.Alignment : tls32.Alignment) },
	};
	m_GenList.SetItemCount((int)m_Data.size());
}
