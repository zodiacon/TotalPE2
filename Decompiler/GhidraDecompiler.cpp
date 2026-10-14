#include "GhidraDecompiler.h"

#include "libdecomp.hh"
#include "sleigh_arch.hh"
#include "loadimage.hh"
#include "funcdata.hh"
#include "database.hh"
#include "printlanguage.hh"
#include "printc.hh"
#include "marshal.hh"
#include "xml.hh"

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace ghidra;

namespace {
	// The image as the decompiler reads it: bytes at virtual addresses, from the caller
	class CallbackLoadImage : public LoadImage {
	public:
		CallbackLoadImage(std::string const& target, GhidraDecompiler::ReadMemory read)
			: LoadImage("image"), m_Target(target), m_Read(std::move(read)) {}

		void loadFill(uint1* ptr, int4 size, const Address& addr) override {
			if (!m_Read(addr.getOffset(), ptr, (size_t)size)) {
				std::ostringstream s;
				addr.printRaw(s);
				throw DataUnavailError("No data at " + s.str());
			}
		}
		std::string getArchType() const override {
			return m_Target;
		}
		void adjustVma(long) override {}

	private:
		std::string m_Target;
		GhidraDecompiler::ReadMemory m_Read;
	};

	// A SLEIGH architecture whose image is read through the callback
	class CallbackArchitecture : public SleighArchitecture {
	public:
		CallbackArchitecture(std::string const& target, GhidraDecompiler::ReadMemory read, std::ostream* errors)
			: SleighArchitecture("image", target, errors), m_Read(std::move(read)), m_Target(target) {}

	protected:
		void buildLoader(DocumentStorage& store) override {
			collectSpecFiles(*errorstream);
			loader = new CallbackLoadImage(m_Target, m_Read);
		}

		// The basic types by the names C uses (as Ghidra's own user interface names them), not the library's
		// "int4" and "xunknown8". The first of a size and kind is the one the decompiler uses for it.
		void buildCoreTypes(DocumentStorage&) override {
			types->setCoreType("void", 1, TYPE_VOID, false);
			types->setCoreType("bool", 1, TYPE_BOOL, false);
			types->setCoreType("uchar", 1, TYPE_UINT, false);
			types->setCoreType("ushort", 2, TYPE_UINT, false);
			types->setCoreType("uint", 4, TYPE_UINT, false);
			types->setCoreType("ulonglong", 8, TYPE_UINT, false);
			types->setCoreType("char", 1, TYPE_INT, true);
			types->setCoreType("short", 2, TYPE_INT, false);
			types->setCoreType("int", 4, TYPE_INT, false);
			types->setCoreType("longlong", 8, TYPE_INT, false);
			types->setCoreType("float", 4, TYPE_FLOAT, false);
			types->setCoreType("double", 8, TYPE_FLOAT, false);
			types->setCoreType("float10", 10, TYPE_FLOAT, false);
			types->setCoreType("float16", 16, TYPE_FLOAT, false);
			// bytes of a size whose type is not known (Ghidra's "undefined")
			types->setCoreType("undef", 1, TYPE_UNKNOWN, false);
			types->setCoreType("undef2", 2, TYPE_UNKNOWN, false);
			types->setCoreType("undef4", 4, TYPE_UNKNOWN, false);
			types->setCoreType("undef8", 8, TYPE_UNKNOWN, false);
			types->setCoreType("code", 1, TYPE_CODE, false);
			types->setCoreType("wchar_t", 2, TYPE_INT, true);
			types->setCoreType("wchar32", 4, TYPE_INT, true);
			types->cacheCoreTypes();
		}

	private:
		GhidraDecompiler::ReadMemory m_Read;
		std::string m_Target;
	};

	// The C printer, with strings: a constant that is not known to point to characters (a pointer-sized integer, or a
	// pointer to anything), but points into read-only memory at a string, is printed as the string. Ghidra does that for
	// pointers to characters only (its own analysis types the strings of a program first).
	class StringPrintC : public PrintC {
	public:
		using PrintC::PrintC;
		// the enums whose values are not flags (see GhidraDecompiler::Type::Exact)
		std::unordered_set<Datatype const*> ExactEnums;

	protected:
		void pushConstant(uintb val, const Datatype* ct, tagtype tag, const Varnode* vn, const PcodeOp* op, uint4 displayFormat) override {
			// a value of such an enum: by its name, or as a number (not as the | of names that it happens to be)
			if (ct->isEnumType() && ExactEnums.count(ct)) {
				TypeEnum::Representation rep;
				static_cast<TypeEnum const*>(ct)->getMatches(val, rep);
				if (rep.matchname.size() == 1 && !rep.complement && rep.shiftAmount == 0)
					pushAtom(Atom(rep.matchname[0], tag, EmitMarkup::const_color, op, vn, val));
				else
					push_integer(val, ct->getSize(), ct->getMetatype() == TYPE_INT, tag, vn, op, displayFormat);
				return;
			}
			if (val != 0 && LooksLikePointer(ct)) {
				Datatype* charType = nullptr;
				Address addr;
				if (FindString(val, op, addr, charType)) {
					std::ostringstream str;
					if (printCharacterConstant(str, addr, charType)) {
						pushAtom(Atom(str.str(), vartoken, EmitMarkup::const_color, op, vn));
						return;
					}
				}
			}
			PrintC::pushConstant(val, ct, tag, vn, op, displayFormat);
		}

	private:
		bool LooksLikePointer(const Datatype* ct) const {
			auto size = glb->getDefaultDataSpace()->getAddrSize();
			switch (ct->getMetatype()) {
				case TYPE_UNKNOWN: case TYPE_INT: case TYPE_UINT:
					return ct->getSize() == size;
				case TYPE_PTR:
				{
					auto to = static_cast<const TypePointer*>(ct)->getPtrTo()->getMetatype();
					return to == TYPE_UNKNOWN || to == TYPE_VOID;
				}
				default:
					return false;
			}
		}

		// A string of at least a few printable characters (UTF-16 or 8 bit), terminated, in read-only memory
		bool FindString(uintb val, const PcodeOp* op, Address& addr, Datatype*& charType) const {
			auto space = glb->getDefaultDataSpace();
			uintb encoding;
			addr = glb->resolveConstant(space, val, space->getAddrSize(), op ? op->getAddr() : Address(), encoding);
			if (addr.isInvalid() || !glb->symboltab->getGlobalScope()->isReadOnly(addr, 1, Address()))
				return false;
			uint1 bytes[512];
			int4 size = 0;
			try {
				// as much as there is, up to the buffer's size
				for (int4 chunk = 64; size < (int4)sizeof(bytes); size += chunk)
					glb->loader->loadFill(bytes + size, chunk, addr + size);
			}
			catch (DataUnavailError const&) {
			}
			bool wide = size >= 4 && bytes[0] != 0 && bytes[1] == 0;
			int count = 0;
			bool terminated = false;
			for (int4 i = 0; i + (wide ? 1 : 0) < size; i += wide ? 2 : 1) {
				unsigned ch = wide ? bytes[i] | (bytes[i + 1] << 8) : bytes[i];
				if (ch == 0) {
					terminated = true;
					break;
				}
				bool printable = ch == '\t' || ch == '\r' || ch == '\n' || (ch >= 0x20 && ch < 0x7F) ||
					(wide && ch >= 0xA0 && (ch < 0xD800 || ch > 0xDFFF) && ch < 0xFFFE);
				if (!printable)
					return false;
				count++;
			}
			if (!terminated || count < 3)
				return false;
			charType = wide ? glb->types->findByName("wchar_t") : glb->types->getBase(1, TYPE_INT);
			return charType != nullptr;
		}
	};

	bool s_Initialized = false;

	// The scope of a name and the name in it. A qualified C++ name ("ns::C::F") is taken whole, in the global scope: it is
	// printed as it is, and a scope of its own (one per namespace or class) would have to own the addresses of its symbols,
	// which the global scope does.
	Scope* ScopeOf(Architecture* arch, std::string const& name, std::string& basename) {
		basename = name;
		return arch->symboltab->getGlobalScope();
	}

	// A name the caller did not give (an unnamed struct, union or enum): one made up from the id
	std::string NameOr(std::string const& name, char const* kind, uint32_t id) {
		if (!name.empty() && name[0] != '<' && name.find("<unnamed") == std::string::npos && name.find("__unnamed") == std::string::npos)
			return name;
		return std::string(kind) + "_" + std::to_string(id);
	}
}

struct GhidraDecompiler::Impl {
	std::string Target;
	ReadMemory Read;
	std::ostringstream Messages;	// what the decompiler says (warnings)
	std::unique_ptr<CallbackArchitecture> Arch;
	uint64_t NextExternal{ 0 };		// where the next import goes (0: no area for them)
	std::unordered_map<uint64_t, std::string> ImportModules;	// the DLL of each import, by its address in the area
	std::unordered_map<uint64_t, std::string> Thunks;	// what is said of a thunk, by its address (once it is known to be one)

	TypeSource Types;
	PrototypeSource Prototypes;
	NameSource Names;
	LocalSource Locals;
	TypeNameSource TypeNames;
	ChangeSource Changes;
	CommentSource Comments;
	BlockSource Blocks;
	// (the code of a handler decompiled in a block's: its blocks not shown)
	bool Nested{ false };
	// (and the variables of the function the block is in, in the establisher frame its handler is given; by handler, the
	// structure of them its parameter points to)
	std::vector<Local> const* Frame{ nullptr };
	std::unordered_map<uint64_t, Datatype*> Frames;
	std::unordered_map<uint64_t, std::string> Applied;	// the changes each function was last decompiled with (as text)
	std::string Pending;	// what is to be said in the code of the next function decompiled (a change that could not be made)
	std::unordered_map<uint32_t, Datatype*> Converted;	// the decompiler's types of the caller's ids
	StringPrintC* Printer{ nullptr };
	std::unordered_set<uint64_t> Prototyped;			// the functions whose prototypes were asked for
	std::unordered_set<uint64_t> Localized;				// the functions whose local variables were asked for
	// The variables of the symbols in registers, by function: where their values are made. A value is rarely in the register
	// in the end (an instruction of x86 makes it in a temporary, copies it to the register, and the copy is propagated): a
	// value that an instruction that sets the register in the variable's ranges makes is the variable's; and one that is put
	// in the register by a copy of another register (a call's result kept), that register's value before it.
	struct RegisterLocal {
		std::string Name;
		Datatype* Type;
		std::set<uint64_t> Makes;
		std::map<uint64_t, std::vector<VarnodeData>> Results;
	};
	std::unordered_map<uint64_t, std::vector<RegisterLocal>> RegisterLocals;
	std::unordered_set<uint64_t> RegisterTyped;		// the functions whose variables in registers were given their types
	std::unordered_map<uint64_t, std::unordered_set<Symbol*>> StackLocals;	// the symbols of those on the stack, by function
	// What the user made of them (by function, by their names in the symbols: "pdb:" and the name is their key, which the
	// decompiler's variables they are in, and their types, do not change), and the keys of the decompiler's variables that are
	// theirs in the function decompiled last
	std::unordered_map<uint64_t, std::map<std::string, VariableChange>> RegisterChanges;
	std::unordered_map<Symbol*, std::string> RegisterKeys;
	std::unordered_map<std::string, std::string> Fixups;	// the call fixups of the compiler, by the names of the functions they replace

	Datatype* Unknown(uint32_t size);
	// a field of a struct or union as it is defined: a bit field has bits
	struct Member {
		std::string Name;
		int4 Offset;		// in bytes (of a bit field: of its first byte)
		Datatype* Type;
		int4 Bits{ 0 };		// of a bit field: its size, and its first bit in the byte at the offset
		int4 First{ 0 };
	};
	void DefineFields(Datatype* type, char const* metatype, uint32_t size, int4 alignment, std::vector<Member> const& fields);
	Datatype* DefineEnum(std::string const& name, uint32_t size, bool isSigned, std::vector<std::pair<std::string, uint64_t>> const& values);
	// a named type of a kind that is there already (the symbols describe a type more than once: const and not); a name
	// that a type of another kind has gets the id after it
	Datatype* Existing(std::string& name, type_metatype meta, uint32_t id);
	Datatype* Convert(uint32_t id, int depth = 0);
	// Types are nested deeply (a class with containers of containers): a recursion that goes deeper than this is not a
	// real type (structs and unions end their own recursion, by name)
	static constexpr int MaxDepth = 1000;
	bool ApplyPrototype(Funcdata* fd);
	void ApplyLocals(Funcdata* fd);
	std::vector<std::pair<Varnode*, RegisterLocal const*>> RegisterValues(Funcdata* fd) const;
	bool ApplyFixup(Funcdata* fd);
	void ApplyFrame(Funcdata* fd, std::vector<Local> const& frame);

