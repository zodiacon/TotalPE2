#include "pch.h"
#include "ClrView.h"
#include "PEStrings.h"

namespace {
	std::wstring ClrFlagsToString(DWORD flags) {
		struct { DWORD Flag; PCWSTR Text; } items[] = {
			{ COMIMAGE_FLAGS_ILONLY, L"IL Only" },
			{ COMIMAGE_FLAGS_32BITREQUIRED, L"32-bit Required" },
			{ COMIMAGE_FLAGS_IL_LIBRARY, L"IL Library" },
			{ COMIMAGE_FLAGS_STRONGNAMESIGNED, L"Strong Name Signed" },
			{ COMIMAGE_FLAGS_NATIVE_ENTRYPOINT, L"Native Entry Point" },
			{ COMIMAGE_FLAGS_TRACKDEBUGDATA, L"Track Debug Data" },
			{ 0x00020000, L"32-bit Preferred" },
		};
		std::wstring text;
		for (auto& i : items) {
			if (flags & i.Flag) {
				if (!text.empty())
					text += L", ";
				text += i.Text;
			}
		}
		return text;
	}

	std::wstring VersionString(ClrAssemblyInfo const& a) {
		return std::format(L"{}.{}.{}.{}", a.Major, a.Minor, a.Build, a.Revision);
	}
}

CClrView::CClrView(IMainFrame* frame, PEFile const& pe) : CViewBase(frame), m_PE(pe) {
}

CString CClrView::GetTitle() const {
	return L".NET (CLR)";
}

CString CClrView::GetColumnText(HWND h, int row, int col) const {
	if (h == m_GenList) {
		auto& item = m_Data[row];
		return col == 0 ? item.Name.c_str() : item.Value.c_str();
	}
	auto& item = m_Items[row];
	switch (col) {
		case 0: return item.Category.c_str();
		case 1: return item.Name.c_str();
		case 2: return item.Value.c_str();
	}
	return CString();
}

void CClrView::DoSort(SortInfo const*) {
}

LRESULT CClrView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	m_GenList.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS);
	m_GenList.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	m_Details.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS);
	m_Details.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	auto cm = GetColumnManager(m_GenList);
	cm->AddColumn(L"Name", LVCFMT_LEFT, 180);
	cm->AddColumn(L"Value", LVCFMT_LEFT, 240);

	cm = GetColumnManager(m_Details);
	cm->AddColumn(L"Category", LVCFMT_LEFT, 120);
	cm->AddColumn(L"Name", LVCFMT_LEFT, 220);
	cm->AddColumn(L"Value", LVCFMT_LEFT, 240);

	m_Splitter.SetSplitterPanes(m_GenList, m_Details);
	m_Splitter.SetSplitterPosPct(40);

	BuildItems();
	return 0;
}

void CClrView::BuildItems() {
	m_Clr.Parse(m_PE);
	if (!m_Clr.HasHeader())
		return;

	auto& h = m_Clr.Header();
	auto hex = [](auto v) { return std::format(L"0x{:X}", v); };
	auto dir = [&](IMAGE_DATA_DIRECTORY const& d) {
		return d.VirtualAddress || d.Size ? std::format(L"RVA 0x{:X}, Size 0x{:X}", d.VirtualAddress, d.Size) : std::wstring(L"(none)");
	};

	m_Data = {
		{ L"Header Size", std::to_wstring(h.cb) },
		{ L"Runtime Version", std::format(L"{}.{}", h.MajorRuntimeVersion, h.MinorRuntimeVersion) },
		{ L"Metadata", dir(h.MetaData) },
		{ L"Flags", hex(h.Flags) },
		{ L"Flags (decoded)", ClrFlagsToString(h.Flags) },
		{ (h.Flags & COMIMAGE_FLAGS_NATIVE_ENTRYPOINT) ? L"Entry Point RVA" : L"Entry Point Token", hex(h.EntryPointToken) },
		{ L"Resources", dir(h.Resources) },
		{ L"Strong Name Signature", dir(h.StrongNameSignature) },
		{ L"Code Manager Table", dir(h.CodeManagerTable) },
		{ L"VTable Fixups", dir(h.VTableFixups) },
		{ L"Export Address Table Jumps", dir(h.ExportAddressTableJumps) },
		{ L"Managed Native Header", dir(h.ManagedNativeHeader) },
	};
	m_GenList.SetItemCount((int)m_Data.size());

	if (!m_Clr.HasMetadata())
		return;

	m_Items.push_back({ L"Metadata", L"Version", m_Clr.Version() });
	if (!m_Clr.ModuleName().empty())
		m_Items.push_back({ L"Metadata", L"Module", m_Clr.ModuleName() });

	if (m_Clr.HasAssembly()) {
		auto& a = m_Clr.Assembly();
		m_Items.push_back({ L"Assembly", L"Name", a.Name });
		m_Items.push_back({ L"Assembly", L"Version", VersionString(a) });
		m_Items.push_back({ L"Assembly", L"Culture", a.Culture.empty() ? L"(neutral)" : a.Culture });
		m_Items.push_back({ L"Assembly", L"Flags", hex(a.Flags) });
	}

	for (auto& s : m_Clr.Streams())
		m_Items.push_back({ L"Stream", std::wstring(s.Name.begin(), s.Name.end()),
			std::format(L"Offset 0x{:X}, Size 0x{:X}", s.Offset, s.Size) });

	for (auto& r : m_Clr.References())
		m_Items.push_back({ L"Assembly Reference", r.Name, VersionString(r) });

	for (auto& t : m_Clr.Tables())
		m_Items.push_back({ L"Table", std::format(L"{} (0x{:02X})", ClrMetadata::TableName(t.Id), t.Id), std::format(L"{} rows", t.Rows) });

	m_Details.SetItemCount((int)m_Items.size());
}
