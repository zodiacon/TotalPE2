#pragma once

namespace Scintilla {
	enum class IndicatorStyle {
		Plain = 0,
		Squiggle = 1,
		TT = 2,
		Diagonal = 3,
		Strike = 4,
		Hidden = 5,
		Box = 6,
		RoundBox = 7,
		StraightBox = 8,
		Dash = 9,
		Dots = 10,
		SquiggleLow = 11,
		DotBox = 12,
		SquigglePixmap = 13,
		CompositionThick = 14,
		CompositionThin = 15,
		FullBox = 16,
		TextFore = 17,
		Point = 18,
		PointCharacter = 19,
		Gradient = 20,
		GradientCentre = 21,
		ExplorerLink = 22,
	};

	enum class MarkerSymbol {
		Circle = 0,
		RoundRect = 1,
		Arrow = 2,
		SmallRect = 3,
		ShortArrow = 4,
		Empty = 5,
		ArrowDown = 6,
		Minus = 7,
		Plus = 8,
		VLine = 9,
		LCorner = 10,
		TCorner = 11,
		BoxPlus = 12,
		BoxPlusConnected = 13,
		BoxMinus = 14,
		BoxMinusConnected = 15,
		LCornerCurve = 16,
		TCornerCurve = 17,
		CirclePlus = 18,
		CirclePlusConnected = 19,
		CircleMinus = 20,
		CircleMinusConnected = 21,
		Background = 22,
		DotDotDot = 23,
		Arrows = 24,
		Pixmap = 25,
		FullRect = 26,
		LeftRect = 27,
		Available = 28,
		Underline = 29,
		RgbaImage = 30,
		Bookmark = 31,
		VerticalBookmark = 32,
		Character = 10000,
	};

	enum class MarkerOutline {
		FolderEnd = 25,
		FolderOpenMid = 26,
		FolderMidTail = 27,
		FolderTail = 28,
		FolderSub = 29,
		Folder = 30,
		FolderOpen = 31,
	};

	enum class MarginType {
		Symbol = 0,
		Number = 1,
		Back = 2,
		Fore = 3,
		Text = 4,
		RText = 5,
		Colour = 6,
	};

	enum class StylesCommon {
		Default = 32,
		LineNumber = 33,
		BraceLight = 34,
		BraceBad = 35,
		ControlChar = 36,
		IndentGuide = 37,
		CallTip = 38,
		FoldDisplayText = 39,
		LastPredefined = 39,
		Max = 255,
	};

	enum class Element {
		List = 0,
		ListBack = 1,
		ListSelected = 2,
		ListSelectedBack = 3,
		SelectionText = 10,
		SelectionBack = 11,
		SelectionAdditionalText = 12,
		SelectionAdditionalBack = 13,
		SelectionSecondaryText = 14,
		SelectionSecondaryBack = 15,
		SelectionInactiveText = 16,
		SelectionInactiveBack = 17,
		Caret = 40,
		CaretAdditional = 41,
		CaretLineBack = 50,
		WhiteSpace = 60,
		WhiteSpaceBack = 61,
		HotSpotActive = 70,
		HotSpotActiveBack = 71,
		FoldLine = 80,
		HiddenLine = 81,
	};

	enum class IndicValue {
		Bit = 0x1000000,
		Mask = 0xFFFFFF,
	};

	enum class IndicFlag {
		None = 0,
		ValueFore = 1,
	};

	enum class AutoCompleteOption {
		Normal = 0,
		FixedSize = 1,
	};

	enum class IndentView {
		None = 0,
		Real = 1,
		LookForward = 2,
		LookBoth = 3,
	};

	enum class PrintOption {
		Normal = 0,
		InvertLight = 1,
		BlackOnWhite = 2,
		ColourOnWhite = 3,
		ColourOnWhiteDefaultBG = 4,
		ScreenColours = 5,
	};

	enum class Alpha {
		Transparent = 0,
		Opaque = 255,
		NoAlpha = 256,
	};

