#include "pch.h"
#include "NamespacePane.h"
#include "Images.h"

namespace {
	using Kinds = Win32Api::Entry::Kinds;

	// the folders of a namespace, in their order
	PCWSTR const s_Groups[] = { L"Functions", L"Interfaces", L"Structs", L"Unions", L"Enums", L"Callbacks", L"Typedefs", L"Constants" };

	std::wstring Wide(std::string const& text) {
		return std::wstring(text.begin(), text.end());	// (the names of the metadata are ASCII)
	}
}

CNamespacePane::CNamespacePane(Win32Api& api) : m_Api(&api) {
}

int CNamespacePane::GroupOf(Kinds kind) {
	switch (kind) {
		case Kinds::Function: return 0;
		case Kinds::Interface: return 1;
		case Kinds::Struct: return 2;
		case Kinds::Union: return 3;
		case Kinds::Enum: return 4;
		case Kinds::Callback: return 5;
		case Kinds::Typedef: return 6;
		case Kinds::Constant: return 7;
	}
	return -1;
}

LRESULT CNamespacePane::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_Tree.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS |
		TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS | TVS_FULLROWSELECT);
	m_Tree.SetExtendedStyle(TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);
	Rebuild();
	return 0;
}

LRESULT CNamespacePane::OnSize(UINT, WPARAM, LPARAM lp, BOOL&) {
	if (m_Tree)
		m_Tree.SetWindowPos(nullptr, 0, 0, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), SWP_NOZORDER | SWP_NOACTIVATE);
	return 0;
}

LRESULT CNamespacePane::OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
	m_Tree.SetFocus();
	return 0;
}

void CNamespacePane::SetImageList(HIMAGELIST images) {
	m_Tree.SetImageList(images, TVSIL_NORMAL);
}

void CNamespacePane::SetApi(Win32Api& api) {
	m_Api = &api;
	if (m_Tree)
		Rebuild();
}

bool CNamespacePane::HasContent(Win32Api::Namespace const& ns) {
	if (auto it = m_HasContent.find(&ns); it != m_HasContent.end())
		return it->second;
	auto const& entries = m_Api->Entries();
	bool content = std::ranges::any_of(ns.Entries, [&](size_t i) { return GroupOf(entries[i].Kind) >= 0; });
	for (auto const& child : ns.Children)
		content = HasContent(child) || content;	// (each child's is remembered)
	m_HasContent[&ns] = content;
	return content;
}

void CNamespacePane::Rebuild() {
	m_Tree.SetRedraw(FALSE);
	m_Tree.DeleteAllItems();
	m_Nodes.clear();
	m_HasContent.clear();
	HTREEITEM first = nullptr;
	for (auto const& ns : m_Api->Namespaces().Children)
		if (HasContent(ns)) {
			auto item = AddNamespace(TVI_ROOT, ns);
			if (!first)
				first = item;
		}
	// (Windows.Win32: what there is to look at)
	if (first) {
		Fill(first);
		m_Tree.Expand(first, TVE_EXPAND);
	}
	m_Tree.SetRedraw(TRUE);
}

HTREEITEM CNamespacePane::AddNode(HTREEITEM hParent, std::wstring const& text, int image, int selectedImage, Node node, bool children) {
	m_Nodes.push_back(node);
	TVINSERTSTRUCT tvis{};
	tvis.hParent = hParent;
	tvis.hInsertAfter = TVI_LAST;
	tvis.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM | TVIF_CHILDREN;
	tvis.item.pszText = const_cast<PWSTR>(text.c_str());
	tvis.item.iImage = image;
	tvis.item.iSelectedImage = selectedImage;
	tvis.item.lParam = (LPARAM)(m_Nodes.size() - 1);
	tvis.item.cChildren = children ? 1 : 0;
	return m_Tree.InsertItem(&tvis);
}

// a namespace, with those it has alone in it ("Windows" has nothing but "Win32": "Windows.Win32")
HTREEITEM CNamespacePane::AddNamespace(HTREEITEM hParent, Win32Api::Namespace const& ns) {
	auto const& entries = m_Api->Entries();
	auto node = &ns;
	auto name = Wide(ns.Name);
	auto hasEntries = [&](Win32Api::Namespace const& n) {
		return std::ranges::any_of(n.Entries, [&](size_t i) { return GroupOf(entries[i].Kind) >= 0; });
	};
	while (node->Children.size() == 1 && !hasEntries(*node)) {
		node = &node->Children[0];
		name += L"." + Wide(node->Name);
	}
	return AddNode(hParent, name, Images::Namespace, Images::Namespace, Node{ NodeType::Namespace, node }, true);
}

CNamespacePane::Node* CNamespacePane::NodeOf(HTREEITEM hItem) {
	if (!hItem)
		return nullptr;
	auto index = (size_t)m_Tree.GetItemData(hItem);
	return index < m_Nodes.size() ? &m_Nodes[index] : nullptr;
}

