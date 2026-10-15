#pragma once

#include "ViewBase.h"
#include "resource.h"
#include <VirtualListView.h>

//
// The COM of the program: the classes a DLL serves (DllGetClassObject: their CLSIDs, the functions that make them, what this
// computer's registry has of them), and the calls that pass the GUIDs of Windows (those the Win32 metadata names): the
// classes it creates (CoCreateInstance(&CLSID_X, ..., &IID_Y, ...)), the interfaces it asks for (QueryInterface(&IID_Y,
// ...), through a pointer), with the function each call is in. Double-click goes to the CLSID or the call; the commands of
// functions are for its function (the class's maker, the call's). It is there once the cross-references are.
//
class CComView :
	public CViewBase<CComView>,
	public CVirtualListView<CComView> {
public:
	CComView(IMainFrame* frame, PEFile const& pe);
	CString GetTitle() const override;
	void XrefsChanged() override;
	void AnnotationsChanged() override;

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	bool OnDoubleClickList(HWND, int row, int col, CPoint const& pt) const;
	bool OnRightClickList(HWND, int row, int col, POINT const& pt) const;
	void OnStateChanged(HWND, int from, int to, UINT oldState, UINT newState) const;
	void UpdateUI(bool first = false) const;

	BEGIN_MSG_MAP(CComView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CComView>)
		CHAIN_MSG_MAP(CViewBase<CComView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_VIEW_DISASSEMBLE, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_DECOMPILE, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_SIDEBYSIDE, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_XREFS, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_FLOWGRAPH, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_CALLGRAPH, OnFunctionCommand)
		CHAIN_MSG_MAP_ALT(CViewBase<CComView>, 1)
	END_MSG_MAP()

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnFunctionCommand(WORD, WORD id, HWND, BOOL&) const;
	void Fill();
	std::wstring Names(std::vector<DWORD> const& guids) const;
	// the function of the row selected (of the list with the focus), 0 if none
	DWORD SelectedFunction() const;

	CSplitterWindowT<false> m_Splitter;
	CListViewCtrl m_Classes;
	std::vector<ComServerClass> m_ClassItems;
	CListViewCtrl m_List;
	PEFile const& m_PE;
	std::vector<ComUse> m_Items;
};
