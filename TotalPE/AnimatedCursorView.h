#pragma once

#include <functional>
#include "ViewBase.h"
#include <CustomSplitterWindow.h>
#include "AniFile.h"

// Plays an animated cursor / icon with the timing stored in the file, at actual size and magnified.
class CAniPreview : public CWindowImpl<CAniPreview> {
public:
	DECLARE_WND_CLASS_EX(L"TotalPE.AniPreview", CS_HREDRAW | CS_VREDRAW, COLOR_WINDOW)

	BEGIN_MSG_MAP(CAniPreview)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLeftButtonDown)
		MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnGetDlgCode)
		MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
	END_MSG_MAP()

	// The icons are owned by the caller and must outlive the preview. One handle per frame of the animation.
	void SetData(AniFile const* ani, std::vector<HICON> const* icons);
	void ShowStep(size_t step);		// pauses
	void Play(bool play);
	bool IsPlaying() const { return m_Playing; }
	size_t CurrentStep() const { return m_Step; }

	std::function<void(CPoint)> OnContext;
	std::function<void(bool playing)> OnPlayingChanged;

private:
	LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnEraseBkgnd(UINT, WPARAM, LPARAM, BOOL&) { return 1; }
	LRESULT OnTimer(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnLeftButtonDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnKeyDown(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnContextMenu(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnGetDlgCode(UINT, WPARAM, LPARAM, BOOL&) { return DLGC_WANTALLKEYS; }
	LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);

	void StartTimer(UINT milliseconds);
	void DrawInfo(CDCHandle dc, CRect const& rc) const;

	static constexpr UINT_PTR TimerId = 1;
	AniFile const* m_Ani{ nullptr };
	std::vector<HICON> const* m_Icons{ nullptr };
	size_t m_Step{ 0 };
	bool m_Playing{ false };
};

class CAnimatedCursorView : public CViewBase<CAnimatedCursorView> {
public:
	CAnimatedCursorView(IMainFrame* frame, PCWSTR title);
	~CAnimatedCursorView();

	CString GetTitle() const override;
	bool CanSave() const override { return !m_Raw.empty(); }
	// false if the data is not a usable .ani file
	bool SetData(std::span<const std::byte> data);

	BEGIN_MSG_MAP(CAnimatedCursorView)
		MESSAGE_HANDLER(WM_CREATE, OnCreate)
		NOTIFY_CODE_HANDLER(NM_DBLCLK, OnListDoubleClick)
		NOTIFY_CODE_HANDLER(NM_RCLICK, OnListRightClick)
		CHAIN_MSG_MAP(CViewBase<CAnimatedCursorView>)
	ALT_MSG_MAP(1)
		COMMAND_ID_HANDLER(ID_FILE_SAVE, OnSave)
		CHAIN_MSG_MAP_ALT(CViewBase<CAnimatedCursorView>, 1)
	END_MSG_MAP()

private:
	LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
	LRESULT OnListDoubleClick(int, LPNMHDR, BOOL&);
	LRESULT OnListRightClick(int, LPNMHDR, BOOL&);
	// the selected frame, or the whole animation if no frame is selected
	LRESULT OnSave(WORD, WORD, HWND, BOOL&);
	void ShowContextMenu(CPoint screen);
	void ExportAni();
	void ExportFrame(size_t step);
	void FillList();

	enum { ID_ANI_EXPORT = 1, ID_ANI_EXPORT_FRAME, ID_ANI_PLAYPAUSE, ID_ANI_SHOW_FRAME };

	CCustomSplitterWindow m_Splitter;
	CAniPreview m_Preview;
	CListViewCtrl m_List;
	std::vector<uint8_t> m_Raw;		// the resource, for exporting
	AniFile m_Ani;
	std::vector<HICON> m_Icons;
	CString m_Title;
};
