#include "pch.h"
#include "XrefView.h"
#include <SortHelper.h>

CXrefView::CXrefView(IMainFrame* frame, PEFile const& pe, uint64_t target, std::span<const Xref> refs, PCWSTR title) :
	CViewBase(frame), m_PE(pe), m_Title(title), m_Target(target) {
	m_Items.reserve(refs.size());
	for (auto const& r : refs)
		m_Items.push_back({ r });
}

CString CXrefView::GetTitle() const {
	return m_Title;
}

// the instruction at an address, as text
CString CXrefView::Describe(uint64_t va) const {
	auto base = m_PE.GetImageBase();
	if (va < base || va - base > 0xFFFFFFFFULL)
		return L"";
	auto offset = m_PE.GetOffsetFromRVA(va - base);
	if (offset == 0 || offset >= m_PE.GetFileSize())
		return L"";

	auto arch = ArchOf(m_PE);
	size_t h;
	if (!arch || !OpenDisassembler(*arch, h))
		return L"";
	csh handle = h;
	auto size = std::min<uint32_t>(16, m_PE.GetFileSize() - (uint32_t)offset);
	auto code = m_PE.GetSpan((uint32_t)offset, size);
	CString text;
	if (auto inst = cs_malloc(handle)) {
		auto bytes = (const uint8_t*)code.data();
		size_t left = code.size();
		if (cs_disasm_iter(handle, &bytes, &left, &va, inst)) {
			text = CString(inst->mnemonic);
			if (inst->op_str[0])
				text += L" " + CString(inst->op_str);
		}
		cs_free(inst, 1);
	}
	cs_close(&handle);
	return text;
}

void CXrefView::Resolve(Item const& item) const {
	if (item.Resolved)
		return;
	item.Resolved = true;
	item.Function = Frame()->ResolveVa(item.Ref.From).c_str();
	item.Instruction = Describe(item.Ref.From);
}

CString CXrefView::GetColumnText(HWND, int row, int col) const {
	auto const& item = m_Items[row];
	switch (col) {
		case 0: return std::format(L"0x{:X}", item.Ref.From).c_str();
		case 1: Resolve(item); return item.Function;
		case 2: return XrefKindToString(item.Ref.Kind);
		case 3: Resolve(item); return item.Instruction;
	}
	return CString();
}

void CXrefView::DoSort(SortInfo const* si) {
	if (si == nullptr)
		return;

	auto asc = si->SortAscending;
	auto compare = [&](Item const& a, Item const& b) {
		switch (si->SortColumn) {
			case 0: return SortHelper::Sort(a.Ref.From, b.Ref.From, asc);
			case 1: Resolve(a); Resolve(b); return SortHelper::Sort(a.Function, b.Function, asc);
			case 2: return SortHelper::Sort((int)a.Ref.Kind, (int)b.Ref.Kind, asc);
			case 3: Resolve(a); Resolve(b); return SortHelper::Sort(a.Instruction, b.Instruction, asc);
		}
		return false;
	};
	std::stable_sort(m_Items.begin(), m_Items.end(), compare);
}

bool CXrefView::OnDoubleClickList(HWND, int row, int, CPoint const&) const {
	if (row < 0 || row >= (int)m_Items.size())
		return false;
	return Frame()->GoToVa(m_Items[row].Ref.From);
}

LRESULT CXrefView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	auto cm = GetColumnManager(m_List);
	cm->AddColumn(L"From", LVCFMT_RIGHT, 140);
	cm->AddColumn(L"Function", LVCFMT_LEFT, 280);
	cm->AddColumn(L"Type", LVCFMT_LEFT, 110);
	cm->AddColumn(L"Instruction", LVCFMT_LEFT, 400);

	m_List.SetItemCount((int)m_Items.size());
	return 0;
}
