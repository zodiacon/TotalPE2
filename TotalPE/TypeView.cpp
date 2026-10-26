#include "pch.h"
#include "TypeView.h"
#include <ClipboardHelper.h>
#include <DpiHelper.h>
#include <ListViewhelper.h>
#include "resource.h"
#include "TypeExporter.h"

namespace {
	// arrays can be large: only the start is shown
	constexpr uint32_t MaxArrayElements = 1024;
}

CTypeView::CTypeView(IMainFrame* frame, DiaSymbol const& type) : CViewBase(frame), m_Type(type) {
}

CString CTypeView::GetTitle() const {
	return m_Type.Name().c_str();
}

UINT CTypeView::TypeIcon(DiaSymbol const& type) {
	switch (type.Tag()) {
		case SymbolTag::Enum:
			return IDI_ENUM;
		case SymbolTag::UDT:
			switch (type.UdtKind()) {
				case UdtType::Class: return IDI_CLASS;
				case UdtType::Union: return IDI_UNION;
			}
			return IDI_TYPE;
		case SymbolTag::ArrayType:
			return IDI_ARRAY;
	}
	return IDI_FIELD;
}

// The name of a type. A function type has no name of its own: Name() gives its return type.
std::wstring CTypeView::TypeName(DiaSymbol const& type) {
	return type ? type.Name() : std::wstring();
}

std::wstring CTypeView::KindName(DiaSymbol const& type) {
	switch (type.Tag()) {
		case SymbolTag::Enum:
			return L"enum";
		case SymbolTag::UDT:
			switch (type.UdtKind()) {
				case UdtType::Class: return L"class";
				case UdtType::Union: return L"union";
			}
			return L"struct";
	}
	return L"";
}

// A struct, class, union or enum has something to show; an array if its elements do
bool CTypeView::IsExpandable(DiaSymbol const& type) {
	if (!type)
		return false;
	switch (type.Tag()) {
		case SymbolTag::UDT:
		case SymbolTag::Enum:
			return true;
		case SymbolTag::ArrayType:
			return type.Count() > 0 && IsExpandable(type.Type());
	}
	return false;
}

std::wstring CTypeView::VariantToString(VARIANT const& value) {
	switch (value.vt) {
		case VT_I1: return std::format(L"{}", (int)value.cVal);
		case VT_I2: return std::format(L"{}", value.iVal);
		case VT_I4: case VT_INT: return std::format(L"{}", value.lVal);
		case VT_I8: return std::format(L"{}", value.llVal);
		case VT_UI1: return std::format(L"{}", value.bVal);
		case VT_UI2: return std::format(L"{}", value.uiVal);
		case VT_UI4: case VT_UINT: return std::format(L"{} (0x{:X})", value.ulVal, value.ulVal);
		case VT_UI8: return std::format(L"{} (0x{:X})", value.ullVal, value.ullVal);
		case VT_BOOL: return value.boolVal ? L"true" : L"false";
	}
	return L"";
}

HTLITEM CTypeView::AddItem(HTLITEM hParent, std::wstring const& name, UINT icon, DiaSymbol const& expand, uint32_t offset) {
	LPARAM data = 0;
	if (IsExpandable(expand)) {
		m_Nodes.push_back({ expand, offset });
		data = (LPARAM)m_Nodes.size();
	}
	auto hItem = m_TL.InsertItem(hParent, TLV_LAST, name.c_str(), Frame()->GetIconIndex(icon), data);
	if (data) {
		// its children are added when it is first expanded (an item is created expanded)
		m_TL.SetHasChildren(hItem, true);
		m_TL.Expand(hItem, false);
	}
	return hItem;
}

void CTypeView::Fill(HTLITEM hParent, DiaSymbol const& type, uint32_t offset) {
	switch (type.Tag()) {
		case SymbolTag::UDT: FillUdt(hParent, type, offset); break;
		case SymbolTag::Enum: FillEnum(hParent, type); break;
		case SymbolTag::ArrayType: FillArray(hParent, type, offset); break;
	}
}

