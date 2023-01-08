#pragma once

template<typename T>
class CScintillaCtrlT : public T {
	using Position = Scintilla::Position;
	using Line = Scintilla::Line;
	using Span = Scintilla::Span;
	using Colour = Scintilla::Colour;
	using ColourAlpha = Scintilla::ColourAlpha;
public:
	HWND Create(HWND hWndParent, _U_RECT rect = nullptr, LPCTSTR szWindowName = nullptr,
		DWORD dwStyle = WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, DWORD dwExStyle = 0, _U_MENUorID MenuOrID = 0U, PVOID lpCreateParam = nullptr) {
		return CWindow::Create(GetWndClassName(), hWndParent, rect.m_lpRect, szWindowName, dwStyle, dwExStyle, MenuOrID.m_hMenu, lpCreateParam);
	}

	static LPCTSTR GetWndClassName() {
		return L"Scintilla";
	}

	LRESULT Execute(UINT Msg, WPARAM wParam = 0, LPARAM lParam = 0) const {
		ATLASSERT(::IsWindow(this->m_hWnd));
		return ::SendMessage(this->m_hWnd, Msg, wParam, lParam);
	}

	Position LineStart(Line line) const;
	Position LineEnd(Line line) const;
	Span SelectionSpan() const {
		return SelectionEnd() - SelectionStart();
	}

	bool IsSelectionEmpty() const {
		return (bool)Execute(SCI_GETSELECTIONEMPTY);
	}

	int SelectionsCount() const {
		return (int)Execute(SCI_GETSELECTIONS);
	}

	Span TargetSpan() const;
	void SetTarget(Span span);
	void ColouriseAll();
	char CharacterAt(Position position) const;
	int UnsignedStyleAt(Position position);
	std::string StringOfSpan(Span span) const;
	Position ReplaceTarget(std::string_view text);
	Position ReplaceTargetRE(std::string_view text);
	Position SearchInTarget(std::string_view text);
	Span SpanSearchInTarget(std::string_view text);