	// the user's changes: types written as C, the changes to the variables of a function, and the variables it shows
	Datatype* NamedType(std::string const& name);
	Datatype* ParseType(std::string const& text, std::string& error);
	static std::string TypeText(Datatype const* type);
	void ApplyChanges(Funcdata* fd, std::vector<VariableChange> const& changes);
	std::string KeyOf(Funcdata* fd, Symbol* sym, Varnode* vn) const;
	void CollectVariables(Funcdata* fd, Variables& variables) const;
};

// Bytes of a size: the core type of the size, or an array of bytes
Datatype* GhidraDecompiler::Impl::Unknown(uint32_t size) {
	auto types = Arch->types;
	if (size == 1 || size == 2 || size == 4 || size == 8)
		return types->getBase((int4)size, TYPE_UNKNOWN);
	return types->getTypeArray(size == 0 ? 1 : (int4)size, types->getBase(1, TYPE_UNKNOWN));
}

// The fields of a struct or union that is defined by name only (a stub). The library sets fields from a description in
// its own format only (as Ghidra sends types): the description is written, and read back.
void GhidraDecompiler::Impl::DefineFields(Datatype* type, char const* metatype, uint32_t size, int4 alignment, std::vector<Member> const& fields) {
	std::ostringstream xml;
	XmlEncode encoder(xml);
	encoder.openElement(ELEM_TYPE);
	encoder.writeString(ATTRIB_NAME, type->getName());
	encoder.writeUnsignedInteger(ATTRIB_ID, type->getId());
	encoder.writeSignedInteger(ATTRIB_SIZE, (int4)size);
	encoder.writeString(ATTRIB_METATYPE, metatype);
	encoder.writeSignedInteger(ATTRIB_ALIGNMENT, alignment);
	for (size_t i = 0; i < fields.size(); i++) {
		auto const& f = fields[i];
		auto& element = f.Bits ? ELEM_BITFIELD : ELEM_FIELD;
		encoder.openElement(element);
		encoder.writeString(ATTRIB_NAME, f.Name);
		encoder.writeSignedInteger(ATTRIB_OFFSET, f.Offset);
		if (f.Bits) {
			encoder.writeSignedInteger(ATTRIB_ID, (int4)i);
			encoder.writeSignedInteger(ATTRIB_SIZE, f.Bits);
			encoder.writeSignedInteger(ATTRIB_FIRST, f.First);
		}
		else if ((int4)i != f.Offset)
			encoder.writeSignedInteger(ATTRIB_ID, (int4)i);
		f.Type->encodeRef(encoder);
		encoder.closeElement(element);
	}
	encoder.closeElement(ELEM_TYPE);

	std::istringstream in(xml.str());
	DocumentStorage store;
	auto doc = store.parseDocument(in);
	XmlDecode decoder(Arch.get(), doc->getRoot());
	Arch->types->decodeType(decoder);
}

// An enumeration of a size (the library makes them of one size only, unless it reads them): written, and read back
Datatype* GhidraDecompiler::Impl::DefineEnum(std::string const& name, uint32_t size, bool isSigned, std::vector<std::pair<std::string, uint64_t>> const& values) {
	std::ostringstream xml;
	XmlEncode encoder(xml);
	encoder.openElement(ELEM_TYPE);
	encoder.writeString(ATTRIB_NAME, name);
	// (the id is the hash of the name, as the library makes it when there is none)
	encoder.writeSignedInteger(ATTRIB_SIZE, (int4)size);
	encoder.writeString(ATTRIB_METATYPE, isSigned ? "enum_int" : "enum_uint");
	uint64_t mask = size >= 8 ? ~0ULL : (1ULL << (size * 8)) - 1;
	std::unordered_set<uint64_t> seen;
	for (auto const& [valueName, value] : values) {
		if (!seen.insert(value & mask).second)
			continue;	// the first name of a value
		encoder.openElement(ELEM_VAL);
		encoder.writeString(ATTRIB_NAME, valueName);
		encoder.writeUnsignedInteger(ATTRIB_VALUE, value & mask);
		encoder.closeElement(ELEM_VAL);
	}
	encoder.closeElement(ELEM_TYPE);

	std::istringstream in(xml.str());
	DocumentStorage store;
	auto doc = store.parseDocument(in);
	XmlDecode decoder(Arch.get(), doc->getRoot());
	return Arch->types->decodeType(decoder);
}

Datatype* GhidraDecompiler::Impl::Existing(std::string& name, type_metatype meta, uint32_t id) {
	auto existing = Arch->types->findByName(name);
	if (!existing)
		return nullptr;
	if (existing->getMetatype() == meta)
		return existing;
	name += "_" + std::to_string(id);
	existing = Arch->types->findByName(name);
	return existing && existing->getMetatype() == meta ? existing : nullptr;
}

// The decompiler's type of one of the caller's (made once). A struct or union is known (by name) before its fields are
// set, so that types that point to each other end.
Datatype* GhidraDecompiler::Impl::Convert(uint32_t id, int depth) {
	auto types = Arch->types;
	if (id == 0)
		return types->getTypeVoid();
	if (auto it = Converted.find(id); it != Converted.end())
		return it->second;
	Type t;
	if (!Types || depth > MaxDepth || !Types(id, t))
		return Unknown(t.Size);		// (not remembered: the same id may come again at a depth that is fine)

	Datatype* result = nullptr;
	try {
		switch (t.Kind) {
			case TypeKind::Void:
				result = types->getTypeVoid();
				break;

			case TypeKind::Integer:
				result = types->getBase(t.Size ? (int4)t.Size : 4, t.Signed ? TYPE_INT : TYPE_UINT);
				break;

			case TypeKind::Boolean:
				result = types->getBase(t.Size ? (int4)t.Size : 1, TYPE_BOOL);
				break;

			case TypeKind::Char:
				result = types->findByName(t.Size == 2 ? "wchar_t" : t.Size == 4 ? "wchar32" : "char");
				if (!result)
					result = types->getBase(t.Size ? (int4)t.Size : 1, TYPE_INT);
				break;

			case TypeKind::Float:
				result = types->getBase(t.Size ? (int4)t.Size : 8, TYPE_FLOAT);
				break;

			case TypeKind::Pointer:
			{
				auto target = Convert(t.Target, depth + 1);
				uint32_t size = t.Size ? t.Size : (uint32_t)Arch->getDefaultDataSpace()->getAddrSize();
				result = types->getTypePointer((int4)size, target, Arch->getDefaultDataSpace()->getWordSize());
				break;
			}

			case TypeKind::Array:
			{
				auto element = Convert(t.Target, depth + 1);
				if (element->getSize() <= 0 || t.Count == 0)
					result = Unknown(t.Size);
				else
					result = types->getTypeArray((int4)t.Count, element);
				break;
			}

			case TypeKind::Struct:
			{
				auto name = NameOr(t.Name, "struct", id);
				if (auto existing = Existing(name, TYPE_STRUCT, id)) {
					result = existing;
					break;
				}
				auto st = types->getTypeStruct(name);
				Converted[id] = st;
				if (st->isIncomplete() && t.Size > 0) {
					// in order, without overlaps (the members of unions without names are flat), inside the struct; bit
					// fields by their bits
					auto firstBit = [](Type::Field const& f) { return f.Offset * 8ULL + (f.Bits ? f.BitPosition : 0); };
					std::vector<Type::Field> sorted = t.Fields;
					std::stable_sort(sorted.begin(), sorted.end(), [&](auto& a, auto& b) { return firstBit(a) < firstBit(b); });
					std::vector<Member> fields;
					int4 alignment = 1;
					uint64_t end = 0;	// in bits
					for (auto const& f : sorted) {
						if (firstBit(f) < end)
							continue;
						auto ft = Convert(f.Type, depth + 1);
						if (ft->getSize() <= 0)
							continue;
						alignment = std::max(alignment, ft->getAlignment());
						if (f.Bits) {
							// of an integer (or a bool or an enum) only
							auto meta = ft->getMetatype();
							if (meta != TYPE_INT && meta != TYPE_UINT && meta != TYPE_BOOL && meta != TYPE_ENUM_INT && meta != TYPE_ENUM_UINT)
								continue;
							auto bit = firstBit(f);
							if ((bit + f.Bits + 7) / 8 > t.Size)
								continue;
							fields.push_back({ f.Name, (int4)(bit / 8), ft, (int4)f.Bits, (int4)(bit % 8) });
							end = bit + f.Bits;
						}
						else {
							if (f.Offset + (uint32_t)ft->getSize() > t.Size)
								continue;
							fields.push_back({ f.Name, (int4)f.Offset, ft });
							end = (f.Offset + (uint64_t)ft->getSize()) * 8;
						}
					}
					DefineFields(st, "struct", t.Size, alignment, fields);
				}
				result = st;
				break;
			}

			case TypeKind::Union:
			{
				auto name = NameOr(t.Name, "union", id);
				if (auto existing = Existing(name, TYPE_UNION, id)) {
					result = existing;
					break;
				}
				auto ut = types->getTypeUnion(name);
				Converted[id] = ut;
				if (ut->isIncomplete() && t.Size > 0) {
					std::vector<Member> fields;
					int4 alignment = 1;
					for (auto const& f : t.Fields) {
						if (f.Bits)
							continue;	// (a union's bit fields are rare: not shown)
						auto ft = Convert(f.Type, depth + 1);
						if (ft->getSize() <= 0 || f.Offset + (uint32_t)ft->getSize() > t.Size)
							continue;
						fields.push_back({ f.Name, (int4)f.Offset, ft });
						alignment = std::max(alignment, ft->getAlignment());
					}
					DefineFields(ut, "union", t.Size, alignment, fields);
				}
				result = ut;
				break;
			}

			case TypeKind::Enum:
			{
				auto name = NameOr(t.Name, "enum", id);
				auto meta = t.Signed ? TYPE_INT : TYPE_UINT;	// an enumeration's metatype is that of its values
				result = Existing(name, meta, id);
				if (!result)
					result = DefineEnum(name, t.Size ? t.Size : 4, t.Signed, t.Values);
				if (result && t.Exact && Printer)
					Printer->ExactEnums.insert(result);
				break;
			}

			case TypeKind::Typedef:
			{
				auto target = Convert(t.Target, depth + 1);
				try {
					result = t.Name.empty() ? target : types->getTypedef(target, t.Name, 0, 0);
				}
				catch (LowlevelError const&) {
					result = target;	// a name that is already another type's
				}
				break;
			}

			case TypeKind::Function:
			{
				PrototypePieces pieces;
				pieces.model = Arch->defaultfp;
				pieces.outtype = Convert(t.Target, depth + 1);
				for (auto parameter : t.Parameters) {
					pieces.intypes.push_back(Convert(parameter, depth + 1));
					pieces.innames.emplace_back();
				}
				pieces.firstVarArgSlot = t.Variadic ? (int4)pieces.intypes.size() : -1;
				result = types->getTypeCode(pieces);
				break;
			}

			default:
				result = Unknown(t.Size);
				break;
		}
	}
	catch (LowlevelError const& e) {
		Messages << "Type " << (t.Name.empty() ? std::to_string(id) : t.Name) << ": " << e.explain << "\n";
		result = nullptr;
	}
	catch (DecoderError const& e) {
		Messages << "Type " << (t.Name.empty() ? std::to_string(id) : t.Name) << ": " << e.explain << "\n";
		result = nullptr;
	}
	if (!result)
		result = Unknown(t.Size);
	Converted[id] = result;
	return result;
}

// The prototype of a function from the caller (once per function): its types and the names of its parameters, locked so
// that the decompiler keeps them. False if there is none (or it was set already).
bool GhidraDecompiler::Impl::ApplyPrototype(Funcdata* fd) {
	auto address = fd->getAddress().getOffset();
	if (!Prototypes || !Types || !Prototyped.insert(address).second)
		return false;
	uint32_t typeId = 0;
	std::vector<std::string> names;
	Type t;
	if (!Prototypes(address, typeId, names) || !Types(typeId, t) || t.Kind != TypeKind::Function)
		return false;
	try {
		PrototypePieces pieces;
		pieces.model = Arch->defaultfp;
		// (a member function's: "this", then where a structure it returns goes, as x64 and x86 pass them)
		if (t.Member)
			if (auto model = Arch->getModel("__thiscall"))
				pieces.model = model;
		pieces.name = fd->getName();
		pieces.outtype = Convert(t.Target);
		for (size_t i = 0; i < t.Parameters.size(); i++) {
			auto type = Convert(t.Parameters[i]);
			if (type->getSize() <= 0)
				return false;	// a parameter of no size: the prototype is not usable
			pieces.intypes.push_back(type);
			pieces.innames.push_back(i < names.size() ? names[i] : std::string());
		}
		pieces.firstVarArgSlot = t.Variadic ? (int4)pieces.intypes.size() : -1;
		auto& proto = fd->getFuncProto();
		proto.setPieces(pieces);
		proto.setInputLock(true);
		proto.setOutputLock(true);
		return true;
	}
	catch (LowlevelError const&) {
		return false;
	}
}