	enum class WhiteSpace {
		Invisible = 0,
		VisibleAlways = 1,
		VisibleAfterIndent = 2,
		VisibleOnlyInIndent = 3,
	};

	enum class TabDrawMode {
		LongArrow = 0,
		StrikeOut = 1,
	};

	enum class EndOfLine {
		CrLf = 0,
		Cr = 1,
		Lf = 2,
	};

	enum class IMEInteraction {
		Windowed = 0,
		Inline = 1,
	};

	enum class Layer {
		Base = 0,
		UnderText = 1,
		OverText = 2,
	};

	enum class IdleStyling {
		None = 0,
		ToVisible = 1,
		AfterVisible = 2,
		All = 3,
	};

	enum class Wrap {
		None = 0,
		Word = 1,
		Char = 2,
		WhiteSpace = 3,
	};

	enum class WrapVisualFlag {
		None = 0x0000,
		End = 0x0001,
		Start = 0x0002,
		Margin = 0x0004,
	};

	enum class WrapVisualLocation {
		Default = 0x0000,
		EndByText = 0x0001,
		StartByText = 0x0002,
	};

	enum class WrapIndentMode {
		Fixed = 0,
		Same = 1,
		Indent = 2,
		DeepIndent = 3,
	};

	enum class LineCache {
		None = 0,
		Caret = 1,
		Page = 2,
		Document = 3,
	};

	enum class PhasesDraw {
		One = 0,
		Two = 1,
		Multiple = 2,
	};

	enum class FontQuality {
		Mask = 0xF,
		Default = 0,
		NonAntialiased = 1,
		Antialiased = 2,
		LcdOptimized = 3,
	};

	enum class MultiPaste {
		Once = 0,
		Each = 1,
	};

	enum class Accessibility {
		Disabled = 0,
		Enabled = 1,
	};

	enum class FindOption {
		None = 0x0,
		WholeWord = 0x2,
		MatchCase = 0x4,
		WordStart = 0x00100000,
		RegExp = 0x00200000,
		Posix = 0x00400000,
		Cxx11RegEx = 0x00800000,
	};
	DEFINE_ENUM_FLAG_OPERATORS(FindOption);

	enum class FoldLevel {
		None = 0x0,
		Base = 0x400,
		WhiteFlag = 0x1000,
		HeaderFlag = 0x2000,
		NumberMask = 0x0FFF,
	};

	enum class FoldDisplayTextStyle {
		Hidden = 0,
		Standard = 1,
		Boxed = 2,
	};

	enum class FoldAction {
		Contract = 0,
		Expand = 1,
		Toggle = 2,
	};

	enum class AutomaticFold {
		None = 0x0000,
		Show = 0x0001,
		Click = 0x0002,
		Change = 0x0004,
	};

	enum class FoldFlag {
		None = 0x0000,
		LineBeforeExpanded = 0x0002,
		LineBeforeContracted = 0x0004,
		LineAfterExpanded = 0x0008,
		LineAfterContracted = 0x0010,
		LevelNumbers = 0x0040,
		LineState = 0x0080,
	};

	enum class CharacterSet {
		Ansi = 0,
		Default = 1,
		Baltic = 186,
		ChineseBig5 = 136,
		EastEurope = 238,
		GB2312 = 134,
		Greek = 161,
		Hangul = 129,
		Mac = 77,
		Oem = 255,
		Russian = 204,
		Oem866 = 866,
		Cyrillic = 1251,
		ShiftJis = 128,
		Symbol = 2,
		Turkish = 162,
		Johab = 130,
		Hebrew = 177,
		Arabic = 178,
		Vietnamese = 163,
		Thai = 222,
		Iso8859_15 = 1000,
	};

	enum class CaseVisible {
		Mixed = 0,
		Upper = 1,
		Lower = 2,
		Camel = 3,
	};

	enum class FontWeight {
		Normal = 400,
		SemiBold = 600,
		Bold = 700,
	};
}

