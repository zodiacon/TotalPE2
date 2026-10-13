#include "pch.h"
#include "ClrTypesView.h"
#include <ClrIL.h>
#include <PEFile.h>

namespace {
	// a type's name without the number of generic parameters ("List`1" -> "List")
	std::wstring WithoutArity(std::wstring const& name) {
		std::wstring text;
		for (size_t i = 0; i < name.size(); i++) {
			if (name[i] == L'`' && i + 1 < name.size() && iswdigit(name[i + 1])) {
				while (i + 1 < name.size() && iswdigit(name[i + 1]))
					i++;
				continue;
			}
			text += name[i];
		}
		return text;
	}

	// The text of a signature for the tree: without "class", "valuetype", "instance" and the assemblies of the types
	std::wstring Simplify(std::wstring const& text) {
		std::wstring result;
		auto boundary = [&](size_t i) { return i == 0 || !(iswalnum(text[i - 1]) || text[i - 1] == L'_' || text[i - 1] == L'.'); };
		for (size_t i = 0; i < text.size(); ) {
			bool removed = false;
			for (auto word : { L"class ", L"valuetype ", L"instance " }) {
				auto len = wcslen(word);
				if (boundary(i) && text.compare(i, len, word) == 0) {
					i += len;
					removed = true;
					break;
				}
			}
			if (removed)
				continue;
			// "[mscorlib]" before a type, but not "[]" or "[0...3]" of an array
			if (text[i] == L'[' && boundary(i) && i + 1 < text.size() && (iswalpha(text[i + 1]) || text[i + 1] == L'.')) {
				if (auto end = text.find(L']', i); end != std::wstring::npos) {
					i = end + 1;
					continue;
				}
			}
			result += text[i++];
		}
		return WithoutArity(result);
	}

	std::wstring Lower(std::wstring text) {
		for (auto& c : text)
			c = towlower(c);
		return text;
	}

	std::string Utf8(std::wstring const& text) {
		int len = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
		std::string result(len, '\0');
		::WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), result.data(), len, nullptr, nullptr);
		return result;
	}

	// the most types a filter shows, so that the tree stays quick
	constexpr size_t MaxFilteredTypes = 3000;
}

CClrTypesView::CClrTypesView(IMainFrame* frame, PEFile const& pe) : CViewBase(frame), m_PE(pe) {
}

CString CClrTypesView::GetTitle() const {
	return L".NET Types";
}

void CClrTypesView::UpdateUI(bool) {
	auto text = std::format(L"{} types, {} methods, {} fields", m_Md.Types().size(), m_Md.Methods().size(), m_Md.Fields().size());
	if (!m_FilterText.empty())
		text += std::format(L" ({} types match{})", m_Shown, m_Shown >= MaxFilteredTypes ? L", the first ones shown" : L"");
	Frame()->SetStatusText(1, text.c_str());
}

LRESULT CClrTypesView::OnCreate(UINT, WPARAM, LPARAM, BOOL& handled) {
	// the filter, in a band of its own (the rebar passes its notifications on to this window)
	CreateSimpleReBar(ATL_SIMPLE_REBAR_NOBORDER_STYLE);
	CRect rc(0, 0, 300, 20);
	m_Filter.Create(m_hWnd, rc, nullptr, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL);
	m_Filter.SetFont(AtlGetDefaultGuiFont());
	m_Filter.SetWatermark(L"Filter types and members (Esc to clear)");
	AddSimpleReBarBand(m_Filter, nullptr, FALSE, 300);

	m_hWndClient = m_Splitter.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	m_Tree.Create(m_Splitter, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS);
	m_Tree.SetExtendedStyle(TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);
	m_Tree.SetImageList(Frame()->GetImageList(), TVSIL_NORMAL);

	m_Code = new CScintillaView(Frame(), m_PE, GetTitle());
	m_Code->DoCreate(m_Splitter);
	m_Code->SetLanguage(LexLanguage::IL);
	m_Code->GetCtrl().SetCodePage(SC_CP_UTF8);
	m_Code->GetCtrl().SetReadOnly(true);
	m_Code->GetCtrl().SendMessage(SCI_SETTABWIDTH, 4);
	m_Code->SetLineAction([this](int line) { return GoToLine(line); });

	m_Splitter.SetSplitterPanes(m_Tree, *m_Code);
	m_Splitter.SetSplitterPosPct(25);

	{
		CWaitCursor wait;
		m_Md.Parse(m_PE);
		Index();
		Fill();
	}
	// the entry point, if there is one
	if (auto entry = m_Md.EntryPointMethod())
		SelectToken((ClrTableId::MethodDef << 24) | entry);

	handled = FALSE;	// the base registers the view for idle handling and the message filter
	return 0;
}

