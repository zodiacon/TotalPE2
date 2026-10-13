#include "pch.h"
#include "ClrIL.h"
#include "ClrMetadata.h"
#include "PEFile.h"
#include <map>
#include <unordered_set>

namespace {
	using enum IlOperand;

	// one-byte opcodes, 0x00 - 0xE0 (null: not an opcode)
	const IlOpcode OneByte[] = {
		{ L"nop", None }, { L"break", None }, { L"ldarg.0", None }, { L"ldarg.1", None }, { L"ldarg.2", None }, { L"ldarg.3", None },
		{ L"ldloc.0", None }, { L"ldloc.1", None }, { L"ldloc.2", None }, { L"ldloc.3", None },
		{ L"stloc.0", None }, { L"stloc.1", None }, { L"stloc.2", None }, { L"stloc.3", None },
		{ L"ldarg.s", Argument8 }, { L"ldarga.s", Argument8 }, { L"starg.s", Argument8 },
		{ L"ldloc.s", Local8 }, { L"ldloca.s", Local8 }, { L"stloc.s", Local8 },	// 0x13
		{ L"ldnull", None }, { L"ldc.i4.m1", None }, { L"ldc.i4.0", None }, { L"ldc.i4.1", None }, { L"ldc.i4.2", None },
		{ L"ldc.i4.3", None }, { L"ldc.i4.4", None }, { L"ldc.i4.5", None }, { L"ldc.i4.6", None }, { L"ldc.i4.7", None },
		{ L"ldc.i4.8", None }, { L"ldc.i4.s", Int8 }, { L"ldc.i4", Int32 }, { L"ldc.i8", Int64 }, { L"ldc.r4", Float32 },
		{ L"ldc.r8", Float64 },	// 0x23
		{ nullptr, None }, { L"dup", None }, { L"pop", None }, { L"jmp", Method }, { L"call", Method }, { L"calli", Signature },
		{ L"ret", None },	// 0x2A
		{ L"br.s", Branch8 }, { L"brfalse.s", Branch8 }, { L"brtrue.s", Branch8 }, { L"beq.s", Branch8 }, { L"bge.s", Branch8 },
		{ L"bgt.s", Branch8 }, { L"ble.s", Branch8 }, { L"blt.s", Branch8 }, { L"bne.un.s", Branch8 }, { L"bge.un.s", Branch8 },
		{ L"bgt.un.s", Branch8 }, { L"ble.un.s", Branch8 }, { L"blt.un.s", Branch8 },	// 0x37
		{ L"br", Branch32 }, { L"brfalse", Branch32 }, { L"brtrue", Branch32 }, { L"beq", Branch32 }, { L"bge", Branch32 },
		{ L"bgt", Branch32 }, { L"ble", Branch32 }, { L"blt", Branch32 }, { L"bne.un", Branch32 }, { L"bge.un", Branch32 },
		{ L"bgt.un", Branch32 }, { L"ble.un", Branch32 }, { L"blt.un", Branch32 },	// 0x44
		{ L"switch", Switch },
		{ L"ldind.i1", None }, { L"ldind.u1", None }, { L"ldind.i2", None }, { L"ldind.u2", None }, { L"ldind.i4", None },
		{ L"ldind.u4", None }, { L"ldind.i8", None }, { L"ldind.i", None }, { L"ldind.r4", None }, { L"ldind.r8", None },
		{ L"ldind.ref", None }, { L"stind.ref", None }, { L"stind.i1", None }, { L"stind.i2", None }, { L"stind.i4", None },
		{ L"stind.i8", None }, { L"stind.r4", None }, { L"stind.r8", None },	// 0x57
		{ L"add", None }, { L"sub", None }, { L"mul", None }, { L"div", None }, { L"div.un", None }, { L"rem", None },
		{ L"rem.un", None }, { L"and", None }, { L"or", None }, { L"xor", None }, { L"shl", None }, { L"shr", None },
		{ L"shr.un", None }, { L"neg", None }, { L"not", None },	// 0x66
		{ L"conv.i1", None }, { L"conv.i2", None }, { L"conv.i4", None }, { L"conv.i8", None }, { L"conv.r4", None },
		{ L"conv.r8", None }, { L"conv.u4", None }, { L"conv.u8", None },	// 0x6E
		{ L"callvirt", Method }, { L"cpobj", Type }, { L"ldobj", Type }, { L"ldstr", String }, { L"newobj", Method },
		{ L"castclass", Type }, { L"isinst", Type }, { L"conv.r.un", None },	// 0x76
		{ nullptr, None }, { nullptr, None }, { L"unbox", Type }, { L"throw", None },
		{ L"ldfld", Field }, { L"ldflda", Field }, { L"stfld", Field }, { L"ldsfld", Field }, { L"ldsflda", Field },
		{ L"stsfld", Field }, { L"stobj", Type },	// 0x81
		{ L"conv.ovf.i1.un", None }, { L"conv.ovf.i2.un", None }, { L"conv.ovf.i4.un", None }, { L"conv.ovf.i8.un", None },
		{ L"conv.ovf.u1.un", None }, { L"conv.ovf.u2.un", None }, { L"conv.ovf.u4.un", None }, { L"conv.ovf.u8.un", None },
		{ L"conv.ovf.i.un", None }, { L"conv.ovf.u.un", None },	// 0x8B
		{ L"box", Type }, { L"newarr", Type }, { L"ldlen", None }, { L"ldelema", Type },
		{ L"ldelem.i1", None }, { L"ldelem.u1", None }, { L"ldelem.i2", None }, { L"ldelem.u2", None }, { L"ldelem.i4", None },
		{ L"ldelem.u4", None }, { L"ldelem.i8", None }, { L"ldelem.i", None }, { L"ldelem.r4", None }, { L"ldelem.r8", None },
		{ L"ldelem.ref", None },	// 0x9A
		{ L"stelem.i", None }, { L"stelem.i1", None }, { L"stelem.i2", None }, { L"stelem.i4", None }, { L"stelem.i8", None },
		{ L"stelem.r4", None }, { L"stelem.r8", None }, { L"stelem.ref", None },	// 0xA2
		{ L"ldelem", Type }, { L"stelem", Type }, { L"unbox.any", Type },	// 0xA5
		{ nullptr, None }, { nullptr, None }, { nullptr, None }, { nullptr, None }, { nullptr, None }, { nullptr, None },
		{ nullptr, None }, { nullptr, None }, { nullptr, None }, { nullptr, None }, { nullptr, None }, { nullptr, None },
		{ nullptr, None },	// 0xA6 - 0xB2
		{ L"conv.ovf.i1", None }, { L"conv.ovf.u1", None }, { L"conv.ovf.i2", None }, { L"conv.ovf.u2", None },
		{ L"conv.ovf.i4", None }, { L"conv.ovf.u4", None }, { L"conv.ovf.i8", None }, { L"conv.ovf.u8", None },	// 0xBA
		{ nullptr, None }, { nullptr, None }, { nullptr, None }, { nullptr, None }, { nullptr, None }, { nullptr, None },
		{ nullptr, None },	// 0xBB - 0xC1
		{ L"refanyval", Type }, { L"ckfinite", None }, { nullptr, None }, { nullptr, None }, { L"mkrefany", Type },	// 0xC6
		{ nullptr, None }, { nullptr, None }, { nullptr, None }, { nullptr, None }, { nullptr, None }, { nullptr, None },
		{ nullptr, None }, { nullptr, None }, { nullptr, None },	// 0xC7 - 0xCF
		{ L"ldtoken", Token }, { L"conv.u2", None }, { L"conv.u1", None }, { L"conv.i", None }, { L"conv.ovf.i", None },
		{ L"conv.ovf.u", None }, { L"add.ovf", None }, { L"add.ovf.un", None }, { L"mul.ovf", None }, { L"mul.ovf.un", None },
		{ L"sub.ovf", None }, { L"sub.ovf.un", None }, { L"endfinally", None }, { L"leave", Branch32 }, { L"leave.s", Branch8 },
		{ L"stind.i", None }, { L"conv.u", None },	// 0xE0
	};
	static_assert(_countof(OneByte) == 0xE1);

