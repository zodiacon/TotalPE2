#pragma once

#include "ViewBase.h"
#include "resource.h"
#include "ScintillaView.h"
#include <QuickFindEdit.h>
#include <ClrMetadata.h>

class PEFile;

//
// The types of a .NET file by namespace, with their nested types, fields, events, properties (with their accessors) and
// methods, and the code of the one selected, decompiled to C# or as IL (of a type: all of it). Double-click or Enter on a
// line of the code goes to the definition of what it refers to, when the file defines it. The filter shows the types and
// members whose names contain its text.
//
class CClrTypesView : public CViewBase<CClrTypesView> {
public:
	CClrTypesView(IMainFrame* frame, PEFile const& pe);

	CString GetTitle() const override;
	void UpdateUI(bool first = false);
	// Selects a type, method or field of the file (a TypeDef, MethodDef or Field token). False if there is no such one.
	bool SelectToken(uint32_t token);

	BEGIN_MSG_MAP(CClrTypesView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(CFindReplaceDialog::GetFindReplaceMsg(), OnForward)
		NOTIFY_CODE_HANDLER(TVN_ITEMEXPANDING, OnItemExpanding)
		NOTIFY_CODE_HANDLER(TVN_SELCHANGED, OnSelChanged)
		COMMAND_CODE_HANDLER(EN_DELAYCHANGE, OnFilterChanged)
		COMMAND_ID_HANDLER(ID_CLR_CSHARP, OnLanguage)
		COMMAND_ID_HANDLER(ID_CLR_IL, OnLanguage)
		CHAIN_MSG_MAP(CViewBase<CClrTypesView>)
	ALT_MSG_MAP(1)
		// the frame's commands (copy, find) are for the IL when the user is in it
		if (uMsg == WM_COMMAND && m_Code && ::GetFocus() == m_Code->GetCtrl().m_hWnd &&
			m_Code->ProcessWindowMessage(hWnd, uMsg, wParam, lParam, lResult, 1))
			return TRUE;
		CHAIN_MSG_MAP_ALT(CViewBase<CClrTypesView>, 1)
	END_MSG_MAP()

private:
	// what a node of the tree is: the kind in the top bits of its data, the index (of a namespace) or row in the others
	enum class Kind : uint32_t {
		Namespace = 1, Type, Method, Field, Property, Event,
	};
	static LPARAM Data(Kind kind, uint32_t index) {
		return ((LPARAM)kind << 28) | index;
	}
	static Kind KindOf(LPARAM data) {
		return (Kind)((uint32_t)data >> 28);
	}
	static uint32_t IndexOf(LPARAM data) {
		return (uint32_t)data & 0x0FFFFFFF;
	}

	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetFocus(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnForward(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnItemExpanding(int, LPNMHDR, BOOL&);
	LRESULT OnSelChanged(int, LPNMHDR, BOOL&);
	LRESULT OnFilterChanged(WORD, WORD, HWND, BOOL&);
	LRESULT OnLanguage(WORD, WORD id, HWND, BOOL&);

	void Index();
	void Fill();
	// adds the children of a namespace or type, if they are not there yet
	void Populate(HTREEITEM hItem);
	HTREEITEM AddType(HTREEITEM hParent, uint32_t type, bool qualified);
	bool HasChildren(uint32_t type) const;
	// a member's name matches the filter (always true without one)
	bool Matches(std::wstring const& name) const;
	bool TypeMatches(uint32_t type) const;
	bool AnyMemberMatches(uint32_t type) const;
	// the names of the tree: generic types with their parameters ("List<T>"), members without the assemblies of the types
	std::wstring TypeText(uint32_t type, bool qualified) const;
	std::wstring MethodText(uint32_t method) const;
	std::wstring FieldText(uint32_t field) const;
	// the type, method, property or event a line of the tree is (its token), 0 for a namespace
	uint32_t TokenOf(HTREEITEM hItem) const;
	int TypeImage(uint32_t type) const;
	HTREEITEM FindChild(HTREEITEM hParent, LPARAM data);
	void Show(HTREEITEM hItem);
	void SetCode(std::wstring const& text);
	bool GoToLine(int line);

	PEFile const& m_PE;
	ClrMetadata m_Md;
	CSplitterWindow m_Splitter;
	CTreeViewCtrl m_Tree;
	CScintillaView* m_Code{ nullptr };
	CQuickFindEdit m_Filter;
	CToolBarCtrl m_tb;
	bool m_CSharp{ true };	// C# or IL
	std::wstring m_FilterText;	// in lowercase
	std::vector<std::wstring> m_Namespaces;			// sorted
	std::vector<std::vector<uint32_t>> m_Types;		// the types of each namespace that are not nested, sorted by name
	std::vector<std::vector<uint32_t>> m_NestedTypes;	// by the row of the type they are nested in
	std::vector<uint32_t> m_LineTokens;				// what each line of the IL refers to
	size_t m_Shown{ 0 };	// the types the tree shows (with a filter)
};
