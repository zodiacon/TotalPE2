#include "pch.h"
#include "resource.h"
#include "CodeColorsDlg.h"
#include "AppSettings.h"

bool CCodeColorsDlg::Dark() const {
	return m_Scheme.GetCurSel() == 1;
}

int CCodeColorsDlg::Selected() const {
	return m_List.GetSelectedIndex();
}

CodeColor& CCodeColorsDlg::ColorOf(int element) {
	return m_Colors[Dark()][element];
}

void CCodeColorsDlg::UpdateButtons() {
	auto element = Selected();
	for (auto id : { IDC_COLORS_CHANGE, IDC_COLORS_BOLD, IDC_COLORS_DEFAULT })
		GetDlgItem(id).EnableWindow(element >= 0);
	CheckDlgButton(IDC_COLORS_BOLD, element >= 0 && ColorOf(element).Bold ? BST_CHECKED : BST_UNCHECKED);
}

LRESULT CCodeColorsDlg::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
	SetDialogIcon(IDR_MAINFRAME);
	CenterWindow(GetParent());

	auto elements = CodeColors::Elements();
	for (int dark = 0; dark < 2; dark++)
		for (auto const& e : elements)
			m_Colors[dark].push_back(CodeColors::Get(e.Key, dark));

	m_Scheme.Attach(GetDlgItem(IDC_COLORS_SCHEME));
	m_Scheme.AddString(L"Light");
	m_Scheme.AddString(L"Dark");
	m_Scheme.SetCurSel(AppSettings::Get().DarkMode() ? 1 : 0);

	m_List.Attach(GetDlgItem(IDC_COLORS_LIST));
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
	m_List.InsertColumn(0, L"Language", LVCFMT_LEFT, 120);
	m_List.InsertColumn(1, L"Element", LVCFMT_LEFT, 200);
	m_List.InsertColumn(2, L"Color", LVCFMT_LEFT, 110);
	for (int i = 0; i < (int)elements.size(); i++) {
		m_List.InsertItem(i, elements[i].Language);
		m_List.SetItemText(i, 1, elements[i].Name);
	}
	m_List.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	UpdateButtons();
	return TRUE;
}

LRESULT CCodeColorsDlg::OnOK(WORD, WORD id, HWND, BOOL&) {
	auto elements = CodeColors::Elements();
	for (int dark = 0; dark < 2; dark++) {
		std::map<std::string, CodeColor> changed;
		for (size_t i = 0; i < elements.size(); i++)
			changed[elements[i].Key] = m_Colors[dark][i];
		CodeColors::SetChanged(dark, changed);
	}
	EndDialog(id);
	return 0;
}

LRESULT CCodeColorsDlg::OnCancel(WORD, WORD id, HWND, BOOL&) {
	EndDialog(id);
	return 0;
}

LRESULT CCodeColorsDlg::OnChange(WORD, WORD, HWND, BOOL&) {
	auto element = Selected();
	if (element < 0)
		return 0;
	CColorDialog dlg(ColorOf(element).Color, CC_FULLOPEN | CC_RGBINIT, m_hWnd);
	dlg.m_cc.lpCustColors = m_Custom;
	if (dlg.DoModal() == IDOK) {
		ColorOf(element).Color = dlg.GetColor();
		m_List.RedrawItems(element, element);
	}
	return 0;
}

LRESULT CCodeColorsDlg::OnBold(WORD, WORD, HWND, BOOL&) {
	if (auto element = Selected(); element >= 0) {
		ColorOf(element).Bold = IsDlgButtonChecked(IDC_COLORS_BOLD) == BST_CHECKED;
		m_List.RedrawItems(element, element);
	}
	return 0;
}

LRESULT CCodeColorsDlg::OnDefault(WORD, WORD, HWND, BOOL&) {
	if (auto element = Selected(); element >= 0) {
		ColorOf(element) = CodeColors::Default(CodeColors::Elements()[element], Dark());
		m_List.RedrawItems(element, element);
		UpdateButtons();
	}
	return 0;
}

LRESULT CCodeColorsDlg::OnResetAll(WORD, WORD, HWND, BOOL&) {
	auto elements = CodeColors::Elements();
	for (size_t i = 0; i < elements.size(); i++)
		m_Colors[Dark()][i] = CodeColors::Default(elements[i], Dark());
	m_List.RedrawItems(0, m_List.GetItemCount());
	UpdateButtons();
	return 0;
}

LRESULT CCodeColorsDlg::OnScheme(WORD, WORD, HWND, BOOL&) {
	m_List.RedrawItems(0, m_List.GetItemCount());
	UpdateButtons();
	return 0;
}

LRESULT CCodeColorsDlg::OnItemChanged(int, LPNMHDR hdr, BOOL&) {
	auto lv = (NMLISTVIEW*)hdr;
	if ((lv->uChanged & LVIF_STATE) && ((lv->uNewState ^ lv->uOldState) & LVIS_SELECTED))
		UpdateButtons();
	return 0;
}

LRESULT CCodeColorsDlg::OnDoubleClick(int, LPNMHDR, BOOL& handled) {
	return OnChange(0, 0, nullptr, handled);
}

// The color column: a sample of the text in the element's color, on the theme's background
LRESULT CCodeColorsDlg::OnCustomDraw(int, LPNMHDR hdr, BOOL&) {
	auto cd = (NMLVCUSTOMDRAW*)hdr;
	switch (cd->nmcd.dwDrawStage) {
		case CDDS_PREPAINT:
			return CDRF_NOTIFYITEMDRAW;
		case CDDS_ITEMPREPAINT:
			return CDRF_NOTIFYSUBITEMDRAW;
		case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
		{
			if (cd->iSubItem != 2)
				return CDRF_DODEFAULT;
			auto element = (int)cd->nmcd.dwItemSpec;
			auto const& c = ColorOf(element);
			CRect rc;
			m_List.GetSubItemRect(element, 2, LVIR_BOUNDS, &rc);
			CDCHandle dc(cd->nmcd.hdc);
			rc.DeflateRect(2, 2);
			// (a background element, as the related lines' is, is shown as a fill; the others as text on the theme's background)
			bool fill = std::string_view(CodeColors::Elements()[element].Key) == "Common.Related";
			dc.FillSolidRect(&rc, fill ? c.Color : Dark() ? RGB(30, 30, 30) : RGB(255, 255, 255));
			dc.Draw3dRect(&rc, RGB(128, 128, 128), RGB(128, 128, 128));
			if (!fill) {
				auto font = m_List.GetFont();
				LOGFONT lf;
				::GetObject(font, sizeof(lf), &lf);
				lf.lfWeight = c.Bold ? FW_BOLD : FW_NORMAL;
				CFont sample;
				sample.CreateFontIndirect(&lf);
				auto old = dc.SelectFont(sample);
				dc.SetBkMode(TRANSPARENT);
				dc.SetTextColor(c.Color);
				dc.DrawText(std::format(L"#{:02X}{:02X}{:02X}", GetRValue(c.Color), GetGValue(c.Color), GetBValue(c.Color)).c_str(), -1, &rc,
					DT_CENTER | DT_VCENTER | DT_SINGLELINE);
				dc.SelectFont(old);
			}
			return CDRF_SKIPDEFAULT;
		}
	}
	return CDRF_DODEFAULT;
}
