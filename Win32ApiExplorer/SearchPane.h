#pragma once

#include <Win32Api.h>
#include <QuickFindEdit.h>
#include <VirtualListView.h>

//
// The functions, types and constants of the metadata in a list: those the filter's terms match (all of them), of the kinds
// chosen. A term is a part of the name ("file"), its start ("^Create"), its end ("W$"), all of it ("^CreateFileW$"), or a
// pattern of it ("Create*W", ? for one letter); "dll:kernel32", the DLL of a function; "ns:threading", a namespace. Case
// does not matter. A filter that is a number (5, -1, 0x80070005) shows what has that value: constants, and the values of enums
// (by their names, with their enums); a GUID shows what it names (IID_IUnknown). With a scope (SetScope), only the entries
// in it are shown (by name): what uses a type. The one selected is previewed (OnOpen, pin false); Enter or a double-click
// opens it in a tab that stays (pin true). Down or Enter in the filter goes to the list.
//
class CSearchPane :
	public CWindowImpl<CSearchPane>,
	public CVirtualListView<CSearchPane> {
public:
	DECLARE_WND_CLASS_EX(L"Win32ApiExplorerSearch", 0, -1)

	explicit CSearchPane(Win32Api& api);

	// an entry opened (name: of a value of an enum, a GUID, to show in it; empty for the entry itself)
	std::function<void(size_t entry, bool pin, std::string const& name)> OnOpen;
	// the counts changed
	std::function<void()> OnCountChanged;
	// Delete on an entry of the list (null: Delete does nothing)
	std::function<void(size_t entry)> OnDelete;

	// the API of another machine (the filter stays; the one selected, by name, if it is there)
	void SetApi(Win32Api& api);
	// the entries shown (null: all of them, and values and GUIDs can be looked for); what they are, for the filter's hint
	void SetScope(std::vector<size_t> const* entries, std::wstring const& description = {});
	void SetImageList(HIMAGELIST images);
	// the font of the filter, the kinds and the list
	void SetUIFont(HFONT font);
	void FocusFilter();
	// selects an entry (the filter is cleared if it hides it), without opening it
	bool SelectEntry(size_t entry);
	size_t GetShownCount() const;
	// of all (or of the scope)
	size_t GetTotalCount() const;
	BOOL PreTranslateMessage(MSG* pMsg);

	CString GetColumnText(HWND, int row, int col) const;
	int GetRowImage(HWND, int row, int col) const;
	void DoSort(SortInfo const* si);
	bool OnDoubleClickList(HWND, int row, int col, CPoint const& pt);

	BEGIN_MSG_MAP(CSearchPane)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		NOTIFY_CODE_HANDLER(LVN_ITEMCHANGED, OnItemChanged)
		COMMAND_CODE_HANDLER(EN_DELAYCHANGE, OnFilterChanged)
		COMMAND_CODE_HANDLER(CBN_SELCHANGE, OnKindChanged)
		CHAIN_MSG_MAP(CVirtualListView<CSearchPane>)
	END_MSG_MAP()

private:
	// a row: an entry, or a name of a value or GUID that is of it
	struct Item {
		size_t Entry;
		std::string Name;	// (empty: the entry's)
		bool operator==(Item const&) const = default;
	};

	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSetFocus(UINT, WPARAM, LPARAM, BOOL&);
	// (in the theme's color: the class's brush is the system's)
	LRESULT OnEraseBkgnd(UINT, WPARAM wp, LPARAM, BOOL&) {
		CDCHandle dc((HDC)wp);
		CRect rc;
		GetClientRect(&rc);
		dc.FillRect(&rc, COLOR_WINDOW);
		return 1;
	}
	LRESULT OnItemChanged(int, LPNMHDR, BOOL&);
	LRESULT OnFilterChanged(WORD, WORD, HWND, BOOL&);
	LRESULT OnKindChanged(WORD, WORD, HWND, BOOL&);

	void Fill();
	void UpdateLayout();
	void UpdateWatermark();
	void ClearFilter();
	bool KindShown(Win32Api::Entry::Kinds kind) const;
	Item const* SelectedItem() const;
	std::string const& NameOf(Item const& item) const;
	void Open(Item const& item, bool pin);

	Win32Api* m_Api;
	// the names, DLLs and namespaces of the entries in lowercase (the filter is not case sensitive): of an API, for all the
	// panes (made once)
	struct Lowercase {
		std::vector<std::string> Names, Where, Namespaces;
	};
	static std::shared_ptr<Lowercase const> LowercaseOf(Win32Api& api);
	std::shared_ptr<Lowercase const> m_Lowercase;
	// the filter's terms
	struct Term {
		enum class Kinds { Part, Start, End, All, Pattern } Kind;
		std::string Text;
	};
	struct Query {
		std::vector<std::string> Dlls, Namespaces;
		std::vector<Term> Names;
	};
	static Query ParseQuery(std::string const& filter);
	bool Matches(Query const& query, size_t entry) const;
	std::vector<Item> m_Items;				// shown
	std::vector<size_t> const* m_Scope{ nullptr };
	std::wstring m_ScopeDescription;
	std::wstring m_FilterText;
	std::string m_Filter;					// in lowercase
	int m_KindFilter{ 0 };
	bool m_Selecting{ false };				// (a selection that is not the user's)
	CQuickFindEdit m_FilterEdit;
	CComboBox m_Kinds;
	CListViewCtrl m_List;
};