	// two-byte opcodes: 0xFE, then 0x00 - 0x1E
	const IlOpcode TwoByte[] = {
		{ L"arglist", None }, { L"ceq", None }, { L"cgt", None }, { L"cgt.un", None }, { L"clt", None }, { L"clt.un", None },
		{ L"ldftn", Method }, { L"ldvirtftn", Method }, { nullptr, None },
		{ L"ldarg", Argument16 }, { L"ldarga", Argument16 }, { L"starg", Argument16 },
		{ L"ldloc", Local16 }, { L"ldloca", Local16 }, { L"stloc", Local16 }, { L"localloc", None }, { nullptr, None },
		{ L"endfilter", None }, { L"unaligned.", UInt8 }, { L"volatile.", None }, { L"tail.", None }, { L"initobj", Type },
		{ L"constrained.", Type }, { L"cpblk", None }, { L"initblk", None }, { L"no.", UInt8 }, { L"rethrow", None },
		{ nullptr, None }, { L"sizeof", Type }, { L"refanytype", None }, { L"readonly.", None },
	};
	static_assert(_countof(TwoByte) == 0x1F);

	struct ByteReader {
		std::span<const std::byte> Data;
		size_t Pos{ 0 };
		bool Ok{ true };

		uint64_t Read(size_t size) {
			if (!Ok || Pos + size > Data.size()) {
				Ok = false;
				return 0;
			}
			uint64_t v = 0;
			for (size_t i = 0; i < size; i++)
				v |= (uint64_t)std::to_integer<uint8_t>(Data[Pos + i]) << (8 * i);
			Pos += size;
			return v;
		}
	};
}

