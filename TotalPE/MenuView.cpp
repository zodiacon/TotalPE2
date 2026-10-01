#include "pch.h"
#include "MenuView.h"

namespace {
	std::wstring StripAccel(std::wstring const& text) {
		// "&File\tCtrl+F" -> "File"
		std::wstring s = text.substr(0, text.find(L'\t'));
		std::wstring result;
		for (size_t i = 0; i < s.size(); i++) {
			if (s[i] == L'&') {
				if (i + 1 < s.size() && s[i + 1] == L'&')
					result += s[i++];
				continue;
			}
			result += s[i];
		}
		return result;
	}

	std::wstring NodeText(MenuNode const& n) {
		if (n.Separator)
			return L"--------  (separator)";
		std::wstring text = n.Text;
		std::replace(text.begin(), text.end(), L'\t', L' ');
		if (text.empty())
			text = n.Popup ? L"(popup)" : L"(no text)";
		if (n.Popup)
			return text;

		text += std::format(L"   [ID {}]", n.Id);
		if (n.State & MFS_GRAYED)
			text += L" [Grayed]";
		if (n.State & MFS_CHECKED)
			text += (n.Type & MFT_RADIOCHECK) ? L" [Radio Checked]" : L" [Checked]";
		if (n.State & MFS_DEFAULT)
			text += L" [Default]";
		return text;
	}
}

CMenuView::CMenuView(IMainFrame* frame, PCWSTR title) : CViewBase(frame), m_Title(title) {
}

CMenuView::~CMenuView() {
	for (auto h : m_Popups)
		if (h)
			::DestroyMenu(h);
}

CString CMenuView::GetTitle() const {
	return m_Title;
}

bool CMenuView::SetData(std::span<const std::byte> data) {
	if (!MenuTemplate::Parse(data, m_Menu))
		return false;

	BuildTree(m_Menu.Items, TVI_ROOT);
	BuildBar();
	return true;
}

LRESULT CMenuView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_Bar.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TBSTYLE_FLAT | TBSTYLE_LIST |
		CCS_NODIVIDER | CCS_NORESIZE | CCS_NOPARENTALIGN);
	m_Bar.SetButtonStructSize();
	m_Bar.SetExtendedStyle(TBSTYLE_EX_MIXEDBUTTONS);

	m_hWndClient = m_Tree.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
		TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS | TVS_FULLROWSELECT, WS_EX_CLIENTEDGE);
	return 0;
}

LRESULT CMenuView::OnSize(UINT, WPARAM, LPARAM lParam, BOOL&) {
	int cx = GET_X_LPARAM(lParam), cy = GET_Y_LPARAM(lParam);
	int barHeight = HIWORD(m_Bar.GetButtonSize()) + 4;
	m_Bar.MoveWindow(0, 0, cx, barHeight);
	m_Tree.MoveWindow(0, barHeight, cx, std::max(0, cy - barHeight));
	return 0;
}

void CMenuView::BuildTree(std::vector<MenuNode> const& nodes, HTREEITEM parent) {
	for (auto& n : nodes) {
		auto text = NodeText(n);
		auto h = m_Tree.InsertItem(text.c_str(), parent, TVI_LAST);
		if (n.Popup)
			BuildTree(n.Children, h);
	}
	if (parent != TVI_ROOT)
		m_Tree.Expand(parent, TVE_EXPAND);
}

void CMenuView::BuildBar() {
	auto& items = m_Menu.Items;
	std::vector<std::wstring> labels;
	labels.reserve(items.size());
	for (size_t i = 0; i < items.size() && i < 0xFFF; i++) {
		auto label = StripAccel(items[i].Text);
		if (items[i].Separator)
			label = L"|";
		else if (label.empty())
			label = items[i].Popup ? L"(popup)" : L"(item)";
		labels.push_back(std::move(label));
		m_Popups.push_back(items[i].Popup ? MenuTemplate::BuildPopup(items[i].Children) : nullptr);
	}
	for (size_t i = 0; i < labels.size(); i++) {
		TBBUTTON btn{};
		btn.iBitmap = I_IMAGENONE;
		btn.idCommand = FirstBarId + (int)i;
		btn.fsState = TBSTATE_ENABLED;
		btn.fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT;
		btn.iString = (INT_PTR)labels[i].c_str();
		m_Bar.AddButtons(1, &btn);
	}
	CRect rc;
	GetClientRect(&rc);
	BOOL handled;
	OnSize(WM_SIZE, 0, MAKELPARAM(rc.Width(), rc.Height()), handled);
}

LRESULT CMenuView::OnBarClick(WORD, WORD id, HWND, BOOL&) {
	size_t index = id - FirstBarId;
	if (index >= m_Popups.size() || !m_Popups[index])
		return 0;

	CRect rc;
	m_Bar.GetItemRect((int)index, &rc);
	m_Bar.ClientToScreen(&rc);
	// TPM_RETURNCMD: the chosen command is returned rather than posted, so a menu that reuses
	// one of this application's own command IDs can never trigger it
	::TrackPopupMenu(m_Popups[index], TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_NONOTIFY, rc.left, rc.bottom, 0, m_hWnd, nullptr);
	return 0;
}