template<typename T>
class CScintillaCtrlT : public T {
	using Position = Sci_Position;
	using Line = intptr_t;
	using Colour = int;
	using ColourAlpha = int;
public:
	HWND Create(HWND hWndParent, _U_RECT rect = nullptr, LPCTSTR szWindowName = nullptr,
		DWORD dwStyle = WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, DWORD dwExStyle = 0, _U_MENUorID MenuOrID = 0U, PVOID lpCreateParam = nullptr) {
		auto hWnd = CWindow::Create(GetWndClassName(), hWndParent, rect.m_lpRect, szWindowName, dwStyle, dwExStyle, MenuOrID.m_hMenu, lpCreateParam);
		if (!hWnd)
			return nullptr;

		m_Sci = (decltype(m_Sci))::SendMessage(hWnd, SCI_GETDIRECTFUNCTION, 0, 0);
		ATLASSERT(m_Sci);
		m_SciWndData = (sptr_t)::SendMessage(hWnd, SCI_GETDIRECTPOINTER, 0, 0);
		ATLASSERT(m_SciWndData);
		return hWnd;
	}

	static LPCTSTR GetWndClassName() {
		return L"Scintilla";
	}

	LRESULT Execute(UINT Msg, WPARAM wParam = 0, LPARAM lParam = 0) const {
		return m_Sci(m_SciWndData, Msg, wParam, lParam);
	};

	LRESULT ExecuteMsg(UINT Msg, WPARAM wParam = 0, LPARAM lParam = 0) const {
		ATLASSERT(::IsWindow(this->m_hWnd));
		return ::SendMessage(this->m_hWnd, Msg, wParam, lParam);
	}

	Position LineStart(Line line) const;
	Position LineEnd(Line line) const;

	bool IsSelectionEmpty() const {
		return (bool)Execute(SCI_GETSELECTIONEMPTY);
	}

	int SelectionsCount() const {
		return (int)Execute(SCI_GETSELECTIONS);
	}

	void ColouriseAll();
	char CharacterAt(Position position) const;
	int UnsignedStyleAt(Position position);
	Position ReplaceTarget(std::string_view text);
	Position ReplaceTargetRE(std::string_view text);
	Position SearchInTarget(std::string_view text);

	void AddText(Position len, const char* text) {
		Execute(SCI_ADDTEXT, len, reinterpret_cast<LPARAM>(text));
	}
	void AppendText(Position len, const char* text) {
		Execute(SCI_APPENDTEXT, len, reinterpret_cast<LPARAM>(text));
	}

	void MarkerDefine(int index, Scintilla::MarkerSymbol symbol) {
		Execute(SCI_MARKERDEFINE, index, static_cast<LPARAM>(symbol));
	}

	void AddStyledText(Position length, const char* c);
	void InsertText(Position pos, const char* text);
	void ChangeInsertion(Position length, const char* text);
	void DeleteRange(Position start, Position lengthDelete);
	void ClearDocumentStyle();

	Position Length() const {
		return (Position)Execute(SCI_GETLENGTH);
	}

	int CharAt(Position pos) const {
		return (int)Execute(SCI_GETCHARAT, pos);
	}
	Position CurrentPos() const {
		return (Position)Execute(SCI_GETCURRENTPOS);
	}
	Position Anchor() const {
		return (Position)Execute(SCI_GETANCHOR);
	}

	int StyleAt(Position pos) const {
		return (int)Execute(SCI_GETSTYLEAT, pos);
	}

	void Redo() {
		Execute(SCI_REDO);
	}
	void SetUndoCollection(bool collectUndo) {
		Execute(SCI_SETUNDOCOLLECTION, collectUndo);
	}
	void SelectAll() {
		Execute(SCI_SELECTALL);
	}
	void SetSavePoint() {
		Execute(SCI_SETSAVEPOINT);
	}

	Position GetStyledText(void* tr) const;

