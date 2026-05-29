#include "pch.h"
#include "RichHeaderView.h"
#include "resource.h"
#include <SortHelper.h>
#include <ClipboardHelper.h>
#include <ListViewhelper.h>

// Product-ID → human-readable tool description.
// The high WORD of the comp-ID is the product ID; it encodes both the tool
// type and the VS toolchain version.  Values here come from public research
// (see corkami/PE-bear, Rich Header databases, and MSVC documentation).
static const std::unordered_map<WORD, std::wstring_view> s_ProductNames{
    // "Unmarked" / padding entries
    { 0x0000, L"Unmarked objects (0)" },
    { 0x0001, L"Import0 (linker-generated stub)" },

    // LINK / LIB / MASM — VS 5 / 6
    { 0x0002, L"LINK 5.10 (VS 5.0)" },
    { 0x0003, L"LINK 5.10 SP3 (VS 5.0 SP3)" },
    { 0x0004, L"LINK 6.00 (VS 6.0)" },
    { 0x0005, L"LINK 6.00 SP5 (VS 6.0 SP5)" },
    { 0x0006, L"MASM 6.13" },

    // VS .NET 2002 (7.0)
    { 0x000E, L"LINK 7.00 (VS .NET 2002)" },
    { 0x000F, L"C/C++ 13.00 (VS .NET 2002)" },

    // VS .NET 2003 (7.1)
    { 0x0015, L"LINK 7.10 (VS .NET 2003)" },
    { 0x0016, L"C/C++ 13.10 (VS .NET 2003)" },
    { 0x0017, L"MASM 7.10 (VS .NET 2003)" },

    // VS 2005 (8.0)
    { 0x005E, L"LINK 8.00 (VS 2005)" },
    { 0x005F, L"C/C++ 14.00 (VS 2005)" },
    { 0x0060, L"MASM 8.00 (VS 2005)" },

    // VS 2008 (9.0)
    { 0x0082, L"LINK 9.00 (VS 2008)" },
    { 0x0083, L"C/C++ 15.00 (VS 2008)" },
    { 0x0084, L"MASM 9.00 (VS 2008)" },

    // VS 2010 (10.0)
    { 0x009D, L"LINK 10.00 (VS 2010)" },
    { 0x009E, L"C/C++ 16.00 (VS 2010)" },
    { 0x009F, L"MASM 10.00 (VS 2010)" },

    // VS 2012 (11.0)
    { 0x00B2, L"LINK 11.00 (VS 2012)" },
    { 0x00B3, L"C/C++ 17.00 (VS 2012)" },
    { 0x00B4, L"MASM 11.00 (VS 2012)" },
    { 0x00B5, L"CVTPGD (VS 2012)" },

    // VS 2013 (12.0)
    { 0x00C7, L"LINK 12.00 (VS 2013)" },
    { 0x00C8, L"C/C++ 18.00 (VS 2013)" },
    { 0x00C9, L"MASM 12.00 (VS 2013)" },
    { 0x00CA, L"CVTPGD (VS 2013)" },

    // VS 2015 (14.0)
    { 0x00CC, L"LINK 14.00 (VS 2015)" },
    { 0x00CD, L"C/C++ 19.00 (VS 2015)" },
    { 0x00CE, L"MASM 14.00 (VS 2015)" },
    { 0x00CF, L"CVTPGD (VS 2015)" },

    // VS 2017 (15.x)
    { 0x00D8, L"LINK 14.10 (VS 2017 RTM)" },
    { 0x00D9, L"C/C++ 19.10 (VS 2017 RTM)" },
    { 0x00DA, L"MASM 14.10 (VS 2017)" },
    { 0x00DB, L"CVTPGD (VS 2017)" },
    { 0x00E3, L"LINK 14.13 (VS 2017 15.6)" },
    { 0x00E4, L"C/C++ 19.13 (VS 2017 15.6)" },
    { 0x00E5, L"MASM 14.13 (VS 2017 15.6)" },

    // VS 2019 (16.x)
    { 0x00EF, L"LINK 14.20 (VS 2019 RTM)" },
    { 0x00F0, L"C/C++ 19.20 (VS 2019 RTM)" },
    { 0x00F1, L"MASM 14.20 (VS 2019)" },
    { 0x0103, L"LINK 14.29 (VS 2019 16.11)" },
    { 0x0104, L"C/C++ 19.29 (VS 2019 16.11)" },
    { 0x0105, L"MASM 14.29 (VS 2019 16.11)" },

    // VS 2022 (17.x)
    { 0x0106, L"LINK 14.30 (VS 2022 RTM)" },
    { 0x0107, L"C/C++ 19.30 (VS 2022 RTM)" },
    { 0x0108, L"MASM 14.30 (VS 2022)" },
    { 0x010E, L"LINK 14.34 (VS 2022 17.4)" },
    { 0x010F, L"C/C++ 19.34 (VS 2022 17.4)" },
    { 0x0110, L"MASM 14.34 (VS 2022 17.4)" },
    { 0x011A, L"LINK 14.40 (VS 2022 17.10)" },
    { 0x011B, L"C/C++ 19.40 (VS 2022 17.10)" },
    { 0x011C, L"MASM 14.40 (VS 2022 17.10)" },
};

