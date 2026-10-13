#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include <QuickFindEdit.h>
#include <SortedFilteredVector.h>
#include "resource.h"
#include <map>
#include <atomic>
#include <memory>

class PEFile;

// The functions of the image, from all that tells of them: the symbols, the exception directory (the functions that have
// unwind information), the exports, the entry point, and the targets of calls (once the cross-references are found). With
// their sizes and how many calls there are to each; a function can be disassembled, decompiled and more from its row.
class CFunctionsView :
	public CViewBase<CFunctionsView>,
	public CVirtualListView<CFunctionsView> {
public:
	CFunctionsView(IMainFrame* frame, PEFile const& pe);
	CString GetTitle() const override;
	// the calls to the functions, and the functions that are known only as their targets
	void XrefsChanged() override;

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	int GetRowImage(HWND, int row, int) const;
	bool OnDoubleClickList(HWND, int row, int col, CPoint const& pt) const;
	bool OnRightClickList(HWND, int row, int col, POINT const& pt) const;
	void OnStateChanged(HWND, int from, int to, UINT oldState, UINT newState) const;
	void UpdateUI(bool first = false) const;

	BEGIN_MSG_MAP(CFunctionsView)
		MESSAGE_HANDLER(CFindReplaceDialog::GetFindReplaceMsg(), OnFind)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
		MESSAGE_HANDLER(WM_SYMBOL_FUNCTIONS, OnSymbolFunctions)
		COMMAND_CODE_HANDLER(EN_DELAYCHANGE, OnFilterChanged)
		CHAIN_MSG_MAP(CVirtualListView<CFunctionsView>)
		CHAIN_MSG_MAP(CViewBase<CFunctionsView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		COMMAND_ID_HANDLER(ID_VIEW_DISASSEMBLE, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_DECOMPILE, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_SIDEBYSIDE, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_XREFS, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_FLOWGRAPH, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_CALLGRAPH, OnFunctionCommand)
		CHAIN_MSG_MAP_ALT(CViewBase<CFunctionsView>, 1)
	END_MSG_MAP()

private:
	enum class Column {
		Name, Address, Rva, Size, Calls, Section, Source
	};
	// what tells of a function (any number of these)
	enum Source : uint8_t {
		FromSymbols = 1, FromExceptions = 2, FromExports = 4, FromEntryPoint = 8, FromCalls = 16,
	};
	struct Function {
		uint32_t Rva;
		uint32_t Size{ 0 };
		std::wstring Name;
		uint8_t Sources{ 0 };
		bool KnownSize{ false };	// from the symbols or the unwind information (otherwise: up to the next function)
		int Section{ -1 };
		uint32_t Calls{ 0 };		// (with those through a stub that jumps to it)
		uint32_t Target{ 0 };		// of a stub: the function it jumps to
	};

	struct Code {
		uint32_t Start, End;
		int Section;
	};
	void Collect();
	void Build();
	Code const* SectionOf(uint32_t rva) const;
	// 'prefer': the name and size replace those from elsewhere (those of the symbols)
	void Add(std::map<uint32_t, Function>& functions, uint32_t rva, uint8_t source, std::wstring const& name = {}, uint32_t size = 0,
		bool prefer = false) const;
	// a function of the symbols, read on another thread
	struct SymbolFunction {
		uint32_t Rva;
		uint32_t Size;
		std::wstring Name;
		bool Public{ false };
	};
	// posted by the thread that reads the symbols: lParam is a std::vector<SymbolFunction>* that the view deletes
	static constexpr UINT WM_SYMBOL_FUNCTIONS = WM_APP + 70;
	void ApplyFilter();
	Function const* GetSelected() const;
	static std::wstring SourceText(uint8_t sources);

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSymbolFunctions(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnFind(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnFilterChanged(WORD, WORD, HWND, BOOL&);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;
	LRESULT OnFunctionCommand(WORD, WORD id, HWND, BOOL&) const;

	CListViewCtrl m_List;
	CQuickFindEdit m_Filter;
	PEFile const& m_PE;
	SortedFilteredVector<Function> m_Items;
	std::map<uint32_t, Function> m_Known;	// from the file and the symbols, by RVA
	std::vector<Code> m_Code;				// the code sections
	bool m_ReadingSymbols{ false };
	std::shared_ptr<std::atomic<bool>> m_Cancel;	// of the thread that reads the symbols
	std::vector<std::wstring> m_SectionNames;
	CString m_FilterText;
};