IlOpcode const* GetIlOpcode(uint16_t opcode) {
	if (opcode < _countof(OneByte))
		return OneByte[opcode].Name ? &OneByte[opcode] : nullptr;
	if (opcode >= 0xFE00 && opcode - 0xFE00 < _countof(TwoByte))
		return TwoByte[opcode - 0xFE00].Name ? &TwoByte[opcode - 0xFE00] : nullptr;
	return nullptr;
}

bool DecodeIl(std::span<const std::byte> code, std::vector<IlInstruction>& instructions) {
	ByteReader r{ code };
	while (r.Pos < code.size()) {
		IlInstruction inst{};
		inst.Offset = (uint32_t)r.Pos;
		uint16_t opcode = (uint16_t)r.Read(1);
		if (opcode == 0xFE)
			opcode = 0xFE00 | (uint16_t)r.Read(1);
		auto op = GetIlOpcode(opcode);
		if (!r.Ok || !op)
			return false;
		inst.Opcode = opcode;
		inst.Operand = op->Operand;
		switch (op->Operand) {
			case Int8: inst.Value = (int8_t)r.Read(1); break;
			case UInt8: case Argument8: case Local8: inst.Value = r.Read(1); break;
			case Argument16: case Local16: inst.Value = r.Read(2); break;
			case Int32: inst.Value = (int32_t)r.Read(4); break;
			case Int64: inst.Value = (int64_t)r.Read(8); break;
			case Float32:
			{
				auto bits = (uint32_t)r.Read(4);
				float f;
				memcpy(&f, &bits, 4);
				inst.Real = f;
				break;
			}
			case Float64:
			{
				auto bits = r.Read(8);
				memcpy(&inst.Real, &bits, 8);
				break;
			}
			case Branch8:
			{
				auto delta = (int8_t)r.Read(1);
				inst.Value = (int64_t)r.Pos + delta;
				break;
			}
			case Branch32:
			{
				auto delta = (int32_t)r.Read(4);
				inst.Value = (int64_t)r.Pos + delta;
				break;
			}
			case Switch:
			{
				auto count = (uint32_t)r.Read(4);
				if (!r.Ok || (uint64_t)count * 4 > code.size() - r.Pos)
					return false;
				auto next = r.Pos + (size_t)count * 4;
				for (uint32_t i = 0; i < count; i++)
					inst.Targets.push_back((uint32_t)(next + (int32_t)r.Read(4)));
				break;
			}
			case Method: case Field: case Type: case Token: case String: case Signature:
				inst.Value = r.Read(4);
				break;
			case None:
				break;
		}
		if (!r.Ok)
			return false;
		inst.Size = (uint32_t)r.Pos - inst.Offset;
		instructions.push_back(std::move(inst));
	}
	return true;
}

