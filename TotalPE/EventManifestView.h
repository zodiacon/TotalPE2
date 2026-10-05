#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include <QuickFindEdit.h>
#include <array>
#include "EventManifest.h"
#include "resource.h"

// A WEVT_TEMPLATE resource: the instrumentation manifest of the ETW providers of the file. One table at a time (events,
// templates and their fields, providers, channels, levels, tasks, opcodes, keywords, value maps), chosen on the toolbar,
// with a filter. The texts of messages come from the message table of the file (or its .mui file).
class CEventManifestView :
	public CViewBase<CEventManifestView>,
	public CVirtualListView<CEventManifestView> {
public:
	CEventManifestView(IMainFrame* frame, PCWSTR title, EventManifest manifest, std::wstring const& path);
	CString GetTitle() const override;

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	void OnStateChanged(HWND, int from, int to, UINT oldState, UINT newState) const;
	void UpdateUI(bool first = false) const;

	BEGIN_MSG_MAP(CEventManifestView)
		MESSAGE_HANDLER(CFindReplaceDialog::GetFindReplaceMsg(), OnFind)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		COMMAND_CODE_HANDLER(EN_DELAYCHANGE, OnFilterChanged)
		COMMAND_RANGE_HANDLER(ID_EVENTS_FIRST, ID_EVENTS_LAST, OnTable)
		CHAIN_MSG_MAP(CVirtualListView<CEventManifestView>)
		CHAIN_MSG_MAP(CViewBase<CEventManifestView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		CHAIN_MSG_MAP_ALT(CViewBase<CEventManifestView>, 1)
	END_MSG_MAP()

private:
	enum class Table { Events, Templates, Providers, Channels, Levels, Tasks, Opcodes, Keywords, Maps, Count };

	struct Column {
		PCWSTR Name;
		int Width;
		int Format{ LVCFMT_LEFT };
	};

	// what a table shows: its columns, and the text of the cells of each row
	struct TableData {
		PCWSTR Name;
		std::vector<Column> Columns;
		std::vector<std::vector<std::wstring>> Rows;
	};

	void BuildTables();
	std::wstring Message(uint32_t id) const;
	void ShowTable(Table table);
	void ApplyFilter();

	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnFind(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnFilterChanged(WORD, WORD, HWND hWnd, BOOL& handled);
	LRESULT OnTable(WORD, WORD id, HWND, BOOL&);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;

	CString m_Title;
	EventManifest m_Manifest;
	HMODULE m_Messages{ nullptr };	// the file, for its message table
	std::array<TableData, (size_t)Table::Count> m_Tables;
	Table m_Table{ Table::Events };
	std::vector<int> m_Rows;		// the rows of the table that are shown
	CListViewCtrl m_List;
	CToolBarCtrl m_tb;
	CQuickFindEdit m_Filter;
	CString m_FilterText;

public:
	~CEventManifestView();
};