	void AddText(Scintilla::Position length, const char* text);
	void AddStyledText(Position length, const char* c);
	void InsertText(Position pos, const char* text);
	void ChangeInsertion(Scintilla::Position length, const char* text);
	void ClearAll() {
		Execute(SCI_CLEARALL);
	}
	void DeleteRange(Scintilla::Position start, Scintilla::Position lengthDelete);
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
	int StyleIndexAt(Position pos) const {
		return (int)Execute(SCI_GETSTYLEINDEXAT, pos);
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
	bool CanRedo() const {
		return Execute(SCI_CANREDO);
	}

	Line MarkerLineFromHandle(int markerHandle);
	void MarkerDeleteHandle(int markerHandle);
	int MarkerHandleFromLine(Line line, int which);
	int MarkerNumberFromLine(Line line, int which);
	bool UndoCollection();
	Scintilla::WhiteSpace ViewWS();
	void SetViewWS(Scintilla::WhiteSpace viewWS);
	Scintilla::TabDrawMode TabDrawMode();
	void SetTabDrawMode(Scintilla::TabDrawMode tabDrawMode);
	Position PositionFromPoint(int x, int y);
	Position PositionFromPointClose(int x, int y);
	void GotoLine(Line line);
	void GotoPos(Position caret) {
		Execute(SCI_GOTOPOS, caret);
	}

	void SetAnchor(Position anchor);
	Position GetCurLine(Position length, char* text);
	std::string GetCurLine(Position length);
	Position EndStyled();
	void ConvertEOLs(Scintilla::EndOfLine eolMode);
	Scintilla::EndOfLine EOLMode();
	void SetEOLMode(Scintilla::EndOfLine eolMode);
	void StartStyling(Position start, int unused);
	void SetStyling(Position length, int style);
	bool BufferedDraw();
	void SetBufferedDraw(bool buffered);
	void SetTabWidth(int tabWidth);
	int TabWidth();
	void SetTabMinimumWidth(int pixels);
	int TabMinimumWidth();
	void ClearTabStops(Line line);
	void AddTabStop(Line line, int x);
	int GetNextTabStop(Line line, int x);
	void SetCodePage(int codePage) {
		Execute(SCI_SETCODEPAGE, codePage);
	}

	void Focus(bool focus = true) {
		Execute(SCI_SETFOCUS, focus);
	}

	void SetFontLocale(const char* localeName);
	int FontLocale(char* localeName);
	std::string FontLocale() const;
	Scintilla::IMEInteraction IMEInteraction() const;
	void SetIMEInteraction(Scintilla::IMEInteraction imeInteraction);
	void MarkerDefine(int markerNumber, Scintilla::MarkerSymbol markerSymbol);
	void MarkerSetFore(int markerNumber, Colour fore);
	void MarkerSetBack(int markerNumber, Colour back);
	void MarkerSetBackSelected(int markerNumber, Colour back);
	void MarkerSetForeTranslucent(int markerNumber, ColourAlpha fore);
	void MarkerSetBackTranslucent(int markerNumber, ColourAlpha back);
	void MarkerSetBackSelectedTranslucent(int markerNumber, ColourAlpha back);
	void MarkerSetStrokeWidth(int markerNumber, int hundredths);
	void MarkerEnableHighlight(bool enabled);
	int MarkerAdd(Line line, int markerNumber);
	void MarkerDelete(Line line, int markerNumber);
	void MarkerDeleteAll(int markerNumber);
	int MarkerGet(Line line);
	Line MarkerNext(Line lineStart, int markerMask);
	Line MarkerPrevious(Line lineStart, int markerMask);
	void MarkerDefinePixmap(int markerNumber, const char* pixmap);
	void MarkerAddSet(Line line, int markerSet);
	void MarkerSetAlpha(int markerNumber, Scintilla::Alpha alpha);
	Scintilla::Layer MarkerGetLayer(int markerNumber);
	void MarkerSetLayer(int markerNumber, Scintilla::Layer layer);
	void SetMarginTypeN(int margin, Scintilla::MarginType marginType);
	Scintilla::MarginType MarginTypeN(int margin);
	void SetMarginWidthN(int margin, int pixelWidth);
	int MarginWidthN(int margin);
	void SetMarginMaskN(int margin, int mask);
	int MarginMaskN(int margin);
	void SetMarginSensitiveN(int margin, bool sensitive);
	bool MarginSensitiveN(int margin);
	void SetMarginCursorN(int margin, Scintilla::CursorShape cursor);
	Scintilla::CursorShape MarginCursorN(int margin);
	void SetMarginBackN(int margin, Colour back);
	Colour MarginBackN(int margin);
	void SetMargins(int margins);
	int Margins();
	void StyleClearAll() {
		Execute(SCI_STYLECLEARALL);
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

	Colour StyleGetBack(int style);
	bool StyleGetBold(int style);
	bool StyleGetItalic(int style);
	int StyleGetSize(int style);
	int StyleGetFont(int style, char* fontName);
	std::string StyleGetFont(int style);
	bool StyleGetEOLFilled(int style);
	bool StyleGetUnderline(int style);
	Scintilla::CaseVisible StyleGetCase(int style);
	Scintilla::CharacterSet StyleGetCharacterSet(int style);
	bool StyleGetVisible(int style);
	bool StyleGetChangeable(int style);
	bool StyleGetHotSpot(int style);
	void StyleSetCase(int style, Scintilla::CaseVisible caseVisible);
	void StyleSetSizeFractional(int style, int sizeHundredthPoints);
	int StyleGetSizeFractional(int style);
	void StyleSetWeight(int style, Scintilla::FontWeight weight);
	Scintilla::FontWeight StyleGetWeight(int style);
	void StyleSetCharacterSet(int style, Scintilla::CharacterSet characterSet);
	void StyleSetHotSpot(int style, bool hotspot);
	void StyleSetCheckMonospaced(int style, bool checkMonospaced);
	bool StyleGetCheckMonospaced(int style);
	void SetElementColour(Scintilla::Element element, ColourAlpha colourElement);
	ColourAlpha ElementColour(Scintilla::Element element);
	void ResetElementColour(Scintilla::Element element);
	bool ElementIsSet(Scintilla::Element element);
	bool ElementAllowsTranslucent(Scintilla::Element element);
	ColourAlpha ElementBaseColour(Scintilla::Element element);
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
	bool CaretLineHighlightSubLine();
	void SetCaretLineHighlightSubLine(bool subLine);
	void SetCaretFore(Colour fore);
	void AssignCmdKey(int keyDefinition, int sciCommand);
	void ClearCmdKey(int keyDefinition);
	void ClearAllCmdKeys();
	void SetStylingEx(Position length, const char* styles);
	void StyleSetVisible(int style, bool visible);
	int CaretPeriod();
	void SetCaretPeriod(int periodMilliseconds);
	void SetWordChars(const char* characters);
	int WordChars(char* characters);
	std::string WordChars();
	void SetCharacterCategoryOptimization(int countCharacters);
	int CharacterCategoryOptimization();
	void BeginUndoAction();
	void EndUndoAction();
	void IndicSetStyle(int indicator, Scintilla::IndicatorStyle indicatorStyle);
	Scintilla::IndicatorStyle IndicGetStyle(int indicator);
	void IndicSetFore(int indicator, Colour fore);
	Colour IndicGetFore(int indicator);
	void IndicSetUnder(int indicator, bool under);
	bool IndicGetUnder(int indicator);
	void IndicSetHoverStyle(int indicator, Scintilla::IndicatorStyle indicatorStyle);
	Scintilla::IndicatorStyle IndicGetHoverStyle(int indicator);
	void IndicSetHoverFore(int indicator, Colour fore);
	Colour IndicGetHoverFore(int indicator);
	void IndicSetFlags(int indicator, Scintilla::IndicFlag flags);
	Scintilla::IndicFlag IndicGetFlags(int indicator);
	void IndicSetStrokeWidth(int indicator, int hundredths);
	int IndicGetStrokeWidth(int indicator);
	void SetWhitespaceFore(bool useSetting, Colour fore);
	void SetWhitespaceBack(bool useSetting, Colour back) {
		Execute(SCI_SETWHITESPACEBACK, useSetting, back);
	}
	void SetWhitespaceSize(int size);
	int WhitespaceSize();
	void SetLineState(Line line, int state);
	int LineState(Line line);
	int MaxLineState();
	bool CaretLineVisible();
	void SetCaretLineVisible(bool show);
	Colour CaretLineBack();
	void SetCaretLineBack(Colour back);
	int CaretLineFrame();
	void SetCaretLineFrame(int width);
	void StyleSetChangeable(int style, bool changeable);
	void AutoCShow(Position lengthEntered, const char* itemList);
	void AutoCCancel();
	bool AutoCActive();
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
	Position LineNumber(Position pos) const {
		return (Position)Execute(SCI_LINEFROMPOSITION, pos);
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
	bool IsReadOnly() {
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
		Sci_TextToFind ttf{ };
		ttf.chrg.cpMax = Length();
		ttf.lpstrText = text;
		return (Position)Execute(SCI_FINDTEXT, static_cast<WPARAM>(searchFlags), reinterpret_cast<LPARAM>(&ttf));
	}

	Position FormatRange(bool draw, void* fr);
	Line FirstVisibleLine();
	Position GetLine(Line line, char* text);
	std::string GetLine(Line line);
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
	std::string GetSelText();
	Position GetTextRange(void* tr);
	void HideSelection(bool hide);
	int PointXFromPosition(Position pos);
	int PointYFromPosition(Position pos);
	Line LineFromPosition(Position pos) const {
		return (Line)Execute(SCI_GETLINEENDPOSITION, pos);
	}

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

	void SetText(const char* text) {
		Execute(SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(text));
	}

	Position GetText(Position length, char* text) const {
		return (Position)Execute(SCI_GETTEXT, length, reinterpret_cast<LPARAM>(text));
	}

	std::string GetText(Position length) const {
		std::string text;
		text.resize((size_t)length);
		GetText(length, text.data());
		return text;
	}
	Position TextLength() const;
};

using CScintillaCtrl = CScintillaCtrlT<CWindow>;

template<typename T>
class CScintillaCommands {
	BEGIN_MSG_MAP(CScintillaCommands)
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