void CNamespacePane::Fill(HTREEITEM hItem) {
	auto node = NodeOf(hItem);
	if (!node || node->Filled)
		return;
	node->Filled = true;
	// (the node may move: the deque grows at its end only, which keeps it where it is; its values are copied anyway)
	auto type = node->Type;
	auto ns = node->Namespace;
	auto group = node->Group;
	auto const& entries = m_Api->Entries();
	m_Tree.SetRedraw(FALSE);
	if (type == NodeType::Namespace) {
		for (auto const& child : ns->Children)
			if (HasContent(child))
				AddNamespace(hItem, child);
		for (int g = 0; g < (int)_countof(s_Groups); g++) {
			auto count = std::ranges::count_if(ns->Entries, [&](size_t i) { return GroupOf(entries[i].Kind) == g; });
			if (count)
				AddNode(hItem, std::format(L"{} ({})", s_Groups[g], count), Images::Folder, Images::FolderOpen,
					Node{ NodeType::Folder, ns, g }, true);
		}
	}
	else if (type == NodeType::Folder) {
		for (auto i : ns->Entries) {
			auto const& e = entries[i];
			if (GroupOf(e.Kind) != group)
				continue;
			auto image = Images::Of(e.Kind);
			AddNode(hItem, Wide(e.Name), image, image, Node{ NodeType::Entry, ns, group, i }, false);
		}
	}
	m_Tree.SetRedraw(TRUE);
}

LRESULT CNamespacePane::OnItemExpanding(int, LPNMHDR hdr, BOOL& handled) {
	if (hdr->hwndFrom != m_Tree) {
		handled = FALSE;
		return 0;
	}
	auto tv = (NMTREEVIEW*)hdr;
	if (tv->action & TVE_EXPAND)
		Fill(tv->itemNew.hItem);
	return 0;
}

LRESULT CNamespacePane::OnSelChanged(int, LPNMHDR hdr, BOOL& handled) {
	if (hdr->hwndFrom != m_Tree) {
		handled = FALSE;
		return 0;
	}
	if (!m_Selecting)
		OpenSelected(false);
	return 0;
}

LRESULT CNamespacePane::OnDoubleClick(int, LPNMHDR hdr, BOOL& handled) {
	if (hdr->hwndFrom != m_Tree) {
		handled = FALSE;
		return 0;
	}
	auto node = NodeOf(m_Tree.GetSelectedItem());
	if (!node || node->Type != NodeType::Entry)
		return 0;	// (a namespace or folder: expanded or collapsed)
	OpenSelected(true);
	return 1;
}

void CNamespacePane::OpenSelected(bool pin) {
	auto node = NodeOf(m_Tree.GetSelectedItem());
	if (node && node->Type == NodeType::Entry && OnOpen)
		OnOpen(node->Entry, pin);
}

BOOL CNamespacePane::PreTranslateMessage(MSG* pMsg) {
	if (pMsg->message == WM_KEYDOWN && pMsg->hwnd == m_Tree && pMsg->wParam == VK_RETURN) {
		OpenSelected(true);
		return TRUE;
	}
	return FALSE;
}

bool CNamespacePane::SelectEntry(size_t entry) {
	auto const& entries = m_Api->Entries();
	if (entry >= entries.size())
		return false;
	auto const& e = entries[entry];
	auto group = GroupOf(e.Kind);
	if (group < 0)
		return false;

	// down to its namespace: each item shows a namespace that is it, or that it is in
	HTREEITEM hNamespace = nullptr;
	for (auto hItem = m_Tree.GetRootItem(); hItem; ) {
		auto node = NodeOf(hItem);
		auto const& full = node && node->Type == NodeType::Namespace ? node->Namespace->FullName : std::string();
		if (!full.empty() && full == e.Namespace) {
			hNamespace = hItem;
			break;
		}
		if (!full.empty() && e.Namespace.starts_with(full + ".")) {
			Fill(hItem);
			hItem = m_Tree.GetChildItem(hItem);
			continue;
		}
		hItem = m_Tree.GetNextSiblingItem(hItem);
	}
	if (!hNamespace)
		return false;
	Fill(hNamespace);
	HTREEITEM hFolder = nullptr;
	for (auto hItem = m_Tree.GetChildItem(hNamespace); hItem && !hFolder; hItem = m_Tree.GetNextSiblingItem(hItem))
		if (auto node = NodeOf(hItem); node && node->Type == NodeType::Folder && node->Group == group)
			hFolder = hItem;
	if (!hFolder)
		return false;
	Fill(hFolder);
	HTREEITEM hEntry = nullptr;
	for (auto hItem = m_Tree.GetChildItem(hFolder); hItem && !hEntry; hItem = m_Tree.GetNextSiblingItem(hItem))
		if (auto node = NodeOf(hItem); node && node->Type == NodeType::Entry && node->Entry == entry)
			hEntry = hItem;
	if (!hEntry)
		return false;
	m_Selecting = true;
	m_Tree.SelectItem(hEntry);
	m_Tree.EnsureVisible(hEntry);
	m_Selecting = false;
	return true;
}
