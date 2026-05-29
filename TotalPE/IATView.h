#pragma once

#include "ViewBase.h"
#include <VirtualListView.h>
#include <PEFile.h>
#include <CustomSplitterWindow.h>

class CIATView :
	public CViewBase<CIATView>,
	public CVirtualListView<CIATView> {
public:
	CIATView(IMainFrame* frame, PEFile const& pe);

	CString GetColumnText(HWND h, int row, int col) const;
	int GetRowImage(HWND h, int row, int) const;
	void DoSort(SortInfo const* si);
	void OnStateChanged(HWND hWnd, int from, int to, DWORD oldState, DWORD newState);
	void UpdateUI(bool first = false) const;

	BEGIN_MSG_MAP(CIATView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		CHAIN_MSG_MAP(CVirtualListView<CIATView>)
		CHAIN_MSG_MAP(CViewBase<CIATView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_EDIT_COPY, OnCopy)
		CHAIN_MSG_MAP_ALT(CViewBase<CIATView>, 1)
	END_MSG_MAP()

	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnCopy(WORD, WORD, HWND, BOOL&) const;

private:
	enum class ModCol  { Module, IatRVA, FileOffset, Count };
	enum class FuncCol { Index, Name, HintOrd, SlotRVA, SlotOffset, RawValue };

	CString GetTitle() const override;
	void BuildItems();
	void PopulateFunctions(int importIndex);

	struct ModuleItem {
		std::string ModuleName;
		DWORD       IatBaseRVA{};
		uint32_t    FileOffset{};
		uint32_t    Count{};
		int         ImportIndex{};  // index into GetImport() vector
	};

	struct FuncItem {
		std::string FuncName;
		WORD        Hint{};
		bool        IsOrdinal{};
		DWORD       Ordinal{};
		DWORD       SlotRVA{};
		uint32_t    SlotOffset{};
		uint64_t    RawValue{};
		int         Index{};
	};

	CListViewCtrl            m_ModList, m_FuncList;
	CCustomHorSplitterWindow m_Splitter;
	std::vector<ModuleItem>  m_Modules;
	std::vector<FuncItem>    m_Functions;
	PEFile const&            m_PE;
	bool                     m_Is64{};
};
