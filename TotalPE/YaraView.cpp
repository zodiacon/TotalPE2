#include "pch.h"
#include "YaraView.h"
#include <PEFile.h>
#include <SortHelper.h>
#include <filesystem>

namespace {
	std::wstring Wide(std::string const& s) {
		if (s.empty())
			return {};
		int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
		std::wstring w(n, L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
		return w;
	}
}

CYaraView::CYaraView(IMainFrame* frame, PEFile const& pe, std::vector<YaraMatch> const& matches, std::vector<YaraDiagnostic> const& errors)
	: CViewBase(frame) {
	for (auto const& m : matches) {
		Row base;
		base.Rule = Wide(m.Rule);
		base.Namespace = Wide(m.Namespace);
		for (auto const& t : m.Tags)
			base.Tags += (base.Tags.empty() ? L"" : L", ") + Wide(t);
		for (auto const& [name, value] : m.Meta)
			if (name == "description") {
				base.Description = Wide(value);
				break;
			}
		if (m.Strings.empty()) {
			base.Index = m_Rows.size();
			m_Rows.push_back(base);
			continue;
		}
		for (auto const& s : m.Strings) {
			Row row = base;
			row.String = Wide(s.Identifier);
			row.Offset = (int64_t)s.Offset;
			row.Length = s.Length;
			for (auto b : s.Data)
				row.Data += std::format(L"{:02X} ", b);
			if (auto sections = pe.GetSecHeaders())
				for (auto const& sec : *sections) {
					auto const& h = sec.SecHdr;
					if (h.SizeOfRawData && s.Offset >= h.PointerToRawData && s.Offset < (uint64_t)h.PointerToRawData + h.SizeOfRawData) {
						row.Section = Wide(sec.SectionName);
						row.Rva = h.VirtualAddress + (int64_t)(s.Offset - h.PointerToRawData);
						break;
					}
				}
			row.Index = m_Rows.size();
			m_Rows.push_back(std::move(row));
		}
	}
	for (auto const& e : errors) {
		Row row;
		row.Rule = L"(left out) " + std::filesystem::path(e.File).filename().wstring();
		row.Description = std::format(L"line {}: {}", e.Line, Wide(e.Message));
		row.Index = m_Rows.size();
		m_Rows.push_back(std::move(row));
	}
}

CString CYaraView::GetTitle() const {
	return L"YARA";
}

CString CYaraView::GetColumnText(HWND, int row, int col) const {
	auto& r = m_Rows[row];
	switch (col) {
		case 0: return r.Rule.c_str();
		case 1: return r.Namespace.c_str();
		case 2: return r.Tags.c_str();
		case 3: return r.String.c_str();
		case 4: return r.Offset >= 0 ? std::format(L"0x{:X}", r.Offset).c_str() : L"";
		case 5: return r.Section.c_str();
		case 6: return r.Rva >= 0 ? std::format(L"0x{:X}", r.Rva).c_str() : L"";
		case 7: return r.Offset >= 0 ? std::format(L"{}", r.Length).c_str() : L"";
		case 8: return r.Data.c_str();
		case 9: return r.Description.c_str();
	}
	return CString();
}

void CYaraView::DoSort(SortInfo const* si) {
	if (si == nullptr) {
		std::ranges::sort(m_Rows, {}, &Row::Index);
		return;
	}
	auto asc = si->SortAscending;
	auto compare = [&](Row const& a, Row const& b) {
		switch (si->SortColumn) {
			case 0: return SortHelper::Sort(a.Rule, b.Rule, asc);
			case 1: return SortHelper::Sort(a.Namespace, b.Namespace, asc);
			case 2: return SortHelper::Sort(a.Tags, b.Tags, asc);
			case 3: return SortHelper::Sort(a.String, b.String, asc);
			case 4: return SortHelper::Sort(a.Offset, b.Offset, asc);
			case 5: return SortHelper::Sort(a.Section, b.Section, asc);
			case 6: return SortHelper::Sort(a.Rva, b.Rva, asc);
			case 7: return SortHelper::Sort(a.Length, b.Length, asc);
			case 8: return SortHelper::Sort(a.Data, b.Data, asc);
			case 9: return SortHelper::Sort(a.Description, b.Description, asc);
		}
		return false;
	};
	std::stable_sort(m_Rows.begin(), m_Rows.end(), compare);
}

bool CYaraView::OnDoubleClickList(HWND, int row, int, CPoint const&) const {
	if (row < 0 || row >= (int)m_Rows.size() || m_Rows[row].Offset < 0)
		return false;
	return Frame()->GoToFileOffset(m_Rows[row].Offset, false);
}

LRESULT CYaraView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	auto cm = GetColumnManager(m_List);
	cm->AddColumn(L"Rule", LVCFMT_LEFT, 200);
	cm->AddColumn(L"Namespace", LVCFMT_LEFT, 110);
	cm->AddColumn(L"Tags", LVCFMT_LEFT, 110);
	cm->AddColumn(L"String", LVCFMT_LEFT, 90);
	cm->AddColumn(L"Offset", LVCFMT_RIGHT, 90);
	cm->AddColumn(L"Section", LVCFMT_LEFT, 70);
	cm->AddColumn(L"RVA", LVCFMT_RIGHT, 90);
	cm->AddColumn(L"Length", LVCFMT_RIGHT, 60);
	cm->AddColumn(L"Data", LVCFMT_LEFT, 300);
	cm->AddColumn(L"Description", LVCFMT_LEFT, 400);
	cm->UpdateColumns();

	m_List.SetItemCount((int)m_Rows.size());
	return 0;
}