// The local variables of a function from the caller (once per function): symbols of the function's scope, their names and
// types locked, as Ghidra's own symbols from a PDB are. One on the stack that overlaps another (a variable of an inner block
// in the same place as one of another block) is left out.
//
// One in a register is known by where its values are made (see RegisterLocal), which its ranges and the instructions tell:
// the flow of the function is followed ahead of the decompiler, which then uses it.
void GhidraDecompiler::Impl::ApplyLocals(Funcdata* fd) {
	auto address = fd->getAddress().getOffset();
	if (!Locals || !Localized.insert(address).second)
		return;
	auto code = fd->getAddress().getSpace();
	std::vector<uint64_t> instructions;
	try {
		fd->followFlow(Address(code, 0), Address(code, code->getHighest()));
		for (auto it = fd->beginOpAll(); it != fd->endOpAll(); ++it)
			instructions.push_back(it->second->getAddr().getOffset());
		std::sort(instructions.begin(), instructions.end());
		instructions.erase(std::unique(instructions.begin(), instructions.end()), instructions.end());
	}
	catch (LowlevelError const&) {
		// (the decompiler finds it again, and says so)
		Arch->clearAnalysis(fd);
		instructions.clear();
	}
	std::vector<Local> locals;
	if (!Locals(address, instructions, locals))
		return;
	auto stack = Arch->getStackSpace();
	if (!stack)
		return;
	auto scope = fd->getScopeLocal();
	for (auto const& local : locals) {
		if (local.Name.empty() || !local.Register.empty())
			continue;
		try {
			auto type = Convert(local.Type);
			if (type->getSize() <= 0)
				continue;
			Address addr(stack, stack->wrapOffset((uintb)local.Offset));
			std::vector<Symbol*> named;
			scope->findByName(local.Name, named);
			if (!named.empty() || scope->findOverlap(addr, type->getSize()))
				continue;
			auto sym = scope->addSymbol(local.Name, type, addr, Address())->getSymbol();
			scope->setAttribute(sym, Varnode::namelock | Varnode::typelock);
			StackLocals[address].insert(sym);
		}
		catch (LowlevelError const&) {
			// one the decompiler does not take: the variable gets a name of its own
		}
	}
	if (instructions.empty())
		return;

	// in registers: the instructions in their ranges that set the register (the one before a range, and those in it but the
	// last, which may set it for the next variable)
	std::map<uint64_t, std::vector<PcodeOp*>> ops;
	for (auto it = fd->beginOpAll(); it != fd->endOpAll(); ++it)
		ops[it->second->getAddr().getOffset()].push_back(it->second);
	std::set<uint64_t> starts;
	auto const& blocks = fd->getBasicBlocks();
	for (int4 i = 0; i < blocks.getSize(); i++)
		if (auto start = blocks.getBlock(i)->getStart(); !start.isInvalid())
			starts.insert(start.getOffset());
	auto setter = [&](uint64_t at, VarnodeData const& place) -> PcodeOp* {
		auto it = ops.find(at);
		if (it != ops.end())
			for (auto op : it->second)
				if (auto out = op->getOut(); out && out->getSpace() == place.space && out->getOffset() < place.offset + place.size &&
					place.offset < out->getOffset() + out->getSize())
					return op;
		return nullptr;
	};
	auto& known = RegisterLocals[address];
	for (auto const& local : locals) {
		if (local.Name.empty() || local.Register.empty() || local.Ranges.empty())
			continue;
		try {
			auto type = Convert(local.Type);
			auto const& reg = Arch->translate->getRegister(local.Register);
			if (type->getSize() <= 0 || (uint4)type->getSize() > reg.size)
				continue;
			RegisterLocal result{ local.Name, type };
			auto add = [&](std::vector<uint64_t>::const_iterator at) {
				auto op = setter(*at, reg);
				if (!op)
					return;
				result.Makes.insert(*at);
				// a copy of another register: where that was set before it in its block (by a call: its result)
				Varnode* source = nullptr;
				for (auto copy : ops[*at])
					if (copy->code() == CPUI_COPY && copy->getIn(0)->getSpace() == reg.space && copy->getIn(0)->getOffset() != reg.offset) {
						source = copy->getIn(0);
						break;
					}
				if (!source)
					return;
				VarnodeData from{ source->getSpace(), source->getOffset(), (uint4)source->getSize() };
				for (auto back = at; !starts.count(*back) && back != instructions.begin(); ) {
					--back;
					auto const& those = ops[*back];
					if (setter(*back, from) || std::any_of(those.begin(), those.end(), [](PcodeOp* other) { return other->isCall(); })) {
						result.Results[*back].push_back(from);
						break;
					}
				}
			};
			for (auto [begin, end] : local.Ranges) {
				auto first = std::lower_bound(instructions.begin(), instructions.end(), begin);
				auto last = std::lower_bound(instructions.begin(), instructions.end(), end);
				if (first != instructions.begin())
					add(first - 1);
				if (first != last)
					for (auto it = first; it != last - 1; ++it)
						add(it);
			}
			if (result.Makes.empty())
				continue;
			// (the same variable of other blocks)
			auto same = std::find_if(known.begin(), known.end(), [&](auto const& other) { return other.Name == local.Name && other.Type == type; });
			if (same == known.end())
				known.push_back(std::move(result));
			else {
				same->Makes.insert(result.Makes.begin(), result.Makes.end());
				for (auto& [at, places] : result.Results)
					same->Results[at].insert(same->Results[at].end(), places.begin(), places.end());
			}
		}
		catch (LowlevelError const&) {
			// a register the decompiler does not know, a type it does not take: the variable gets a name of its own
		}
	}
}

// The values of the variables of the symbols in registers (see RegisterLocal) in a function's code: of an instruction, the
// last it makes (what it puts in the register: an address it loads from is made before)
std::vector<std::pair<Varnode*, GhidraDecompiler::Impl::RegisterLocal const*>> GhidraDecompiler::Impl::RegisterValues(Funcdata* fd) const {
	std::vector<std::pair<Varnode*, RegisterLocal const*>> values;
	auto known = RegisterLocals.find(fd->getAddress().getOffset());
	if (known == RegisterLocals.end())
		return values;
	std::map<std::pair<RegisterLocal const*, uint64_t>, size_t> made;
	auto later = [](Varnode* a, Varnode* b) {
		auto x = a->getDef(), y = b->getDef();
		return x->getParent() == y->getParent() && x->getSeqNum().getOrder() > y->getSeqNum().getOrder();
	};
	for (auto it = fd->beginLoc(); it != fd->endLoc(); ++it) {
		auto vn = *it;
		if (!vn->isWritten() || vn->isAnnotation())
			continue;
		auto at = vn->getDef()->getAddr().getOffset();
		for (auto const& local : known->second) {
			auto size = local.Type->getSize();
			bool match = vn->getSize() == size && local.Makes.count(at);
			if (auto results = local.Results.find(at); !match && results != local.Results.end())
				for (auto const& from : results->second)
					if (vn->getSize() >= size && vn->getSpace() == from.space && vn->getOffset() < from.offset + from.size &&
						from.offset < vn->getOffset() + vn->getSize())
						match = true;
			if (match) {
				auto [where, added] = made.try_emplace({ &local, at }, values.size());
				if (added)
					values.emplace_back(vn, &local);
				else if (later(vn, values[where->second].first))
					values[where->second].first = vn;
				break;
			}
		}
	}
	return values;
}

// A handler (a funclet) of the function a block is in: its second parameter is that function's establisher frame, a pointer
// to a structure of the variables there ("frame->h"). Those that overlap others (of other blocks) are left out.
void GhidraDecompiler::Impl::ApplyFrame(Funcdata* fd, std::vector<Local> const& frame) {
	auto address = fd->getAddress().getOffset();
	auto& type = Frames[address];
	if (!type) {
		std::vector<Local> sorted(frame);
		std::stable_sort(sorted.begin(), sorted.end(), [](auto const& a, auto const& b) { return a.Offset < b.Offset; });
		std::vector<Member> fields;
		std::unordered_set<std::string> names;
		int4 alignment = 1;
		int64_t end = 0;
		for (auto const& v : sorted) {
			if (v.Name.empty() || v.Offset < end || !names.insert(v.Name).second)
				continue;
			Datatype* ft = nullptr;
			try {
				ft = Convert(v.Type);
			}
			catch (LowlevelError const&) {
			}
			if (!ft || ft->getSize() <= 0)
				continue;
			fields.push_back({ v.Name, (int4)v.Offset, ft });
			alignment = std::max(alignment, ft->getAlignment());
			end = v.Offset + ft->getSize();
		}
		if (fields.empty())
			return;
		std::ostringstream name;
		name << "frame_" << std::hex << address;
		auto st = Arch->types->getTypeStruct(name.str());
		if (st->isIncomplete())
			DefineFields(st, "struct", (uint32_t)((end + alignment - 1) / alignment * alignment), alignment, fields);
		type = st;
	}
	try {
		auto& proto = fd->getFuncProto();
		auto space = Arch->getDefaultDataSpace();
		PrototypePieces pieces;
		pieces.model = Arch->defaultfp;
		pieces.name = fd->getName();
		pieces.outtype = proto.getOutputType();
		pieces.firstVarArgSlot = -1;
		pieces.intypes = { Unknown((uint32_t)space->getAddrSize()), Arch->types->getTypePointer(space->getAddrSize(), type, space->getWordSize()) };
		pieces.innames = { "", "frame" };
		proto.setPieces(pieces);
		proto.setInputLock(true);
		proto.setOutputLock(false);
	}
	catch (LowlevelError const&) {
	}
}

// A function that the compiler's specification replaces at its calls (by p-code of what the call does: one that moves the
// stack pointer, a prologue that is called...): true if it was not replaced before
bool GhidraDecompiler::Impl::ApplyFixup(Funcdata* fd) {
	auto it = Fixups.find(fd->getName());
	if (it == Fixups.end() || fd->getFuncProto().isInline())
		return false;
	auto id = Arch->pcodeinjectlib->getPayloadId(InjectPayload::CALLFIXUP_TYPE, it->second);
	if (id < 0)
		return false;
	fd->getFuncProto().setInjectId(id);
	return true;
}

namespace {
	std::string Trim(std::string const& text) {
		auto first = text.find_first_not_of(" \t");
		if (first == std::string::npos)
			return {};
		return text.substr(first, text.find_last_not_of(" \t") - first + 1);
	}

	// the words of a type, without those that do not change its storage ("const struct _X" -> "_X")
	std::string WithoutQualifiers(std::string const& text) {
		std::istringstream in(text);
		std::string result;
		for (std::string word; in >> word; ) {
			if (word == "const" || word == "volatile" || word == "struct" || word == "union" || word == "enum" || word == "class")
				continue;
			result += (result.empty() ? "" : " ") + word;
		}
		return result;
	}

	bool StartsWith(std::string const& text, char const* prefix) {
		return text.rfind(prefix, 0) == 0;
	}

	// Ghidra puts a comment of its own line at a column of its own (20): it goes where the statement after it starts
	void IndentComments(std::string& code) {
		std::vector<std::string> lines;
		std::istringstream in(code);
		for (std::string line; std::getline(in, line); )
			lines.push_back(line);
		auto isComment = [](std::string const& line) {
			auto first = line.find_first_not_of(' ');
			return first != std::string::npos && line.compare(first, 2, "/*") == 0 && line.size() >= first + 4 &&
				line.compare(line.size() - 2, 2, "*/") == 0;
		};
		for (size_t i = 0; i < lines.size(); i++) {
			if (!isComment(lines[i]))
				continue;
			// the indentation of the next line that is not a comment
			size_t next = i + 1;
			while (next < lines.size() && isComment(lines[next]))
				next++;
			if (next >= lines.size())
				continue;
			auto indent = lines[next].find_first_not_of(' ');
			if (indent == std::string::npos)
				continue;
			lines[i] = std::string(indent, ' ') + lines[i].substr(lines[i].find_first_not_of(' '));
		}
		std::string result;
		for (auto const& line : lines)
			result += line + "\n";
		if (!code.empty() && code.back() != '\n' && !result.empty())
			result.pop_back();
		code = std::move(result);
	}

