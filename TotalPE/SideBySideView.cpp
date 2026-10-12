#include "pch.h"
#include "resource.h"
#include "SideBySideView.h"
#include <map>

CSideBySideView::CSideBySideView(IMainFrame* frame, PEFile const& pe, PCWSTR title) : CViewBase(frame), m_PE(pe), m_Title(title) {
}

CString CSideBySideView::GetTitle() const {
	return m_Title;
}

LRESULT CSideBySideView::OnCreate(UINT, WPARAM, LPARAM, BOOL& handled) {
	m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	m_Asm = new CScintillaView(Frame(), m_PE, m_Title);
	m_C = new CScintillaView(Frame(), m_PE, m_Title);
	m_Asm->DoCreate(m_Splitter);
	m_C->DoCreate(m_Splitter);
	auto notify = [this](CScintillaView& side, SCNotification const& scn) { OnNotify(side, scn); };
	m_Asm->SetNotify(notify);
	m_C->SetNotify(notify);
	m_Splitter.SetSplitterPanes(*m_Asm, *m_C);
	m_Splitter.SetSplitterPosPct(50);
	m_Active = m_C;

	handled = FALSE;	// the base registers the view for idle handling and the message filter
	return 0;
}

LRESULT CSideBySideView::OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
	// the sides go with their windows (each deletes itself)
	m_Asm->SetNotify(nullptr);
	m_C->SetNotify(nullptr);
	m_Active = nullptr;
	handled = FALSE;
	return 0;
}

LRESULT CSideBySideView::OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
	if (m_Active)
		m_Active->SetFocus();
	return 0;
}

LRESULT CSideBySideView::OnForward(UINT msg, WPARAM wp, LPARAM lp, BOOL&) {
	return m_Active ? m_Active->SendMessage(msg, wp, lp) : 0;
}

bool CSideBySideView::SetAsm(std::span<const std::byte> code, uint64_t address, CpuArch arch) {
	m_Asm->SetLanguage(LexLanguage::Asm);
	m_Asm->SetAsmCode(code, address, arch);
	m_Asm->GetCtrl().SetReadOnly(true);
	m_C->SetLanguage(LexLanguage::C);
	m_C->SetFunctionAddress(address);
	m_C->GetCtrl().SetReadOnly(true);
	return true;
}

void CSideBySideView::SetC(std::string const& c, GhidraDecompiler::LineAddresses lines, std::unordered_map<std::wstring, uint64_t> links) {
	auto& ctrl = m_C->GetCtrl();
	ctrl.SetReadOnly(false);
	m_C->SetText(c.c_str());
	ctrl.SetReadOnly(true);
	ctrl.EmptyUndoBuffer();
	m_C->SetFunctionLinks(std::move(links));
	m_Lines = std::move(lines);
	Align();
	// the caret's line on the side the user is in marks the other side's lines
	if (m_Active)
		LinkFrom(*m_Active);
}

