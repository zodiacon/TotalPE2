#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include <ELFFile.h>

class CELFSymbolsView :
    public CViewBase<CELFSymbolsView>,
    public CVirtualListView<CELFSymbolsView> {
public:
    CELFSymbolsView(IMainFrame* frame, ELFFile const& elf);

    CString GetColumnText(HWND, int row, int col) const;
    int GetRowImage(HWND, int row, int) const;
    void DoSort(SortInfo const* si);
    void OnStateChanged(HWND, int from, int to, DWORD oldState, DWORD newState);
    void UpdateUI(bool first = false);

    BEGIN_MSG_MAP(CELFSymbolsView)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        CHAIN_MSG_MAP(CVirtualListView<CELFSymbolsView>)
        CHAIN_MSG_MAP(CViewBase<CELFSymbolsView>)
    ALT_MSG_MAP(1)
        COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
        CHAIN_MSG_MAP_ALT(CViewBase<CELFSymbolsView>, 1)
    END_MSG_MAP()

private:
    enum class ColumnType { Name, Value, Size, Type, Binding, Visibility, Section, Kind };

    CString GetTitle() const override;
    LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;

    CListViewCtrl                   m_List;
    ELFFile const&                  m_ELF;
    std::vector<ELFSymbolEntry>     m_Symbols;
};