	// The braces a line opens (+) and closes (-), in order, outside of strings, characters and comments; 'lowest': the
	// lowest the count gets in it
	int BraceBalance(std::string const& line, int& lowest) {
		int depth = 0;
		lowest = 0;
		for (size_t i = 0; i < line.size(); i++) {
			auto ch = line[i];
			if (ch == '"' || ch == '\'') {
				for (i++; i < line.size() && line[i] != ch; i++)
					if (line[i] == '\\')
						i++;
			}
			else if (ch == '/' && i + 1 < line.size() && line[i + 1] == '*') {
				auto end = line.find("*/", i + 2);
				if (end == std::string::npos)
					break;
				i = end + 1;
			}
			else if (ch == '/' && i + 1 < line.size() && line[i + 1] == '/')
				break;
			else if (ch == '{')
				depth++;
			else if (ch == '}')
				lowest = std::min(lowest, --depth);
		}
		return depth;
	}

	// The caller's blocks (a __try, a C++ try) in the code: the lines made from the instructions of a block's ranges are put
	// between its opening line and its closing lines, a level deeper. They must be a sequence of whole statements: none of
	// other code among them, and the braces they open closed in them (the lines that close those that are left open are
	// taken too). Else, comments mark where the block begins and ends.
	void ShowBlocks(std::string& code, GhidraDecompiler::LineAddresses& addresses, std::vector<GhidraDecompiler::Block> const& blocks) {
		std::vector<std::string> lines;
		std::istringstream in(code);
		for (std::string line; std::getline(in, line); )
			lines.push_back(line);
		// (the addresses may have an entry for the end of the text, after its last line)
		if (addresses.size() < lines.size() || addresses.size() > lines.size() + 1)
			return;
		GhidraDecompiler::LineAddresses extra(addresses.begin() + lines.size(), addresses.end());
		addresses.resize(lines.size());
		// the indentation of a level: of the first line of the body
		std::string unit = "  ";
		for (size_t i = 0; i + 1 < lines.size(); i++)
			if (lines[i] == "{") {
				auto indent = lines[i + 1].find_first_not_of(' ');
				if (indent != std::string::npos && indent > 0)
					unit = std::string(indent, ' ');
				break;
			}
		auto indentOf = [](std::string const& line) {
			auto indent = line.find_first_not_of(' ');
			return indent == std::string::npos ? std::string() : line.substr(0, indent);
		};
		auto trimmed = [](std::string const& line) {
			auto first = line.find_first_not_of(' '), last = line.find_last_not_of(' ');
			return first == std::string::npos ? std::string() : line.substr(first, last - first + 1);
		};
		// a line that ends a statement (or is none: empty, a comment, a label, "{"); one that does not goes on in the next
		auto terminated = [&](size_t i) {
			auto text = trimmed(lines[i]);
			return text.empty() || text.back() == ';' || text.back() == '{' || text.back() == '}' || text.back() == ':' ||
				(text.size() >= 2 && text.compare(text.size() - 2, 2, "*/") == 0);
		};
		auto begins = [&](size_t i, size_t bodyBegin) { return i <= bodyBegin || terminated(i - 1); };
		// a label (the decompiler's, at the start of the line, not at the level of its statements)
		auto isLabel = [&](size_t i) {
			auto text = trimmed(lines[i]);
			return text.size() > 1 && text.back() == ':' && text.find(' ') == std::string::npos && text != "default:";
		};
		auto closeLine = [&](std::string const& indent, std::string const& text) {
			size_t tabs = 0;
			while (tabs < text.size() && text[tabs] == '\t')
				tabs++;
			std::string line = indent;
			for (size_t i = 0; i < tabs; i++)
				line += unit;
			return line + text.substr(tabs);
		};

		// the body of the function: between its braces (the line of its name has the address of its entry)
		size_t bodyBegin = 0, bodyEnd = lines.size();
		for (size_t i = 0; i < lines.size(); i++)
			if (lines[i] == "{") {
				bodyBegin = i + 1;
				break;
			}
		for (size_t i = lines.size(); i > bodyBegin; i--)
			if (lines[i - 1] == "}") {
				bodyEnd = i - 1;
				break;
			}
		// (a comment cannot be in a comment: the closing lines, as a comment, without theirs)
		auto uncommented = [](std::string text) {
			for (auto marker : { "/* ", " */", "/*", "*/" })
				for (size_t at; (at = text.find(marker)) != std::string::npos; )
					text.erase(at, strlen(marker));
			return text;
		};

		for (auto const& block : blocks) {
			auto inside = [&](uint64_t address) {
				return std::any_of(block.Ranges.begin(), block.Ranges.end(), [&](auto const& r) { return address >= r.first && address < r.second; });
			};
			// 1: of the block's code, -1: of other code, 0: of none (braces, declarations, comments) or of both (a statement
			// the decompiler made of instructions in it and after it): it may be in the block, but does not make it longer
			// (if none is only of its code, those of both are its: "return *p;" of a __try that is only that)
			bool loose = false;
			auto kindOf = [&](size_t i) {
				if (addresses[i].empty())
					return 0;
				auto in = std::count_if(addresses[i].begin(), addresses[i].end(), inside);
				return in == 0 ? -1 : in == (ptrdiff_t)addresses[i].size() || loose ? 1 : 0;
			};
			size_t first = bodyEnd, last = 0;
			for (int pass = 0; pass < 2 && first == bodyEnd; pass++) {
				loose = pass == 1;
				for (size_t i = bodyBegin; i < bodyEnd; i++)
					if (kindOf(i) == 1) {
						first = std::min(first, i);
						last = i;
					}
			}
			// (as comments: the closing lines without the code of the handlers in them)
			auto isClosing = [](std::string const& text) {
				auto first = text.find_first_not_of('\t');
				return first != std::string::npos && (text[first] == '}' || text.compare(first, 2, "/*") == 0);
			};
			std::string closing;
			for (auto const& text : block.Close)
				if (isClosing(text))
					closing += (closing.empty() ? "" : " ") + uncommented(text.substr(std::min(text.size(), text.find_first_not_of('\t'))));
			if (first == bodyEnd) {
				// none of its code is shown (the decompiler took it away, or it is only in other functions)
				if (bodyBegin > 0) {
					lines.insert(lines.begin() + bodyBegin, unit + "/* " + block.Open + " " + closing + " (the code it guards is not shown) */");
					addresses.insert(addresses.begin() + bodyBegin, std::vector<uint64_t>());
					bodyEnd++;
				}
				continue;
			}
			// Whole statements: none of other code among them, the braces they close opened in them, those they open closed (by
			// the lines after them that are not of other code). A block that begins in one the caller placed (an inner one, of
			// lines of their own) begins before it.
			// (the last line of the statements from 'from' to 'to', and after it those that close their braces; npos if they are not)
			auto closes = [&](size_t from, size_t to) {
				bool ok = begins(from, bodyBegin);
				int depth = 0;
				size_t end = to;
				for (size_t i = from; i <= end && ok; i++) {
					int lowest;
					auto change = BraceBalance(lines[i], lowest);
					ok = kindOf(i) != -1 && depth + lowest >= 0;
					depth += change;
				}
				while (ok && depth > 0 && end + 1 < bodyEnd && kindOf(end + 1) != -1) {
					int lowest;
					end++;
					auto change = BraceBalance(lines[end], lowest);
					ok = depth + lowest >= 0;
					depth += change;
				}
				return ok && depth == 0 && terminated(end) ? end : std::string::npos;
			};
			// (from the start of the statement it begins in, to its end)
			while (first > bodyBegin && !terminated(first - 1) && kindOf(first - 1) != -1)
				first--;
			while (last + 1 < bodyEnd && !terminated(last) && kindOf(last + 1) != -1)
				last++;
			bool whole = false;
			auto start = first;
			for (int attempt = 0; attempt < 64 && !whole; attempt++) {
				if (auto end = closes(first, last); end != std::string::npos) {
					whole = true;
					last = end;
				}
				else if (first > bodyBegin && kindOf(first - 1) == 0 && addresses[first - 1].empty() &&
					lines[first - 1].find('{') != std::string::npos)
					first--;	// (the line that opens the block it begins in)
				else
					break;
			}
			// (of one that may go on after its code: the whole statements that have the most lines of its code)
			if (!whole && block.Approximate) {
				size_t best = 0, bestFirst = 0, bestLast = 0;
				for (size_t from = start; from <= last; from++) {
					if (kindOf(from) != 1 || !begins(from, bodyBegin))
						continue;
					int depth = 0;
					size_t count = 0;
					for (size_t i = from; i < bodyEnd && kindOf(i) != -1; i++) {
						int lowest;
						auto change = BraceBalance(lines[i], lowest);
						if (depth + lowest < 0)
							break;
						depth += change;
						if (kindOf(i) == 1)
							count++;
						if (depth == 0 && terminated(i) && count > best) {
							best = count;
							bestFirst = from;
							bestLast = i;
						}
					}
				}
				if (best) {
					whole = true;
					first = bestFirst;
					last = bestLast;
				}
			}
			// (of its first statement: a label is before the level of the statements)
			auto indent = indentOf(lines[first]);
			for (size_t i = first; i <= last; i++)
				if (!isLabel(i) && !trimmed(lines[i]).empty()) {
					indent = indentOf(lines[i]);
					break;
				}
			std::vector<std::string> after;
			if (whole) {
				for (size_t i = first; i <= last; i++)
					if (!lines[i].empty())
						lines[i] = unit + lines[i];
				for (auto const& text : block.Close)
					after.push_back(closeLine(indent, text));
				lines.insert(lines.begin() + first, indent + block.Open);
			}
			else {
				// (a comment for each part: "} catch (...) { ... }")
				std::string part;
				for (auto const& text : block.Close) {
					if (!isClosing(text))
						continue;
					auto line = uncommented(text.substr(std::min(text.size(), text.find_first_not_of('\t'))));
					if (!part.empty() && line.size() > 1 && line[0] == '}') {	// (a "}" alone ends the part)
						after.push_back(indent + "/* " + part + " */");
						part.clear();
					}
					part += (part.empty() ? "" : " ") + line;
				}
				if (!part.empty())
					after.push_back(indent + "/* " + part + " */");
				lines.insert(lines.begin() + first, indent + "/* " + uncommented(block.Open) + " */");
			}
			addresses.insert(addresses.begin() + first, std::vector<uint64_t>());
			last++;
			lines.insert(lines.begin() + last + 1, after.begin(), after.end());
			addresses.insert(addresses.begin() + last + 1, after.size(), std::vector<uint64_t>());
			bodyEnd += 1 + after.size();
		}

		addresses.insert(addresses.end(), extra.begin(), extra.end());
		std::string result;
		for (auto const& line : lines)
			result += line + "\n";
		code = std::move(result);
	}

	// Ghidra names a variable on the stack by its type, then the space and its offset ("uStack_c", "auStack_28", "lStackX_10"):
	// it is named local_ and the offset ("local_c", "local_28"; "localX_10" in the caller's part of the stack)
	std::string StackName(std::string const& name) {
		// (the type's letters: "u", "au", or of a type's name: "S" of SYSTEM_INFO)
		auto at = name.find("Stack");
		if (at == std::string::npos || !std::all_of(name.begin(), name.begin() + at, [](char c) { return isalpha((unsigned char)c) != 0; }))
			return {};
		auto rest = name.substr(at + 5);	// [X|Y]_offset
		size_t i = 0;
		if (i < rest.size() && (rest[i] == 'X' || rest[i] == 'Y'))
			i++;
		if (i + 1 >= rest.size() || rest[i] != '_' || rest.find_first_not_of("0123456789abcdef", i + 1) != std::string::npos)
			return {};
		return "local" + rest;
	}

