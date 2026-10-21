#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include <YaraRules.h>

class PEFile;

// The YARA rules that matched the file: a row for each match of a string of a rule (a rule that matched on its condition
// alone has a row of its own). Double-click a row of a string to see its place in the hex view. The rule files that were left
// out (they have errors) come last, with their errors.
class CYaraView :
	public CViewBase<CYaraView>,
	public CVirtualListView<CYaraView> {
public:
	CYaraView(IMainFrame* frame, PEFile const& pe, std::vector<YaraMatch> const& matches, std::vector<YaraDiagnostic> const& errors);
	CString GetTitle() const override;

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	bool OnDoubleClickList(HWND, int row, int col, CPoint const& pt) const;

	BEGIN_MSG_MAP(CYaraView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CYaraView>)
		CHAIN_MSG_MAP(CViewBase<CYaraView>)
	ALT_MSG_MAP(1)
		CHAIN_MSG_MAP_ALT(CViewBase<CYaraView>, 1)
	END_MSG_MAP()

	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);

private:
	struct Row {
		std::wstring Rule, Namespace, Tags, Description;
		std::wstring String;		// "$a" (empty for a rule without matches of strings)
		int64_t Offset{ -1 };
		std::wstring Section;
		int64_t Rva{ -1 };
		uint32_t Length{};
		std::wstring Data;			// in hex
		size_t Index{};				// the order of the rules and their matches (for sorting back)
	};

	CListViewCtrl m_List;
	std::vector<Row> m_Rows;
};