// Base classes and data members in the order of the symbols, then the member functions
void CTypeView::FillUdt(HTLITEM hParent, DiaSymbol const& udt, uint32_t offset) {
	std::vector<DiaSymbol> functions;
	for (auto const& child : udt.FindChildren()) {
		switch (child.Tag()) {
			case SymbolTag::BaseClass:
			{
				auto base = child.Type() ? child.Type() : child;
				bool isVirtual = child.IsVirtual();
				auto hItem = AddItem(hParent, child.Name(), TypeIcon(base), base, isVirtual ? 0 : offset + child.Offset());
				if (!isVirtual)
					m_TL.SetItemText(hItem, Column::Offset, std::format(L"+0x{:X}", offset + child.Offset()).c_str());
				if (auto size = base.Length())
					m_TL.SetItemText(hItem, Column::Size, std::format(L"0x{:X}", size).c_str());
				m_TL.SetItemText(hItem, Column::Type, KindName(base).c_str());
				m_TL.SetItemText(hItem, Column::Details, isVirtual ? L"virtual base class" : L"base class");
				break;
			}

			case SymbolTag::Data:
				AddMember(hParent, child, offset);
				break;

			case SymbolTag::Function:
				functions.push_back(child);
				break;
		}
	}

	for (auto const& f : functions) {
		auto hItem = AddItem(hParent, f.Name() + L"()", IDI_FUNCTION, DiaSymbol::Empty, 0);
		if (auto type = f.Type())
			m_TL.SetItemText(hItem, Column::Type, TypeName(type.Type()).c_str());	// the return type
		std::wstring details;
		if (f.IsPureVirtual())
			details = L"pure virtual";
		else if (f.IsVirtual())
			details = L"virtual";
		if (f.IsCompilerGenerated())
			details += details.empty() ? L"compiler generated" : L", compiler generated";
		m_TL.SetItemText(hItem, Column::Details, details.c_str());
	}
}

HTLITEM CTypeView::AddMember(HTLITEM hParent, DiaSymbol const& data, uint32_t offset) {
	auto type = data.Type();
	auto location = data.Location();
	bool isStatic = location == LocationKind::Static || location == LocationKind::Constant || location == LocationKind::TLS;
	auto memberOffset = isStatic ? 0 : offset + data.Offset();

	UINT icon = location == LocationKind::BitField ? IDI_BITFIELD : TypeIcon(type);
	auto hItem = AddItem(hParent, data.Name(), icon, type, memberOffset);
	if (!isStatic)
		m_TL.SetItemText(hItem, Column::Offset, std::format(L"+0x{:X}", memberOffset).c_str());
	m_TL.SetItemText(hItem, Column::Type, TypeName(type).c_str());

	std::wstring size, details;
	if (location == LocationKind::BitField) {
		auto bits = data.Length();
		size = std::format(L"{} bit{}", bits, bits == 1 ? L"" : L"s");
		details = std::format(L"bit {}", data.BitPosition());
	}
	else if (type && type.Length()) {
		size = std::format(L"0x{:X}", type.Length());
	}
	if (location == LocationKind::Constant) {
		auto value = data.Value();
		details = L"static const = " + VariantToString(value);
		::VariantClear(&value);
	}
	else if (isStatic) {
		details = L"static";
	}
	m_TL.SetItemText(hItem, Column::Size, size.c_str());
	m_TL.SetItemText(hItem, Column::Details, details.c_str());
	return hItem;
}

void CTypeView::FillEnum(HTLITEM hParent, DiaSymbol const& type) {
	for (auto const& value : type.FindChildren(nullptr, SymbolTag::Data)) {
		auto hItem = AddItem(hParent, value.Name(), IDI_FIELD, DiaSymbol::Empty, 0);
		auto v = value.Value();
		m_TL.SetItemText(hItem, Column::Details, (L"= " + VariantToString(v)).c_str());
		::VariantClear(&v);
	}
}

void CTypeView::FillArray(HTLITEM hParent, DiaSymbol const& array, uint32_t offset) {
	auto element = array.Type();
	auto count = array.Count();
	auto size = element.Length();
	auto name = TypeName(element);
	auto icon = TypeIcon(element);
	for (uint32_t i = 0; i < count && i < MaxArrayElements; i++) {
		auto elementOffset = offset + (uint32_t)(i * size);
		auto hItem = AddItem(hParent, std::format(L"[{}]", i), icon, element, elementOffset);
		m_TL.SetItemText(hItem, Column::Offset, std::format(L"+0x{:X}", elementOffset).c_str());
		m_TL.SetItemText(hItem, Column::Size, std::format(L"0x{:X}", size).c_str());
		m_TL.SetItemText(hItem, Column::Type, name.c_str());
	}
	if (count > MaxArrayElements)
		AddItem(hParent, std::format(L"... ({} more)", count - MaxArrayElements), IDI_ARRAY, DiaSymbol::Empty, 0);
}