	// Ghidra names the other variables by their type as well: a value in a register "uVar1" ("pcVar3"...), a global
	// variable "uRam0000000140003000". Without the type: "Var1", "Ram0000000140003000".
	// (the type's letters: "u", "pc", or of a type's name: "W" of WIN32_ERROR, "H" of HRESULT)
	std::string Untyped(std::string const& name) {
		for (char const* kind : { "Var", "Ram" }) {
			auto at = name.rfind(kind);
			auto length = strlen(kind);
			if (at == std::string::npos || at == 0 || name.size() <= at + length ||
				name.find_first_not_of(*kind == 'V' ? "0123456789" : "0123456789abcdef", at + length) != std::string::npos)
				continue;
			bool letters = std::all_of(name.begin(), name.begin() + at, [](char c) { return isalpha((unsigned char)c) != 0; });
			if (letters)
				return name.substr(at);
		}
		return {};
	}

	std::string Hex(uint64_t value) {
		std::ostringstream text;
		text << "0x" << std::hex << std::uppercase << value;
		return text.str();
	}
}

// A type by its name: the decompiler's (its own, and those of the symbols it was given), one of the symbols, or a name C
// and Windows have for an integer or a pointer
Datatype* GhidraDecompiler::Impl::NamedType(std::string const& name) {
	auto types = Arch->types;
	if (name == "void")
		return types->getTypeVoid();
	if (auto type = types->findByName(name))
		return type;
	if (StartsWith(name, "undefined"))
		if (auto type = types->findByName("undef" + name.substr(9)))
			return type;
	if (TypeNames && Types)
		if (auto id = TypeNames(name))
			if (auto type = Convert(id); type && type->getSize() > 0)
				return type;
	auto space = Arch->getDefaultDataSpace();
	int4 pointer = space->getAddrSize();
	bool windows = Target.find("windows") != std::string::npos;
	struct Synonym {
		char const* Name;
		int4 Size;		// 0: of a pointer, -1: of a long (4 on Windows, 8 elsewhere)
		type_metatype Meta;
		char const* Pointee;	// of a pointer: what it points to
	};
	static const Synonym synonyms[] = {
		{ "signed char", 1, TYPE_INT }, { "unsigned char", 1, TYPE_UINT }, { "short", 2, TYPE_INT }, { "short int", 2, TYPE_INT },
		{ "signed short", 2, TYPE_INT }, { "unsigned short", 2, TYPE_UINT }, { "signed", 4, TYPE_INT }, { "signed int", 4, TYPE_INT },
		{ "unsigned", 4, TYPE_UINT }, { "unsigned int", 4, TYPE_UINT }, { "long", -1, TYPE_INT }, { "long int", -1, TYPE_INT },
		{ "signed long", -1, TYPE_INT }, { "unsigned long", -1, TYPE_UINT }, { "long long", 8, TYPE_INT },
		{ "signed long long", 8, TYPE_INT }, { "unsigned long long", 8, TYPE_UINT }, { "__int8", 1, TYPE_INT },
		{ "__int16", 2, TYPE_INT }, { "__int32", 4, TYPE_INT }, { "__int64", 8, TYPE_INT }, { "unsigned __int8", 1, TYPE_UINT },
		{ "unsigned __int16", 2, TYPE_UINT }, { "unsigned __int32", 4, TYPE_UINT }, { "unsigned __int64", 8, TYPE_UINT },
		{ "int8_t", 1, TYPE_INT }, { "uint8_t", 1, TYPE_UINT }, { "int16_t", 2, TYPE_INT }, { "uint16_t", 2, TYPE_UINT },
		{ "int32_t", 4, TYPE_INT }, { "uint32_t", 4, TYPE_UINT }, { "int64_t", 8, TYPE_INT }, { "uint64_t", 8, TYPE_UINT },
		{ "size_t", 0, TYPE_UINT }, { "uintptr_t", 0, TYPE_UINT }, { "intptr_t", 0, TYPE_INT }, { "ptrdiff_t", 0, TYPE_INT },
		{ "ssize_t", 0, TYPE_INT }, { "BYTE", 1, TYPE_UINT }, { "UCHAR", 1, TYPE_UINT }, { "CHAR", 1, TYPE_INT },
		{ "BOOLEAN", 1, TYPE_UINT }, { "WORD", 2, TYPE_UINT }, { "USHORT", 2, TYPE_UINT }, { "SHORT", 2, TYPE_INT },
		{ "DWORD", 4, TYPE_UINT }, { "UINT", 4, TYPE_UINT }, { "ULONG", 4, TYPE_UINT }, { "INT", 4, TYPE_INT }, { "LONG", 4, TYPE_INT },
		{ "BOOL", 4, TYPE_INT }, { "NTSTATUS", 4, TYPE_INT }, { "HRESULT", 4, TYPE_INT }, { "QWORD", 8, TYPE_UINT },
		{ "DWORD64", 8, TYPE_UINT }, { "ULONG64", 8, TYPE_UINT }, { "ULONGLONG", 8, TYPE_UINT }, { "LONG64", 8, TYPE_INT },
		{ "LONGLONG", 8, TYPE_INT }, { "SIZE_T", 0, TYPE_UINT }, { "ULONG_PTR", 0, TYPE_UINT }, { "DWORD_PTR", 0, TYPE_UINT },
		{ "UINT_PTR", 0, TYPE_UINT }, { "LONG_PTR", 0, TYPE_INT }, { "INT_PTR", 0, TYPE_INT },
		{ "HANDLE", 0, TYPE_PTR, "void" }, { "PVOID", 0, TYPE_PTR, "void" }, { "LPVOID", 0, TYPE_PTR, "void" },
		{ "LPCVOID", 0, TYPE_PTR, "void" }, { "HMODULE", 0, TYPE_PTR, "void" }, { "HINSTANCE", 0, TYPE_PTR, "void" },
		{ "HKEY", 0, TYPE_PTR, "void" }, { "HWND", 0, TYPE_PTR, "void" }, { "LPSTR", 0, TYPE_PTR, "char" },
		{ "LPCSTR", 0, TYPE_PTR, "char" }, { "PSTR", 0, TYPE_PTR, "char" }, { "PCSTR", 0, TYPE_PTR, "char" },
		{ "LPWSTR", 0, TYPE_PTR, "wchar_t" }, { "LPCWSTR", 0, TYPE_PTR, "wchar_t" }, { "PWSTR", 0, TYPE_PTR, "wchar_t" },
		{ "PCWSTR", 0, TYPE_PTR, "wchar_t" },
	};
	if (name == "WCHAR")
		return types->findByName("wchar_t");
	for (auto const& synonym : synonyms) {
		if (name != synonym.Name)
			continue;
		if (synonym.Meta == TYPE_PTR)
			return types->getTypePointer(pointer, NamedType(synonym.Pointee), space->getWordSize());
		auto size = synonym.Size == 0 ? pointer : synonym.Size < 0 ? (windows ? 4 : 8) : synonym.Size;
		return types->getBase(size, synonym.Meta);
	}
	return nullptr;
}

// A type as C writes it: a name, then '*'s, then an array's count ("char *", "BYTE[16]", "struct _LIST_ENTRY *"). Null,
// with the reason, if it is not one a variable can have.
Datatype* GhidraDecompiler::Impl::ParseType(std::string const& text, std::string& error) {
	auto rest = Trim(text);
	uint32_t count = 0;
	if (!rest.empty() && rest.back() == ']') {
		auto open = rest.rfind('[');
		auto digits = open == std::string::npos ? std::string() : Trim(rest.substr(open + 1, rest.size() - open - 2));
		if (digits.empty() || digits.find_first_not_of("0123456789") != std::string::npos || digits.size() > 7) {
			error = "'" + text + "' is not a type (the size of an array is a number)";
			return nullptr;
		}
		count = (uint32_t)std::stoul(digits);
		rest = Trim(rest.substr(0, open));
	}
	int pointers = 0;
	while (!rest.empty() && rest.back() == '*') {
		pointers++;
		rest = Trim(rest.substr(0, rest.size() - 1));
	}
	auto name = WithoutQualifiers(rest);
	if (name.empty()) {
		error = "'" + text + "' is not a type";
		return nullptr;
	}
	auto type = NamedType(name);
	if (!type) {
		error = "the type '" + name + "' is not known";
		return nullptr;
	}
	auto types = Arch->types;
	auto space = Arch->getDefaultDataSpace();
	for (int i = 0; i < pointers; i++)
		type = types->getTypePointer(space->getAddrSize(), type, space->getWordSize());
	if (count) {
		if (type->getSize() <= 0) {
			error = "'" + text + "' is not a type a variable can have";
			return nullptr;
		}
		type = types->getTypeArray((int4)count, type);
	}
	if (type->getMetatype() == TYPE_VOID || type->getSize() <= 0) {
		error = "a variable cannot be '" + text + "'";
		return nullptr;
	}
	return type;
}

// a type as C writes it (as ParseType reads it)
std::string GhidraDecompiler::Impl::TypeText(Datatype const* type) {
	if (!type)
		return {};
	switch (type->getMetatype()) {
		case TYPE_PTR:
		{
			auto pointee = TypeText(static_cast<TypePointer const*>(type)->getPtrTo());
			return pointee + (!pointee.empty() && pointee.back() == '*' ? "*" : " *");
		}
		case TYPE_ARRAY:
		{
			auto array = static_cast<TypeArray const*>(type);
			return TypeText(array->getBase()) + "[" + std::to_string(array->numElements()) + "]";
		}
		default:
			return type->getDisplayName().empty() ? "undef" + std::to_string(type->getSize()) : type->getDisplayName();
	}
}

// The user's changes to the variables of a function (made before it is decompiled, after the prototype and the locals of
// the symbols are set): the parameters are set together, in the prototype, which is then locked; the others are symbols
// of the function's scope, their names (and, if retyped, their types) locked
void GhidraDecompiler::Impl::ApplyChanges(Funcdata* fd, std::vector<VariableChange> const& changes) {
	auto say = [&](VariableChange const& change, std::string const& what) {
		Messages << change.Name << ": " << what << "\n";
	};
	auto typeOf = [&](VariableChange const& change) -> Datatype* {
		std::string error;
		auto type = ParseType(change.Type, error);
		if (!type)
			say(change, error);
		return type;
	};

	// the parameters
	std::map<int, VariableChange const*> parameters;
	for (auto const& change : changes)
		if (StartsWith(change.Key, "param:"))
			parameters[std::atoi(change.Key.c_str() + 6)] = &change;
	if (!parameters.empty() && parameters.begin()->first >= 0 && parameters.rbegin()->first < 256) {
		auto& proto = fd->getFuncProto();
		bool locked = proto.isInputLocked(), outputLocked = proto.isOutputLocked(), modelLocked = proto.isModelLocked();
		PrototypePieces pieces;
		if (locked)
			proto.getPieces(pieces);
		else {
			// the parameters the decompiler found: all of them are among the changes (the user's, and the others with it)
			pieces.model = proto.hasModel() ? Arch->getModel(proto.getModelName()) : Arch->defaultfp;
			pieces.outtype = proto.getOutputType();
			pieces.firstVarArgSlot = -1;
			for (int i = 0; i <= parameters.rbegin()->first; i++) {
				pieces.intypes.push_back(Unknown((uint32_t)Arch->getDefaultDataSpace()->getAddrSize()));
				pieces.innames.push_back("param_" + std::to_string(i + 1));
			}
		}
		pieces.name = fd->getName();
		bool ok = true;
		for (auto [index, change] : parameters) {
			if (index >= (int)pieces.intypes.size()) {
				say(*change, "the function has no parameter " + std::to_string(index + 1));
				continue;
			}
			if (change->Retyped || !locked) {
				if (auto type = typeOf(*change))
					pieces.intypes[index] = type;
				else if (!locked)
					ok = false;
			}
			pieces.innames[index] = change->Name;
		}
		if (ok) {
			try {
				proto.setPieces(pieces);
				proto.setOutputLock(outputLocked);
				proto.setModelLock(modelLocked);
			}
			catch (LowlevelError const& e) {
				Messages << "The parameters could not be changed: " << e.explain << "\n";
			}
		}
	}

	// The other variables. A symbol is kept from one analysis to the next only if its type is locked: one that is only
	// renamed is locked to bytes of its size (which leaves the decompiler free to find their type, as Ghidra does with
	// "undef" types), unless it is larger than a register (an array: its type is kept).
	auto scope = fd->getScopeLocal();
	for (auto const& change : changes) {
		if (StartsWith(change.Key, "param:"))
			continue;
		// one of the symbols in a register: when its values are found
		if (StartsWith(change.Key, "pdb:")) {
			if (!change.Retyped || typeOf(change))
				RegisterChanges[fd->getAddress().getOffset()][change.Key.substr(4)] = change;
			continue;
		}
		auto type = typeOf(change);
		if (!type)
			continue;
		if (!change.Retyped)
			if (auto size = type->getSize(); size == 1 || size == 2 || size == 4 || size == 8)
				type = Unknown((uint32_t)size);
		uint4 locks = Varnode::namelock | Varnode::typelock;
		try {
			auto at = change.Key.find('@');
			auto where = at == std::string::npos ? 0 : std::stoull(change.Key.substr(at + 1), nullptr, 0);
			Address usepoint = where ? Address(Arch->getDefaultCodeSpace(), where) : Address();
			if (StartsWith(change.Key, "stack:")) {
				auto stack = Arch->getStackSpace();
				Address addr(stack, stack->wrapOffset((uintb)std::stoll(change.Key.substr(6))));
				// a variable of the symbols there is changed, rather than replaced
				if (auto entry = scope->findAddr(addr, Address()); entry && entry->getAddr() == addr) {
					auto sym = entry->getSymbol();
					scope->renameSymbol(sym, change.Name);
					if (change.Retyped)
						scope->retypeSymbol(sym, type);
					scope->setAttribute(sym, locks);
				}
				else if (scope->findOverlap(addr, type->getSize()))
					say(change, "the type is larger than the variable's place on the stack");
				else
					scope->setAttribute(scope->addSymbol(change.Name, type, addr, Address())->getSymbol(), locks);
			}
			else if (StartsWith(change.Key, "reg:") && at != std::string::npos) {
				auto name = change.Key.substr(4, at - 4);
				auto const& reg = Arch->translate->getRegister(name);
				if ((uint4)type->getSize() != reg.size) {
					say(change, "the variable is in " + name + ", its type is of " + std::to_string(reg.size) + " bytes");
					continue;
				}
				scope->setAttribute(scope->addSymbol(change.Name, type, Address(reg.space, reg.offset), usepoint)->getSymbol(), locks);
			}
			else if (StartsWith(change.Key, "hash:") && at != std::string::npos) {
				auto hash = std::stoull(change.Key.substr(5, at - 5), nullptr, 16);
				scope->setAttribute(scope->addDynamicSymbol(change.Name, type, usepoint, hash), locks);
			}
		}
		catch (LowlevelError const& e) {
			say(change, e.explain);
		}
		catch (std::exception const&) {
			say(change, "it is not known which variable it is (" + change.Key + ")");
		}
	}
}