	Line MarkerLineFromHandle(int markerHandle);
	void MarkerDeleteHandle(int markerHandle);
	int MarkerHandleFromLine(Line line, int which);
	int MarkerNumberFromLine(Line line, int which);
	bool UndoCollection();
	Position PositionFromPoint(int x, int y);
	Position PositionFromPointClose(int x, int y);
	void GotoLine(Line line) {
		Execute(SCI_GOTOLINE, line);
	}

	void GotoPos(Position caret) {
		Execute(SCI_GOTOPOS, caret);
	}
	void UsePopup(int mode) {
		Execute(SCI_USEPOPUP, mode);
	}
	void SetAnchor(Position anchor);
	Position GetCurLine(Position length, char* text);

	std::string GetCurLine() const {
		std::string text;
		auto len = GetLineLength(LineFromPosition(GetCurrentPos()));
		if (len == 0)
			return "";
		text.resize(len);
		Execute(SCI_GETCURLINE, len, reinterpret_cast<LPARAM>(text.data()));
		return text;
	}

	Position GetLineLength(Line line) const {
		return Execute(SCI_LINELENGTH, line);
	}

	Position GetCurrentPos() const {
		return Execute(SCI_GETCURRENTPOS);
	}

	Line LineFromPosition(Position pos) const {
		return Execute(SCI_LINEFROMPOSITION, pos);
	}

	Position GetEndStyled() const {
		return Execute(SCI_GETENDSTYLED);
	}

	void StartStyling(Position start) {
		Execute(SCI_STARTSTYLING, start);
	}

	void SetStyling(Position length, int style) {
		Execute(SCI_SETSTYLING, length, style);
	}

	bool BufferedDraw();
	void SetBufferedDraw(bool buffered);
	void SetTabWidth(int tabWidth) {
		Execute(SCI_SETTABWIDTH, tabWidth);
	}
	int GetTabWidth() const {
		return Execute(SCI_GETTABWIDTH);
	}

	void SetTabMinimumWidth(int pixels);
	int GetTabMinimumWidth() const;
	void ClearTabStops(Line line);
	void AddTabStop(Line line, int x);
	int GetNextTabStop(Line line, int x) const;
	void SetCodePage(int codePage) {
		Execute(SCI_SETCODEPAGE, codePage);
	}

	void Focus(bool focus = true) {
		Execute(SCI_SETFOCUS, focus);
	}

	void SetFontLocale(const char* localeName);
	int GetFontLocale(char* localeName) const;
	std::string GetFontLocale() const;

	void MarkerSetFore(int markerNumber, Colour fore) {
		Execute(SCI_MARKERSETFORE, markerNumber, fore);
	}

	void MarkerSetBack(int markerNumber, Colour back) {
		Execute(SCI_MARKERSETBACK, markerNumber, back);
	}

	void MarkerSetBackSelected(int markerNumber, Colour back);
	void MarkerSetForeTranslucent(int markerNumber, ColourAlpha fore);
	void MarkerSetBackTranslucent(int markerNumber, ColourAlpha back);
	void MarkerSetBackSelectedTranslucent(int markerNumber, ColourAlpha back);
	void MarkerSetStrokeWidth(int markerNumber, int hundredths);
	void MarkerEnableHighlight(bool enabled);

	int MarkerAdd(Line line, int markerNumber) {
		return Execute(SCI_MARKERADD, line, markerNumber);
	}

	void MarkerDelete(Line line, int markerNumber) {
		Execute(SCI_MARKERDELETE, line, markerNumber);
	}
	void MarkerDeleteAll(int markerNumber) {
		Execute(SCI_MARKERDELETEALL);
	}

	int MarkerGet(Line line) const {
		return Execute(SCI_MARKERGET, line);
	}

	Line MarkerNext(Line lineStart, int markerMask) const;
	Line MarkerPrevious(Line lineStart, int markerMask) const;
	void MarkerDefinePixmap(int markerNumber, const char* pixmap);
	void MarkerAddSet(Line line, int markerSet);