bool ReadIlMethodBody(PEFile const& pe, uint32_t rva, IlMethodBody& body) {
	if (rva == 0)
		return false;
	auto offset = pe.GetOffsetFromRVA(rva);
	if (offset == 0 || offset >= pe.GetFileSize())
		return false;
	auto data = pe.GetSpan((uint32_t)offset, (uint32_t)(pe.GetFileSize() - offset));
	ByteReader r{ data };
	body.Rva = rva;
	auto first = (uint8_t)r.Read(1);
	uint32_t codeSize = 0;
	uint16_t flags = 0;
	if ((first & 3) == 2) {	// tiny
		body.HeaderSize = 1;
		body.MaxStack = 8;
		codeSize = first >> 2;
	}
	else if ((first & 3) == 3) {	// fat
		flags = (uint16_t)(first | (r.Read(1) << 8));
		body.HeaderSize = (flags >> 12) * 4;
		body.MaxStack = (uint32_t)r.Read(2);
		codeSize = (uint32_t)r.Read(4);
		body.LocalVarSigToken = (uint32_t)r.Read(4);
		body.InitLocals = (flags & 0x10) != 0;
		if (body.HeaderSize < 12)
			return false;
		r.Pos = body.HeaderSize;
	}
	else
		return false;
	if (!r.Ok || codeSize > data.size() - r.Pos)
		return false;
	body.Code = data.subspan(r.Pos, codeSize);
	r.Pos += codeSize;

	// the sections after the code (aligned on 4 bytes from the start of the header): the exception clauses
	bool more = (flags & 0x08) != 0;
	for (int sections = 0; more && sections < 64; sections++) {
		r.Pos = (r.Pos + 3) & ~size_t(3);
		auto kind = (uint8_t)r.Read(1);
		if (!r.Ok)
			break;
		bool fat = (kind & 0x40) != 0;
		more = (kind & 0x80) != 0;
		uint32_t size = fat ? (uint32_t)r.Read(3) : (uint32_t)r.Read(1);
		if (!fat)
			r.Read(2);	// reserved
		if (!r.Ok || size < 4)
			break;
		auto start = r.Pos - 4;
		if ((kind & 0x3F) == 1) {	// the exception handling table
			auto count = (size - 4) / (fat ? 24 : 12);
			for (uint32_t i = 0; i < count && r.Ok && i < 4096; i++) {
				IlExceptionClause c{};
				if (fat) {
					c.Flags = (uint32_t)r.Read(4);
					c.TryOffset = (uint32_t)r.Read(4);
					c.TryLength = (uint32_t)r.Read(4);
					c.HandlerOffset = (uint32_t)r.Read(4);
					c.HandlerLength = (uint32_t)r.Read(4);
				}
				else {
					c.Flags = (uint32_t)r.Read(2);
					c.TryOffset = (uint32_t)r.Read(2);
					c.TryLength = (uint32_t)r.Read(1);
					c.HandlerOffset = (uint32_t)r.Read(2);
					c.HandlerLength = (uint32_t)r.Read(1);
				}
				auto last = (uint32_t)r.Read(4);
				(c.Flags & 1 ? c.FilterOffset : c.ClassToken) = last;
				if (r.Ok)
					body.Clauses.push_back(c);
			}
		}
		r.Pos = start + size;
	}
	return true;
}