std::wstring CRichHeaderView::ToolName(WORD productId) {
    if (auto it = s_ProductNames.find(productId); it != s_ProductNames.end())
        return std::wstring(it->second);
    return std::format(L"Unknown (0x{:04X})", productId);
}

CRichHeaderView::CRichHeaderView(IMainFrame* frame, PEFile const& pe)
    : CViewBase(frame), m_PE(pe) {
}

CString CRichHeaderView::GetTitle() const {
    return L"Rich Header";
}

CString CRichHeaderView::GetColumnText(HWND, int row, int col) const {
    auto& item = m_Items[row];
    switch (col) {
        case 0: return std::format(L"0x{:04X}", item.ProductId).c_str();
        case 1: return std::format(L"0x{:04X}", item.BuildId).c_str();
        case 2: return std::to_wstring(item.Count).c_str();
        case 3: return item.Tool.c_str();
    }
    return CString();
}

void CRichHeaderView::DoSort(SortInfo const* si) {
    auto asc = si->SortAscending;
    auto compare = [&](auto& a, auto& b) {
        switch (si->SortColumn) {
            case 0: return SortHelper::Sort(a.ProductId, b.ProductId, asc);
            case 1: return SortHelper::Sort(a.BuildId,   b.BuildId,   asc);
            case 2: return SortHelper::Sort(a.Count,     b.Count,     asc);
            case 3: return SortHelper::Sort(a.Tool,      b.Tool,      asc);
        }
        return false;
    };
    std::ranges::sort(m_Items, compare);
}

LRESULT CRichHeaderView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
    m_hWndClient = m_List.Create(*this, rcDefault, nullptr,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_OWNERDATA, 0);
    m_List.SetExtendedListViewStyle(LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

    auto cm = GetColumnManager(m_List);
    cm->AddColumn(L"Product ID", LVCFMT_RIGHT, 90);
    cm->AddColumn(L"Build ID",   LVCFMT_RIGHT, 90);
    cm->AddColumn(L"Count",      LVCFMT_RIGHT, 70);
    cm->AddColumn(L"Tool",       LVCFMT_LEFT,  400);

    BuildItems();

    return 0;
}

void CRichHeaderView::BuildItems() {
    auto const* rh = m_PE.GetRichHeader();
    if (!rh) return;

    // Show XOR key and offset in the status bar
    Frame()->SetStatusText(1, std::format(L"XOR Key: 0x{:08X}   File Offset: 0x{:X}",
        rh->Key, rh->Offset).c_str());

    m_Items.reserve(rh->Entries.size());
    for (auto const& e : rh->Entries) {
        Item item;
        static_cast<PERichEntry&>(item) = e;
        item.Tool = ToolName(e.ProductId);
        m_Items.push_back(std::move(item));
    }
    m_List.SetItemCount((int)m_Items.size());
}

LRESULT CRichHeaderView::OnCopy(WORD, WORD, HWND, BOOL&) const {
    ClipboardHelper::CopyText(m_hWnd, ListViewHelper::GetSelectedRowsAsString(m_List, L"\t"));
    return 0;
}
