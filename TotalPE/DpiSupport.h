#pragma once

#include <ToolbarHelper.h>
#include <DpiHelper.h>
#include <functional>
#include <map>

//
// Per-monitor DPI: what the windows of the application need besides WTLHelper's DpiHelper (sizes given at 96 DPI, scaled to
// the DPI of the window they are used in)
//
namespace Dpi {
	// A tool bar (ToolbarHelper::CreateAndInitToolBar's) that follows the DPI of its window: when it changes, its images are
	// made again in their size for it, and the band of the rebar it is in (if any) takes its new size. The buttons are
	// copied (their texts are kept as pointers: literals). size: of the images, at 96 DPI
	HWND CreateToolBar(HWND hParent, ToolBarButtonInfo const* buttons, int count, int size = 24);

	// An image list the controls have was changed in place (its images are of another size now): the list views and tree
	// views of the thread's windows that have it take it again, which measures their items again
	void ImageListResized(HIMAGELIST images);

	//
	// Image lists of the same images (at the same indices), one for each DPI there are controls at, made as they are asked
	// for. A list view or tree view of the thread that has one of them takes the one of its DPI when its DPI changes (it
	// moved to a monitor of another DPI, or into a window that is on one), with Install's hook. The lists are the set's:
	// the controls share them (LVS_SHAREIMAGELISTS).
	//
	class ImageSet {
	public:
		// fill: adds the images to a list (made in their size); size: of the images at 96 DPI
		ImageSet(int size, std::function<void(CImageList&)> fill);
		~ImageSet();
		ImageSet(ImageSet const&) = delete;
		ImageSet& operator=(ImageSet const&) = delete;

		HIMAGELIST For(UINT dpi);
		bool Empty() const;
		// a change to the images, in every list
		void ForEach(std::function<void(CImageList&)> const& change);
		// the set an image list is of (null: none)
		static ImageSet* Of(HIMAGELIST images);

	private:
		int m_Size;
		std::function<void(CImageList&)> m_Fill;
		std::map<UINT, CImageList> m_Lists;
	};

	// (once, at startup) the list views and tree views of the thread follow their image sets' DPIs
	void Install();

	// The placement of a window as it is kept in the settings, with the DPI of its sizes. A placement saved before the DPI
	// was (dpi 0) was saved by a TotalPE that was not DPI aware: in the coordinates Windows gives such a process, which are
	// those of the system DPI scaled to 96.
	void RestorePlacement(HWND hWnd, WINDOWPLACEMENT wp, UINT dpi);

	// the rectangle Windows suggests with WM_DPICHANGED (lParam), taken by a top-level window
	void TakeSuggestedRect(HWND hWnd, LPARAM lParam);
}
