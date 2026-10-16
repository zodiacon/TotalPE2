#pragma once

#include <Win32Api.h>

//
// The namespaces of the metadata as a tree: in each, the namespaces in it, then its functions and types in folders of
// their kinds (not its constants: the search has them). A namespace with nothing but one namespace in it is shown with
// that one ("Windows.Win32"). The tree is filled as it is expanded. The entry selected is previewed (OnOpen, pin false);
// Enter or a double-click opens it in a tab that stays (pin true).
//
class CNamespacePane : public CWindowImpl<CNamespacePane> {
public:
	DECLARE_WND_CLASS_EX(L"Win32ApiExplorerNamespaces", 0, -1)

	explicit CNamespacePane(Win32Api& api);

	std::function<void(size_t entry, bool pin)> OnOpen;

	// the API of another machine (the tree is made again)
	void SetApi(Win32Api& api);
	void SetImageList(HIMAGELIST images);
	// shows an entry in the tree (its namespace and folder expanded), without opening it; false if it is not in it
	bool SelectEntry(size_t entry);
	BOOL PreTranslateMessage(MSG* pMsg);

	BEGIN_MSG_MAP(CNamespacePane)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_SETFOCUS, OnSetFocus)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		NOTIFY_CODE_HANDLER(TVN_ITEMEXPANDING, OnItemExpanding)
		NOTIFY_CODE_HANDLER(TVN_SELCHANGED, OnSelChanged)
		NOTIFY_CODE_HANDLER(NM_DBLCLK, OnDoubleClick)
	END_MSG_MAP()

private:
	enum class NodeType { Namespace, Folder, Entry };
	struct Node {
		NodeType Type;
		Win32Api::Namespace const* Namespace{ nullptr };	// of a namespace (the last of those it shows), of a folder
		int Group{ 0 };			// of a folder
		size_t Entry{ 0 };		// of an entry
		bool Filled{ false };	// (its children are in the tree)
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
	LRESULT OnItemExpanding(int, LPNMHDR, BOOL&);
	LRESULT OnSelChanged(int, LPNMHDR, BOOL&);
	LRESULT OnDoubleClick(int, LPNMHDR, BOOL&);

	void Rebuild();
	void Fill(HTREEITEM hItem);
	HTREEITEM AddNamespace(HTREEITEM hParent, Win32Api::Namespace const& ns);
	HTREEITEM AddNode(HTREEITEM hParent, std::wstring const& text, int image, int selectedImage, Node node, bool children);
	bool HasContent(Win32Api::Namespace const& ns);
	Node* NodeOf(HTREEITEM hItem);
	// the folder an entry is in (-1: none: a constant)
	static int GroupOf(Win32Api::Entry::Kinds kind);
	void OpenSelected(bool pin);

	Win32Api* m_Api;
	CTreeViewCtrl m_Tree;
	std::deque<Node> m_Nodes;		// (an item's data is its index)
	std::unordered_map<Win32Api::Namespace const*, bool> m_HasContent;
	bool m_Selecting{ false };
};
