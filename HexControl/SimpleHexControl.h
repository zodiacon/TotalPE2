#pragma once

#include "Selection.h"
#include "BufferManager.h"

struct HexControlColors {
	COLORREF Text{ ::GetSysColor(COLOR_WINDOWTEXT) };
	COLORREF Background{ ::GetSysColor(COLOR_WINDOW) };
	COLORREF Ascii{ RGB(128, 0, 0) };
	COLORREF Offset{ RGB(0, 0, 128) };
	COLORREF SelectionText{ ::GetSysColor(COLOR_HIGHLIGHTTEXT) };
	COLORREF SelectionBackground{ ::GetSysColor(COLOR_HIGHLIGHT) };
	COLORREF Modified{ RGB(255, 0, 0) };
	COLORREF Ruler{ RGB(0, 0, 192) };
};

enum class HexControlOptions {
	None = 0,
	DisableUndoRedo = 1,
	GrayOutZeros = 2,
};
DEFINE_ENUM_FLAG_OPERATORS(HexControlOptions);

constexpr auto NMHX_SELECTION_CHANGED = 0x2000;
constexpr auto NMHX_CARET_CHANGED = 0x2001;
constexpr auto NMHX_VALUE_CHANGED = 0x2002;
constexpr auto NMHX_DATA_SIZE_CHANGED = 0x2003;
constexpr auto NMHX_BPL_CHANGED = 0x2004;
constexpr auto NMHX_BUFFER_CHANGED = 0x2005;
constexpr auto NMHX_UNDO = 0x2006;
constexpr auto NMHX_REDO = 0x2007;
constexpr auto NMHX_FIND_NOT_FOUND = 0x2008;
constexpr auto NMHX_GOTO_REQUESTED = 0x2009;

struct NMHexControlNotify : NMHDR {};

struct NMHexControlCaretChanged : NMHDR {
	int64_t OldOffset;
	int64_t NewOffset;
};

struct NMHexControlValueChanged : NMHDR {
	int64_t  Offset;
	uint64_t OldValue;
	uint64_t NewValue;
	int32_t  DataSize;
};

struct NMHexControlDataSizeChanged : NMHDR {
	int32_t OldDataSize;
	int32_t NewDataSize;
};

struct NMHexControlBytesPerLineChanged : NMHDR {
	int32_t OldBytesPerLine;
	int32_t NewBytesPerLine;
};

struct HexHighlight {
	int64_t  Offset;
	int64_t  Length;
	COLORREF TextColor;
	COLORREF BkColor;
	int      Id;
};

struct UndoRecord {
	enum class Type { Overwrite, Insert, Delete, Compound };
	Type                     Op{};
	int64_t                  Offset{ 0 };
	std::vector<uint8_t>     OldData;
	std::vector<uint8_t>     NewData;
	std::vector<bool>        OldModified;
	std::vector<UndoRecord>  Children;
};

class CHexControl :
	public CBufferedPaintWindowImpl<CHexControl> {
public:
	DECLARE_WND_CLASS_EX(L"SimpleHexControl", CS_OWNDC | CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW, NULL)

	HWND GetHwnd() const;
	void SetBufferManager(IBufferManager* mgr);
	IBufferManager* GetBufferManager() const;
	void SetReadOnly(bool readonly);
	bool IsReadOnly() const;
	void SetSize(int64_t size);
	bool SetDataSize(int32_t size);
	int32_t GetDataSize() const;
	bool SetBytesPerLine(int32_t bytesPerLine);
	int32_t GetBytesPerLine() const;
	bool Copy(int64_t offset = -1, int64_t size = 0, int base = 16) const;
	bool Paste(int64_t offset = -1);
	bool HasSelection() const;
	bool CanCopy() const;
	bool CanPaste() const;
	bool Cut();
	bool Delete();
	bool CanCut() const;
	bool CanDelete() const;
	int64_t GetCaretOffset() const;
	int64_t GetSelectionOffset() const;
	int64_t GetSelectionLength() const;
	SelectionType GetSelectionType() const;
	void SetSelection(int64_t offset, int64_t length);
	void SetBoxSelection(int64_t offset, int width, int height);
	int64_t Find(const uint8_t* pattern, uint32_t patternSize, int64_t startOffset = 0, bool forward = true);
	int64_t FindNext();
	int64_t FindPrev();
	int64_t SetBiasOffset(int64_t offset);
	int64_t GetBiasOffset() const;
	HexControlColors& GetColors();
	void SetRuler(bool show);
	bool GetRuler() const;
	void SetInsertMode(bool insert);
	bool GetInsertMode() const;
	void SetFont(PCWSTR faceName, int pointSizeTenths);
	void SetBigEndian(bool bigEndian);
	bool GetBigEndian() const;
	void SetMaxUndoLevels(size_t maxLevels);
	size_t GetMaxUndoLevels() const;
	void SetColumnSeparator(uint32_t everyNBytes);
	void SetAddressDecimal(bool decimal);
	bool GetAddressDecimal() const;
	int  AddHighlight(int64_t offset, int64_t length, COLORREF textColor, COLORREF bkColor);
	bool RemoveHighlight(int id);
	void ClearHighlights();
	const std::vector<HexHighlight>& GetHighlights() const;
	void GotoOffset(int64_t offset, bool scrollIntoView = true);
	std::wstring GetText(int64_t offset, int64_t size) const;
	bool CopyAsText(int64_t offset = -1, int64_t size = 0) const;
	uint64_t GetValueAt(int64_t offset, int32_t size) const;
	std::vector<uint8_t> GetSelectedBytes() const;
	std::vector<std::pair<int64_t, int64_t>> GetModifiedRanges() const;
	void Refresh();
	bool IsModified(int64_t offset) const;
	bool IsModified() const;
	bool DeleteState(int64_t offset);
	uint32_t Fill(int64_t offset, const uint8_t* pattern, uint32_t patternSize, uint32_t count);
	uint32_t FillSelection(const uint8_t* pattern, uint32_t patternSize);
	void SetOptions(HexControlOptions options);
	HexControlOptions GetOptions() const;
	bool Undo();
	bool Redo();
	bool CanUndo() const;
	bool CanRedo() const;
	void ClearUndoHistory();
	bool IsUndoRedoEnabled() const noexcept;

	BEGIN_MSG_MAP(CHexControl)
		MESSAGE_HANDLER(WM_CHAR, OnChar)
		MESSAGE_HANDLER(WM_VSCROLL, OnVScroll)
		MESSAGE_HANDLER(WM_HSCROLL, OnHScroll)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLeftButtonUp)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLeftButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLeftButtonDblClk)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_KILLFOCUS, OnKillFocus)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnGetDialogCode)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(WM_COPY, OnCopy)
		MESSAGE_HANDLER(WM_CUT, OnCut)
		MESSAGE_HANDLER(WM_PASTE, OnPaste)
		CHAIN_MSG_MAP(CBufferedPaintWindowImpl<CHexControl>)
	END_MSG_MAP()

	void DoPaint(CDCHandle dc, RECT& rect);