	void SetMarginWidth(int margin, int pixelWidth) {
		Execute(SCI_SETMARGINWIDTHN, margin, pixelWidth);
	}

	int GetMarginWidthN(int margin) const {
		return Execute(SCI_GETMARGINWIDTHN, margin);
	}

	void SetMarginMaskN(int margin, int mask);
	int GetMarginMaskN(int margin) const;
	void SetMarginSensitive(int margin, bool sensitive) {
		Execute(SCI_SETMARGINSENSITIVEN, margin, sensitive);
	}

	bool GetMarginSensitiveN(int margin) const;
	void SetMarginBack(int margin, Colour back) {
		Execute(SCI_SETMARGINBACKN, margin, back);
	}

	Colour GetMarginBackN(int margin) const;
	void SetMargins(int margins);
	int GetMargins() const;
	void StyleClearAll() {
		Execute(SCI_STYLECLEARALL);
	}

	void SetMarginType(int index, Scintilla::MarginType type) {
		Execute(SCI_SETMARGINTYPEN, index, (LPARAM)type);
	}

	void StyleSetFore(int style, Colour fore) {
		Execute(SCI_STYLESETFORE, style, fore);
	}

	void StyleSetBack(int style, Colour back) {
		Execute(SCI_STYLESETBACK, style, back);
	}

	void StyleSetBold(int style, bool bold) {
		Execute(SCI_STYLESETBOLD, style, bold);
	}

	void StyleSetItalic(int style, bool italic) {
		Execute(SCI_STYLESETITALIC, style, italic);
	}
	void StyleSetSize(int style, int sizePoints) {
		Execute(SCI_STYLESETSIZE, style, sizePoints);
	}

	void StyleSetFont(int style, const char* fontName) {
		Execute(SCI_STYLESETFONT, style, reinterpret_cast<LPARAM>(fontName));
	}

	void SetKeywords(int index, char const* keywords) {
		Execute(SCI_SETKEYWORDS, index, reinterpret_cast<LPARAM>(keywords));
	}

	void SetLexer(Scintilla::ILexer5* lexer) {
		Execute(SCI_SETILEXER, 0, reinterpret_cast<LPARAM>(lexer));
	}

	void StartLexer() {
		Execute(SCI_LEXER_START);
	}

	void StyleSetEOLFilled(int style, bool eolFilled);
	void StyleResetDefault() {
		Execute(SCI_STYLERESETDEFAULT);
	}
	void StyleSetUnderline(int style, bool underline);
	Colour StyleGetFore(int style) {
		return (Colour)Execute(SCI_STYLEGETFORE, style);
	}

