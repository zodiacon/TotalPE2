#include "pch.h"
#include "resource.h"
#include "IconsView.h"
#include "IconWriter.h"
#include <WTLHelper.h>
#include "ResourceContent.h"
#include "SaveData.h"

void CIconsView::SetGroupIconData(std::span<const std::byte> data) {
#pragma pack(push, 1)
	struct IconDirectoryEntry {
		BYTE  bWidth;
		BYTE  bHeight;
		BYTE  bColorCount;
		BYTE  bReserved;
		WORD  wPlanes;
		WORD  wBitCount;
		DWORD dwBytesInRes;
		WORD Id;
	};

	struct IconHeader {
		WORD Reserved;
		WORD Type;
		WORD Count;
		IconDirectoryEntry Entries[1];
	};
#pragma pack(pop)

	auto header = (IconHeader const*)data.data();
	ATLASSERT(header->Reserved == 0);
	bool isIcon = header->Type == 1;

	int y = 10;
	auto& resources = Frame()->GetFlatResources();
	for (int i = 0; i < header->Count; i++) {
		auto const& entry = header->Entries[i];
		auto id = std::format(L"#{}", entry.Id);
		if (auto it = std::ranges::find_if(resources, [&](auto& e) { return e.Name == id; }); it != resources.end()) {
			CIconHandle icon;
			auto& idata = it->Data;
			auto width = entry.bWidth ? entry.bWidth : 256;
			icon.m_hIcon = ::CreateIconFromResourceEx((PBYTE)idata.data(),
				(DWORD)idata.size(), isIcon, 0x30000, width, width, LR_DEFAULTCOLOR);
			if (icon) {
				IconData id;
				id.Icon = icon;
				id.Size = width;
				id.Id = entry.Id;
				id.Colors = entry.wBitCount * entry.wPlanes;
				m_Icons.emplace_back(std::move(id));
				y += width + 12;
			}
		}
	}
	SetScrollSize(450, y);
}

CIconsView::CIconsView(IMainFrame* frame, PCWSTR title) : CViewBase(frame), m_Title(title) {
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
}

LRESULT CIconsView::OnCreate(UINT, WPARAM, LPARAM, BOOL& handled) {
	return 0;
}

LRESULT CIconsView::OnContextMenu(UINT, WPARAM, LPARAM lp, BOOL&) {
	CPoint pt(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
	CPoint pt2(pt);
	ScreenToClient(&pt);
	CPoint offset;
	GetScrollOffset(offset);
	pt += offset;
	if (m_Icon) {
		if (m_IconRect.PtInRect(pt)) {
			CMenu menu;
			menu.LoadMenu(IDR_CONTEXT);
			Frame()->GetUI().UIEnable(ID_ICON_EXPORT, TRUE);
			Frame()->ShowContextMenu(menu.GetSubMenu(2), 0, pt2.x, pt2.y);
		}
		return 0;
	}
	int i = 0;
	for (auto const& icon : m_Icons) {
		if (icon.Rect.PtInRect(pt)) {
			CMenu menu;
			menu.LoadMenu(IDR_CONTEXT);
			m_SelectedIcon = i;
			Frame()->GetUI().UIEnable(ID_ICON_EXPORT, TRUE);
			Frame()->ShowContextMenu(menu.GetSubMenu(2), 0, pt2.x, pt2.y);
			break;
		}
		i++;
	}
	return 0;
}

LRESULT CIconsView::OnDestroy(UINT, WPARAM, LPARAM, BOOL&) {
	if (m_Icon)
		m_Icon.DestroyIcon();
	for (auto& icon : m_Icons)
		icon.Icon.DestroyIcon();

	return 0;
}

LRESULT CIconsView::OnEraseBkgnd(UINT, WPARAM wp, LPARAM, BOOL&) {
	return 0;
}

LRESULT CIconsView::OnExportIcon(WORD, WORD, HWND, BOOL&) {
	if (m_Icon) {
		// the resource as an .ico or .cur file, as it is (the colors, the PNG data, the hot spot of a cursor)
		auto file = MakeResourceFile(m_IconData, m_IsIcon ? 3 : 1, L"");
		auto path = AskSaveFile(m_hWnd, m_IsIcon ? L"Export Icon" : L"Export Cursor", file.Extension.c_str(), nullptr,
			m_IsIcon ? L"Icon Files (*.ico)\0*.ico\0All Files\0*.*\0" : L"Cursor Files (*.cur)\0*.cur\0All Files\0*.*\0");
		if (!path.IsEmpty() && !WriteFileData(path, file.Data.data(), file.Data.size()))
			AtlMessageBox(m_hWnd, m_IsIcon ? L"Failed to save icon" : L"Failed to save cursor", IDR_MAINFRAME, MB_ICONERROR);
		return 0;
	}
	if (m_SelectedIcon < 0 || m_SelectedIcon >= (int)m_Icons.size())
		return 0;
	CSimpleFileDialog dlg(FALSE, L"ico", nullptr, OFN_EXPLORER | OFN_ENABLESIZING | OFN_OVERWRITEPROMPT,
		L"Icon Files (*.ico)\0*.ico\0All Files\0*.*\0", m_hWnd);
	WTLHelper::SuspendHook();
	if (IDOK == dlg.DoModal()) {
		auto const& icon = m_Icons[m_SelectedIcon];
		if (!IconWriter::Save(dlg.m_szFileName, icon.Icon.m_hIcon, icon.Colors)) {
			AtlMessageBox(m_hWnd, L"Failed to save icon", IDR_MAINFRAME, MB_ICONERROR);
		}
	}
	WTLHelper::ResumeHook();

	return 0;
}

CString CIconsView::GetTitle() const {
	return m_Title;
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
		for (auto& icon : m_Icons) {
			auto size = icon.Size;
			CRect rc(10, y, 160, y + size);
			auto text = std::format(L"{} X {} ({} bit)", size, size, icon.Colors);
			dc.DrawText(text.c_str(),
				-1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
			icon.Icon.DrawIconEx(dc, 180, y, size, size, 0, nullptr, DI_NORMAL);
			icon.Rect = CRect(180, y, 180 + size, y + size);
			y += size + 12;
		}
	}
}
