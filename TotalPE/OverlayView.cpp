#include "pch.h"
#include "OverlayView.h"
#include "PEStrings.h"
#include "Md5.h"
#include "Authenticode.h"
#include <PEFile.h>
#include <ClipboardHelper.h>
#include <ListViewHelper.h>

COverlayView::COverlayView(IMainFrame* frame, PEFile const& pe, OverlayInfo const& info) : CViewBase(frame), m_PE(pe), m_Info(info) {
}

CString COverlayView::GetTitle() const {
	return L"Overlay";
}

CString COverlayView::GetColumnText(HWND, int row, int col) const {
	auto const& item = m_Items[row];
	switch (col) {
		case 0: return item.Name.c_str();
		case 1: return item.Value.c_str();
		case 2: return item.Details.c_str();
	}
	return CString();
}

bool COverlayView::OnDoubleClickList(HWND, int row, int, CPoint const&) const {
	if (row < 0 || row >= (int)m_Items.size() || m_Items[row].Offset < 0)
		return false;
	return Frame()->GoToFileOffset(m_Items[row].Offset, false);
}

void COverlayView::UpdateUI(bool) {
	Frame()->GetUI().UIEnable(ID_EDIT_COPY, m_List.GetSelectedCount() > 0);
}

LRESULT COverlayView::OnCopy(WORD, WORD, HWND, BOOL&) const {
	ClipboardHelper::CopyText(m_hWnd, ListViewHelper::GetSelectedRowsAsString(m_List, L"\t"));
	return 0;
}

LRESULT COverlayView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_List.Create(*this, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS);
	m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	auto cm = GetColumnManager(m_List);
	cm->AddColumn(L"Name", LVCFMT_LEFT, 150);
	cm->AddColumn(L"Value", LVCFMT_LEFT, 520);
	cm->AddColumn(L"Details", LVCFMT_LEFT, 400);

	BuildItems();
	return 0;
}

void COverlayView::BuildItems() {
	CWaitCursor wait;
	m_Items.clear();
	auto add = [&](std::wstring name, std::wstring value, std::wstring details = L"", int64_t offset = -1) {
		m_Items.push_back({ std::move(name), std::move(value), std::move(details), offset });
	};

	const int64_t start = (int64_t)m_Info.Offset;
	add(L"Offset", PEStrings::ToHex(m_Info.Offset), L"The first byte after the last section", start);
	add(L"Size", PEStrings::ToMemorySize(m_Info.Size), L"", start);
	add(L"End", PEStrings::ToHex(m_Info.Offset + m_Info.Size), L"", start + (int64_t)m_Info.Size - 1);
	add(L"Looks Like", m_Info.Kind.empty() ? L"Unknown" : m_Info.Kind, L"", start);

	const wchar_t* entropyNote = m_Info.Entropy >= 7.5 ? L"Compressed or encrypted data" : m_Info.Entropy >= 6 ? L"Probably compiled code or compressed data" :
		m_Info.Entropy < 1 ? L"Almost no variation: padding, perhaps" : L"";
	add(L"Entropy", std::format(L"{:.3f} of 8", m_Info.Entropy), entropyNote, start);

	// the hashes of the data, and its first bytes
	auto data = m_PE.GetSpan((uint32_t)m_Info.Offset, (uint32_t)std::min<uint64_t>(m_Info.Size, 0xFFFFFFFF));
	if (data.size() < m_Info.Size)
		add(L"Note", L"The data is larger than 4 GB: the hashes cover the first 4 GB");
	auto md5 = Md5Hex(data.data(), data.size());
	add(L"MD5", std::wstring(md5.begin(), md5.end()), L"", start);
	auto sha256 = Sha256Hex(data);
	add(L"SHA-256", std::wstring(sha256.begin(), sha256.end()), L"", start);

	std::wstring bytes, text;
	for (size_t i = 0; i < std::min<size_t>(data.size(), 32); i++) {
		auto b = std::to_integer<uint8_t>(data[i]);
		bytes += std::format(L"{:02X} ", b);
		text += b >= 0x20 && b < 0x7F ? (wchar_t)b : L'.';
	}
	add(L"First Bytes", bytes, text, start);

	if (m_Info.CertificateSize)
		add(L"Certificate Table", std::format(L"{} at {}", PEStrings::ToMemorySize(m_Info.CertificateSize), PEStrings::ToHex(m_Info.CertificateOffset)),
			L"Not part of the overlay (see Security)", (int64_t)m_Info.CertificateOffset);

	m_List.SetItemCount((int)m_Items.size());
}
