#pragma once

#include "ViewBase.h"
#include <TreeListView.h>
#include <DiaHelper.h>
#include <deque>

// The layout of a type from the symbols (a struct, class, union or enum): its members, base classes and functions.
// Members of a type of their own (structs, enums, arrays of them) are expanded on demand.
class CTypeView : public CViewBase<CTypeView> {
public:
	CTypeView(IMainFrame* frame, DiaSymbol const& type);

	CString GetTitle() const override;
	void UpdateUI(bool first = false) const;

	// the icon of a type: class, struct, union or enum
	static UINT TypeIcon(DiaSymbol const& type);

	BEGIN_MSG_MAP(CTypeView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		NOTIFY_CODE_HANDLER(TLVN_ITEMEXPANDING, OnItemExpanding)
		NOTIFY_CODE_HANDLER(TLVN_SELCHANGED, OnSelChanged)
		CHAIN_MSG_MAP(CViewBase<CTypeView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		CHAIN_MSG_MAP_ALT(CViewBase<CTypeView>, 1)
	END_MSG_MAP()

private:
	enum Column {
		Member, Offset, Size, Type, Details,
	};

	// a type to expand under an item, at an offset from the start of the root type
	struct Node {
		DiaSymbol Type;
		uint32_t Offset;
		bool Filled{ false };
	};

	void Fill(HTLITEM hParent, DiaSymbol const& type, uint32_t offset);
	void FillUdt(HTLITEM hParent, DiaSymbol const& udt, uint32_t offset);
	void FillEnum(HTLITEM hParent, DiaSymbol const& type);
	void FillArray(HTLITEM hParent, DiaSymbol const& array, uint32_t offset);
	HTLITEM AddMember(HTLITEM hParent, DiaSymbol const& data, uint32_t offset);
	HTLITEM AddItem(HTLITEM hParent, std::wstring const& name, UINT icon, DiaSymbol const& expand, uint32_t offset);

	static bool IsExpandable(DiaSymbol const& type);
	static std::wstring TypeName(DiaSymbol const& type);
	static std::wstring KindName(DiaSymbol const& type);
	static std::wstring VariantToString(VARIANT const& value);

	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnItemExpanding(int, LPNMHDR, BOOL&);
	LRESULT OnSelChanged(int, LPNMHDR, BOOL&);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;

	CTreeListViewCtrl m_TL;
	DiaSymbol m_Type;
	std::deque<Node> m_Nodes;	// the item data of an item to expand is its index + 1
};
