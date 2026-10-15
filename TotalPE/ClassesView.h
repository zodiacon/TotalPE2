#pragma once

#include "ViewBase.h"
#include "resource.h"
#include <VirtualListView.h>

//
// The C++ classes of the program that have virtual functions, from its run-time type information (MSVC's RTTI): their names,
// base classes and vtables; below, the virtual functions of the class selected (the slots of its vtables). Double-click on a
// class goes to its vtable, on a function to its code; the commands of functions (Decompile, Disassemble...) are for the
// function selected.
//
class CClassesView :
	public CViewBase<CClassesView>,
	public CVirtualListView<CClassesView> {
public:
	CClassesView(IMainFrame* frame, PEFile const& pe);
	CString GetTitle() const override;
	void AnnotationsChanged() override;

	CString GetColumnText(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	bool OnDoubleClickList(HWND, int row, int col, CPoint const& pt) const;
	bool OnRightClickList(HWND, int row, int col, POINT const& pt) const;
	void OnStateChanged(HWND, int from, int to, UINT oldState, UINT newState);
	void UpdateUI(bool first = false) const;

	BEGIN_MSG_MAP(CClassesView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CClassesView>)
		CHAIN_MSG_MAP(CViewBase<CClassesView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_VIEW_DISASSEMBLE, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_DECOMPILE, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_SIDEBYSIDE, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_XREFS, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_FLOWGRAPH, OnFunctionCommand)
		COMMAND_ID_HANDLER(ID_EXPORT_CALLGRAPH, OnFunctionCommand)
		CHAIN_MSG_MAP_ALT(CViewBase<CClassesView>, 1)
	END_MSG_MAP()

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnFunctionCommand(WORD, WORD id, HWND, BOOL&) const;
	void FillFunctions();

	struct Function {
		int Slot;
		uint32_t Rva;
		uint32_t Offset;		// of the vtable (of the part of the object)
		std::wstring Name;
		bool Inherited;			// declared by a base (its name is the base's)
	};
	CSplitterWindowT<false> m_Splitter;
	CListViewCtrl m_Classes, m_Functions;
	PEFile const& m_PE;
	std::vector<RttiClass const*> m_Items;
	std::vector<Function> m_FunctionItems;
	RttiClass const* m_Selected{ nullptr };
};