// What identifies a variable from one decompilation to the next (see Variable); empty if there is nothing that does
std::string GhidraDecompiler::Impl::KeyOf(Funcdata* fd, Symbol* sym, Varnode* vn) const {
	if (sym->getCategory() == Symbol::function_parameter)
		return "param:" + std::to_string(sym->getCategoryIndex());
	if (auto it = RegisterKeys.find(sym); it != RegisterKeys.end())
		return it->second;
	auto entry = sym->getFirstWholeMap();
	if (!entry)
		return {};
	auto use = [&] {
		auto first = entry->getFirstUseAddress();
		return first.isInvalid() ? std::string("0") : Hex(first.getOffset());
	};
	if (sym->getScope()->isGlobal())
		return entry->isDynamic() ? std::string() : "global:" + Hex(entry->getAddr().getOffset());
	if (entry->isDynamic()) {
		std::ostringstream text;
		text << "hash:" << std::hex << std::uppercase << entry->getHash() << "@" << use();
		return text.str();
	}
	auto const& addr = entry->getAddr();
	auto space = addr.getSpace();
	if (space == Arch->getStackSpace()) {
		auto bits = space->getAddrSize() * 8;
		auto offset = (int64_t)addr.getOffset();
		if (bits < 64 && (offset & (1LL << (bits - 1))))
			offset -= 1LL << bits;
		return "stack:" + std::to_string(offset);
	}
	if (space->getType() == IPTR_PROCESSOR && space != Arch->getDefaultDataSpace()) {
		auto name = Arch->translate->getRegisterName(space, addr.getOffset(), entry->getSize());
		return name.empty() ? std::string() : "reg:" + name + "@" + use();
	}
	if (space->getType() == IPTR_INTERNAL && vn) {
		// a temporary: by the operations it is between
		DynamicHash hash;
		hash.uniqueHash(vn, fd);
		if (hash.getHash() == 0)
			return {};
		std::ostringstream text;
		text << "hash:" << std::hex << std::uppercase << hash.getHash() << "@" << Hex(hash.getAddress().getOffset());
		return text.str();
	}
	return {};
}

// The variables a function shows: those that have a name of their own (not the part of another one)
void GhidraDecompiler::Impl::CollectVariables(Funcdata* fd, Variables& variables) const {
	variables.clear();
	std::unordered_set<Symbol*> seen;
	auto constants = Arch->getConstantSpace();
	for (int4 i = 0; i < Arch->numSpaces(); i++) {
		auto space = Arch->getSpace(i);
		if (!space || space == constants)
			continue;
		for (auto it = fd->beginLoc(space); it != fd->endLoc(space); ++it) {
			auto vn = *it;
			if (vn->isFree())
				continue;
			auto high = vn->getHigh();
			if (!high || high->getNameRepresentative() != vn || high->getSymbolOffset() >= 0)
				continue;
			auto sym = high->getSymbol();
			if (!sym || sym->getName().empty() || !seen.insert(sym).second)
				continue;
			auto key = KeyOf(fd, sym, vn);
			if (!key.empty())
				variables.push_back({ sym->getName(), TypeText(sym->getType()), key, (uint32_t)std::max(0, sym->getType()->getSize()) });
		}
	}
	// and those of the function that are only referred to (an array on the stack, whose address is passed on)
	auto scope = fd->getScopeLocal();
	auto add = [&](SymbolEntry const* entry) {
		auto sym = entry->getSymbol();
		if (!sym || sym->getName().empty() || !seen.insert(sym).second)
			return;
		auto key = KeyOf(fd, sym, nullptr);
		if (!key.empty())
			variables.push_back({ sym->getName(), TypeText(sym->getType()), key, (uint32_t)std::max(0, sym->getType()->getSize()) });
	};
	for (auto it = scope->begin(); it != scope->end(); ++it)
		add(*it);
	for (auto it = scope->beginDynamic(); it != scope->endDynamic(); ++it)
		add(&*it);
}

bool GhidraDecompiler::Initialize(std::wstring const& languagesFolder, std::string& error) {
	if (s_Initialized)
		return true;
	try {
		// the decompiler's file names are narrow: the folder is expected to be ASCII
		auto narrow = [](std::wstring const& text) {
			std::string result;
			for (auto ch : text)
				result += (char)ch;
			return result;
		};
		std::vector<std::string> paths{ narrow(languagesFolder) };
		std::error_code ec;
		for (auto const& entry : std::filesystem::directory_iterator(languagesFolder, ec))
			if (entry.is_directory())
				paths.push_back(narrow(entry.path().wstring()));
		startDecompilerLibrary(paths);
		s_Initialized = true;
		return true;
	}
	catch (LowlevelError const& e) {
		error = e.explain;
	}
	return false;
}

GhidraDecompiler::GhidraDecompiler(std::string const& target, ReadMemory read) : m_Impl(std::make_unique<Impl>()) {
	m_Impl->Target = target;
	m_Impl->Read = std::move(read);
}

GhidraDecompiler::~GhidraDecompiler() = default;

bool GhidraDecompiler::Open(std::string& error) {
	if (m_Impl->Arch)
		return true;
	if (!s_Initialized) {
		error = "The decompiler is not initialized";
		return false;
	}
	try {
		auto arch = std::make_unique<CallbackArchitecture>(m_Impl->Target, m_Impl->Read, &m_Impl->Messages);
		DocumentStorage store;
		arch->init(store);
		// the printer of C with strings, in the place of Ghidra's (with its settings)
		auto print = new StringPrintC(arch.get(), "c-language");
		m_Impl->Printer = print;
		print->setOutputStream(arch->print->getOutputStream());
		print->initializeFromArchitecture();
		print->setBraceFormatFunction(Emit::next_line);	// the function's brace right below its declaration (not after a blank line)
		arch->printlist.push_back(print);
		arch->print = print;
		print->adjustTypeOperators();
		// The functions that the compiler's call fixups are for: the library registers the fixups by their names only
		// (Ghidra itself gives them to the functions by name)
		if (auto spec = store.getTag("compiler_spec")) {
			for (auto fixup : spec->getChildren()) {
				if (fixup->getName() != "callfixup")
					continue;
				try {
					auto const& name = fixup->getAttributeValue("name");
					for (auto target : fixup->getChildren())
						if (target->getName() == "target")
							m_Impl->Fixups[target->getAttributeValue("name")] = name;
				}
				catch (DecoderError const&) {
				}
			}
		}
		m_Impl->Arch = std::move(arch);
		return true;
	}
	catch (LowlevelError const& e) {
		error = e.explain;
	}
	catch (DecoderError const& e) {
		error = e.explain;
	}
	return false;
}

void GhidraDecompiler::AddFunction(uint64_t address, std::string const& name) {
	auto arch = m_Impl->Arch.get();
	if (!arch || name.empty())
		return;
	try {
		Address addr(arch->getDefaultCodeSpace(), address);
		auto global = arch->symboltab->getGlobalScope();
		if (global->queryFunction(addr))
			return;
		std::string basename;
		auto scope = ScopeOf(arch, name, basename);
		scope->addFunction(addr, basename);
	}
	catch (LowlevelError const&) {
		// a name the decompiler does not take: the function keeps a name of its own
	}
}

void GhidraDecompiler::AddReadOnly(uint64_t address, uint64_t size) {
	auto arch = m_Impl->Arch.get();
	if (!arch || size == 0)
		return;
	auto space = arch->getDefaultDataSpace();
	arch->symboltab->setPropertyRange(Varnode::readonly, Range(space, address, address + size - 1));
}

void GhidraDecompiler::SetExternalArea(uint64_t address) {
	m_Impl->NextExternal = address;
}

// The function an import is: a function without code (outside the image), and an external reference from the slot to it,
// which makes a call through the slot a call of the function
uint64_t GhidraDecompiler::AddImport(uint64_t slot, std::string const& name, std::string const& module) {
	auto arch = m_Impl->Arch.get();
	if (!arch || name.empty() || m_Impl->NextExternal == 0)
		return 0;
	try {
		Address target(arch->getDefaultCodeSpace(), m_Impl->NextExternal);
		m_Impl->NextExternal += 16;
		auto global = arch->symboltab->getGlobalScope();
		global->addFunction(target, name)->getFunction()->setNoCode(true);
		global->addExternalRef(Address(arch->getDefaultDataSpace(), slot), target, name);
		if (!module.empty())
			m_Impl->ImportModules[target.getOffset()] = module;
		return target.getOffset();
	}
	catch (LowlevelError const&) {
		// a name the decompiler does not take: the call stays a call through the pointer
	}
	return 0;
}

void GhidraDecompiler::SetTypeSource(TypeSource source) {
	m_Impl->Types = std::move(source);
}

void GhidraDecompiler::SetPrototypeSource(PrototypeSource source) {
	m_Impl->Prototypes = std::move(source);
}

void GhidraDecompiler::SetNameSource(NameSource source) {
	m_Impl->Names = std::move(source);
}

void GhidraDecompiler::SetLocalSource(LocalSource source) {
	m_Impl->Locals = std::move(source);
}

void GhidraDecompiler::SetTypeNameSource(TypeNameSource source) {
	m_Impl->TypeNames = std::move(source);
}

void GhidraDecompiler::SetChangeSource(ChangeSource source) {
	m_Impl->Changes = std::move(source);
}

void GhidraDecompiler::SetCommentSource(CommentSource source) {
	m_Impl->Comments = std::move(source);
}

void GhidraDecompiler::SetBlockSource(BlockSource source) {
	m_Impl->Blocks = std::move(source);
}

// The function gets the name (an empty one: the name it had, from the caller, or one made up)
void GhidraDecompiler::RenameFunction(uint64_t address, std::string const& name) {
	auto arch = m_Impl->Arch.get();
	if (!arch)
		return;
	try {
		Address addr(arch->getDefaultCodeSpace(), address);
		auto fname = name;
		if (fname.empty() && m_Impl->Names)
			fname = m_Impl->Names(address);
		if (fname.empty())
			arch->nameFunction(addr, fname);
		// (a function keeps the name it was made with: one with another name is made in its place, and its prototype and
		// changes are set again when it is decompiled)
		auto global = arch->symboltab->getGlobalScope();
		if (auto fd = global->queryFunction(addr)) {
			if (fd->getName() == fname)
				return;
			global->removeSymbol(fd->getSymbol());
			m_Impl->Prototyped.erase(address);
			m_Impl->Localized.erase(address);
			m_Impl->Applied.erase(address);
		}
		AddFunction(address, fname);
	}
	catch (LowlevelError const& e) {
		m_Impl->Pending += name + ": " + e.explain + "\n";
	}
}

