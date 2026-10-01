#include "pch.h"
#include "BoundImportView.h"

namespace {
	std::wstring TimeStampToString(DWORD ts) {
		if (ts == 0)
			return L"0";
		if (ts == 0xFFFFFFFF)
			return L"0xFFFFFFFF (bound to new-style imports)";
		__time64_t t = ts;
		tm tm{};
		if (_gmtime64_s(&tm, &t))
			return std::format(L"0x{:X}", ts);
		return std::format(L"0x{:X} ({:04}-{:02}-{:02} {:02}:{:02}:{:02} UTC)", ts,
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	}

	std::wstring ReadAnsiString(PEFile const& pe, uint64_t offset) {
		if (offset >= pe.GetFileSize())
			return {};
		auto size = (uint32_t)std::min<uint64_t>(260, pe.GetFileSize() - offset);
		auto span = pe.GetSpan((uint32_t)offset, size);
		auto p = reinterpret_cast<const char*>(span.data());
		auto len = strnlen(p, span.size());
		return std::wstring(p, p + len);
	}
}

CBoundImportView::CBoundImportView(IMainFrame* frame, PEFile const& pe) : CViewBase(frame), m_PE(pe) {
}

CString CBoundImportView::GetTitle() const {
	return L"Bound Imports";
}

CString CBoundImportView::GetColumnText(HWND, int row, int col) const {
	auto& item = m_Items[row];
	switch (col) {
		case 0: return (item.Forwarder ? L"    " + item.Module : item.Module).c_str();
		case 1: return item.Forwarder ? L"Forwarder Ref" : L"Module";
		case 2: return TimeStampToString(item.TimeStamp).c_str();
		case 3: return std::format(L"0x{:X}", item.NameOffset).c_str();
		case 4: return item.Forwarder ? L"" : std::to_wstring(item.ForwarderCount).c_str();
	}
	return CString();
}

void CBoundImportView::DoSort(SortInfo const*) {
}

LRESULT CBoundImportView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	auto cm = GetColumnManager(m_List);
	cm->AddColumn(L"Module", LVCFMT_LEFT, 220);
	cm->AddColumn(L"Type", LVCFMT_LEFT, 110);
	cm->AddColumn(L"Time Stamp", LVCFMT_LEFT, 280);
	cm->AddColumn(L"Name Offset", LVCFMT_RIGHT, 100);
	cm->AddColumn(L"Forwarder Refs", LVCFMT_RIGHT, 100);

	BuildItems();
	return 0;
}

void CBoundImportView::BuildItems() {
	auto dirs = m_PE.GetDataDirs();
	if (!dirs || dirs->size() <= IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT)
		return;
	auto& dd = (*dirs)[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT].DataDir;
	if (dd.VirtualAddress == 0 || dd.Size < sizeof(IMAGE_BOUND_IMPORT_DESCRIPTOR))
		return;

	// the bound import directory is usually not mapped into the image, so its address is a
	// file offset in practice; try the RVA mapping first and fall back to the raw value
	uint64_t base = m_PE.GetOffsetFromRVA(dd.VirtualAddress);
	if (base == 0 || base >= m_PE.GetFileSize())
		base = dd.VirtualAddress;
	if (base >= m_PE.GetFileSize())
		return;

	auto size = (uint32_t)std::min<uint64_t>(dd.Size, m_PE.GetFileSize() - base);
	uint32_t pos = 0;
	while (pos + sizeof(IMAGE_BOUND_IMPORT_DESCRIPTOR) <= size && m_Items.size() < 100000) {
		auto desc = m_PE.Read<IMAGE_BOUND_IMPORT_DESCRIPTOR>((uint32_t)base + pos);
		if (desc.TimeDateStamp == 0 && desc.OffsetModuleName == 0 && desc.NumberOfModuleForwarderRefs == 0)
			break;
		pos += sizeof(desc);
		m_Items.push_back({ ReadAnsiString(m_PE, base + desc.OffsetModuleName), false,
			desc.TimeDateStamp, desc.OffsetModuleName, desc.NumberOfModuleForwarderRefs });

		for (WORD i = 0; i < desc.NumberOfModuleForwarderRefs && pos + sizeof(IMAGE_BOUND_FORWARDER_REF) <= size; i++) {
			auto ref = m_PE.Read<IMAGE_BOUND_FORWARDER_REF>((uint32_t)base + pos);
			pos += sizeof(ref);
			m_Items.push_back({ ReadAnsiString(m_PE, base + ref.OffsetModuleName), true,
				ref.TimeDateStamp, ref.OffsetModuleName, 0 });
		}
	}
	m_List.SetItemCount((int)m_Items.size());
}
