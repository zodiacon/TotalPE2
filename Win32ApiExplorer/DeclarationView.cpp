#include "pch.h"
#include "DeclarationView.h"
#include "resource.h"
#include "AppSettings.h"
#include <WTLHelper.h>

namespace {
	constexpr int IndicatorLink = 8;

	struct Palette {
		COLORREF Back, Fore, Comment, Keyword, Type, Number, String, Preprocessor, Link, CaretLine, Selection, Caret, TipBack;
	};

	Palette CurrentPalette() {
		if (WTLHelper::IsDarkMode())
			return { RGB(30, 30, 30), RGB(220, 220, 220), RGB(87, 166, 74), RGB(86, 156, 214), RGB(78, 201, 176), RGB(181, 206, 168),
				RGB(214, 157, 133), RGB(155, 155, 155), RGB(100, 160, 230), RGB(40, 40, 44), RGB(38, 79, 120), RGB(240, 240, 240),
				RGB(50, 50, 54) };
		return { RGB(255, 255, 255), RGB(0, 0, 0), RGB(0, 128, 0), RGB(0, 0, 255), RGB(43, 145, 175), RGB(9, 136, 90),
			RGB(163, 21, 21), RGB(128, 128, 128), RGB(0, 102, 204), RGB(240, 240, 248), RGB(173, 214, 255), RGB(0, 0, 0),
			RGB(246, 246, 238) };
	}

	bool IsWordChar(char c) {
		return isalnum((unsigned char)c) || c == '_';
	}

	// where a name is in a line, as a word of its own (npos: it is not)
	size_t FindWord(std::string_view line, std::string_view word) {
		for (size_t at = line.find(word); at != std::string_view::npos; at = line.find(word, at + 1)) {
			bool before = at == 0 || !IsWordChar(line[at - 1]);
			bool after = at + word.size() >= line.size() || !IsWordChar(line[at + word.size()]);
			if (before && after)
				return at;
		}
		return std::string_view::npos;
	}
}

CDeclarationView::CDeclarationView(Win32Api& api) : m_Api(&api) {
}

LRESULT CDeclarationView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_Sci = ::CreateWindowEx(0, L"Scintilla", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, m_hWnd, nullptr,
		_Module.GetModuleInstance(), nullptr);
	if (!m_Sci)
		return -1;
	Sci(SCI_SETCODEPAGE, SC_CP_UTF8);
	Sci(SCI_USEPOPUP, SC_POPUP_NEVER);
	Sci(SCI_SETTABWIDTH, 4);
	Sci(SCI_SETMARGINWIDTHN, 0, 0);
	Sci(SCI_SETMARGINWIDTHN, 1, 6);
	Sci(SCI_SETCARETLINEVISIBLE, TRUE);
	Sci(SCI_SETCARETLINEVISIBLEALWAYS, TRUE);
	Sci(SCI_SETSCROLLWIDTHTRACKING, TRUE);
	Sci(SCI_SETSCROLLWIDTH, 1);
	Sci(SCI_SETMOUSEDWELLTIME, 500);

	auto lexer = CreateLexer("cpp");
	lexer->WordListSet(0, "auto break case char const continue default do double else enum extern float for goto if inline "
		"int long register restrict return short signed sizeof static struct switch typedef union unsigned void volatile "
		"while interface bool wchar_t");
	lexer->WordListSet(1, "uchar ushort uint ulonglong longlong undef1 undef2 undef4 undef8 GUID");
	Sci(SCI_SETILEXER, 0, (LPARAM)lexer);
	UpdateColors();
	Sci(SCI_SETREADONLY, TRUE);
	return 0;
}

LRESULT CDeclarationView::OnSize(UINT, WPARAM, LPARAM lp, BOOL&) {
	if (m_Sci)
		::SetWindowPos(m_Sci, nullptr, 0, 0, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), SWP_NOZORDER | SWP_NOACTIVATE);
	return 0;
}

