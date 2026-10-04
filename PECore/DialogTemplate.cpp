#include "pch.h"
#include "DialogTemplate.h"

namespace {
	struct Reader {
		std::span<const std::byte> Data;
		size_t Pos{ 0 };
		bool Ok{ true };

		uint32_t Read(size_t size) {
			if (!Ok || Pos + size > Data.size()) {
				Ok = false;
				return 0;
			}
			uint32_t v = 0;
			for (size_t i = 0; i < size; i++)
				v |= (uint32_t)std::to_integer<uint8_t>(Data[Pos + i]) << (8 * i);
			Pos += size;
			return v;
		}
		uint8_t U8() { return (uint8_t)Read(1); }
		uint16_t U16() { return (uint16_t)Read(2); }
		uint32_t U32() { return Read(4); }
		short S16() { return (short)U16(); }
		void Align(size_t a) { Pos = (Pos + a - 1) & ~(a - 1); }

		// sz_Or_Ord: 0x0000 = empty, 0xFFFF + ordinal, otherwise a zero-terminated string
		std::wstring SzOrOrd(bool classAtoms = false) {
			auto w = U16();
			if (!Ok || w == 0)
				return {};
			if (w == 0xFFFF) {
				auto ord = U16();
				if (classAtoms) {
					switch (ord) {
						case 0x80: return L"Button";
						case 0x81: return L"Edit";
						case 0x82: return L"Static";
						case 0x83: return L"ListBox";
						case 0x84: return L"ScrollBar";
						case 0x85: return L"ComboBox";
					}
					return std::format(L"#atom 0x{:X}", ord);
				}
				return std::format(L"#{}", ord);
			}
			std::wstring s;
			while (Ok && w) {
				s += (wchar_t)w;
				w = U16();
			}
			return s;
		}
	};

	constexpr DWORD DS_SETFONT_ = 0x40;
	constexpr DWORD DS_DROP = 0x1 | 0x2 | 0x200 | 0x800 | 0x1000;	// ABSALIGN, SYSMODAL, SETFOREGROUND, CENTER, CENTERMOUSE
	constexpr DWORD DS_NOFAILCREATE_ = 0x10;

	struct Writer {
		std::vector<BYTE> Buf;
		void U8(uint8_t v) { Buf.push_back(v); }
		void U16(uint16_t v) { U8(v & 0xFF); U8(v >> 8); }
		void U32(uint32_t v) { U16(v & 0xFFFF); U16(v >> 16); }
		void Align(size_t a) { while (Buf.size() % a) U8(0); }
		void Str(std::wstring const& s) {
			for (auto c : s)
				if (c)
					U16((uint16_t)c);
			U16(0);
		}
	};
}

bool DlgTemplate::Parse(std::span<const std::byte> data, DlgTemplate& dlg) {
	Reader r{ data };
	auto ver = r.U16();
	auto sig = r.U16();
	dlg.Extended = ver == 1 && sig == 0xFFFF;
	if (dlg.Extended) {
		dlg.HelpId = r.U32();
		dlg.ExStyle = r.U32();
		dlg.Style = r.U32();
	}
	else {
		r.Pos = 0;
		dlg.Style = r.U32();
		dlg.ExStyle = r.U32();
	}
	auto count = r.U16();
	dlg.X = r.S16(); dlg.Y = r.S16(); dlg.CX = r.S16(); dlg.CY = r.S16();
	dlg.Menu = r.SzOrOrd();
	dlg.ClassName = r.SzOrOrd(true);
	dlg.Title = r.SzOrOrd();
	if (!r.Ok)
		return false;

	if (dlg.Style & DS_SETFONT_) {
		dlg.HasFont = true;
		dlg.PointSize = r.U16();
		if (dlg.Extended) {
			dlg.Weight = r.U16();
			dlg.Italic = r.U8();
			dlg.Charset = r.U8();
		}
		dlg.Typeface = r.SzOrOrd();
		if (!r.Ok)
			return false;
	}

	for (unsigned i = 0; i < count; i++) {
		r.Align(4);
		DlgControl c;
		if (dlg.Extended) {
			c.HelpId = r.U32();
			c.ExStyle = r.U32();
			c.Style = r.U32();
		}
		else {
			c.Style = r.U32();
			c.ExStyle = r.U32();
		}
		c.X = r.S16(); c.Y = r.S16(); c.CX = r.S16(); c.CY = r.S16();
		c.Id = dlg.Extended ? (int)r.U32() : (int)r.S16();
		c.ClassName = r.SzOrOrd(true);
		c.Title = r.SzOrOrd();
		auto extra = r.U16();
		r.Pos += extra;
		if (!r.Ok)
			break;	// keep what was parsed so far
		dlg.Controls.push_back(std::move(c));
	}
	return true;
}

std::vector<BYTE> DlgTemplate::BuildPreviewTemplate() const {
	Writer w;
	DWORD style = (Style & ~(WS_POPUP | WS_DISABLED | DS_DROP)) | WS_CHILD | WS_VISIBLE | DS_NOFAILCREATE_;
	DWORD exStyle = ExStyle & ~(WS_EX_TOPMOST | WS_EX_APPWINDOW | WS_EX_NOACTIVATE);

	w.U16(1);
	w.U16(0xFFFF);
	w.U32(0);
	w.U32(exStyle);
	w.U32(style);
	w.U16((uint16_t)std::min<size_t>(Controls.size(), 0xFFFF));
	w.U16(0); w.U16(0);
	w.U16((uint16_t)CX); w.U16((uint16_t)CY);
	w.U16(0);	// no menu
	w.U16(0);	// no custom class
	w.Str(Title);
	if (style & DS_SETFONT_) {
		w.U16(PointSize ? PointSize : 8);
		w.U16(Weight);
		w.U8(Italic);
		w.U8(Charset);
		w.Str(Typeface);
	}
	for (size_t i = 0; i < Controls.size() && i < 0xFFFF; i++) {
		auto& c = Controls[i];
		w.Align(4);
		w.U32(0);
		w.U32(c.ExStyle);
		w.U32(c.Style & ~WS_POPUP);
		w.U16((uint16_t)c.X); w.U16((uint16_t)c.Y);
		w.U16((uint16_t)c.CX); w.U16((uint16_t)c.CY);
		w.U32((uint32_t)c.Id);
		w.Str(c.ClassName);
		// resource ordinals (icons, bitmaps) would resolve against this process, not the inspected file
		bool ordinal = c.Title.size() > 1 && c.Title[0] == L'#' &&
			std::all_of(c.Title.begin() + 1, c.Title.end(), [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; });
		w.Str(ordinal ? std::wstring() : c.Title);
		w.U16(0);
	}
	return std::move(w.Buf);
}
