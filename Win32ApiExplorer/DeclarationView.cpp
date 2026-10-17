#include "pch.h"
#include "DeclarationView.h"
#include "resource.h"
#include "AppSettings.h"
#include <WTLHelper.h>

namespace {
	constexpr int IndicatorLink = 8;
	constexpr int MarkerDifferent = 1;

	struct Palette {
		COLORREF Back, Fore, Comment, Keyword, Type, Number, String, Preprocessor, Link, CaretLine, Selection, Caret, TipBack, Different;
	};

	Palette CurrentPalette() {
		if (WTLHelper::IsDarkMode())
			return { RGB(30, 30, 30), RGB(220, 220, 220), RGB(87, 166, 74), RGB(86, 156, 214), RGB(78, 201, 176), RGB(181, 206, 168),
				RGB(214, 157, 133), RGB(155, 155, 155), RGB(100, 160, 230), RGB(40, 40, 44), RGB(38, 79, 120), RGB(240, 240, 240),
				RGB(50, 50, 54), RGB(80, 42, 42) };
		return { RGB(255, 255, 255), RGB(0, 0, 0), RGB(0, 128, 0), RGB(0, 0, 255), RGB(43, 145, 175), RGB(9, 136, 90),
			RGB(163, 21, 21), RGB(128, 128, 128), RGB(0, 102, 204), RGB(240, 240, 248), RGB(173, 214, 255), RGB(0, 0, 0),
			RGB(246, 246, 238), RGB(255, 224, 224) };
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

	// the lines of a comparison that differ: in a color of their own
	Sci(SCI_MARKERDEFINE, MarkerDifferent, SC_MARK_BACKGROUND);
	Sci(SCI_MARKERSETBACK, MarkerDifferent, p.Different);

	Sci(SCI_INDICSETSTYLE, IndicatorLink, INDIC_PLAIN);
	Sci(SCI_INDICSETFORE, IndicatorLink, p.Link);
	Sci(SCI_INDICSETUNDER, IndicatorLink, TRUE);
	Sci(SCI_INDICSETHOVERSTYLE, IndicatorLink, INDIC_STRAIGHTBOX);
	Sci(SCI_INDICSETHOVERFORE, IndicatorLink, p.Link);

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
	Sci(SCI_MARKERDELETEALL, MarkerDifferent);
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
	Render();
	if (OnShownChanged)
		OnShownChanged();
}

void CDeclarationView::SetMode(Mode mode) {
	if (mode == m_Mode)
		return;
	m_Mode = mode;
	if (m_Sci)
		Render();
}

void CDeclarationView::Render() {
	m_LineTypes.clear();
	m_DifferentLines.clear();
	if (m_Entry < 0) {
		SetText("");
		return;
	}
	auto const& entry = m_Api->Entries()[m_Entry];
	std::string text;
	// (a layout is of a struct or union: of the others, their declarations)
	if (m_Mode != Mode::Declaration && (entry.Kind == Win32Api::Entry::Kinds::Struct || entry.Kind == Win32Api::Entry::Kinds::Union ||
		entry.Kind == Win32Api::Entry::Kinds::Interface)) {
		CWaitCursor wait;
		text = m_Mode == Mode::Layout ? LayoutText(entry) : CompareText(entry);
	}
	if (text.empty()) {
		m_LineTypes.clear();
		m_DifferentLines.clear();
		text = m_Api->Declaration(entry, &m_LineTypes);
		// those of its name but for a suffix (CreateFileA, CreateFile2): each a line of its own, to follow
		if (auto related = m_Api->RelatedOf(m_Entry); !related.empty()) {
			text += "\r\n// related:\r\n";
			m_LineTypes.push_back({});
			m_LineTypes.push_back({});
			for (auto i : related) {
				auto const& r = m_Api->Entries()[i];
				text += std::format("//   {} ({})\r\n", r.Name, r.Where);
				m_LineTypes.push_back(r.Name);
			}
		}
	}
	SetText(text);
	Sci(SCI_MARKERDELETEALL, MarkerDifferent);
	for (auto line : m_DifferentLines)
		Sci(SCI_MARKERADD, line, MarkerDifferent);
}

namespace {
	PCSTR MachineName(Win32Api::Machine machine) {
		return machine == Win32Api::Machine::X86 ? "x86" : machine == Win32Api::Machine::X64 ? "x64" : "ARM64";
	}