LRESULT CClrTypesView::OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
	m_Tree.SetFocus();
	return 0;
}

LRESULT CClrTypesView::OnForward(UINT msg, WPARAM wp, LPARAM lp, BOOL&) {
	return m_Code ? m_Code->SendMessage(msg, wp, lp) : 0;
}

void CClrTypesView::Index() {
	auto const& types = m_Md.Types();
	m_NestedTypes.assign(types.size() + 1, {});
	std::vector<uint32_t> outer;
	for (uint32_t t = 1; t <= types.size(); t++) {
		if (auto enclosing = types[t - 1].Enclosing)
			m_NestedTypes[enclosing].push_back(t);
		else
			outer.push_back(t);
	}
	for (auto const& t : outer)
		m_Namespaces.push_back(types[t - 1].Namespace);
	std::sort(m_Namespaces.begin(), m_Namespaces.end());
	m_Namespaces.erase(std::unique(m_Namespaces.begin(), m_Namespaces.end()), m_Namespaces.end());
	m_Types.assign(m_Namespaces.size(), {});
	for (auto t : outer) {
		auto it = std::lower_bound(m_Namespaces.begin(), m_Namespaces.end(), types[t - 1].Namespace);
		m_Types[it - m_Namespaces.begin()].push_back(t);
	}
	auto byName = [&](uint32_t a, uint32_t b) { return _wcsicmp(types[a - 1].Name.c_str(), types[b - 1].Name.c_str()) < 0; };
	for (auto& list : m_Types)
		std::stable_sort(list.begin(), list.end(), byName);
	for (auto& list : m_NestedTypes)
		std::stable_sort(list.begin(), list.end(), byName);
}

bool CClrTypesView::Matches(std::wstring const& name) const {
	return m_FilterText.empty() || Lower(name).find(m_FilterText) != std::wstring::npos;
}

bool CClrTypesView::TypeMatches(uint32_t type) const {
	return Matches(m_Md.FullName(type));
}

bool CClrTypesView::AnyMemberMatches(uint32_t type) const {
	for (auto m : m_Md.MethodsOf(type))
		if (Matches(m_Md.Methods()[m - 1].Name))
			return true;
	for (auto f : m_Md.FieldsOf(type))
		if (Matches(m_Md.Fields()[f - 1].Name))
			return true;
	return false;
}

bool CClrTypesView::HasChildren(uint32_t type) const {
	auto& t = m_Md.Types()[type - 1];
	return (m_FilterText.empty() && !m_NestedTypes[type].empty()) || t.EndMethod > t.FirstMethod || t.EndField > t.FirstField;
}

std::wstring CClrTypesView::TypeText(uint32_t type, bool qualified) const {
	auto& t = m_Md.Types()[type - 1];
	std::wstring name = t.Name;
	if (qualified) {
		// with the types it is nested in, without the namespace
		name = m_Md.FullName(type);
		if (!t.Namespace.empty() && t.Enclosing == 0)
			name = name.substr(t.Namespace.size() + 1);
		else if (t.Enclosing) {
			auto outer = type;
			while (m_Md.Types()[outer - 1].Enclosing && outer)
				outer = m_Md.Types()[outer - 1].Enclosing;
			auto& ns = m_Md.Types()[outer - 1].Namespace;
			if (!ns.empty() && name.starts_with(ns + L"."))
				name = name.substr(ns.size() + 1);
		}
	}
	name = WithoutArity(name);
	auto params = m_Md.GenericParamNames((ClrTableId::TypeDef << 24) | type);
	if (!params.empty()) {
		name += L"<";
		for (size_t i = 0; i < params.size(); i++)
			name += (i ? L", " : L"") + params[i];
		name += L">";
	}
	return name;
}