std::wstring IlQuote(std::wstring_view text) {
	std::wstring s = L"\"";
	for (auto c : text) {
		switch (c) {
			case L'"': s += L"\\\""; break;
			case L'\\': s += L"\\\\"; break;
			case L'\n': s += L"\\n"; break;
			case L'\r': s += L"\\r"; break;
			case L'\t': s += L"\\t"; break;
			case L'\0': s += L"\\0"; break;
			default:
				if (c < 0x20 || (c >= 0x7F && c < 0xA0) || (c >= 0xD800 && c < 0xE000))
					s += std::format(L"\\u{:04X}", (unsigned)c);
				else
					s += c;
		}
	}
	return s + L"\"";
}

namespace {
	std::wstring Label(int64_t offset) {
		return std::format(L"IL_{:04x}", (uint32_t)offset);
	}

	// how a real number is written: as C# would, but always with a decimal point or an exponent
	std::wstring RealText(double value) {
		if (std::isnan(value))
			return L"(NaN)";
		if (std::isinf(value))
			return value > 0 ? L"(+Infinity)" : L"(-Infinity)";
		auto text = std::format(L"{}", value);
		if (text.find_first_of(L".e") == std::wstring::npos)
			text += L".0";
		return text;
	}

	std::wstring OperandText(IlInstruction const& inst, ClrMetadata const& md, uint32_t method, bool hasThis,
		std::vector<std::wstring> const& locals) {
		switch (inst.Operand) {
			case None: return {};
			case Int8: case Int32: case Int64: case UInt8:
				return std::to_wstring(inst.Value);
			case Float32: case Float64:
				return RealText(inst.Real);
			case Branch8: case Branch32:
				return Label(inst.Value);
			case Switch:
			{
				std::wstring text = L"(";
				for (size_t i = 0; i < inst.Targets.size(); i++)
					text += (i ? L", " : L"") + Label(inst.Targets[i]);
				return text + L")";
			}
			case Argument8: case Argument16:
			{
				// argument 0 of an instance method is 'this'; the parameters are numbered from 1
				auto n = (uint32_t)inst.Value;
				if (hasThis && n == 0)
					return L"this";
				auto name = md.ParamName(method, hasThis ? n : n + 1);
				return name.empty() ? std::to_wstring(n) : name;
			}
			case Local8: case Local16:
				return std::format(L"V_{}", inst.Value);
			case String:
				if (auto s = md.UserString((uint32_t)inst.Value))
					return IlQuote(*s);
				return std::format(L"(string 0x{:08X})", (uint32_t)inst.Value);
			case Method: case Field: case Type: case Token: case Signature:
				return md.TokenText((uint32_t)inst.Value);
		}
		return {};
	}

	// the start and end of the blocks of exception handling, at an offset of the code
	struct Block {
		uint32_t Start, End;
		std::wstring Header;	// ".try", "catch [mscorlib]System.Exception", "finally"...
		uint32_t Token{ 0 };	// the type of a catch
	};
}

namespace {
	// the lines of the text, each with the token it refers to (0 for none)
	struct IlLines {
		std::vector<std::wstring> Text;
		std::vector<uint32_t> Tokens;

		void Add(std::wstring line, uint32_t token = 0) {
			Text.push_back(std::move(line));
			Tokens.push_back(token);
		}
		std::wstring Join(std::vector<uint32_t>* tokens) const {
			std::wstring text;
			for (auto& line : Text)
				text += line + L"\r\n";
			if (tokens)
				*tokens = Tokens;
			return text;
		}
	};

