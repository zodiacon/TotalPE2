#pragma once

#include "ViewBase.h"
#include <ELFFile.h>

class CELFImageView : public CViewBase<CELFImageView> {
public:
    CELFImageView(IMainFrame* frame, ELFFile const& elf);

    CString GetTitle() const override;
    void UpdateUI(bool first = false);

    BEGIN_MSG_MAP(CELFImageView)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        CHAIN_MSG_MAP(CViewBase<CELFImageView>)
    END_MSG_MAP()

private:
    struct DataItem {
        std::wstring Name;
        std::wstring Value;
    };

    LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);

    CListViewCtrl            m_List;
    ELFFile const&           m_ELF;
    std::vector<DataItem>    m_Items;
};