	Colour StyleGetBack(int style) const;
	bool StyleGetBold(int style) const;
	bool StyleGetItalic(int style) const;
	int StyleGetSize(int style) const;
	int StyleGetFont(int style, char* fontName) const;
	std::string StyleGetFont(int style) const;
	bool StyleGetEOLFilled(int style) const;
	bool StyleGetUnderline(int style) const;
	bool StyleGetVisible(int style) const;
	bool StyleGetChangeable(int style) const;
	bool StyleGetHotSpot(int style) const;
	void StyleSetSizeFractional(int style, int sizeHundredthPoints);
	int StyleGetSizeFractional(int style);
	void StyleSetWeight(int style, int weight);
	int StyleGetWeight(int style);
	void StyleSetCharacterSet(int style, Scintilla::CharacterSet characterSet);
	void StyleSetHotSpot(int style, bool hotspot);
	void StyleSetCheckMonospaced(int style, bool checkMonospaced);
	bool StyleGetCheckMonospaced(int style);
	void SetElementColour(Scintilla::Element element, ColourAlpha colourElement);
	ColourAlpha ElementColour(Scintilla::Element element);
	void ResetElementColour(Scintilla::Element element);
	bool IsElementIsSet(Scintilla::Element element) const;
	bool IsElementAllowsTranslucent(Scintilla::Element element) const;
	ColourAlpha GetElementBaseColour(Scintilla::Element element) const;
	void SetSelFore(bool useSetting, Colour fore);
	void SetSelBack(bool useSetting, Colour back);
	Scintilla::Alpha SelAlpha();
	void SetSelAlpha(Scintilla::Alpha alpha);
	bool SelEOLFilled();
	void SetSelEOLFilled(bool filled);
	Scintilla::Layer SelectionLayer();
	void SetSelectionLayer(Scintilla::Layer layer);
	Scintilla::Layer CaretLineLayer();
	void SetCaretLineLayer(Scintilla::Layer layer);
	bool GetCaretLineHighlightSubLine() const;
	void SetCaretLineHighlightSubLine(bool subLine);
	void SetCaretFore(Colour fore);
	void AssignCmdKey(int keyDefinition, int sciCommand);
	void ClearCmdKey(int keyDefinition);
	void ClearAllCmdKeys();
	void SetStylingEx(Position length, const char* styles);
	void StyleSetVisible(int style, bool visible);
	int GetCaretPeriod() const;
	void SetCaretPeriod(int periodMilliseconds);
	void SetWordChars(const char* characters);
	int GetWordChars(char* characters) const;
	std::string GetWordChars() const;
	void SetCharacterCategoryOptimization(int countCharacters);
	int GetCharacterCategoryOptimization() const;
	void BeginUndoAction();
	void EndUndoAction();
	void IndicSetStyle(int indicator, Scintilla::IndicatorStyle indicatorStyle);
	Scintilla::IndicatorStyle IndicGetStyle(int indicator);
	void IndicSetFore(int indicator, Colour fore);
	Colour IndicGetFore(int indicator) const;
	void IndicSetUnder(int indicator, bool under);
	bool IndicGetUnder(int indicator) const;
	void IndicSetHoverStyle(int indicator, Scintilla::IndicatorStyle indicatorStyle);
	Scintilla::IndicatorStyle IndicGetHoverStyle(int indicator);
	void IndicSetHoverFore(int indicator, Colour fore);
	Colour IndicGetHoverFore(int indicator) const;
	void IndicSetFlags(int indicator, Scintilla::IndicFlag flags);
	Scintilla::IndicFlag IndicGetFlags(int indicator) const;
	void IndicSetStrokeWidth(int indicator, int hundredths);
	int IndicGetStrokeWidth(int indicator) const;
	void SetWhitespaceFore(bool useSetting, Colour fore);
	void SetWhitespaceBack(bool useSetting, Colour back) {
		Execute(SCI_SETWHITESPACEBACK, useSetting, back);
	}
	void SetWhitespaceSize(int size);
	int GetWhitespaceSize() const;
	void SetLineState(Line line, int state);
	int GetLineState(Line line) const;
	int GetMaxLineState() const;
	bool IsCaretLineVisible() const;
	void SetCaretLineVisible(bool show);
	Colour GetCaretLineBack() const;
	void SetCaretLineBack(Colour back);
	int GetCaretLineFrame() const;
	void SetCaretLineFrame(int width);
	void StyleSetChangeable(int style, bool changeable);
	void AutoCShow(Position lengthEntered, const char* itemList);
	void AutoCCancel();
	bool IsAutoCActive() const;
	Position AutoCPosStart();
	void AutoCComplete();
	void AutoCStops(const char* characterSet);
	void AutoCSetSeparator(int separatorCharacter);
	int AutoCGetSeparator();
	void AutoCSelect(const char* select);
	void AutoCSetCancelAtStart(bool cancel);
	bool AutoCGetCancelAtStart();
	void AutoCSetFillUps(const char* characterSet);
	void AutoCSetChooseSingle(bool chooseSingle);
	bool AutoCGetChooseSingle();
	void AutoCSetIgnoreCase(bool ignoreCase);
	bool AutoCGetIgnoreCase();
	void UserListShow(int listType, const char* itemList);
	void AutoCSetAutoHide(bool autoHide);
	bool AutoCGetAutoHide();
	void AutoCSetOptions(Scintilla::AutoCompleteOption options);
	Scintilla::AutoCompleteOption AutoCGetOptions();
	void AutoCSetDropRestOfWord(bool dropRestOfWord);
	bool AutoCGetDropRestOfWord();
	void RegisterImage(int type, const char* xpmData);
	void ClearRegisteredImages();
	int AutoCGetTypeSeparator();
	void AutoCSetTypeSeparator(int separatorCharacter);
	void AutoCSetMaxWidth(int characterCount);
	int AutoCGetMaxWidth();
	void AutoCSetMaxHeight(int rowCount);
	int AutoCGetMaxHeight();
	void SetIndent(int indentSize);
	int Indent();
	void SetUseTabs(bool useTabs);
	bool UseTabs();
	void SetLineIndentation(Line line, int indentation);
	int LineIndentation(Line line);

