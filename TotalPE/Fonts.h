#pragma once

//
// The fonts of the application (Options): the code font, of the views of code (disassembly, C/C++, IDL, IL, XML, INF, text)
// and the application font, of everything else (lists, trees, edit boxes...) but dialogs, whose layouts are made for their
// own. Controls get the application font as they are created; a change of either is applied to those there are.
// The LOGFONTs (as the settings keep them) are at 96 DPI; a control gets its font at its own DPI, and a new one when its DPI
// changes (it moved to a monitor with another DPI).
//
namespace Fonts {
	// the code font: the user's, or Consolas, 11 points (at 96 DPI)
	LOGFONT Code();
	// the application font (at 96 DPI); false if the user has not chosen one (the controls keep their own)
	bool Application(LOGFONT& font);
	// the size of a font (at 96 DPI) in points
	int Points(LOGFONT const& font);
	// a font at 96 DPI for dpi, and back
	LOGFONT ToDpi(LOGFONT font, UINT dpi);
	LOGFONT FromDpi(LOGFONT font, UINT dpi);

	// a control that shows code (with the code font, not the application's)
	void MarkCode(HWND hWnd);
	// the code font at the DPI of a window (the font is kept: the caller does not delete it)
	HFONT CodeFont(HWND hWnd);

	// gives the controls of the thread's windows the application font as they are created (once, at startup)
	void Install();
	// the fonts again, to the controls there are (after a change)
	void Apply();
}
