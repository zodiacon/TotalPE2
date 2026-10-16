#pragma once

#include "ViewBase.h"
#include "resource.h"
#include <WinRtServer.h>
#include <VirtualListView.h>
#include <map>

//
// The Windows Runtime classes the image serves (WinRtServer.h): of WRL's creator map, and what the registry has of them;
// below them the interfaces of the one selected, of the metadata of Windows (those of its factories and statics, which
// its factory implements, and those of its instances) with their methods and their slots in the interfaces' vtables.
// Double-click goes to the function that makes the class's factory (or the class's entry in the creator map); the
// commands of functions (Decompile, Disassemble...) are for that function.
//
class CWinRtView :
	public CViewBase<CWinRtView>,
	public CVirtualListView<CWinRtView> {
public:
	CWinRtView(IMainFrame* frame, PEFile const& pe);
	CString GetTitle() const override;
	void AnnotationsChanged() override;

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	bool OnDoubleClickList(HWND, int row, int col, CPoint const& pt) const;
	bool OnRightClickList(HWND, int row, int col, POINT const& pt) const;
	void OnStateChanged(HWND, int from, int to, UINT oldState, UINT newState);
	void UpdateUI(bool first = false) const;

	BEGIN_MSG_MAP(CWinRtView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CWinRtView>)
		CHAIN_MSG_MAP(CViewBase<CWinRtView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_VIEW_DISASSEMBLE, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_DECOMPILE, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_SIDEBYSIDE, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_XREFS, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_FLOWGRAPH, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_CALLGRAPH, OnFunctionCommand)
		CHAIN_MSG_MAP_ALT(CViewBase<CWinRtView>, 1)
	END_MSG_MAP()

private:
	// a method of an interface of the class selected (or an interface without methods: a generic one, one not found)
	struct Member {
		WinRtInterface const* Interface;
		WinRtMethod const* Method;		// null for an interface without methods
	};

	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnFunctionCommand(WORD, WORD id, HWND, BOOL&) const;
	// the interfaces of the class selected
	void FillMembers();
	// the class selected; null if none
	WinRtClass const* SelectedClass() const;
	// what the metadata has of a class (found once)
	WinRtClassMetadata const& MetadataOf(WinRtClass const& c) const;

	CSplitterWindowT<false> m_Splitter;
	CListViewCtrl m_Classes, m_Members;
	PEFile const& m_PE;
	std::vector<WinRtClass const*> m_ClassItems;
	std::vector<Member> m_MemberItems;
	WinRtClass const* m_Selected{ nullptr };
	mutable std::map<std::wstring, WinRtClassMetadata> m_Metadata;
};