std::wstring CClrTypesView::MethodText(uint32_t method) const {
	return Simplify(m_Md.MethodSignature(method));
}

std::wstring CClrTypesView::FieldText(uint32_t field) const {
	return Simplify(m_Md.FieldSignature(field));
}

int CClrTypesView::TypeImage(uint32_t type) const {
	auto kind = m_Md.TypeKind(type);
	UINT icon = kind == L"interface" ? IDI_INTERFACE : kind == L"enum" ? IDI_ENUM : kind == L"struct" ? IDI_TYPE :
		kind == L"delegate" ? IDI_FUNCTION2 : IDI_CLASS;
	return Frame()->GetIconIndex(icon);
}

HTREEITEM CClrTypesView::AddType(HTREEITEM hParent, uint32_t type, bool qualified) {
	TVINSERTSTRUCT tvis{};
	tvis.hParent = hParent;
	tvis.hInsertAfter = TVI_LAST;
	auto text = TypeText(type, qualified);
	auto image = TypeImage(type);
	tvis.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_CHILDREN;
	tvis.item.pszText = (PWSTR)text.c_str();
	tvis.item.iImage = tvis.item.iSelectedImage = image;
	tvis.item.lParam = Data(Kind::Type, type);
	tvis.item.cChildren = HasChildren(type) ? 1 : 0;
	return m_Tree.InsertItem(&tvis);
}

void CClrTypesView::Fill() {
	m_Tree.SetRedraw(FALSE);
	m_Tree.DeleteAllItems();
	m_Shown = 0;
	auto folder = Frame()->GetIconIndex(IDI_DIR_OPEN);
	auto addNamespace = [&](size_t index) {
		TVINSERTSTRUCT tvis{};
		tvis.hParent = TVI_ROOT;
		tvis.hInsertAfter = TVI_LAST;
		auto& name = m_Namespaces[index];
		tvis.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_CHILDREN;
		tvis.item.pszText = (PWSTR)(name.empty() ? L"(no namespace)" : name.c_str());
		tvis.item.iImage = tvis.item.iSelectedImage = folder;
		tvis.item.lParam = Data(Kind::Namespace, (uint32_t)index);
		tvis.item.cChildren = 1;
		return m_Tree.InsertItem(&tvis);
	};

	for (size_t i = 0; i < m_Namespaces.size(); i++) {
		if (m_FilterText.empty()) {
			addNamespace(i);
			continue;
		}
		// the types (nested ones as well) whose names match, or that have members that do
		std::vector<uint32_t> matching;
		std::function<void(uint32_t)> visit = [&](uint32_t type) {
			if (m_Shown + matching.size() >= MaxFilteredTypes)
				return;
			if (TypeMatches(type) || AnyMemberMatches(type))
				matching.push_back(type);
			for (auto nested : m_NestedTypes[type])
				visit(nested);
		};
		for (auto type : m_Types[i])
			visit(type);
		if (matching.empty())
			continue;
		m_Shown += matching.size();
		auto hNamespace = addNamespace(i);
		for (auto type : matching)
			AddType(hNamespace, type, true);
		m_Tree.Expand(hNamespace, TVE_EXPAND);
	}
	m_Tree.SetRedraw(TRUE);
	UpdateUI();
}

