#include "pch.h"
#include "resource.h"
#include "IconsView.h"
#include "ResourceContent.h"
#include "SaveData.h"

namespace {
	constexpr uint16_t TypeCursor = 1, TypeIcon = 3;
}

CIconsView::CIconsView(IMainFrame* frame, PCWSTR title) : CViewBase(frame), m_Title(title) {
}

CString CIconsView::GetTitle() const {
	return m_Title;
}

bool CIconsView::CanSave() const {
	return m_Icon || !m_Icons.empty();
}

// The group lists its images by their IDs; they are the icons (or cursors) with those IDs.
// An icon's entry has the width in a byte (0 for 256); a cursor's has it in 16 bits.
void CIconsView::SetGroupIconData(std::span<const std::byte> data) {
	if (data.size() < 6)
		return;
	auto u16 = [&](size_t at) {
		return at + 2 <= data.size() ? (uint16_t)(std::to_integer<uint8_t>(data[at]) | (std::to_integer<uint8_t>(data[at + 1]) << 8)) : (uint16_t)0;
	};
	m_GroupData.assign(data.begin(), data.end());
	m_IsIcon = u16(2) == 1;
	auto count = u16(4);

	int y = 10;
	auto const& resources = Frame()->GetFlatResources();
	for (int i = 0; i < count && 6 + (i + 1) * 14 <= (int)data.size(); i++) {
		auto at = 6 + (size_t)i * 14;
		auto id = u16(at + 12);
		auto it = std::ranges::find_if(resources, [&](auto const& r) {
			return r.TypeID == (m_IsIcon ? TypeIcon : TypeCursor) && r.NameID == id;
		});
		if (it == resources.end())
			continue;
		int width = m_IsIcon ? std::to_integer<uint8_t>(data[at]) : u16(at);
		if (width == 0)
			width = 256;
		CIconHandle icon;
		icon.m_hIcon = ::CreateIconFromResourceEx((PBYTE)it->Data.data(), (DWORD)it->Data.size(), m_IsIcon, 0x30000, width, width, LR_DEFAULTCOLOR);
		if (!icon)
			continue;
		IconData idata;
		idata.Icon = icon;
		idata.Size = width;
		idata.Id = id;
		idata.Colors = u16(at + 4) * u16(at + 6);	// planes * bit count
		idata.Data = it->Data;
		m_Icons.push_back(std::move(idata));
		y += width + 12;
	}
	SetScrollSize(450, y);
}

void CIconsView::SetIconData(std::span<const std::byte> data, bool icon) {
	// a cursor starts with its hot spot; then a BITMAPINFOHEADER (the width follows its size), or a PNG image (the width is in its header)
	size_t image = icon ? 0 : 4;
	auto bytes = (const uint8_t*)data.data();
	if (data.size() >= image + 24 && memcmp(bytes + image, "\x89PNG", 4) == 0)
		m_IconSize = (bytes[image + 16] << 24) | (bytes[image + 17] << 16) | (bytes[image + 18] << 8) | bytes[image + 19];
	else if (data.size() >= image + 8)
		m_IconSize = *(int const*)(bytes + image + 4);
	if (m_IconSize <= 0 || m_IconSize > 1024)
		m_IconSize = 32;
	m_IconData.assign(data.begin(), data.end());
	m_IsIcon = icon;
	m_Icon = ::CreateIconFromResourceEx((PBYTE)data.data(), (DWORD)data.size(), icon, 0x30000, m_IconSize, m_IconSize, LR_DEFAULTCOLOR);
	m_IconRect = CRect(10, 50, 10 + m_IconSize, 50 + m_IconSize);
	SetScrollSize(std::max<int>(500, m_IconRect.right + 10), std::max<int>(300, m_IconRect.bottom + 10));
}

// the state of Export is shared by the views (the hex view and the flow graph use the command too): it is set here for the icons
void CIconsView::UpdateUI(bool) const {
	Frame()->GetUI().UIEnable(ID_ICON_EXPORT, !m_Icons.empty() || m_Icon);
	if (!m_Icons.empty()) {
		auto kind = m_IsIcon ? L"icon" : L"cursor";
		Frame()->SetStatusText(1, m_SelectedIcon >= 0
			? std::format(L"Selected: {0} x {0} ({1} bit). Save saves this image", m_Icons[m_SelectedIcon].Size, m_Icons[m_SelectedIcon].Colors).c_str()
			: std::format(L"Click an image to select it. Save saves the whole {} group", kind).c_str());
	}
}

LRESULT CIconsView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	return 0;
}

int CIconsView::HitTest(CPoint pt) const {
	CPoint offset;
	GetScrollOffset(offset);
	pt += offset;
	if (m_Icon)
		return m_IconRect.PtInRect(pt) ? 0 : -1;
	for (int i = 0; i < (int)m_Icons.size(); i++)
		if (m_Icons[i].Rect.PtInRect(pt))
			return i;
	return -1;
}

void CIconsView::Select(int index) {
	if (m_Icon || index == m_SelectedIcon)
		return;
	m_SelectedIcon = index;
	Invalidate();
	UpdateUI();
}

// a click on an image selects it; anywhere else, nothing is selected (Save then saves the whole group)
LRESULT CIconsView::OnLeftButtonDown(UINT, WPARAM, LPARAM lp, BOOL&) {
	SetFocus();
	Select(HitTest(CPoint(GET_X_LPARAM(lp), GET_Y_LPARAM(lp))));
	return 0;
}