	void MethodLines(PEFile const& pe, ClrMetadata const& md, uint32_t method, IlLines& out, std::wstring const& indent) {
		auto& m = md.Methods()[method - 1];
		out.Add(indent + md.MethodDeclaration(method), (ClrTableId::MethodDef << 24) | method);
		out.Add(indent + L"{");
		if (md.EntryPointMethod() == method)
			out.Add(indent + L"\t.entrypoint");

		IlMethodBody body;
		if (m.Rva == 0 || (m.ImplFlags & 3) != 0) {
			// abstract, extern (P/Invoke), implemented by the runtime, or native code (C++/CLI)
			if (m.Rva && (m.ImplFlags & 3) == 1)
				out.Add(indent + std::format(L"\t// native code at RVA 0x{:X}", m.Rva));
			out.Add(indent + L"}");
			return;
		}
		if (!ReadIlMethodBody(pe, m.Rva, body)) {
			out.Add(indent + std::format(L"\t// the body at RVA 0x{:X} cannot be read", m.Rva));
			out.Add(indent + L"}");
			return;
		}

		out.Add(indent + std::format(L"\t// RVA 0x{:X}, {} bytes of code", m.Rva, body.Code.size()));
		out.Add(indent + std::format(L"\t.maxstack {}", body.MaxStack));
		auto locals = md.LocalTypes(body.LocalVarSigToken, method);
		for (size_t i = 0; i < locals.size(); i++) {
			auto local = std::format(L"[{}] {} V_{}", i, locals[i], i);
			if (i == 0)
				local = (body.InitLocals ? L"\t.locals init (" : L"\t.locals (") + local;
			else
				local = L"\t\t" + local;
			out.Add(indent + local + (i + 1 == locals.size() ? L")" : L","));
		}
		out.Add(L"");

		std::vector<IlInstruction> instructions;
		bool complete = DecodeIl(body.Code, instructions);

		// the blocks: a try that several handlers share is opened once
		std::vector<Block> blocks;
		for (auto& c : body.Clauses) {
			Block tryBlock{ c.TryOffset, c.TryOffset + c.TryLength, L".try" };
			if (std::find_if(blocks.begin(), blocks.end(), [&](Block const& b) {
				return b.Start == tryBlock.Start && b.End == tryBlock.End && b.Header == tryBlock.Header; }) == blocks.end())
				blocks.push_back(tryBlock);
			switch (c.Flags & 7) {
				case 0: blocks.push_back({ c.HandlerOffset, c.HandlerOffset + c.HandlerLength, L"catch " + md.TypeName(c.ClassToken), c.ClassToken }); break;
				case 1:
					blocks.push_back({ c.FilterOffset, c.HandlerOffset, L"filter" });
					blocks.push_back({ c.HandlerOffset, c.HandlerOffset + c.HandlerLength, L"" });	// the handler of the filter
					break;
				case 2: blocks.push_back({ c.HandlerOffset, c.HandlerOffset + c.HandlerLength, L"finally" }); break;
				case 4: blocks.push_back({ c.HandlerOffset, c.HandlerOffset + c.HandlerLength, L"fault" }); break;
				default: blocks.push_back({ c.HandlerOffset, c.HandlerOffset + c.HandlerLength, std::format(L"// handler 0x{:X}", c.Flags) }); break;
			}
		}
		// at an offset: the blocks that end, innermost first, then those that start, outermost first
		std::map<uint32_t, std::vector<Block const*>> starts, ends;
		for (auto& b : blocks) {
			starts[b.Start].push_back(&b);
			ends[b.End].push_back(&b);
		}
		for (auto& [_, list] : starts)
			std::stable_sort(list.begin(), list.end(), [](Block const* a, Block const* b) { return a->End > b->End; });
		for (auto& [_, list] : ends)
			std::stable_sort(list.begin(), list.end(), [](Block const* a, Block const* b) { return a->Start > b->Start; });

		bool hasThis = (m.Flags & 0x10) == 0;	// not static
		int depth = 1;
		auto tabs = [&] { return indent + std::wstring(std::max(depth, 1), L'\t'); };
		auto blockEvents = [&](uint32_t offset) {
			if (auto it = ends.find(offset); it != ends.end())
				for (size_t i = 0; i < it->second.size(); i++) {
					depth--;
					out.Add(tabs() + L"}");
				}
			if (auto it = starts.find(offset); it != starts.end())
				for (auto b : it->second) {
					if (!b->Header.empty())
						out.Add(tabs() + b->Header, b->Token);
					out.Add(tabs() + L"{");
					depth++;
				}
		};
		for (auto& inst : instructions) {
			blockEvents(inst.Offset);
			auto op = GetIlOpcode(inst.Opcode);
			auto operand = OperandText(inst, md, method, hasThis, locals);
			auto line = tabs() + Label(inst.Offset) + L":  " + op->Name;
			if (!operand.empty())
				line += std::wstring((size_t)std::max<int>(1, 11 - (int)wcslen(op->Name)), L' ') + operand;
			bool token = inst.Operand >= IlOperand::Method && inst.Operand != IlOperand::String;
			out.Add(std::move(line), token ? (uint32_t)inst.Value : 0);
		}
		blockEvents((uint32_t)body.Code.size());
		if (!complete) {
			auto at = instructions.empty() ? 0 : instructions.back().Offset + instructions.back().Size;
			out.Add(indent + std::format(L"\t// {} bytes at IL_{:04x} are not instructions", body.Code.size() - at, at));
		}
		out.Add(indent + L"}");
	}
}

