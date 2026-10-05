// A lexer for setup information (.inf) files, as REGINST resources hold them. Lines are independent of each other:
// the styles of each come from InfLineStyles (PECore), whose InfStyle values are the styles of Scintilla.

#include "pch.h"
#include <cassert>
#include <string>
#include <string_view>

#include <scintilla/Scintilla.h>
#include <lexilla/SciLexer.h>
#include <lexilla/lexlib/WordList.h>
#include <lexilla/lexlib/LexAccessor.h>
#include <lexilla/lexlib/Accessor.h>
#include <lexilla/lexlib/LexerModule.h>
#include "InfSyntax.h"

using namespace Lexilla;

static void ColouriseInfDoc(Sci_PositionU startPos, Sci_Position length, int, WordList*[], Accessor& styler) {
	// from the start of the line
	auto line = styler.GetLine(startPos);
	Sci_PositionU pos = styler.LineStart(line);
	Sci_PositionU endPos = startPos + length;
	styler.StartAt(pos);
	styler.StartSegment(pos);

	std::string text;
	while (pos < endPos) {
		Sci_PositionU next = styler.LineStart(++line);
		if (next <= pos)
			next = endPos;
		text.clear();
		for (auto p = pos; p < next; p++)
			text += styler[p];
		auto styles = InfLineStyles(text);
		for (size_t i = 0; i < styles.size(); i++)
			if (i + 1 == styles.size() || styles[i + 1] != styles[i])
				styler.ColourTo(pos + i, (int)styles[i]);
		pos = next;
	}
	styler.Flush();
}

LexerModule lmInf(SCLEX_PROPERTIES, ColouriseInfDoc, "inf");
