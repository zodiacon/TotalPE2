#include "pch.h"
#include "HexView.h"
#include "PEFile.h"
#include "MemoryBuffer.h"

CHexView::CHexView(IMainFrame* frame, CString const& title) : CViewBase(frame), m_Title(title) {
}

CHexControl& CHexView::Hex() {
	return m_Hex;
}

bool CHexView::SetData(PEFile const& pe, uint32_t offset, uint32_t size) {
	uint32_t bias;
	m_Ptr = pe.Map<BYTE>(offset, size, bias);
	ATLASSERT(m_Ptr);
	if (!m_Ptr)
		return false;

	m_Buffer = std::make_unique<MemoryBuffer>(m_Ptr.get() + bias, size, false);
	m_Hex.SetBufferManager(m_Buffer.get());
	m_Hex.SetBiasOffset(offset);

	return true;
}

void CHexView::ClearData() {
	m_Hex.SetBufferManager(nullptr);
}

LRESULT CHexView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_hWndClient = m_Hex.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE);

	return 0;
}

LRESULT CHexView::OnCopy(WORD, WORD, HWND, BOOL&) const {
	return 0;
}
