// View.h : interface of the CView class
//
/////////////////////////////////////////////////////////////////////////////

#pragma once

#include "CodeAnalysis.h"

#include "ScintillaCtrl.h"
#include "ViewBase.h"
#include <GhidraDecompiler.h>
#include <optional>
#include <functional>
#include <unordered_map>

class PEFile;

enum class LexLanguage {
	Xml,
	Asm,
	Html,
	Text,
	Inf,
	C,
	Cpp,
	CSharp,
	IL,
};

class CScintillaView :
	public CViewBase<CScintillaView> {
public:
	CScintillaView(IMainFrame* frame, PEFile const& pe, PCWSTR title);

	CString GetTitle() const override;
	CScintillaCtrl& GetCtrl();

	// Enter follows the call or jump of the line, X lists the references to the instruction, G shows the flow graph of the function
	BOOL PreTranslateMessage(MSG* pMsg);

	void UpdateUI(bool first = false);

	bool SetAsmCode(std::span<const std::byte> code, uint64_t address, CpuArch arch);

	void SetText(PCWSTR text);
	void SetText(PCSTR text);
	void SetLanguage(LexLanguage lang);
	// The functions the text refers to (decompiled code), by the names it shows: the one whose name is under the mouse can
	// be decompiled or disassembled from the context menu
	void SetFunctionLinks(std::unordered_map<std::wstring, uint64_t> links);
	// The function the view shows: its disassembly and C can be shown side by side (from the context menu). Set by the
	// disassembly to the address it starts at.
	void SetFunctionAddress(uint64_t address);
	// The variables the decompiled code shows: the one under the caret (or the mouse) can be renamed (N) or retyped (Y), as
	// can a function it calls (renamed)
	void SetVariables(GhidraDecompiler::Variables variables);
	// The addresses of the instructions each line of decompiled code was made from: a line's bookmark and comment are those
	// of its first instruction (of the function, for a line without one)
	void SetLineAddresses(GhidraDecompiler::LineAddresses lines);
	// the comments shown again (in the disassembly), and the lines of the bookmarks marked
	void AnnotationsChanged() override;

	// The disassembly's lines: the address of a line's instruction (0 if it is not one), the line of an instruction (-1)
	uint64_t LineAddress(int line) const;
	int LineOfAddress(uint64_t va) const;
	// Lines marked (a background) as related to something elsewhere; none clears the marks
	void MarkLines(std::vector<int> const& lines);
	// A side of a side-by-side view: the control's notifications of scrolling, the caret, zoom and focus go to it, and the
	// view does not offer to be shown side by side
	using Notify = std::function<void(CScintillaView&, SCNotification const&)>;
	void SetNotify(Notify notify);
	// Where a line of the text leads (IL: to the definition of what the line refers to): a double-click or Enter on the
	// line, or Go to Definition in its context menu, calls it; false if the line leads nowhere
	using LineAction = std::function<bool(int line)>;
	void SetLineAction(LineAction action);

	int64_t GetNavigationPosition() const override;
	void SetNavigationPosition(int64_t va) override;
	bool GoToAddress(uint64_t va) override;
	// the disassembly again, with the labels of the cross-references (where the user was stays in view)
	void XrefsChanged() override;

	BEGIN_MSG_MAP(CScintillaView)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(::RegisterWindowMessage(L"WTLHelperUpdateTheme"), OnUpdateTheme)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		NOTIFY_CODE_HANDLER(SCN_DOUBLECLICK, OnDoubleClick)
		NOTIFY_CODE_HANDLER(SCN_UPDATEUI, OnSciNotify)
		NOTIFY_CODE_HANDLER(SCN_ZOOM, OnSciNotify)
		NOTIFY_CODE_HANDLER(SCN_FOCUSIN, OnSciNotify)
		NOTIFY_CODE_HANDLER(SCN_DWELLSTART, OnDwell)
		NOTIFY_CODE_HANDLER(SCN_DWELLEND, OnDwell)
		CHAIN_MSG_MAP(CViewBase<CScintillaView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_ASSEMBLY_GOTOADDRESS, OnGoToAddress)
		COMMAND_ID_HANDLER(ID_ASSEMBLY_FOLLOW, OnFollow)
		COMMAND_ID_HANDLER(ID_ASSEMBLY_XREFS_HERE, OnXrefsHere)
		COMMAND_ID_HANDLER(ID_ASSEMBLY_XREFS_TARGET, OnXrefsTarget)
		COMMAND_ID_HANDLER(ID_ASSEMBLY_FLOWGRAPH, OnFlowGraph)
		COMMAND_ID_HANDLER(ID_ASSEMBLY_CALLGRAPH, OnFlowGraph)
		COMMAND_ID_HANDLER(ID_ASSEMBLY_DISASSEMBLEATTHEEND, OnDisassembleAtEnd)
		COMMAND_ID_HANDLER(ID_ASSEMBLY_DISASSEMBLEINANEWTAB, OnDisassembleNewTab)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnEditCopy)
		COMMAND_ID_HANDLER(ID_CODE_DECOMPILE, OnCodeLink)
		COMMAND_ID_HANDLER(ID_CODE_DISASSEMBLE, OnCodeLink)
		COMMAND_ID_HANDLER(ID_CODE_DECOMPILE_HERE, OnCodeLink)
		COMMAND_ID_HANDLER(ID_CODE_DISASSEMBLE_HERE, OnCodeLink)
		COMMAND_ID_HANDLER(ID_CODE_SIDEBYSIDE, OnSideBySide)
		COMMAND_ID_HANDLER(ID_CODE_GOTODEFINITION, OnGoToDefinition)
		COMMAND_ID_HANDLER(ID_CODE_WIN32API, OnWin32Api)
		COMMAND_ID_HANDLER(ID_CODE_WIN32DOCS, OnWin32Api)
		COMMAND_RANGE_HANDLER(ID_CODE_SHOWAS_FIRST, ID_CODE_SHOWAS_LAST, OnShowAs)
		COMMAND_ID_HANDLER(ID_CODE_RENAME, OnRename)
		COMMAND_ID_HANDLER(ID_CODE_RETYPE, OnRename)
		COMMAND_ID_HANDLER(ID_HEX_BOOKMARK_TOGGLE, OnAnnotate)
		COMMAND_ID_HANDLER(ID_NAV_COMMENT, OnAnnotate)
		COMMAND_ID_HANDLER(ID_CODE_BOOKMARK, OnAnnotate)
		COMMAND_ID_HANDLER(ID_CODE_COMMENT, OnAnnotate)
		COMMAND_ID_HANDLER(ID_HEX_BOOKMARK_NEXT, OnNextBookmark)
		COMMAND_ID_HANDLER(ID_HEX_BOOKMARK_PREV, OnNextBookmark)
		COMMAND_ID_HANDLER(ID_EDIT_CUT, OnEdit)
		COMMAND_ID_HANDLER(ID_EDIT_PASTE, OnEdit)
		COMMAND_ID_HANDLER(ID_EDIT_UNDO, OnEdit)
		COMMAND_ID_HANDLER(ID_EDIT_REDO, OnEdit)
		CHAIN_MSG_MAP_ALT(CViewBase<CScintillaView>, 1)
	END_MSG_MAP()