// Each line of the C goes on the row of the first instruction it was made from. Rows can only be added (blank lines of
// annotations, which are not in the text), so the lines are aligned in order: those whose instructions are out of order
// (the longest run in order is kept) are where the lines around them put them.
void CSideBySideView::Align() {
	struct Pair {
		int C, Asm;
	};
	std::vector<Pair> pairs;
	for (int line = 0; line < (int)m_Lines.size(); line++) {
		int first = -1;
		for (auto va : m_Lines[line]) {
			auto asmLine = m_Asm->LineOfAddress(va);
			if (asmLine >= 0 && (first < 0 || asmLine < first))
				first = asmLine;
		}
		if (first >= 0)
			pairs.push_back({ line, first });
	}

	// the longest run of pairs whose instructions are in order (by patience sorting)
	std::vector<int> tails, previous(pairs.size(), -1);
	for (int i = 0; i < (int)pairs.size(); i++) {
		auto it = std::lower_bound(tails.begin(), tails.end(), pairs[i].Asm, [&](int index, int value) { return pairs[index].Asm < value; });
		if (it != tails.begin())
			previous[i] = *(it - 1);
		if (it == tails.end())
			tails.push_back(i);
		else
			*it = i;
	}
	std::vector<Pair> run;
	for (int i = tails.empty() ? -1 : tails.back(); i >= 0; i = previous[i])
		run.push_back(pairs[i]);
	std::ranges::reverse(run);

	// the rows to add below lines of each side, so that the lines of each pair are on the same row
	std::map<int, int> padAsm, padC;
	int addedAsm = 0, addedC = 0;
	for (auto const& [c, a] : run) {
		int rowC = c + addedC, rowAsm = a + addedAsm;
		if (rowC < rowAsm && c > 0) {
			padC[c - 1] += rowAsm - rowC;
			addedC += rowAsm - rowC;
		}
		else if (rowAsm < rowC && a > 0) {
			padAsm[a - 1] += rowC - rowAsm;
			addedAsm += rowC - rowAsm;
		}
	}
	// and at the end of the shorter one, so that the two scroll to their ends together
	auto& asmCtrl = m_Asm->GetCtrl();
	auto& cCtrl = m_C->GetCtrl();
	int rowsAsm = (int)asmCtrl.SendMessage(SCI_GETLINECOUNT) + addedAsm, rowsC = (int)cCtrl.SendMessage(SCI_GETLINECOUNT) + addedC;
	if (rowsAsm < rowsC)
		padAsm[(int)asmCtrl.SendMessage(SCI_GETLINECOUNT) - 1] += rowsC - rowsAsm;
	else if (rowsC < rowsAsm)
		padC[(int)cCtrl.SendMessage(SCI_GETLINECOUNT) - 1] += rowsAsm - rowsC;

	auto apply = [](CScintillaCtrl& ctrl, std::map<int, int> const& pads) {
		ctrl.SendMessage(SCI_ANNOTATIONCLEARALL);
		for (auto const& [line, count] : pads) {
			std::string blank(" ");
			blank.append(count - 1, '\n');
			ctrl.SendMessage(SCI_ANNOTATIONSETTEXT, line, (LPARAM)blank.c_str());
			ctrl.SendMessage(SCI_ANNOTATIONSETSTYLE, line, STYLE_DEFAULT);
		}
		ctrl.SendMessage(SCI_ANNOTATIONSETVISIBLE, ANNOTATION_STANDARD);
	};
	apply(asmCtrl, padAsm);
	apply(cCtrl, padC);
}

void CSideBySideView::OnNotify(CScintillaView& side, SCNotification const& scn) {
	auto& other = &side == m_Asm ? *m_C : *m_Asm;
	switch (scn.nmhdr.code) {
		case SCN_FOCUSIN:
			m_Active = &side;
			break;

		case SCN_ZOOM:
			if (!m_Syncing) {
				m_Syncing = true;
				other.GetCtrl().SendMessage(SCI_SETZOOM, side.GetCtrl().SendMessage(SCI_GETZOOM));
				m_Syncing = false;
			}
			break;

		case SCN_UPDATEUI:
			// the rows are aligned: the other side shows the same rows
			if ((scn.updated & SC_UPDATE_V_SCROLL) && !m_Syncing) {
				m_Syncing = true;
				other.GetCtrl().SendMessage(SCI_SETFIRSTVISIBLELINE, side.GetCtrl().SendMessage(SCI_GETFIRSTVISIBLELINE));
				m_Syncing = false;
			}
			if ((scn.updated & SC_UPDATE_SELECTION) && &side == m_Active)
				LinkFrom(side);
			break;
	}
}

// The lines of the other side that the caret's line corresponds to: the instructions of a line of C, the lines of C
// that an instruction is in
void CSideBySideView::LinkFrom(CScintillaView& side) {
	auto& ctrl = side.GetCtrl();
	auto line = (int)ctrl.LineFromPosition(ctrl.GetCurrentPos());
	std::vector<int> related;
	if (&side == m_C) {
		if (line < (int)m_Lines.size())
			for (auto va : m_Lines[line])
				if (auto asmLine = m_Asm->LineOfAddress(va); asmLine >= 0)
					related.push_back(asmLine);
		m_Asm->MarkLines(related);
	}
	else {
		if (auto va = m_Asm->LineAddress(line))
			for (int i = 0; i < (int)m_Lines.size(); i++)
				if (std::ranges::find(m_Lines[i], va) != m_Lines[i].end())
					related.push_back(i);
		m_C->MarkLines(related);
	}
	side.MarkLines({});
}

int64_t CSideBySideView::GetNavigationPosition() const {
	return m_Asm ? m_Asm->GetNavigationPosition() : -1;
}

void CSideBySideView::SetNavigationPosition(int64_t va) {
	GoToAddress((uint64_t)va);
}

bool CSideBySideView::GoToAddress(uint64_t va) {
	return m_Asm && m_Asm->GoToAddress(va);
}