void CClrTypesView::Populate(HTREEITEM hItem) {
	if (!hItem || m_Tree.GetChildItem(hItem))
		return;
	auto data = m_Tree.GetItemData(hItem);
	auto index = IndexOf(data);
	switch (KindOf(data)) {
		case Kind::Namespace:
			if (index < m_Types.size())
				for (auto type : m_Types[index])
					AddType(hItem, type, false);
			break;

		case Kind::Type:
		{
			// with a filter, the nested types are in the namespace, and only the members that match are shown (all of them
			// if the type's name matches)
			bool all = m_FilterText.empty() || TypeMatches(index);
			if (m_FilterText.empty())
				for (auto nested : m_NestedTypes[index])
					AddType(hItem, nested, false);
			auto addMember = [&](Kind kind, uint32_t row, std::wstring const& text, int image) {
				TVINSERTSTRUCT tvis{};
				tvis.hParent = hItem;
				tvis.hInsertAfter = TVI_LAST;
				tvis.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
				tvis.item.pszText = (PWSTR)text.c_str();
				tvis.item.iImage = tvis.item.iSelectedImage = image;
				tvis.item.lParam = Data(kind, row);
				m_Tree.InsertItem(&tvis);
			};
			auto field = Frame()->GetIconIndex(IDI_FIELD), constant = Frame()->GetIconIndex(IDI_DATA);
			for (auto f : m_Md.FieldsOf(index))
				if (all || Matches(m_Md.Fields()[f - 1].Name))
					addMember(Kind::Field, f, FieldText(f), (m_Md.Fields()[f - 1].Flags & 0x40) ? constant : field);
			auto method = Frame()->GetIconIndex(IDI_FUNCTION);
			for (auto m : m_Md.MethodsOf(index))
				if (all || Matches(m_Md.Methods()[m - 1].Name))
					addMember(Kind::Method, m, MethodText(m), method);
			break;
		}
	}
	if (!m_Tree.GetChildItem(hItem)) {
		TVITEM item{ TVIF_CHILDREN };
		item.hItem = hItem;
		m_Tree.SetItem(&item);
	}
}

LRESULT CClrTypesView::OnItemExpanding(int, LPNMHDR hdr, BOOL&) {
	auto tv = (NMTREEVIEW*)hdr;
	if (tv->hdr.hwndFrom == m_Tree && tv->action == TVE_EXPAND) {
		m_Tree.SetRedraw(FALSE);
		Populate(tv->itemNew.hItem);
		m_Tree.SetRedraw(TRUE);
	}
	return FALSE;
}

LRESULT CClrTypesView::OnSelChanged(int, LPNMHDR hdr, BOOL&) {
	if (hdr->hwndFrom == m_Tree)
		Show(m_Tree.GetSelectedItem());
	return 0;
}

LRESULT CClrTypesView::OnFilterChanged(WORD, WORD, HWND hWnd, BOOL& handled) {
	if (hWnd != m_Filter) {
		handled = FALSE;
		return 0;
	}
	CString text;
	m_Filter.GetWindowText(text);
	m_FilterText = Lower((PCWSTR)text);
	CWaitCursor wait;
	Fill();
	return 0;
}

void CClrTypesView::SetCode(std::wstring const& text) {
	auto& ctrl = m_Code->GetCtrl();
	ctrl.SetReadOnly(false);
	m_Code->SetText(Utf8(text).c_str());
	ctrl.SetReadOnly(true);
	ctrl.SendMessage(SCI_GOTOPOS, 0);
	ctrl.SendMessage(SCI_EMPTYUNDOBUFFER);
}

void CClrTypesView::Show(HTREEITEM hItem) {
	m_LineTokens.clear();
	if (!hItem) {
		SetCode(L"");
		return;
	}
	auto data = m_Tree.GetItemData(hItem);
	auto index = IndexOf(data);
	switch (KindOf(data)) {
		case Kind::Namespace:
		{
			// the declarations of its types
			std::wstring text;
			if (index < m_Types.size()) {
				for (auto type : m_Types[index]) {
					text += m_Md.TypeDeclaration(type) + L"\r\n";
					m_LineTokens.push_back((ClrTableId::TypeDef << 24) | type);
				}
			}
			SetCode(text);
			break;
		}
		case Kind::Type:
			SetCode(DisassembleIlType(m_PE, m_Md, index, &m_LineTokens));
			break;
		case Kind::Method:
			SetCode(DisassembleIlMethod(m_PE, m_Md, index, &m_LineTokens));
			break;
		case Kind::Field:
		{
			auto type = m_Md.Fields()[index - 1].Type;
			SetCode(m_Md.FieldDeclaration(index) + L"\r\n\r\n// in " + m_Md.TypeDeclaration(type) + L"\r\n");
			m_LineTokens = { (ClrTableId::Field << 24) | index, 0, (ClrTableId::TypeDef << 24) | type };
			break;
		}
	}
}