	Position LineIndentPosition(Line line);
	Position Column(Position pos) const {
		return (Position)Execute(SCI_GETCOLUMN, pos);
	}
	Position CountCharacters(Position start, Position end);
	Position CountCodeUnits(Position start, Position end);
	void SetHScrollBar(bool visible);
	bool HScrollBar();
	void SetIndentationGuides(Scintilla::IndentView indentView);
	Scintilla::IndentView IndentationGuides();
	void SetHighlightGuide(Position column);
	Position HighlightGuide();
	Position LineEndPosition(Line line);
	int CodePage();
	Colour CaretFore();
	bool IsReadOnly() const {
		return Execute(SCI_GETREADONLY);
	}
	void SetCurrentPos(Position caret) {
		Execute(SCI_SETCURRENTPOS, caret);
	}
	void SetSelectionStart(Position anchor) {
		Execute(SCI_SETSELECTIONSTART, anchor);
	}
	Position SelectionStart() const {
		return (Position)Execute(SCI_GETSELECTIONNANCHOR);
	}

	void SetSelectionEnd(Position caret) {
		Execute(SCI_SETSELECTIONEND, caret);
	}
	Position SelectionEnd();
	void SetEmptySelection(Position caret);
	void SetPrintMagnification(int magnification);
	int PrintMagnification();
	void SetPrintColourMode(Scintilla::PrintOption mode);
	Scintilla::PrintOption PrintColourMode();

	Position FindText(Scintilla::FindOption searchFlags, char const* text) {
		Sci_TextToFind ttf{};
		ttf.chrg.cpMax = (long)Length();
		ttf.lpstrText = text;
		return (Position)Execute(SCI_FINDTEXT, static_cast<WPARAM>(searchFlags), reinterpret_cast<LPARAM>(&ttf));
	}

	Position FormatRange(bool draw, void* fr);
	Line FirstVisibleLine() const;
	Position GetLine(Line line, char* text) const;

	std::string GetLine(Line line) const {
		std::string text;
		auto len = GetLineLength(line);
		if (len == 0)
			return "";
		text.resize(len);
		Execute(SCI_GETLINE, line, reinterpret_cast<LPARAM>(text.data()));
		return text;
	}

	Line LineCount();
	void AllocateLines(Line lines);
	void SetMarginLeft(int pixelWidth);
	int MarginLeft();
	void SetMarginRight(int pixelWidth);
	int MarginRight();
	bool IsModified() {
		return Execute(SCI_GETMODIFY);
	}
	void SetSel(Position anchor, Position caret);
	Position GetSelText(char* text);
	std::string GetSelText() {
		auto len = Execute(SCI_GETTEXTLENGTH);
		std::string text;
		text.resize(len);
		Execute(SCI_GETSELTEXT, len, reinterpret_cast<LPARAM>(text.data()));
		return text;
	}
	Position GetTextRange(void* tr);
	void HideSelection(bool hide);
	int PointXFromPosition(Position pos);
	int PointYFromPosition(Position pos);

