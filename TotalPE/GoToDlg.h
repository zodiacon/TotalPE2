#pragma once

#include <DialogHelper.h>
#include <VirtualListView.h>
#include <optional>
#include "resource.h"
#include "HexNumber.h"
#include "SearchDlg.h"

enum class GoToKind {
	Rva,
	Va,
	FileOffset,
};

struct GoToOptions {
	std::wstring Text;
	GoToKind Kind{ GoToKind::Rva };
	bool Disassemble{ false };
	uint64_t Value{};		// the parsed text
};

class CGoToDlg :
	public CDialogImpl<CGoToDlg>,
	public CDialogHelper<CGoToDlg> {
public:
	enum { IDD = IDD_GOTO };

	explicit CGoToDlg(GoToOptions& options) : m_Options(options) {}

	BEGIN_MSG_MAP(CGoToDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnOK)
		COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
	END_MSG_MAP()

	LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnOK(WORD, WORD, HWND, BOOL&);
	LRESULT OnCancel(WORD, WORD wID, HWND, BOOL&);

private:
	GoToOptions& m_Options;
};

// A function to disassemble (or decompile): its name or its address. The functions whose names match what is typed are
// listed (those of the exports and of the symbols, and those the user named), the best first; Up and Down choose one,
// Enter (or a double-click) takes it; one chosen in the list itself (a click) has its name put in the box. With nothing typed, those opened last are listed.
class CDisassembleFunctionDlg :
	public CDialogImpl<CDisassembleFunctionDlg>,
	public CDialogHelper<CDisassembleFunctionDlg>,
	public CVirtualListView<CDisassembleFunctionDlg> {
public:
	enum { IDD = IDD_DISASM_FUNCTION };

	// 'caption' and 'button': the dialog's title and the text of its OK button, if not those of disassembling
	CDisassembleFunctionDlg(std::wstring& text, ISearchHost* host, PCWSTR caption = nullptr, PCWSTR button = nullptr)
		: m_Text(text), m_Host(host), m_Caption(caption), m_Button(button), m_Edit(this, 1) {}

	// the address of the function chosen in the list; 0 if the text was typed (the caller finds what it names)
	uint64_t Address() const { return m_Address; }

	CString GetColumnText(HWND, int row, int col) const;
	int GetRowImage(HWND, int row, int col) const;
	void DoSort(SortInfo const*) {}
	bool OnDoubleClickList(HWND, int row, int col, CPoint const& pt);

	BEGIN_MSG_MAP(CDisassembleFunctionDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		COMMAND_HANDLER(IDC_DISASM_TEXT, EN_CHANGE, OnTextChanged)
		COMMAND_ID_HANDLER(IDOK, OnOK)
		COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
		NOTIFY_HANDLER(IDC_DISASM_LIST, LVN_ITEMCHANGED, OnListItemChanged)
		CHAIN_MSG_MAP(CVirtualListView<CDisassembleFunctionDlg>)
	ALT_MSG_MAP(1)
		// the keys that move in the list go to it from the text
		MESSAGE_HANDLER(WM_KEYDOWN, OnEditKeyDown)
	END_MSG_MAP()

	LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTimer(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnTextChanged(WORD, WORD, HWND, BOOL&);
	LRESULT OnOK(WORD, WORD, HWND, BOOL&);
	LRESULT OnCancel(WORD, WORD wID, HWND, BOOL&);
	LRESULT OnEditKeyDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnListItemChanged(int, LPNMHDR, BOOL&);

private:
	void Update();
	void Choose(int row);

	struct Function {
		SearchItem const* Item;
		uint64_t Address;
		mutable std::optional<std::wstring> Declaration;	// (asked for when it is shown)
	};
	std::wstring& m_Text;
	ISearchHost* m_Host;
	PCWSTR m_Caption;
	PCWSTR m_Button;
	CContainedWindowT<CEdit> m_Edit;
	CListViewCtrl m_List;
	std::vector<Function> m_Functions;
	std::vector<SearchItem> m_Recent;
	std::vector<SearchItem> m_ByAddress;	// the functions whose addresses start with what is typed
	uint64_t m_Address{ 0 };
	bool m_Setting{ false };	// (the text is set from the list: what it lists stays)
	bool m_Selecting{ false };	// (the list's selection is the box's: its best match, its keys)
	static constexpr size_t MaxResults = 200;
};