bool CClrTypesView::GoToLine(int line) {
	if (line < 0 || line >= (int)m_LineTokens.size() || m_LineTokens[line] == 0)
		return false;
	auto definition = m_Md.Definition(m_LineTokens[line]);
	if (!definition)
		return false;
	// the line of the selected item itself leads nowhere new
	auto selected = m_Tree.GetSelectedItem();
	if (selected) {
		auto data = m_Tree.GetItemData(selected);
		auto kind = KindOf(data);
		auto table = definition >> 24;
		if (IndexOf(data) == (definition & 0xFFFFFF) && ((kind == Kind::Method && table == ClrTableId::MethodDef) ||
			(kind == Kind::Field && table == ClrTableId::Field) || (kind == Kind::Type && table == ClrTableId::TypeDef)))
			return false;
	}
	return SelectToken(definition);
}

HTREEITEM CClrTypesView::FindChild(HTREEITEM hParent, LPARAM data) {
	auto hItem = hParent ? m_Tree.GetChildItem(hParent) : m_Tree.GetRootItem();
	for (; hItem; hItem = m_Tree.GetNextSiblingItem(hItem))
		if (m_Tree.GetItemData(hItem) == (DWORD_PTR)data)
			return hItem;
	return nullptr;
}

bool CClrTypesView::SelectToken(uint32_t token) {
	auto row = token & 0xFFFFFF;
	uint32_t type = 0;
	LPARAM member = 0;
	switch (token >> 24) {
		case ClrTableId::TypeDef:
			type = row;
			break;
		case ClrTableId::MethodDef:
			if (row == 0 || row > m_Md.Methods().size())
				return false;
			type = m_Md.Methods()[row - 1].Type;
			member = Data(Kind::Method, row);
			break;
		case ClrTableId::Field:
			if (row == 0 || row > m_Md.Fields().size())
				return false;
			type = m_Md.Fields()[row - 1].Type;
			member = Data(Kind::Field, row);
			break;
	}
	if (type == 0 || type > m_Md.Types().size())
		return false;

	// the filter may hide it
	if (!m_FilterText.empty()) {
		m_FilterText.clear();
		m_Filter.SetWindowText(L"");
		Fill();
	}

	// the types it is nested in, outermost first
	std::vector<uint32_t> chain{ type };
	while (m_Md.Types()[chain.back() - 1].Enclosing && chain.size() < 32)
		chain.push_back(m_Md.Types()[chain.back() - 1].Enclosing);
	std::reverse(chain.begin(), chain.end());
	auto& ns = m_Md.Types()[chain[0] - 1].Namespace;
	auto it = std::lower_bound(m_Namespaces.begin(), m_Namespaces.end(), ns);
	if (it == m_Namespaces.end() || *it != ns)
		return false;

	m_Tree.SetRedraw(FALSE);
	auto hItem = FindChild(nullptr, Data(Kind::Namespace, (uint32_t)(it - m_Namespaces.begin())));
	for (auto t : chain) {
		Populate(hItem);
		hItem = FindChild(hItem, Data(Kind::Type, t));
		if (!hItem)
			break;
	}
	if (hItem && member) {
		Populate(hItem);
		hItem = FindChild(hItem, member);
	}
	m_Tree.SetRedraw(TRUE);
	if (!hItem)
		return false;
	m_Tree.SelectItem(hItem);
	m_Tree.EnsureVisible(hItem);
	return true;
}
