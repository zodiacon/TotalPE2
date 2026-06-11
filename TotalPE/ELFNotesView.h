#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include <ELFFile.h>

class CELFNotesView :
    public CViewBase<CELFNotesView>,
    public CVirtualListView<CELFNotesView> {
public:
    CELFNotesView(IMainFrame* frame, ELFFile const& elf);

    CString GetColumnText(HWND, int row, int col) const;
    int GetRowImage(HWND, int row, int) const;
    void DoSort(SortInfo const* si);
    void OnStateChanged(HWND, int from, int to, DWORD oldState, DWORD newState);
    void UpdateUI(bool first = false);

    BEGIN_MSG_MAP(CELFNotesView)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        CHAIN_MSG_MAP(CVirtualListView<CELFNotesView>)
        CHAIN_MSG_MAP(CViewBase<CELFNotesView>)
    ALT_MSG_MAP(1)
        COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
        CHAIN_MSG_MAP_ALT(CViewBase<CELFNotesView>, 1)
    END_MSG_MAP()

private:
    enum class ColumnType { Name, Type, DescriptionSize };

    CString GetTitle() const override;
    LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;

    CListViewCtrl               m_List;
    ELFFile const&              m_ELF;
    std::vector<ELFNoteEntry>   m_Notes;
};