std::wstring DisassembleIlMethod(PEFile const& pe, ClrMetadata const& md, uint32_t method, std::vector<uint32_t>* tokens) {
	if (method == 0 || method > md.Methods().size())
		return {};
	IlLines lines;
	MethodLines(pe, md, method, lines, L"");
	return lines.Join(tokens);
}

std::wstring DisassembleIlType(PEFile const& pe, ClrMetadata const& md, uint32_t type, std::vector<uint32_t>* tokens) {
	if (type == 0 || type > md.Types().size())
		return {};
	IlLines lines;
	auto& t = md.Types()[type - 1];
	lines.Add(md.TypeDeclaration(type), t.Extends);
	lines.Add(L"{");
	auto nested = md.NestedTypes(type);
	for (auto n : nested)
		lines.Add(std::format(L"\t// nested {} {}", md.TypeKind(n), md.Types()[n - 1].Name), (ClrTableId::TypeDef << 24) | n);
	if (!nested.empty())
		lines.Add(L"");
	auto fields = md.FieldsOf(type);
	for (auto f : fields)
		lines.Add(L"\t" + md.FieldDeclaration(f), (ClrTableId::Field << 24) | f);
	if (!fields.empty())
		lines.Add(L"");
	for (auto m : md.MethodsOf(type)) {
		MethodLines(pe, md, m, lines, L"\t");
		lines.Add(L"");
	}
	lines.Add(L"} // end of " + md.FullName(type));
	return lines.Join(tokens);
}