private:
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnHScroll(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnVScroll(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnMouseWheel(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSetFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnKillFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnLeftButtonDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnLeftButtonDblClk(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnGetDialogCode(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnKeyDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnChar(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnMouseMove(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnLeftButtonUp(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCopy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCut(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnPaste(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);

private:
	bool CopyText(PCWSTR text) const;
	void SendNotify(NMHDR& hdr, UINT code);
	void SendSelectionChanged();
	void SendCaretChanged(int64_t oldOffset);
	void RecalcLayout();
	void InitFontMetrics();
	CPoint GetPointFromOffset(int64_t offset) const;
	CPoint GetAsciiPointFromOffset(int64_t offset) const;
	int64_t GetOffsetFromPoint(const POINT& pt) const;
	int GetAsciiStartX() const;
	int GetHScrollX() const;
	int GetRulerHeight() const;
	int GetSeparatorExtraX(int byteCol) const;
	int64_t GetAsciiOffsetFromPoint(const POINT& pt) const;
	void DrawNumber(CDCHandle dc, int64_t offset, uint64_t value, uint32_t editDigits);
	CString FormatNumber(ULONGLONG number, int size = 0) const;

	void UpdateCaret();
	void RedrawWindow(RECT* = nullptr);
	void ClearSelection();
	void CommitValue(int64_t offset, uint64_t value);
	void ResetInput();
	int64_t NormalizeOffset(int64_t offset) const;
	void RedrawCaretLine();
	void PushUndo(UndoRecord record);
	std::vector<uint8_t> ReadBytes(int64_t offset, int64_t count) const;
	std::vector<bool>    ReadModified(int64_t offset, int64_t count) const;
	void ApplyUndo(const UndoRecord& record);
	void ApplyRedo(const UndoRecord& record);
	const HexHighlight* GetHighlightAt(int64_t offset) const;

private:
	HexControlColors m_Colors;
	CFont m_Font;
	WCHAR m_FontFaceName[LF_FACESIZE]{ L"Consolas" };
	int m_FontPointSize{ 110 };
	int m_Lines{ 1 };
	int m_Chars{ 0 };
	int m_CharWidth, m_CharHeight;
	IBufferManager* m_Buffer{ nullptr };
	std::vector<std::wstring> m_Text;
	int64_t m_StartOffset{ 0 }, m_EndOffset, m_BiasOffset{ 0 };
	uint32_t m_DataSize{ 1 }, m_BytesPerLine{ 32 };
	int64_t m_CaretOffset{ 0 };
	int m_AddressDigits{ 8 };
	int m_EditDigits{ 0 }, m_LastDigits{ 0 };
	Selection m_Selection;
	uint64_t m_CurrentInput{ 0 }, m_OldValue;
	std::vector<bool> m_Modified;
	HexControlOptions  m_Options{ HexControlOptions::None };
	size_t   m_MaxUndoLevels{ 1000 };
	uint32_t m_ColSeparator{ 0 };   // 0 = disabled
	std::vector<UndoRecord>   m_UndoStack;
	std::vector<UndoRecord>   m_RedoStack;
	std::vector<uint8_t>      m_FindPattern;
	std::vector<HexHighlight> m_Highlights;
	int                       m_NextHighlightId{ 1 };
	bool	 m_FindForward : 1{ true };
	bool     m_InsertMode : 1{ false };
	bool     m_ReadOnly : 1{ true };
	bool     m_SelectionFromAscii : 1{ false };
	bool     m_ShowRuler : 1 { true };
	bool     m_BigEndian : 1 { false };
	bool     m_DecimalAddresses : 1{ false };
};

