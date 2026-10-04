#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include "CoffObject.h"
#include "CodeViewInfo.h"

enum class DebugInfoViewKind {
	Subsections, Symbols, Lines, Files, Types,
};

// The CodeView information of an object file (see CodeViewInfo.h): the subsections of its .debug$S sections, the symbols,
// the source lines and files, and the types. Double-click a row to see its place: the code or the data a symbol or a line is for,
// or else the record itself.
class CDebugInfoView :
	public CViewBase<CDebugInfoView>,
	public CVirtualListView<CDebugInfoView> {
public:
	// 'member' is the member of the library the object is (-1 for an object file); 'owner' its name for the title
	CDebugInfoView(IMainFrame* frame, CoffObject const& obj, CodeViewInfo const& cv, DebugInfoViewKind kind, int member, PCWSTR owner);
	CString GetTitle() const override;

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	bool IsSortable(HWND, int col) const;
	bool OnDoubleClickList(HWND, int row, int col, CPoint const& pt) const;
	void OnStateChanged(HWND, int from, int to, DWORD oldState, DWORD newState) const;
	void UpdateUI(bool first = false) const;

	BEGIN_MSG_MAP(CDebugInfoView)
		MESSAGE_HANDLER(CFindReplaceDialog::GetFindReplaceMsg(), OnFind)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CDebugInfoView>)
		CHAIN_MSG_MAP(CViewBase<CDebugInfoView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		CHAIN_MSG_MAP_ALT(CViewBase<CDebugInfoView>, 1)
	END_MSG_MAP()

private:
	enum Column {
		Section, Offset, Type, Size,											// subsections (and the records of the others)
		Kind, Name, Details, Target,											// symbols, types
		Function, CodeOffset, File, Line, LineColumn, Statement,				// lines
		Id, ChecksumKind, Checksum,												// files
		Index,																	// types
	};

	std::wstring SectionText(uint32_t section) const;
	std::wstring TargetText(CvTarget const& target) const;

	LRESULT OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnFind(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;

	CListViewCtrl m_List;
	CoffObject const& m_Object;
	CodeViewInfo const& m_CodeView;
	DebugInfoViewKind m_Kind;
	int m_Member;
	CString m_Owner;
	std::vector<int> m_Rows;		// the position of each row in its list of the CodeView information: the rows can be sorted
	std::vector<std::wstring> m_Functions;	// of the lines, undecorated
};