std::vector<IlStyle> IlLineStyles(std::string_view line) {
	static const std::unordered_set<std::string_view> keywords = {
		"public", "private", "family", "assembly", "famandassem", "famorassem", "privatescope", "static", "instance",
		"hidebysig", "newslot", "specialname", "rtspecialname", "abstract", "virtual", "final", "pinvokeimpl", "cil", "native",
		"runtime", "managed", "unmanaged", "optil", "preservesig", "internalcall", "synchronized", "noinlining",
		"aggressiveinlining", "nooptimization", "aggressiveoptimization", "interface", "auto", "sequential", "explicit",
		"ansi", "unicode", "autochar", "sealed", "import", "serializable", "beforefieldinit", "extends", "implements",
		"nested", "initonly", "literal", "notserialized", "init", "class", "valuetype", "catch", "finally", "filter", "fault",
		"vararg", "modreq", "modopt", "pinned", "method", "nullref", "cdecl", "stdcall", "thiscall", "fastcall",
	};
	static const std::unordered_set<std::string_view> types = {
		"void", "bool", "char", "int8", "uint8", "int16", "uint16", "int32", "uint32", "int64", "uint64", "float32", "float64",
		"string", "object", "typedref", "int", "uint",
	};
	static const std::unordered_set<std::string_view> directives = {
		".class", ".method", ".field", ".locals", ".maxstack", ".entrypoint", ".try",
	};
	auto isWord = [](char c) { return isalnum((unsigned char)c) || c == '_' || c == '.' || c == '`' || c == '$'; };

	std::vector<IlStyle> styles(line.size(), IlStyle::Default);
	bool afterLabel = false;	// the word after "IL_0000:" is the opcode
	size_t i = 0;
	while (i < line.size()) {
		auto c = line[i];
		if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') {
			std::fill(styles.begin() + i, styles.end(), IlStyle::Comment);
			break;
		}
		if (c == '"') {
			auto start = i++;
			while (i < line.size() && line[i] != '"')
				i += line[i] == '\\' ? 2 : 1;
			i = std::min(i + 1, line.size());
			std::fill(styles.begin() + start, styles.begin() + i, IlStyle::String);
			continue;
		}
		if (!isWord(c) && !(c == '-' && i + 1 < line.size() && isdigit((unsigned char)line[i + 1]))) {
			i++;
			continue;
		}
		auto start = i++;
		while (i < line.size() && isWord(line[i]))
			i++;
		auto word = line.substr(start, i - start);
		IlStyle style = IlStyle::Default;
		if (word.size() > 3 && word.starts_with("IL_")) {
			style = IlStyle::Label;
			if (i < line.size() && line[i] == ':') {
				afterLabel = true;
				std::fill(styles.begin() + start, styles.begin() + i, style);
				continue;
			}
		}
		else if (afterLabel)
			style = IlStyle::Opcode;
		else if (directives.contains(word))
			style = IlStyle::Directive;
		else if (keywords.contains(word))
			style = IlStyle::Keyword;
		else if (types.contains(word))
			style = IlStyle::Type;
		else if (isdigit((unsigned char)word[0]) || word[0] == '-')
			style = IlStyle::Number;
		afterLabel = false;
		std::fill(styles.begin() + start, styles.begin() + i, style);
	}
	return styles;
}

std::vector<IlStringUse> FindIlStringUses(PEFile const& pe, ClrMetadata const& md) {
	std::vector<IlStringUse> uses;
	auto const& methods = md.Methods();
	std::vector<IlInstruction> instructions;
	for (uint32_t i = 0; i < methods.size(); i++) {
		auto& m = methods[i];
		IlMethodBody body;
		if (m.Rva == 0 || (m.ImplFlags & 3) != 0 || !ReadIlMethodBody(pe, m.Rva, body))
			continue;
		instructions.clear();
		DecodeIl(body.Code, instructions);
		for (auto& inst : instructions)
			if (inst.Operand == IlOperand::String)
				if (auto rva = md.UserStringRva((uint32_t)inst.Value))
					uses.push_back({ rva, i + 1 });
	}
	std::sort(uses.begin(), uses.end(), [](auto& a, auto& b) { return a.Rva < b.Rva || (a.Rva == b.Rva && a.Method < b.Method); });
	uses.erase(std::unique(uses.begin(), uses.end(), [](auto& a, auto& b) { return a.Rva == b.Rva && a.Method == b.Method; }), uses.end());
	return uses;
}