LRESULT CIconsView::OnContextMenu(UINT, WPARAM, LPARAM lp, BOOL&) {
	CPoint screen(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
	CPoint pt(screen);
	ScreenToClient(&pt);
	auto index = HitTest(pt);
	if (index < 0)
		return 0;
	Select(index);
	CMenu menu;
	menu.LoadMenu(IDR_CONTEXT);
	Frame()->GetUI().UIEnable(ID_ICON_EXPORT, TRUE);
	Frame()->ShowContextMenu(menu.GetSubMenu(2), 0, screen.x, screen.y);
	return 0;
}

LRESULT CIconsView::OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
	if (m_Icon)
		m_Icon.DestroyIcon();
	for (auto& icon : m_Icons)
		icon.Icon.DestroyIcon();
	handled = FALSE;	// the base class has to know too
	return 0;
}

// "#101 (Icon Group)" -> "101"
CString CIconsView::FileName() const {
	auto name = m_Title;
	if (auto paren = name.Find(L" ("); paren > 0)
		name = name.Left(paren);
	name.Remove(L'#');
	return ToFileName(name);
}

void CIconsView::SaveImage(std::span<const std::byte> data) {
	// the resource as an .ico or .cur file, as it is (the colors, the PNG data, the hot spot of a cursor)
	auto file = MakeResourceFile(data, m_IsIcon ? TypeIcon : TypeCursor, L"");
	auto path = AskSaveFile(m_hWnd, m_IsIcon ? L"Save Icon" : L"Save Cursor", file.Extension.c_str(), FileName() + L"." + file.Extension.c_str(),
		m_IsIcon ? L"Icon Files (*.ico)\0*.ico\0All Files\0*.*\0" : L"Cursor Files (*.cur)\0*.cur\0All Files\0*.*\0");
	if (!path.IsEmpty() && !WriteFileData(path, file.Data.data(), file.Data.size()))
		AtlMessageBox(m_hWnd, m_IsIcon ? L"Failed to save the icon" : L"Failed to save the cursor", IDR_MAINFRAME, MB_ICONERROR);
}

// all the images of the group in one file, as .ico and .cur files have them
void CIconsView::SaveGroup() {
	auto file = MakeIconGroupFile(m_GroupData, [&](uint16_t id) -> std::span<const std::byte> {
		auto it = std::ranges::find(m_Icons, (UINT)id, &IconData::Id);
		return it == m_Icons.end() ? std::span<const std::byte>() : it->Data;
	});
	if (file.empty()) {
		AtlMessageBox(m_hWnd, L"The images of the group are not in the file", IDR_MAINFRAME, MB_ICONWARNING);
		return;
	}
	auto ext = m_IsIcon ? L"ico" : L"cur";
	auto path = AskSaveFile(m_hWnd, m_IsIcon ? L"Save Icon Group" : L"Save Cursor Group", ext, FileName() + L"." + ext,
		m_IsIcon ? L"Icon Files (*.ico)\0*.ico\0All Files\0*.*\0" : L"Cursor Files (*.cur)\0*.cur\0All Files\0*.*\0");
	if (!path.IsEmpty() && !WriteFileData(path, file.data(), file.size()))
		AtlMessageBox(m_hWnd, L"Failed to save the file", IDR_MAINFRAME, MB_ICONERROR);
}

// Export: the image that was right-clicked (or the icon itself)
LRESULT CIconsView::OnExportIcon(WORD, WORD, HWND, BOOL&) {
	if (m_Icon)
		SaveImage(m_IconData);
	else if (m_SelectedIcon >= 0 && m_SelectedIcon < (int)m_Icons.size())
		SaveImage(m_Icons[m_SelectedIcon].Data);
	return 0;
}

// Save: the icon, the selected image of a group, or the whole group if no image is selected
LRESULT CIconsView::OnSave(WORD, WORD, HWND, BOOL&) {
	if (m_Icon)
		SaveImage(m_IconData);
	else if (m_SelectedIcon >= 0 && m_SelectedIcon < (int)m_Icons.size())
		SaveImage(m_Icons[m_SelectedIcon].Data);
	else if (!m_Icons.empty())
		SaveGroup();
	return 0;
}

void CIconsView::DoPaint(CDCHandle dc) {
	CFont font;
	font.CreatePointFont(110, L"Consolas");
	dc.SelectFont(font);
	dc.SetBkMode(TRANSPARENT);
	if (m_Icon) {
		dc.TextOutW(10, 10, std::format(L"{} X {}", m_IconSize, m_IconSize).c_str());
		dc.DrawIconEx(10, 50, m_Icon, m_IconSize, m_IconSize, 0, nullptr, DI_NORMAL);
	}
	else {
		int y = 10;
		for (int i = 0; i < (int)m_Icons.size(); i++) {
			auto& icon = m_Icons[i];
			auto size = icon.Size;
			CRect rc(10, y, 160, y + size);
			auto text = std::format(L"{} X {} ({} bit)", size, size, icon.Colors);
			dc.DrawText(text.c_str(), -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
			icon.Icon.DrawIconEx(dc, 180, y, size, size, 0, nullptr, DI_NORMAL);
			icon.Rect = CRect(180, y, 180 + size, y + size);
			if (i == m_SelectedIcon) {
				// the selection: a frame around the image
				CRect frame(icon.Rect);
				frame.InflateRect(4, 4);
				CBrush brush;
				brush.CreateSolidBrush(::GetSysColor(COLOR_HIGHLIGHT));
				dc.FrameRect(&frame, brush);
				frame.DeflateRect(1, 1);
				dc.FrameRect(&frame, brush);
			}
			y += size + 12;
		}
	}
}
