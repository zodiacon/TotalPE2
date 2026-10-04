#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include "ElfFile.h"

enum class ElfViewKind {
	Header, ProgramHeaders, Sections, Symbols, Dynamic, Relocations, Notes,
};

// The lists of an ELF file (see ElfFile.h). Double-click a row to see what it points to: code is disassembled (x86 and x64),
// anything else is shown in the hex view.
class CElfView :
	public CViewBase<CElfView>,
	public CVirtualListView<CElfView> {
public:
	CElfView(IMainFrame* frame, ElfFile const& elf, ElfViewKind kind);
	CString GetTitle() const override;

	CString GetColumnText(HWND, int row, int col) const;
	int GetRowImage(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	bool IsSortable(HWND, int col) const;
	bool OnDoubleClickList(HWND, int row, int col, CPoint const& pt) const;
	void OnStateChanged(HWND, int from, int to, DWORD oldState, DWORD newState) const;
	void UpdateUI(bool first = false) const;

	BEGIN_MSG_MAP(CElfView)
		MESSAGE_HANDLER(CFindReplaceDialog::GetFindReplaceMsg(), OnFind)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CElfView>)
		CHAIN_MSG_MAP(CViewBase<CElfView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		CHAIN_MSG_MAP_ALT(CViewBase<CElfView>, 1)
	END_MSG_MAP()

private:
	struct Column {
		PCWSTR Name;
		int Width;
		bool Right;		// numbers: right aligned, and sorted by value
	};

	// where a double-click goes
	enum class Target {
		None,
		Address,		// a virtual address (executables, shared objects)
		SectionOffset,	// an offset in a section (relocatable objects, where every section is at address 0)
		FileOffset,
	};

	struct Row {
		std::vector<std::wstring> Cells;
		Target Go{ Target::None };
		uint64_t Where{};		// the address or the offset
		int Section{ -1 };		// for SectionOffset
		bool Code{ false };		// disassemble, rather than show in hex
	};

	void Build();
	void BuildHeader();
	void BuildProgramHeaders();
	void BuildSections();
	void BuildSymbols();
	void BuildDynamic();
	void BuildRelocations();
	void BuildNotes();
	std::wstring SectionName(uint32_t index) const;
	// the address a symbol or relocation means; relocatable objects have offsets in a section instead
	bool IsRelocatable() const { return m_Elf.Type() == 1; }

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnFind(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;

	CListViewCtrl m_List;
	ElfFile const& m_Elf;
	ElfViewKind m_Kind;
	std::vector<Column> m_Columns;
	std::vector<Row> m_Rows;
};
