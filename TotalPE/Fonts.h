#pragma once

//
// The fonts of the application (Options): the code font, of the views of code (disassembly, C/C++, IDL, IL, XML, INF, text)
// and the application font, of everything else (lists, trees, edit boxes...) but dialogs, whose layouts are made for their
// own. Controls get the application font as they are created; a change of either is applied to those there are.
//
namespace Fonts {
	// the code font: the user's, or Consolas, 11 points
	LOGFONT Code();
	// the application font; false if the user has not chosen one (the controls keep their own)
	bool Application(LOGFONT& font);
	// the size of a font in points
	int Points(LOGFONT const& font);

	// a control that shows code (with the code font, not the application's)
	void MarkCode(HWND hWnd);
	HFONT CodeFont();

	// gives the controls of the thread's windows the application font as they are created (once, at startup)
	void Install();
	// the fonts again, to the controls there are (after a change)
	void Apply();
}