LRESULT CDeclarationView::OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
	if (m_Sci)
		::SetFocus(m_Sci);
	return 0;
}

void CDeclarationView::UpdateColors() {
	if (!m_Sci)
		return;
	auto p = CurrentPalette();
	auto font = CodeFont();
	CClientDC dc(m_hWnd);
	auto points = std::max(1, ::MulDiv(std::abs(font.lfHeight), 72, dc.GetDeviceCaps(LOGPIXELSY)));
	Sci(SCI_STYLESETFONT, STYLE_DEFAULT, (LPARAM)(PCSTR)CW2A(font.lfFaceName, CP_UTF8));
	Sci(SCI_STYLESETSIZE, STYLE_DEFAULT, points);
	Sci(SCI_STYLESETBOLD, STYLE_DEFAULT, font.lfWeight >= FW_BOLD);
	Sci(SCI_STYLESETITALIC, STYLE_DEFAULT, font.lfItalic != 0);
	Sci(SCI_STYLESETFORE, STYLE_DEFAULT, p.Fore);
	Sci(SCI_STYLESETBACK, STYLE_DEFAULT, p.Back);
	Sci(SCI_STYLECLEARALL);
	for (auto id : { SCE_C_COMMENT, SCE_C_COMMENTLINE, SCE_C_COMMENTDOC, SCE_C_COMMENTLINEDOC })
		Sci(SCI_STYLESETFORE, id, p.Comment);
	Sci(SCI_STYLESETFORE, SCE_C_WORD, p.Keyword);
	Sci(SCI_STYLESETFORE, SCE_C_WORD2, p.Type);
	Sci(SCI_STYLESETFORE, SCE_C_NUMBER, p.Number);
	Sci(SCI_STYLESETFORE, SCE_C_STRING, p.String);
	Sci(SCI_STYLESETFORE, SCE_C_CHARACTER, p.String);
	Sci(SCI_STYLESETFORE, SCE_C_PREPROCESSOR, p.Preprocessor);
	Sci(SCI_STYLESETFORE, STYLE_LINENUMBER, p.Fore);
	Sci(SCI_STYLESETBACK, STYLE_LINENUMBER, p.Back);
	// the tips: in the code's font, a little apart from the code's background
	Sci(SCI_CALLTIPUSESTYLE, 0);
	Sci(SCI_STYLESETFORE, STYLE_CALLTIP, p.Fore);
	Sci(SCI_STYLESETBACK, STYLE_CALLTIP, p.TipBack);

	auto opaque = [](COLORREF c) { return (LPARAM)(c | 0xFF000000); };
	Sci(SCI_SETELEMENTCOLOUR, SC_ELEMENT_CARET_LINE_BACK, opaque(p.CaretLine));
	Sci(SCI_SETELEMENTCOLOUR, SC_ELEMENT_SELECTION_BACK, opaque(p.Selection));
	Sci(SCI_SETELEMENTCOLOUR, SC_ELEMENT_SELECTION_INACTIVE_BACK, opaque(p.Selection));
	Sci(SCI_SETELEMENTCOLOUR, SC_ELEMENT_CARET, opaque(p.Caret));

	Sci(SCI_INDICSETSTYLE, IndicatorLink, INDIC_PLAIN);
	Sci(SCI_INDICSETFORE, IndicatorLink, p.Link);
	Sci(SCI_INDICSETUNDER, IndicatorLink, TRUE);

	// the scroll bars (dark mode's themes are of the controls it knows, which Scintilla is not one of)
	::SetWindowTheme(m_Sci, WTLHelper::IsDarkMode() ? L"DarkMode_Explorer" : nullptr, nullptr);
	::SetWindowPos(m_Sci, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

LOGFONT CDeclarationView::CodeFont() {
	auto font = AppSettings::Get().CodeFont();
	if (font.lfFaceName[0])
		return font;
	font = {};
	wcscpy_s(font.lfFaceName, L"Consolas");
	CClientDC dc(nullptr);
	font.lfHeight = -::MulDiv(10, dc.GetDeviceCaps(LOGPIXELSY), 72);
	font.lfWeight = FW_NORMAL;
	font.lfCharSet = DEFAULT_CHARSET;
	return font;
}

void CDeclarationView::SetText(std::string const& text) {
	Sci(SCI_SETREADONLY, FALSE);
	Sci(SCI_SETTEXT, 0, (LPARAM)text.c_str());
	Sci(SCI_SETREADONLY, TRUE);
	Sci(SCI_EMPTYUNDOBUFFER);
	Sci(SCI_SETSCROLLWIDTH, 1);
	Sci(SCI_GOTOPOS, 0);
	MarkLinks();
}

// the names of the types the lines refer to, underlined
void CDeclarationView::MarkLinks() {
	Sci(SCI_SETINDICATORCURRENT, IndicatorLink);
	Sci(SCI_INDICATORCLEARRANGE, 0, Sci(SCI_GETLENGTH));
	auto lines = (int)Sci(SCI_GETLINECOUNT);
	for (int line = 0; line < lines && line < (int)m_LineTypes.size(); line++) {
		auto const& type = m_LineTypes[line];
		if (type.empty())
			continue;
		auto length = (int)Sci(SCI_LINELENGTH, line);
		std::string text(length, '\0');
		Sci(SCI_GETLINE, line, (LPARAM)text.data());
		auto at = FindWord(text, type);
		if (at != std::string::npos)
			Sci(SCI_INDICATORFILLRANGE, Sci(SCI_POSITIONFROMLINE, line) + at, type.size());
	}
}

void CDeclarationView::Show(int entry) {
	auto const& entries = m_Api->Entries();
	if (entry >= (int)entries.size())
		entry = -1;
	m_Entry = entry;
	if (entry < 0) {
		m_LineTypes.clear();
		SetText("");
	}
	else {
		auto text = m_Api->Declaration(entries[entry], &m_LineTypes);
		SetText(text);
	}
	if (OnShownChanged)
		OnShownChanged();
}

int CDeclarationView::Entry() const {
	return m_Entry;
}

std::string CDeclarationView::Name() const {
	return m_Entry >= 0 ? m_Api->Entries()[m_Entry].Name : std::string();
}

void CDeclarationView::Remember(std::string name) {
	if (name.empty() || (!m_Back.empty() && m_Back.back() == name))
		return;
	m_Back.push_back(std::move(name));
	m_Forward.clear();
}

bool CDeclarationView::Navigate(std::string const& name) {
	auto entry = m_Api->Find(name);
	if (!entry)
		return false;
	auto index = (int)(entry - m_Api->Entries().data());
	if (index == m_Entry)
		return true;
	Remember(Name());
	Show(index);
	return true;
}

void CDeclarationView::ClearHistory() {
	m_Back.clear();
	m_Forward.clear();
}

bool CDeclarationView::CanGoBack() const {
	return !m_Back.empty();
}

bool CDeclarationView::CanGoForward() const {
	return !m_Forward.empty();
}

void CDeclarationView::GoBack() {
	while (!m_Back.empty()) {
		auto name = std::move(m_Back.back());
		m_Back.pop_back();
		if (auto entry = m_Api->Find(name)) {
			if (m_Entry >= 0)
				m_Forward.push_back(Name());
			Show((int)(entry - m_Api->Entries().data()));
			break;
		}
	}
}

void CDeclarationView::GoForward() {
	while (!m_Forward.empty()) {
		auto name = std::move(m_Forward.back());
		m_Forward.pop_back();
		if (auto entry = m_Api->Find(name)) {
			if (m_Entry >= 0)
				m_Back.push_back(Name());
			Show((int)(entry - m_Api->Entries().data()));
			break;
		}
	}
}

void CDeclarationView::SetApi(Win32Api& api) {
	auto name = Name();
	m_Api = &api;
	auto entry = name.empty() ? nullptr : api.Find(name);
	Show(entry ? (int)(entry - api.Entries().data()) : -1);
}

std::string CDeclarationView::ReferredAtCaret() const {
	auto line = (int)Sci(SCI_LINEFROMPOSITION, Sci(SCI_GETCURRENTPOS));
	if (line < 0 || line >= (int)m_LineTypes.size() || m_LineTypes[line] == Name())
		return {};
	return m_LineTypes[line];
}

bool CDeclarationView::FollowLine(int line, bool newTab) {
	if (line < 0 || line >= (int)m_LineTypes.size())
		return false;
	auto const& name = m_LineTypes[line];
	if (name.empty() || name == Name() || !m_Api->Find(name))
		return false;
	if (OnFollow)
		OnFollow(name, newTab);
	else if (!newTab)
		Navigate(name);
	return true;
}

bool CDeclarationView::FollowCaret(bool newTab) {
	return FollowLine((int)Sci(SCI_LINEFROMPOSITION, Sci(SCI_GETCURRENTPOS)), newTab);
}

bool CDeclarationView::HighlightName(std::string const& name) {
	if (name.empty())
		return false;
	Sci(SCI_SETTARGETRANGE, 0, Sci(SCI_GETLENGTH));
	Sci(SCI_SETSEARCHFLAGS, SCFIND_MATCHCASE | SCFIND_WHOLEWORD);
	auto at = Sci(SCI_SEARCHINTARGET, name.size(), (LPARAM)name.c_str());
	if (at < 0)
		return false;
	Sci(SCI_SETSEL, at, at + name.size());
	// (in the middle of what is shown)
	auto line = (int)Sci(SCI_LINEFROMPOSITION, at);
	auto shown = (int)Sci(SCI_LINESONSCREEN);
	Sci(SCI_SETFIRSTVISIBLELINE, std::max(0, line - shown / 2));
	return true;
}

std::string CDeclarationView::TipOf(std::string const& name) const {
	constexpr int MaxLines = 16;
	// a type, function or constant (not the one shown): its declaration, its first lines
	if (auto entry = m_Api->Find(name); entry && name != Name()) {
		auto text = m_Api->Declaration(*entry);
		std::string tip;
		int lines = 0;
		for (size_t start = 0; start < text.size(); lines++) {
			auto end = text.find("\r\n", start);
			if (end == std::string::npos)
				end = text.size();
			if (lines == MaxLines) {
				tip += "\n...";
				break;
			}
			if (!tip.empty())
				tip += '\n';
			tip += text.substr(start, end - start);
			start = end + 2;
		}
		return tip;
	}
	// a value of an enum: its value, and its other names
	if (auto constant = m_Api->ConstantOf(name)) {
		auto tip = std::format("{} = 0x{:X} ({})\n{}", name, constant->Value, (int64_t)constant->Value, constant->Type);
		std::string others;
		for (auto const& other : constant->Names)
			if (other != name)
				others += (others.empty() ? "" : ", ") + other;
		if (!others.empty())
			tip += "\nalso: " + others;
		return tip;
	}
	return {};
}

LRESULT CDeclarationView::OnDwellStart(int, LPNMHDR hdr, BOOL&) {
	auto scn = (SCNotification*)hdr;
	if (scn->position < 0)
		return 0;
	auto start = Sci(SCI_WORDSTARTPOSITION, scn->position, TRUE);
	auto end = Sci(SCI_WORDENDPOSITION, scn->position, TRUE);
	if (end <= start)
		return 0;
	std::string word(end - start + 1, '\0');
	Sci_TextRangeFull range{ { start, end }, word.data() };
	Sci(SCI_GETTEXTRANGEFULL, 0, (LPARAM)&range);
	word.resize(strlen(word.c_str()));
	auto tip = TipOf(word);
	// (a tip does not expand tabs)
	for (size_t at = 0; (at = tip.find('\t', at)) != std::string::npos; at += 4)
		tip.replace(at, 1, "    ");
	if (!tip.empty())
		Sci(SCI_CALLTIPSHOW, start, (LPARAM)tip.c_str());
	return 0;
}

LRESULT CDeclarationView::OnDwellEnd(int, LPNMHDR, BOOL&) {
	Sci(SCI_CALLTIPCANCEL);
	return 0;
}

void CDeclarationView::Copy() {
	Sci(SCI_COPYALLOWLINE);
}

void CDeclarationView::SelectAll() {
	Sci(SCI_SELECTALL);
}

BOOL CDeclarationView::PreTranslateMessage(MSG* pMsg) {
	if (pMsg->message == WM_KEYDOWN && pMsg->hwnd == m_Sci && pMsg->wParam == VK_RETURN) {
		FollowCaret(::GetKeyState(VK_CONTROL) < 0);
		return TRUE;
	}
	return FALSE;
}

LRESULT CDeclarationView::OnDoubleClick(int, LPNMHDR hdr, BOOL&) {
	auto scn = (SCNotification*)hdr;
	if (FollowLine((int)scn->line, (scn->modifiers & SCMOD_CTRL) != 0)) {
		// (not the word the double-click selected)
		Sci(SCI_SETEMPTYSELECTION, Sci(SCI_GETCURRENTPOS));
	}
	return 0;
}

LRESULT CDeclarationView::OnContextMenu(UINT, WPARAM, LPARAM lp, BOOL&) {
	CPoint pt(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
	if (pt.x == -1 && pt.y == -1) {
		// from the keyboard: at the caret
		auto pos = Sci(SCI_GETCURRENTPOS);
		pt.x = (int)Sci(SCI_POINTXFROMPOSITION, 0, pos);
		pt.y = (int)(Sci(SCI_POINTYFROMPOSITION, 0, pos) + Sci(SCI_TEXTHEIGHT, 0));
		::ClientToScreen(m_Sci, &pt);
	}
	else {
		// the line clicked (the selection stays if it was clicked in)
		CPoint client(pt);
		::ScreenToClient(m_Sci, &client);
		auto pos = Sci(SCI_POSITIONFROMPOINT, client.x, client.y);
		auto start = Sci(SCI_GETSELECTIONSTART), end = Sci(SCI_GETSELECTIONEND);
		if (start == end || pos < start || pos > end)
			Sci(SCI_GOTOPOS, pos);
	}
	auto referred = ReferredAtCaret();
	std::wstring name(referred.begin(), referred.end());
	CMenu menu;
	menu.CreatePopupMenu();
	UINT linkFlags = referred.empty() ? MF_GRAYED : 0;
	menu.AppendMenu(MF_STRING | linkFlags, ID_GO_DEFINITION,
		referred.empty() ? L"Go to &Definition\tF12" : std::format(L"Go to &{}\tF12", name).c_str());
	menu.AppendMenu(MF_STRING | linkFlags, ID_GO_DEFINITION_NEWTAB,
		referred.empty() ? L"Open Definition in New &Tab\tCtrl+F12" : std::format(L"Open {} in New &Tab\tCtrl+F12", name).c_str());
	menu.AppendMenu(MF_STRING, ID_GO_USEDBY, L"Find &Usages\tShift+F12");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, ID_EDIT_COPY, L"&Copy\tCtrl+C");
	menu.AppendMenu(MF_STRING, ID_EDIT_SELECT_ALL, L"Select &All\tCtrl+A");
	menu.AppendMenu(MF_STRING, ID_EDIT_COPYNAME, L"Copy &Name\tCtrl+Shift+C");
	auto command = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd);
	if (command)
		GetTopLevelParent().SendMessage(WM_COMMAND, command);
	return 0;
}
