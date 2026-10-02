#pragma once

#include <CustomSplitterWindow.h>
#include <functional>
#include <string>
#include <vector>

struct HexInspectorInput {
	const uint8_t* Data{};		// bytes starting at the caret (up to 32)
	size_t Count{};
	int64_t DisplayOffset{};	// the offset as shown in the address column
	int64_t SelectionLength{};
	bool BigEndian{ false };
	bool HasPE{ false };		// the data comes from a PE file, so RVA, symbol and region are meaningful
	std::wstring Rva, Symbol, Region;	// empty if unknown
};

struct HexInspectorRegion {
	int64_t Offset{};			// offset in the hex control's buffer
	int64_t Length{};
	int64_t DisplayOffset{};
	std::wstring Name;
	COLORREF Text{};
	COLORREF Back{};
};

// Side panel of the hex view: the caret's bytes interpreted as various types, plus a list of the file's regions.
class CHexInspector : public CWindowImpl<CHexInspector> {
public:
	DECLARE_WND_CLASS(L"TotalPE.HexInspector")

	static constexpr int IdValues = 1, IdRegions = 2;

	BEGIN_MSG_MAP(CHexInspector)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		NOTIFY_HANDLER(IdRegions, NM_DBLCLK, OnRegionActivated)
		NOTIFY_HANDLER(IdRegions, NM_CUSTOMDRAW, OnRegionCustomDraw)
	END_MSG_MAP()

	void Update(HexInspectorInput const& input);
	void SetRegions(std::vector<HexInspectorRegion> regions);

	// called when a region is double-clicked
	std::function<void(HexInspectorRegion const&)> OnRegionClicked;

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) { return 1; }
	LRESULT OnRegionActivated(int, LPNMHDR, BOOL&);
	LRESULT OnRegionCustomDraw(int, LPNMHDR, BOOL&);

	CCustomHorSplitterWindow m_Split;
	CListViewCtrl m_Values, m_RegionList;
	std::vector<HexInspectorRegion> m_Regions;
};
