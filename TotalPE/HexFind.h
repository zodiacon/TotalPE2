#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <DialogHelper.h>
#include "resource.h"

enum class HexSearchType {
	Hex,		// "4D 5A ?? 00", ?? and single ? nibbles are wildcards
	Ascii,		// text, searched as UTF-8
	Unicode,	// text, searched as UTF-16 (little endian)
};

struct HexFindOptions {
	std::wstring Text;
	HexSearchType Type{ HexSearchType::Hex };
	bool MatchCase{ false };
	bool Down{ true };
};

struct HexPattern {
	std::vector<uint8_t> Bytes;
	std::vector<uint8_t> Mask;	// bits that must match (0 = wildcard)
	std::vector<uint8_t> Fold;	// non-zero: byte is a letter that matches case-insensitively
	bool empty() const { return Bytes.empty(); }
};

namespace HexSearch {
	bool BuildPattern(HexFindOptions const& options, HexPattern& pattern, std::wstring& error);

	// Searches data[0, size). Forward search starts at 'start' and goes up, backward search starts at 'start' and goes down.
	// Returns the offset of the match or -1.
	int64_t Find(const uint8_t* data, int64_t size, HexPattern const& pattern, int64_t start, bool forward);
}

class CHexFindDlg :
	public CDialogImpl<CHexFindDlg>,
	public CDialogHelper<CHexFindDlg> {
public:
	enum { IDD = IDD_HEXFIND };

	explicit CHexFindDlg(HexFindOptions& options) : m_Options(options) {}

	BEGIN_MSG_MAP(CHexFindDlg)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		COMMAND_ID_HANDLER(IDOK, OnOK)
		COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
		COMMAND_RANGE_HANDLER(IDC_HEXFIND_HEX, IDC_HEXFIND_UNICODE, OnTypeChanged)
	END_MSG_MAP()

	LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnOK(WORD, WORD, HWND, BOOL&);
	LRESULT OnCancel(WORD, WORD wID, HWND, BOOL&);
	LRESULT OnTypeChanged(WORD, WORD, HWND, BOOL&);

private:
	HexFindOptions& m_Options;
};
