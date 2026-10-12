#pragma once

#include <DialogHelper.h>
#include "resource.h"
#include "HexNumber.h"

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

// A function to disassemble: its name or its address
class CDisassembleFunctionDlg :
	public CDialogImpl<CDisassembleFunctionDlg>,
	public CDialogHelper<CDisassembleFunctionDlg> {
public:
	enum { IDD = IDD_DISASM_FUNCTION };

	// 'caption' and 'button': the dialog's title and the text of its OK button, if not those of disassembling
	explicit CDisassembleFunctionDlg(std::wstring& text, PCWSTR caption = nullptr, PCWSTR button = nullptr)
		: m_Text(text), m_Caption(caption), m_Button(button) {}

	BEGIN_MSG_MAP(CDisassembleFunctionDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnOK)
		COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
	END_MSG_MAP()

	LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnOK(WORD, WORD, HWND, BOOL&);
	LRESULT OnCancel(WORD, WORD wID, HWND, BOOL&);

private:
	std::wstring& m_Text;
	PCWSTR m_Caption;
	PCWSTR m_Button;
};
