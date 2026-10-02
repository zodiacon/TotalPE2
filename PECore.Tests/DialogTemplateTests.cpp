#include "TestCommon.h"
#include <DialogTemplate.h>
#include "ByteWriter.h"

namespace {
	constexpr uint32_t DS_SETFONT_ = 0x40;

	// a legacy DLGTEMPLATE: a dialog with a font and two controls
	ByteWriter LegacyDialog() {
		ByteWriter w;
		w.U32(WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_SETFONT_ | 0x800 /* DS_CENTER */).U32(0);
		w.U16(2).U16(10).U16(20).U16(200).U16(100);
		w.U16(0xFFFF).U16(101);							// menu: ordinal 101
		w.U16(0);										// class: default
		w.Str(L"Test Dialog");
		w.U16(9).Str(L"Segoe UI");						// font
		// OK button (class atom 0x80 = Button)
		w.Align(4).U32(WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON).U32(0);
		w.U16(80).U16(70).U16(50).U16(14).U16(IDOK);
		w.U16(0xFFFF).U16(0x80).Str(L"OK").U16(0);
		// static text with a resource ordinal as its title (an icon)
		w.Align(4).U32(WS_CHILD | WS_VISIBLE | SS_ICON).U32(0);
		w.U16(7).U16(7).U16(20).U16(20).U16(0xFFFF);	// id -1
		w.U16(0xFFFF).U16(0x82).U16(0xFFFF).U16(128).U16(0);
		return w;
	}

	// a DLGTEMPLATEEX: control ids are DWORDs
	ByteWriter ExtendedDialog() {
		ByteWriter w;
		w.U16(1).U16(0xFFFF).U32(77);					// version, signature, help id
		w.U32(WS_EX_TOPMOST).U32(WS_POPUP | WS_CAPTION | DS_SETFONT_);
		w.U16(1).U16(0).U16(0).U16(150).U16(60);
		w.U16(0);										// no menu
		w.Str(L"MyDialogClass");						// custom class
		w.Str(L"Extended");
		w.U16(10).U16(700).U8(1).U8(1).Str(L"Tahoma");	// size, weight, italic, charset, face
		w.Align(4).U32(5).U32(0).U32(WS_CHILD | WS_VISIBLE);
		w.U16(10).U16(10).U16(100).U16(12).U32(1234);
		w.Str(L"Edit").Str(L"hello").U16(0);
		return w;
	}
}

TEST_CASE("A legacy DLGTEMPLATE is parsed", "[dialog]") {
	DlgTemplate dlg;
	REQUIRE(DlgTemplate::Parse(LegacyDialog().Span(), dlg));
	CHECK_FALSE(dlg.Extended);
	CHECK(dlg.Title == L"Test Dialog");
	CHECK(dlg.X == 10);
	CHECK(dlg.Y == 20);
	CHECK(dlg.CX == 200);
	CHECK(dlg.CY == 100);
	CHECK(dlg.Menu == L"#101");
	CHECK(dlg.ClassName.empty());
	CHECK(dlg.HasFont);
	CHECK(dlg.PointSize == 9);
	CHECK(dlg.Typeface == L"Segoe UI");

	REQUIRE(dlg.Controls.size() == 2);
	auto& ok = dlg.Controls[0];
	CHECK(ok.ClassName == L"Button");
	CHECK(ok.Title == L"OK");
	CHECK(ok.Id == IDOK);
	CHECK(ok.X == 80);
	CHECK(ok.CX == 50);
	CHECK((ok.Style & BS_DEFPUSHBUTTON) != 0);

	auto& icon = dlg.Controls[1];
	CHECK(icon.ClassName == L"Static");
	CHECK(icon.Id == -1);
	CHECK(icon.Title == L"#128");	// an ordinal title
}