	// "HANDLE hFile", "SECURITY_ATTRIBUTES *lpAttributes"
	std::string Declare(std::string const& type, std::string const& name) {
		return type.empty() ? name : type + (type.ends_with("*") ? "" : " ") + name;
	}
}

// "0x0008  0x0008      void *lpSecurityDescriptor;": where each field is, from the start, and its size; the fields of the
// structs and unions in it, further in; the padding
std::string CDeclarationView::LayoutText(Win32Api::Entry const& entry) {
	std::string text;
	auto line = [&](std::string const& s, std::string const& type = {}) {
		text += s + "\r\n";
		m_LineTypes.push_back(type);
	};
	// an interface: its vtable ("   3  0x018  HRESULT CreateInstance(...)"), by the interfaces that declare its methods
	if (entry.Kind == Win32Api::Entry::Kinds::Interface) {
		auto slots = m_Api->VtableOf(entry);
		if (slots.empty())
			return {};
		auto pointer = m_Api->PointerSize();
		line(std::format("// interface {}: {} methods, its vtable 0x{:X} bytes ({})", entry.Name, slots.size(), slots.size() * pointer,
			MachineName(m_Api->GetMachine())));
		line("// slot  offset");
		std::string declaredBy;
		for (size_t i = 0; i < slots.size(); i++) {
			auto const& slot = slots[i];
			if (slot.Interface != declaredBy) {
				declaredBy = slot.Interface;
				line("   // " + declaredBy, declaredBy);
			}
			line(std::format("   {:4}  0x{:03X}  {};", i, i * pointer, slot.Signature), slot.Referred);
		}
		return text;
	}
	auto layout = m_Api->LayoutOf(entry);
	if (!layout)
		return {};
	line(std::format("// {} {}: 0x{:X} bytes, aligned to {} ({})", entry.Kind == Win32Api::Entry::Kinds::Union ? "union" : "struct",
		entry.Name, layout->Size, layout->Alignment, MachineName(m_Api->GetMachine())));
	line("// offset  size");
	using Kinds = Win32Api::LayoutItem::Kinds;
	for (auto const& item : layout->Items) {
		std::string indent(item.Depth * 4, ' ');
		switch (item.Kind) {
			case Kinds::Field:
				line(std::format("   0x{:04X}  0x{:04X}  {}{};{}", item.Offset, item.Size, indent, Declare(item.Type, item.Name),
					item.Note.empty() ? "" : "\t// " + item.Note), item.Referred);
				break;
			case Kinds::Padding:
				line(std::format("   0x{:04X}  0x{:04X}  {}// padding", item.Offset, item.Size, indent));
				break;
			case Kinds::Bitfield:
				line(std::format("   0x{:04X}          {}// {}: {}", item.Offset, indent, item.Name, item.Note));
				break;
		}
	}
	return text;
}

// The layouts on x86, x64 and ARM64, side by side: each field (by its path) where it is and how large, on each machine
// (missing: "-"); the lines that differ are marked
std::string CDeclarationView::CompareText(Win32Api::Entry const& entry) {
	using Machine = Win32Api::Machine;
	Machine const machines[] = { Machine::X86, Machine::X64, Machine::Arm64 };
	constexpr int Count = _countof(machines);
	std::optional<Win32Api::StructLayout> layouts[Count];
	for (int m = 0; m < Count; m++)
		if (auto api = Win32Api::Get(machines[m]))
			if (auto e = api->Find(entry.Name))
				layouts[m] = api->LayoutOf(*e);
	if (std::ranges::none_of(layouts, [](auto const& l) { return l.has_value(); }))
		return {};

	// the fields of all, in the order they are in (one that is not on the first machines: after the one before it)
	struct Row {
		int Depth;
		std::string Path, Name, Type, Referred;
		std::optional<std::pair<uint32_t, uint32_t>> At[Count];
	};
	std::vector<Row> rows;
	for (int m = 0; m < Count; m++) {
		if (!layouts[m])
			continue;
		size_t after = 0;
		for (auto const& item : layouts[m]->Items) {
			if (item.Kind == Win32Api::LayoutItem::Kinds::Padding)
				continue;
			auto it = std::ranges::find(rows, item.Path, &Row::Path);
			if (it == rows.end()) {
				it = rows.insert(rows.begin() + std::min(after, rows.size()), Row{ item.Depth, item.Path,
					item.Kind == Win32Api::LayoutItem::Kinds::Bitfield ? item.Name + ": " + item.Note : Declare(item.Type, item.Name),
					item.Type, item.Referred });
			}
			it->At[m] = { item.Offset, item.Size };
			after = (size_t)(it - rows.begin()) + 1;
		}
	}

	auto cell = [](std::optional<std::pair<uint32_t, uint32_t>> const& at) {
		return at ? std::format("+0x{:03X} ({:X})", at->first, at->second) : std::string("-");
	};
	size_t width = 24;
	for (auto const& row : rows)
		width = std::max(width, row.Depth * 4 + row.Name.size() + 2);
	auto padded = [](std::string s, size_t size) {
		if (s.size() < size)
			s.append(size - s.size(), ' ');
		return s;
	};
	constexpr size_t CellWidth = 16;

	std::string text;
	int lineNumber = 0;
	auto line = [&](std::string const& s, std::string const& type = {}, bool different = false) {
		text += s + "\r\n";
		m_LineTypes.push_back(type);
		if (different)
			m_DifferentLines.push_back(lineNumber);
		lineNumber++;
	};
	line(std::format("// {} {} on each machine (what differs is marked)", entry.Kind == Win32Api::Entry::Kinds::Union ? "union" : "struct", entry.Name));
	std::string header = "   " + padded("", width);
	for (auto machine : machines)
		header += padded(MachineName(machine), CellWidth);
	line("//" + header.substr(2));
	// its size and alignment
	auto summary = [&](char const* what, auto value) {
		std::string s = "   " + padded(what, width);
		std::vector<std::string> cells;
		for (auto const& layout : layouts)
			cells.push_back(layout ? std::format("0x{:X}", value(*layout)) : std::string("-"));
		for (auto const& c : cells)
			s += padded(c, CellWidth);
		line(s, {}, std::ranges::adjacent_find(cells, std::not_equal_to{}) != cells.end());
	};
	summary("size", [](Win32Api::StructLayout const& l) { return l.Size; });
	summary("alignment", [](Win32Api::StructLayout const& l) { return l.Alignment; });
	for (auto const& row : rows) {
		std::string s = "   " + padded(std::string(row.Depth * 4, ' ') + row.Name, width);
		std::vector<std::string> cells;
		for (int m = 0; m < Count; m++)
			if (layouts[m])
				cells.push_back(cell(row.At[m]));
		for (int m = 0; m < Count; m++)
			s += padded(layouts[m] ? cell(row.At[m]) : std::string("-"), CellWidth);
		line(s, row.Referred, std::ranges::adjacent_find(cells, std::not_equal_to{}) != cells.end());
	}
	return text;
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

LRESULT CDeclarationView::OnIndicatorRelease(int, LPNMHDR hdr, BOOL&) {
	auto scn = (SCNotification*)hdr;
	if (!(scn->modifiers & SCMOD_CTRL) || !(Sci(SCI_INDICATORALLONFOR, scn->position) & (1 << IndicatorLink)))
		return 0;
	// (not while Scintilla handles the click: the text changes)
	PostMessage(WM_FOLLOWLINE, Sci(SCI_LINEFROMPOSITION, scn->position), (scn->modifiers & SCMOD_SHIFT) ? 1 : 0);
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
	menu.AppendMenu(MF_STRING, ID_GO_SIDE,
		referred.empty() ? L"Open to the &Side\tCtrl+\\" : std::format(L"Open {} to the &Side\tCtrl+\\", name).c_str());
	menu.AppendMenu(MF_STRING | linkFlags, ID_GO_DEFINITION_NEWTAB,
		referred.empty() ? L"Open Definition in New &Tab\tCtrl+F12" : std::format(L"Open {} in New &Tab\tCtrl+F12", name).c_str());
	menu.AppendMenu(MF_STRING, ID_GO_USEDBY, L"Find &Usages\tShift+F12");
	menu.AppendMenu(MF_STRING, ID_EDIT_DECODE, L"&Decode Value...\tCtrl+Shift+V");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, ID_EDIT_COPY, L"&Copy\tCtrl+C");
	menu.AppendMenu(MF_STRING, ID_EDIT_SELECT_ALL, L"Select &All\tCtrl+A");
	menu.AppendMenu(MF_STRING, ID_EDIT_COPYNAME, L"Copy &Name\tCtrl+Shift+C");
	CMenu copyAs;
	copyAs.CreatePopupMenu();
	copyAs.AppendMenu(MF_STRING, ID_EDIT_COPYCSHARP, L"&C#");
	copyAs.AppendMenu(MF_STRING, ID_EDIT_COPYRUST, L"&Rust");
	copyAs.AppendMenu(MF_STRING, ID_EDIT_COPYPYTHON, L"&Python (ctypes)");
	menu.AppendMenu(MF_POPUP, copyAs.Detach(), L"Copy &As");
	auto command = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, m_hWnd);
	if (command)
		GetTopLevelParent().SendMessage(WM_COMMAND, command);
	return 0;
}