	Position PositionFromLine(Line line) const {
		return (Position)Execute(SCI_POSITIONFROMLINE, line);
	}

	void LineScroll(Position columns, Line lines);
	void ScrollCaret();
	void ScrollRange(Position secondary, Position primary);
	void ReplaceSel(const char* text);
	void SetReadOnly(bool readOnly) {
		Execute(SCI_SETREADONLY, readOnly);
	}
	void Null();

	bool CanPaste() const {
		return Execute(SCI_CANPASTE);
	}

	bool CanUndo() const {
		return Execute(SCI_CANUNDO);
	}

	bool CanRedo() const {
		return Execute(SCI_CANREDO);
	}

	bool CanCopy() const {
		return !Execute(SCI_GETSELECTIONEMPTY);
	}

	bool CanCut() const {
		return !Execute(SCI_GETSELECTIONEMPTY) && !IsReadOnly();
	}

	void EmptyUndoBuffer() {
		Execute(SCI_EMPTYUNDOBUFFER);
	}

	void Undo() {
		Execute(SCI_UNDO);
	}

	void Cut() {
		Execute(SCI_CUT);
	}

	void Copy() {
		Execute(SCI_COPY);
	}

	void Paste() {
		Execute(SCI_PASTE);
	}

	void Clear() {
		Execute(SCI_CLEAR);
	}

	void ClearAll() {
		Execute(SCI_CLEARALL);
	}

	void SetText(const char* text) {
		Execute(SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(text));
	}

	Position GetText(Position length, char* text) const {
		return (Position)Execute(SCI_GETTEXT, length, reinterpret_cast<LPARAM>(text));
	}

	std::string GetText(Position length) const {
		std::string text;
		text.resize((size_t)length);
		GetText(length + 1, text.data());
		return text;
	}

	std::string GetAllText() const {
		std::string text;
		text.resize(GetTextLength());
		return GetText(text.length());
	}

	Position GetTextLength() const {
		return Execute(SCI_GETTEXTLENGTH);
	}

	SciFnDirect m_Sci;
	sptr_t m_SciWndData;
};

using CScintillaCtrl = CScintillaCtrlT<CWindow>;

template<typename T>
class CScintillaCommands {
	BEGIN_MSG_MAP(CScintillaCommands)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_EDIT_PASTE, OnPaste)
		COMMAND_ID_HANDLER(ID_EDIT_CUT, OnCut)
		COMMAND_ID_HANDLER(ID_EDIT_UNDO, OnUndo)
		COMMAND_ID_HANDLER(ID_EDIT_REDO, OnRedo)
		COMMAND_ID_HANDLER(ID_EDIT_CLEAR_ALL, OnClearAll)
		ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_EDIT_PASTE, OnPaste)
		COMMAND_ID_HANDLER(ID_EDIT_CUT, OnCut)
		COMMAND_ID_HANDLER(ID_EDIT_UNDO, OnUndo)
		COMMAND_ID_HANDLER(ID_EDIT_REDO, OnRedo)
		COMMAND_ID_HANDLER(ID_EDIT_CLEAR_ALL, OnClearAll)
	END_MSG_MAP()

	LRESULT OnPaste(WORD, WORD, HWND, BOOL&) {
		static_cast<T*>(this)->Paste();
		return 0;
	}

	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) {
		static_cast<T*>(this)->Copy();
		return 0;
	}

	LRESULT OnCut(WORD, WORD, HWND, BOOL&) {
		static_cast<T*>(this)->Cut();
		return 0;
	}

	LRESULT OnUndo(WORD, WORD, HWND, BOOL&) {
		static_cast<T*>(this)->Undo();
		return 0;
	}

	LRESULT OnRedo(WORD, WORD, HWND, BOOL&) {
		static_cast<T*>(this)->Redo();
		return 0;
	}

	LRESULT OnClearAll(WORD, WORD, HWND, BOOL&) {
		static_cast<T*>(this)->ClearAll();
		return 0;
	}

};