TEST_CASE("A DLGTEMPLATEEX is parsed", "[dialog]") {
	DlgTemplate dlg;
	REQUIRE(DlgTemplate::Parse(ExtendedDialog().Span(), dlg));
	CHECK(dlg.Extended);
	CHECK(dlg.HelpId == 77);
	CHECK(dlg.ExStyle == WS_EX_TOPMOST);
	CHECK(dlg.Title == L"Extended");
	CHECK(dlg.ClassName == L"MyDialogClass");
	CHECK(dlg.Menu.empty());
	CHECK(dlg.HasFont);
	CHECK(dlg.PointSize == 10);
	CHECK(dlg.Weight == 700);
	CHECK(dlg.Italic == 1);
	CHECK(dlg.Typeface == L"Tahoma");

	REQUIRE(dlg.Controls.size() == 1);
	CHECK(dlg.Controls[0].HelpId == 5);
	CHECK(dlg.Controls[0].Id == 1234);
	CHECK(dlg.Controls[0].ClassName == L"Edit");
	CHECK(dlg.Controls[0].Title == L"hello");
}

TEST_CASE("The preview template is safe to instantiate", "[dialog]") {
	DlgTemplate original;
	REQUIRE(DlgTemplate::Parse(ExtendedDialog().Span(), original));
	auto bytes = original.BuildPreviewTemplate();

	// it is itself a valid extended template
	DlgTemplate preview;
	REQUIRE(DlgTemplate::Parse(std::as_bytes(std::span(bytes)), preview));
	CHECK(preview.Extended);

	SECTION("no custom class and no menu") {
		CHECK(preview.ClassName.empty());
		CHECK(preview.Menu.empty());
	}
	SECTION("a visible child window instead of a popup") {
		CHECK((preview.Style & WS_POPUP) == 0);
		CHECK((preview.Style & WS_CHILD) != 0);
		CHECK((preview.Style & WS_VISIBLE) != 0);
		CHECK((preview.Style & 0x10 /* DS_NOFAILCREATE */) != 0);
		CHECK((preview.ExStyle & WS_EX_TOPMOST) == 0);
		CHECK((preview.Style & WS_CAPTION) == WS_CAPTION);	// the look of the dialog is kept
	}
	SECTION("text, font and controls survive") {
		CHECK(preview.Title == L"Extended");
		CHECK(preview.Typeface == L"Tahoma");
		CHECK(preview.PointSize == 10);
		REQUIRE(preview.Controls.size() == 1);
		CHECK(preview.Controls[0].Id == 1234);
		CHECK(preview.Controls[0].Title == L"hello");
	}
}

TEST_CASE("Resource ordinals never reach the preview", "[dialog]") {
	DlgTemplate original;
	REQUIRE(DlgTemplate::Parse(LegacyDialog().Span(), original));
	auto bytes = original.BuildPreviewTemplate();

	DlgTemplate preview;
	REQUIRE(DlgTemplate::Parse(std::as_bytes(std::span(bytes)), preview));
	REQUIRE(preview.Controls.size() == 2);
	CHECK(preview.Controls[1].Title.empty());	// "#128" would load an icon from this process
	CHECK(preview.Menu.empty());
}

TEST_CASE("Damaged dialog resources are handled", "[dialog]") {
	DlgTemplate dlg;

	SECTION("empty input") {
		CHECK_FALSE(DlgTemplate::Parse({}, dlg));
	}
	SECTION("a count that promises more controls than the data holds keeps the ones that exist") {
		auto w = LegacyDialog();
		w.Bytes[8] = 50;	// 50 controls
		if (DlgTemplate::Parse(w.Span(), dlg))
			CHECK(dlg.Controls.size() == 2);
	}
	SECTION("every truncation of a valid dialog") {
		for (auto text : { LegacyDialog(), ExtendedDialog() })
			for (size_t length = 0; length < text.Bytes.size(); length++) {
				DlgTemplate d;
				DlgTemplate::Parse(text.Span().first(length), d);
				d.BuildPreviewTemplate();	// the result of a partial parse can be built too
			}
	}
}