// A symbol of the global scope at the address is renamed (and retyped), or made; an empty name removes the symbol
void GhidraDecompiler::ChangeGlobal(uint64_t address, VariableChange const& change) {
	auto arch = m_Impl->Arch.get();
	if (!arch)
		return;
	try {
		Address addr(arch->getDefaultDataSpace(), address);
		auto global = arch->symboltab->getGlobalScope();
		auto entry = global->findAddr(addr, Address());
		auto sym = entry && entry->getAddr() == addr ? entry->getSymbol() : nullptr;
		if (sym && dynamic_cast<FunctionSymbol*>(sym))
			return;		// code, not a variable
		if (change.Name.empty()) {
			if (sym)
				global->removeSymbol(sym);
			return;
		}
		std::string error;
		auto type = m_Impl->ParseType(change.Type, error);
		if (!type) {
			m_Impl->Pending += change.Name + ": " + error + "\n";
			return;
		}
		uint4 locks = Varnode::namelock | (change.Retyped ? Varnode::typelock : 0);
		if (sym) {
			global->renameSymbol(sym, change.Name);
			if (change.Retyped)
				global->retypeSymbol(sym, type);
			global->setAttribute(sym, locks);
		}
		else
			global->setAttribute(global->addSymbol(change.Name, type, addr, Address())->getSymbol(), locks);
	}
	catch (LowlevelError const& e) {
		m_Impl->Pending += change.Name + ": " + e.explain + "\n";
	}
}

void GhidraDecompiler::AddGlobal(uint64_t address, std::string const& name, uint32_t type) {
	auto arch = m_Impl->Arch.get();
	if (!arch || name.empty())
		return;
	try {
		Address addr(arch->getDefaultDataSpace(), address);
		if (arch->symboltab->getGlobalScope()->queryFunction(addr))
			return;		// code, not a variable
		auto ct = m_Impl->Convert(type);
		if (ct->getSize() <= 0)
			return;
		std::string basename;
		auto scope = ScopeOf(arch, name, basename);
		scope->addSymbol(basename, ct, addr, Address());
	}
	catch (LowlevelError const&) {
		// one that the decompiler does not take (overlapping another): it goes without
	}
}

namespace {
	// The code as the printer marks it up (packed: each token an element, the operation it shows by its time), as text and the
	// addresses of each line: those of the operations, labels and comments of the line; the prototype is at the entry.
	class MarkupReader {
	public:
		MarkupReader(Funcdata const* fd, std::string& text, GhidraDecompiler::LineAddresses& lines) : m_Text(text), m_Lines(lines) {
			for (auto it = fd->beginOpAll(); it != fd->endOpAll(); ++it)
				m_OpAddress.emplace(it->second->getTime(), it->second->getAddr().getOffset());
			m_Entry = fd->getAddress().getOffset();
			m_Lines.emplace_back();
		}

		// an element (a token, or a part of the code: a statement, a block...) and what is in it
		void Read(Decoder& decoder, bool proto = false) {
			auto id = decoder.openElement();
			if (id == ELEM_BREAK.getId()) {
				int indent = 0;
				for (uint4 attr; (attr = decoder.getNextAttributeId()) != 0; )
					if (attr == ATTRIB_INDENT.getId())
						indent = (int)decoder.readSignedInteger();
				m_Text += '\n';
				m_Lines.emplace_back();
				m_Text.append((size_t)std::max(0, indent), ' ');
				decoder.closeElement(id);
				return;
			}
			proto |= id == ELEM_FUNCPROTO.getId();
			bool located = id == ELEM_LABEL.getId() || id == ELEM_COMMENT.getId();
			for (uint4 attr; (attr = decoder.getNextAttributeId()) != 0; ) {
				if (attr == ATTRIB_OPREF.getId()) {
					if (auto it = m_OpAddress.find((uintm)decoder.readUnsignedInteger()); it != m_OpAddress.end())
						Add(it->second);
				}
				else if (attr == ATTRIB_OFF.getId() && located)
					Add(decoder.readUnsignedInteger());
				else if (attr == ATTRIB_CONTENT.getId()) {
					m_Text += decoder.readString();
					if (proto)
						Add(m_Entry);
				}
			}
			while (decoder.peekElement() != 0)
				Read(decoder, proto);
			decoder.closeElement(id);
		}

	private:
		void Add(uint64_t address) {
			auto& line = m_Lines.back();
			if (std::find(line.begin(), line.end(), address) == line.end())
				line.push_back(address);
		}

		std::string& m_Text;
		GhidraDecompiler::LineAddresses& m_Lines;
		std::unordered_map<uintm, uint64_t> m_OpAddress;
		uint64_t m_Entry;
	};
}

// The statements of a function, as lines of a block: a tab for its level, then those of their own. What it returns at its end
// is not one (a catch's handler returns where to continue, a __finally's nothing).
std::vector<std::string> GhidraDecompiler::BodyOf(uint64_t address, std::vector<Local> const& frame) {
	std::string text, error;
	m_Impl->Nested = true;
	m_Impl->Frame = &frame;
	bool ok = false;
	try {
		ok = Decompile(address, {}, text, error);
	}
	catch (...) {
	}
	m_Impl->Nested = false;
	m_Impl->Frame = nullptr;
	if (!ok)
		return {};
	std::vector<std::string> lines;
	std::istringstream in(text);
	for (std::string line; std::getline(in, line); )
		lines.push_back(line);
	size_t open = lines.size(), close = 0;
	for (size_t i = 0; i < lines.size(); i++)
		if (lines[i] == "{") {
			open = i;
			break;
		}
	for (size_t i = lines.size(); i > 0; i--)
		if (lines[i - 1] == "}") {
			close = i - 1;
			break;
		}
	if (open >= close)
		return {};
	std::vector<std::string> body(lines.begin() + open + 1, lines.begin() + close);
	auto trimmed = [](std::string const& line) {
		auto first = line.find_first_not_of(' ');
		return first == std::string::npos ? std::string() : line.substr(first);
	};
	auto indentOf = [](std::string const& line) {
		auto first = line.find_first_not_of(' ');
		return first == std::string::npos ? std::string::npos : first;
	};
	// the halts (where it does not return), and the return at its end
	size_t base = std::string::npos;
	for (auto const& line : body)
		if (!trimmed(line).empty()) {
			base = indentOf(line);
			break;
		}
	if (base == std::string::npos)
		return {};
	std::vector<std::string> result;
	for (auto const& line : body) {
		auto statement = trimmed(line);
		if (statement.rfind("halt_", 0) == 0 || statement.rfind("halt(", 0) == 0)
			continue;
		result.push_back(line);
	}
	while (!result.empty() && trimmed(result.back()).empty())
		result.pop_back();
	if (!result.empty() && indentOf(result.back()) == base && trimmed(result.back()).rfind("return", 0) == 0)
		result.pop_back();
	while (!result.empty() && trimmed(result.back()).empty())
		result.pop_back();
	// the declarations (before the first empty line; one may go on to the next lines) of the variables its statements do not
	// name are not shown
	if (auto blank = std::find_if(result.begin(), result.end(), [&](auto const& line) { return trimmed(line).empty(); }); blank != result.end()) {
		std::string statements;
		for (auto it = blank; it != result.end(); ++it)
			statements += *it + "\n";
		auto identifier = [](char c) { return isalnum((unsigned char)c) || c == '_' || c == '$'; };
		auto named = [&](std::string const& name) {
			for (auto at = statements.find(name); at != std::string::npos; at = statements.find(name, at + 1))
				if ((at == 0 || !identifier(statements[at - 1])) && (at + name.size() >= statements.size() || !identifier(statements[at + name.size()])))
					return true;
			return false;
		};
		std::vector<std::string> kept, declaration;
		for (auto it = result.begin(); it != blank; ++it) {
			declaration.push_back(*it);
			auto text = trimmed(*it);
			if (text.empty() || text.back() != ';')
				continue;
			// its name: the last identifier before the ";" (an array's before its "[")
			auto end = std::min(text.find('['), text.size() - 1);
			auto start = end;
			while (start > 0 && identifier(text[start - 1]))
				start--;
			auto name = text.substr(start, end - start);
			if (name.empty() || named(name))
				kept.insert(kept.end(), declaration.begin(), declaration.end());
			declaration.clear();
		}
		kept.insert(kept.end(), declaration.begin(), declaration.end());
		if (kept.empty())
			result.erase(result.begin(), blank + 1);
		else {
			auto rest = std::vector<std::string>(blank, result.end());
			result = std::move(kept);
			result.insert(result.end(), rest.begin(), rest.end());
		}
	}
	// (the empty line after the declarations, at the start, and none at all: nothing)
	bool any = std::any_of(result.begin(), result.end(), [&](auto const& line) { return !trimmed(line).empty(); });
	if (!any)
		return {};
	for (auto& line : result)
		line = line.size() > base ? "\t" + line.substr(base) : std::string("\t");
	return result;
}