LRESULT CTypeView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_TL.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
	// the image list is the frame's: not to be destroyed with the control
	m_TL.GetListCtrl().ModifyStyle(0, LVS_SHAREIMAGELISTS);
	m_TL.SetImageList(Frame()->GetImageList(), LVSIL_SMALL);

	m_TL.InsertColumn(Column::Member, L"Member", DpiHelper::Scale(280, m_TL));
	m_TL.InsertColumn(Column::Offset, L"Offset", DpiHelper::Scale(80, m_TL), LVCFMT_RIGHT);
	m_TL.InsertColumn(Column::Size, L"Size", DpiHelper::Scale(70, m_TL), LVCFMT_RIGHT);
	m_TL.InsertColumn(Column::Type, L"Type", DpiHelper::Scale(250, m_TL));
	m_TL.InsertColumn(Column::Details, L"Details", DpiHelper::Scale(200, m_TL));
	ListViewHelper::EnableColumnDpiScaling(m_TL.GetListCtrl());

	m_TL.BeginUpdate();
	auto hRoot = m_TL.InsertItem(TLV_ROOT, TLV_LAST, m_Type.Name().c_str(), Frame()->GetIconIndex(TypeIcon(m_Type)));
	if (auto size = m_Type.Length())
		m_TL.SetItemText(hRoot, Column::Size, std::format(L"0x{:X}", size).c_str());
	m_TL.SetItemText(hRoot, Column::Type, KindName(m_Type).c_str());
	Fill(hRoot, m_Type, 0);
	m_TL.EndUpdate();
	m_TL.SelectItem(hRoot);

	return 0;
}

LRESULT CTypeView::OnItemExpanding(int, LPNMHDR hdr, BOOL&) {
	auto nm = (NMTLVEXPAND*)hdr;
	if (!nm->bExpanded)
		return 0;
	auto index = m_TL.GetItemData(nm->hItem);
	if (index == 0 || index > (LPARAM)m_Nodes.size())
		return 0;
	auto& node = m_Nodes[index - 1];
	if (node.Filled)
		return 0;
	node.Filled = true;

	CWaitCursor wait;
	m_TL.BeginUpdate();
	Fill(nm->hItem, node.Type, node.Offset);
	m_TL.EndUpdate();
	if (!m_TL.GetChildItem(nm->hItem))
		m_TL.SetHasChildren(nm->hItem, false);
	return 0;
}

// The type of the item (the root is the type of the view; a member, its own type) can be saved as a definition
LRESULT CTypeView::OnRightClick(int, LPNMHDR hdr, BOOL&) {
	if (hdr->hwndFrom != m_TL.m_hWnd)
		return 0;
	POINT pt;
	::GetCursorPos(&pt);
	POINT client(pt);
	m_TL.ScreenToClient(&client);
	auto hItem = m_TL.HitTest(client);
	if (!hItem)
		return 0;
	m_TL.SelectItem(hItem);

	auto type = DiaSymbol::Empty;
	if (hItem == m_TL.GetRootItem())
		type = m_Type;
	else if (auto index = m_TL.GetItemData(hItem); index > 0 && index <= (LPARAM)m_Nodes.size())
		type = TypeExporter::ExportableType(m_Nodes[index - 1].Type);
	if (!type)
		return 0;

	TypeExporter::ShowMenu(Frame(), m_hWnd, type, pt);
	return 0;
}

LRESULT CTypeView::OnSelChanged(int, LPNMHDR, BOOL&) {
	UpdateUI();
	return 0;
}

void CTypeView::UpdateUI(bool) const {
	Frame()->GetUI().UIEnable(ID_EDIT_COPY, m_TL.GetSelectedItem() != nullptr);
}

LRESULT CTypeView::OnCopy(WORD, WORD, HWND, BOOL&) const {
	auto hItem = m_TL.GetSelectedItem();
	if (!hItem)
		return 0;
	CString text;
	for (int c = 0; c < m_TL.GetColumnCount(); c++) {
		if (c)
			text += L"\t";
		text += m_TL.GetItemText(hItem, c);
	}
	ClipboardHelper::CopyText(m_hWnd, text);
	return 0;
}