private:
	// what the user can follow from a line of the disassembly
	struct Line {
		uint64_t Va{ 0 };					// 0 for the lines that are not an instruction (labels)
		std::optional<uint64_t> Branch;		// the target of a call or jump
		std::optional<uint64_t> Memory;		// the address of a memory operand
		std::optional<uint64_t> Target() const { return Branch ? Branch : Memory; }
	};

	void UpdateColors();
	CStringA Disassemble(std::span<const std::byte> code, uint64_t address);
	int CurrentLine() const;
	int InstructionLine(int line) const;
	Line const* GetLine(int line) const;
	void ShowLine(int line);
	bool NavigateTo(uint64_t va);
	bool Follow(int line);

	// Handler prototypes (uncomment arguments if needed):
	//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
	//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
	//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)

	LRESULT OnSetFocus(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnUpdateTheme(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnGoToAddress(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDisassembleNewTab(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDisassembleAtEnd(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnEdit(WORD, WORD id, HWND, BOOL&);
	LRESULT OnCodeLink(WORD, WORD id, HWND, BOOL&);
	LRESULT OnSideBySide(WORD, WORD, HWND, BOOL&);
	LRESULT OnGoToDefinition(WORD, WORD, HWND, BOOL&);
	LRESULT OnRename(WORD, WORD id, HWND, BOOL&);
	LRESULT OnWin32Api(WORD, WORD, HWND, BOOL&);
	LRESULT OnShowAs(WORD, WORD, HWND, BOOL&);
	LRESULT OnDwell(int, LPNMHDR, BOOL&);
	std::wstring m_ContextApi;	// a function or type of Windows the context menu was opened on
	std::wstring CalleeAt(intptr_t pos);
	std::wstring m_ContextDocs;	// the one whose documentation it shows (a constant's function, if its type has none)
	std::vector<std::string> m_ContextNames;	// the other names of the value of a constant the menu was opened on
	// renames (or retypes) a variable or function by the name the code shows; false if it is neither
	bool Rename(std::wstring const& name, bool type);
	GhidraDecompiler::Variable const* VariableNamed(std::wstring const& name) const;
	GhidraDecompiler::Variables m_Variables;
	std::wstring m_ContextName;		// the name the context menu was opened on
	// bookmarks and comments: of the caret's line (of the line of the context menu: ID_CODE_BOOKMARK, ID_CODE_COMMENT)
	LRESULT OnAnnotate(WORD, WORD id, HWND, BOOL&);
	LRESULT OnNextBookmark(WORD, WORD id, HWND, BOOL&);
	// the address a line of code is of (0: none)
	uint64_t AddressOfLine(int line) const;
	void MarkBookmarks();
	bool HasAnnotations() const;
	GhidraDecompiler::LineAddresses m_LineAddresses;	// of decompiled code
	static constexpr int MarkerBookmark = 2;
	LRESULT OnSciNotify(int, LPNMHDR, BOOL&);
	Notify m_Notify;
	LineAction m_LineAction;
	uint64_t m_Function{ 0 };
	// the name (of a function: with its scopes and template arguments) at a position of the text, empty if none
	std::wstring NameAt(intptr_t pos) const;
	// the function of a name that the text shows (0 if it is not one)
	uint64_t FunctionOf(std::wstring const& name) const;
	std::unordered_map<std::wstring, uint64_t> m_Links;
	uint64_t m_LinkAddress{ 0 };	// the function of the context menu
	LRESULT OnEditCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnDoubleClick(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
	LRESULT OnFollow(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnXrefsHere(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnXrefsTarget(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnFlowGraph(WORD /*wNotifyCode*/, WORD id, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

	std::optional<uint64_t> FlowGraphAddress();

	CString m_Title;
	CScintillaCtrl m_Sci;
	LexLanguage m_Language{ LexLanguage::Text };
	PEFile const& m_PE;
	CpuArch m_Arch{ CpuArch::X64 };
	std::span<const std::byte> m_Code;				// what the disassembly is of, and its address
	uint64_t m_CodeAddress{ 0 };
	std::vector<Line> m_Lines;						// by line of the text: what each line of the disassembly is
	std::unordered_map<uint64_t, int> m_LineOfVa;	// the line of each instruction
	int m_ContextLine{ -1 };						// the line the context menu was opened on
	static constexpr int MarkerRelated = 1;
};