bool GhidraDecompiler::Decompile(uint64_t address, std::string const& name, std::string& code, std::string& error, FunctionRefs* functions,
	LineAddresses* lines, Variables* variables) {
	if (!Open(error))
		return false;
	auto arch = m_Impl->Arch.get();
	try {
		Address addr(arch->getDefaultCodeSpace(), address);
		auto global = arch->symboltab->getGlobalScope();
		auto fd = global->queryFunction(addr);
		if (!fd) {
			std::string fname = name;
			if (fname.empty() && m_Impl->Names)
				fname = m_Impl->Names(address);
			if (fname.empty())
				arch->nameFunction(addr, fname);
			std::string basename;
			auto scope = ScopeOf(arch, fname, basename);
			fd = scope->addFunction(addr, basename)->getFunction();
		}
		// The user's changes to its variables: a function decompiled with others (or none) starts over, without the symbols
		// and the prototype they made
		std::vector<VariableChange> changes;
		if (m_Impl->Changes)
			m_Impl->Changes(address, changes);
		std::string applied;
		for (auto const& change : changes)
			applied += change.Key + "\t" + change.Name + "\t" + change.Type + (change.Retyped ? "\t+" : "") + "\n";
		bool changed = m_Impl->Applied[address] != applied;
		if (changed) {
			auto fname = fd->getName();
			global->removeSymbol(fd->getSymbol());
			m_Impl->Prototyped.erase(address);
			m_Impl->Localized.erase(address);
			m_Impl->RegisterLocals.erase(address);
			m_Impl->RegisterTyped.erase(address);
			m_Impl->RegisterChanges.erase(address);
			m_Impl->StackLocals.erase(address);
			fd = global->addFunction(addr, fname)->getFunction();
			m_Impl->Applied[address] = applied;
		}
		if (fd->isProcStarted())
			arch->clearAnalysis(fd);
		m_Impl->Messages.str({});		// what is said about this function
		m_Impl->Messages << m_Impl->Pending;
		m_Impl->Pending.clear();
		m_Impl->ApplyPrototype(fd);
		if (m_Impl->Nested && m_Impl->Frame && !m_Impl->Frame->empty())
			m_Impl->ApplyFrame(fd, *m_Impl->Frame);
		m_Impl->ApplyLocals(fd);
		if (changed)
			m_Impl->ApplyChanges(fd, changes);

		auto action = arch->allacts.getCurrent();
		auto run = [&] {
			action->reset(*fd);
			return action->perform(*fd);
		};
		int4 result = run();

		// An indirect jump to a known function (through the slot of an import, as a thunk does) is a tail call: a call, then a
		// return. Ghidra first takes it for a switch whose table it cannot find, with warnings, so it is told what it is.
		bool more = false;
		for (int4 i = 0; i < fd->numCalls(); i++) {
			auto fc = fd->getCallSpecs(i);
			if (fc->isBadJumpTable() && !fc->getEntryAddress().isInvalid() && fc->getOp()) {
				fd->getOverride().insertFlowOverride(fc->getOp()->getAddr(), Override::CALL_RETURN);
				more = true;
				// a function that is only that jump (a thunk) passes its parameters on, which cannot be seen in it (this is
				// remembered: when it is decompiled again, the jump is already a call)
				auto entry = fc->getEntryAddress().getOffset();
				if (fc->getOp()->getAddr() == fd->getAddress())
					if (auto it = m_Impl->ImportModules.find(entry); it != m_Impl->ImportModules.end()) {
						auto callee = global->queryFunction(fc->getEntryAddress());
						m_Impl->Thunks[address] = "A thunk: it jumps to " + (callee ? callee->getName() : std::string("the import")) + " in "
							+ it->second + ". The parameters it passes on are not known here.";
					}
			}
		}
		if (auto it = m_Impl->Thunks.find(address); it != m_Impl->Thunks.end()) {
			// (with a prototype, what it passes on is known)
			auto note = it->second;
			if (fd->getFuncProto().isInputLocked())
				note = note.substr(0, note.find(" The parameters"));
			m_Impl->Messages << note << "\n";
		}

		// the names and prototypes of the functions it calls: if there are any, it is decompiled again with them
		for (int4 i = 0; i < fd->numCalls(); i++) {
			auto fc = fd->getCallSpecs(i);
			auto const& entry = fc->getEntryAddress();
			if (entry.isInvalid())
				continue;
			auto callee = fc->getFuncdata();
			if (!callee)
				callee = global->queryFunction(entry);
			if (!callee && m_Impl->Names) {
				auto calleeName = m_Impl->Names(entry.getOffset());
				if (!calleeName.empty()) {
					AddFunction(entry.getOffset(), calleeName);
					callee = global->queryFunction(entry);
					more |= callee != nullptr;
				}
			}
			if (callee && m_Impl->ApplyPrototype(callee))
				more = true;
			if (callee && m_Impl->ApplyFixup(callee))
				more = true;
		}
		if (more) {
			arch->clearAnalysis(fd);
			result = run();
		}

		// The variables of the symbols in registers, of their types: once, a symbol (of the value, as the decompiler finds it
		// again) for a value of one whose type tells more than the decompiler's (a pointer to a structure...), then again
		auto& registerChanges = m_Impl->RegisterChanges[address];
		auto changeOf = [&](Impl::RegisterLocal const* local) -> VariableChange const* {
			auto it = registerChanges.find(local->Name);
			return it == registerChanges.end() ? nullptr : &it->second;
		};
		if (m_Impl->RegisterLocals.count(address) && m_Impl->RegisterTyped.insert(address).second) {
			auto scope = fd->getScopeLocal();
			bool typed = false;
			std::unordered_set<Impl::RegisterLocal const*> done;
			for (auto [vn, local] : m_Impl->RegisterValues(fd))
				try {
					auto type = local->Type;
					auto change = changeOf(local);
					std::string error;
					if (change && change->Retyped)
						type = m_Impl->ParseType(change->Type, error);
					if (!type)
						continue;
					auto meta = type->getMetatype();
					bool telling = (change && change->Retyped) || type->isEnumType() || meta == TYPE_STRUCT || meta == TYPE_UNION ||
						meta == TYPE_FLOAT || (meta == TYPE_PTR && ((TypePointer*)type)->getPtrTo()->getMetatype() != TYPE_VOID &&
							((TypePointer*)type)->getPtrTo()->getMetatype() != TYPE_UNKNOWN);
					auto high = vn->getHigh();
					if (!telling || high->getType() == type || (high->getSymbol() && high->getSymbol()->isTypeLocked()) ||
						!done.insert(local).second)
						continue;
					DynamicHash hash;
					hash.uniqueHash(vn, fd);
					if (hash.getHash() == 0)
						continue;
					scope->setAttribute(scope->addDynamicSymbol(change ? change->Name : local->Name, type, hash.getAddress(), hash.getHash()),
						Varnode::namelock | Varnode::typelock);
					typed = true;
				}
				catch (LowlevelError const&) {
				}
			if (typed) {
				arch->clearAnalysis(fd);
				result = run();
			}
		}

		// The variables the decompiler named are named without their types: those on the stack local_ and their offset,
		// those in registers Var and a number, the global ones Ram and their address
		{
			auto scope = fd->getScopeLocal();
			// the variables of the symbols in registers (one that has a symbol of its own is named)
			std::unordered_map<Symbol*, std::string> registerNames;
			m_Impl->RegisterKeys.clear();
			for (auto [vn, local] : m_Impl->RegisterValues(fd))
				try {
					auto sym = vn->getHigh()->getSymbol();
					if (!sym || m_Impl->RegisterKeys.count(sym))
						continue;
					m_Impl->RegisterKeys[sym] = "pdb:" + local->Name;
					if (!sym->isNameLocked())
						registerNames[sym] = changeOf(local) ? changeOf(local)->Name : local->Name;
				}
				catch (LowlevelError const&) {
				}
			// The variables of the symbols on the stack that the code of the function does not use (optimized code keeps them in
			// registers, or a handler of an exception uses them): not shown, nor the decompiler's warning about each (and the name
			// of one is free for its value in a register)
			if (auto known = m_Impl->StackLocals.find(address); known != m_Impl->StackLocals.end() && !known->second.empty()) {
				std::unordered_set<Symbol*> used;
				for (auto it = fd->beginLoc(); it != fd->endLoc(); ++it) {
					auto vn = *it;
					if (auto entry = vn->getSymbolEntry())
						used.insert(entry->getSymbol());
					if (!vn->isAnnotation() && (!vn->isFree() || vn->isConstant()))
						try {
							if (auto sym = vn->getHigh()->getSymbol())
								used.insert(sym);
						}
						catch (LowlevelError const&) {
						}
				}
				std::unordered_set<std::string> unused;
				for (auto it = known->second.begin(); it != known->second.end(); )
					if (!used.count(*it)) {
						unused.insert("WARNING: Variable defined which should be unmapped: " + (*it)->getName());
						scope->removeSymbol(*it);
						it = known->second.erase(it);
					}
					else
						++it;
				std::vector<Comment*> warnings;
				for (auto it = arch->commentdb->beginComment(fd->getAddress()); it != arch->commentdb->endComment(fd->getAddress()); ++it)
					if ((*it)->getType() == Comment::warningheader && unused.count((*it)->getText()))
						warnings.push_back(*it);
				for (auto comment : warnings)
					arch->commentdb->deleteComment(comment);
			}
			std::vector<std::pair<Symbol*, std::string>> renames;
			std::unordered_set<std::string> names;
			std::unordered_set<Symbol*> seen;
			auto consider = [&](Symbol* sym) {
				if (!sym || sym->isNameLocked() || !seen.insert(sym).second)
					return;
				auto registerName = registerNames.find(sym);
				auto name = registerName != registerNames.end() ? registerName->second :
					sym->getScope()->isGlobal() ? std::string() : StackName(sym->getName());
				if (name.empty())
					name = Untyped(sym->getName());
				if (name.empty())
					return;
				// (unique: a variable of the symbols may have the name already)
				auto unique = name;
				for (int n = 2; names.count(unique); n++)
					unique = name + "_" + std::to_string(n);
				names.insert(unique);
				renames.emplace_back(sym, unique);
			};
			for (auto it = scope->begin(); it != scope->end(); ++it)
				if (auto sym = (*it)->getSymbol())
					names.insert(sym->getName());
			for (auto it = scope->beginDynamic(); it != scope->endDynamic(); ++it)
				if (auto sym = it->getSymbol())
					names.insert(sym->getName());
			for (auto it = scope->begin(); it != scope->end(); ++it)
				consider((*it)->getSymbol());
			for (auto it = scope->beginDynamic(); it != scope->endDynamic(); ++it)
				consider(it->getSymbol());
			// the global variables the function uses (read and written, or referred to by their addresses)
			for (auto it = fd->beginLoc(); it != fd->endLoc(); ++it) {
				auto vn = *it;
				Symbol* sym = nullptr;
				if (auto entry = vn->getSymbolEntry())
					sym = entry->getSymbol();
				if (!sym && !vn->isAnnotation() && (!vn->isFree() || vn->isConstant()))
					try {
						sym = vn->getHigh()->getSymbol();
					}
					catch (LowlevelError const&) {
					}
				if (sym && sym->getScope()->isGlobal() && !dynamic_cast<FunctionSymbol*>(sym))
					consider(sym);
			}
			for (auto& [sym, name] : renames)
				sym->getScope()->renameSymbol(sym, name);
		}

		// the user's comments in the function's code (in its blocks: an instruction may not be an operation of its own)
		if (m_Impl->Comments) {
			std::vector<std::pair<uint64_t, std::string>> comments;
			m_Impl->Comments(comments);
			arch->commentdb->clearType(fd->getAddress(), Comment::user2);
			auto const& blocks = fd->getBasicBlocks();
			for (auto const& [at, text] : comments) {
				Address where(arch->getDefaultCodeSpace(), at);
				for (int4 i = 0; i < blocks.getSize(); i++) {
					auto block = static_cast<BlockBasic*>(blocks.getBlock(i));
					if (block->getStart() <= where && where <= block->getStop()) {
						arch->commentdb->addComment(Comment::user2, fd->getAddress(), where, text);
						break;
					}
				}
			}
		}

		// the caller's blocks (its __try regions, its C++ try blocks), and the names of the functions they refer to
		std::vector<Block> blocks;
		if (m_Impl->Blocks && !m_Impl->Nested)
			m_Impl->Blocks(address, blocks);
		for (auto const& block : blocks)
			for (auto const& [linkName, at] : block.Links)
				if (!global->queryFunction(Address(arch->getDefaultCodeSpace(), at)))
					AddFunction(at, linkName);
		// (the blocks are placed by the addresses of the lines)
		GhidraDecompiler::LineAddresses ownLines;
		auto lineMap = lines ? lines : blocks.empty() ? nullptr : &ownLines;

		std::ostringstream out;
		if (result < 0)
			out << "/* The decompiler stopped before it was done */\n";
		// the decompiler's messages (types it could not take, warnings), as a comment
		if (auto messages = m_Impl->Messages.str(); !messages.empty()) {
			out << "/*\n";
			std::istringstream lines(messages);
			for (std::string line; std::getline(lines, line); )
				if (!line.empty())
					out << " * " << line << "\n";
			out << " */\n";
		}
		if (lineMap) {
			// marked up: the text is read back from it, with the addresses of its lines (the lines of the comment above have none)
			lineMap->clear();
			auto header = out.str();
			for (auto ch : header)
				if (ch == '\n')
					lineMap->emplace_back();
			std::ostringstream markup;
			arch->print->setMarkup(true);
			arch->print->setOutputStream(&markup);
			try {
				arch->print->docFunction(fd);
			}
			catch (...) {
				arch->print->setMarkup(false);
				throw;
			}
			arch->print->setMarkup(false);
			std::istringstream in(markup.str());
			PackedDecode decoder(arch);
			decoder.ingestStream(in);
			std::string text;
			GhidraDecompiler::LineAddresses body;
			MarkupReader reader(fd, text, body);
			while (decoder.peekElement() != 0)
				reader.Read(decoder);
			code = header + text;
			if (!code.empty() && code.back() != '\n')
				code += '\n';
			lineMap->insert(lineMap->end(), body.begin(), body.end());
		}
		else {
			arch->print->setOutputStream(&out);
			arch->print->docFunction(fd);
			code = out.str();
		}

		IndentComments(code);
		// the code of the blocks' handlers, in them (after the code of this function is made: the decompiler is used again)
		for (auto& block : blocks) {
			std::sort(block.Bodies.begin(), block.Bodies.end(), [](auto const& a, auto const& b) { return a.Line > b.Line; });
			for (auto const& body : block.Bodies)
				if (body.Line < block.Close.size()) {
					auto statements = BodyOf(body.Address, body.Frame);
					block.Close.insert(block.Close.begin() + body.Line + 1, statements.begin(), statements.end());
				}
		}
		if (lineMap && !blocks.empty())
			ShowBlocks(code, *lineMap, blocks);
		if (variables)
			m_Impl->CollectVariables(fd, *variables);
		if (functions) {
			functions->clear();
			functions->emplace_back(fd->getName(), address);
			for (int4 i = 0; i < fd->numCalls(); i++) {
				auto fc = fd->getCallSpecs(i);
				auto const& entry = fc->getEntryAddress();
				if (entry.isInvalid())
					continue;
				if (!fc->getName().empty())
					functions->emplace_back(fc->getName(), entry.getOffset());
				if (auto callee = fc->getFuncdata(); callee && callee->getName() != fc->getName())
					functions->emplace_back(callee->getName(), entry.getOffset());
			}
			for (auto const& block : blocks)
				for (auto const& link : block.Links)
					functions->push_back(link);
		}
		return true;
	}
	catch (LowlevelError const& e) {
		error = e.explain;
	}
	catch (DecoderError const& e) {
		error = e.explain;
	}
	return false;
}
