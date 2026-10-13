#include "pch.h"
#include "ClrDecompiler.h"
#include "ClrMetadata.h"
#include "ClrIL.h"
#include <functional>
#include <map>
#include <set>

namespace {
	using K = ClrValueKind;

	constexpr uint32_t TableOf(uint32_t token) { return token >> 24; }
	constexpr uint32_t RowOf(uint32_t token) { return token & 0xFFFFFF; }
	constexpr uint32_t MethodToken(uint32_t row) { return (ClrTableId::MethodDef << 24) | row; }
	constexpr uint32_t FieldToken(uint32_t row) { return (ClrTableId::Field << 24) | row; }
	constexpr uint32_t TypeToken(uint32_t row) { return (ClrTableId::TypeDef << 24) | row; }

	bool IsKeyword(std::wstring_view word) {
		static const std::set<std::wstring_view> keywords = {
			L"abstract", L"as", L"base", L"bool", L"break", L"byte", L"case", L"catch", L"char", L"checked", L"class", L"const",
			L"continue", L"decimal", L"default", L"delegate", L"do", L"double", L"else", L"enum", L"event", L"explicit", L"extern",
			L"false", L"finally", L"fixed", L"float", L"for", L"foreach", L"goto", L"if", L"implicit", L"in", L"int", L"interface",
			L"internal", L"is", L"lock", L"long", L"namespace", L"new", L"null", L"object", L"operator", L"out", L"override",
			L"params", L"private", L"protected", L"public", L"readonly", L"ref", L"return", L"sbyte", L"sealed", L"short",
			L"sizeof", L"stackalloc", L"static", L"string", L"struct", L"switch", L"this", L"throw", L"true", L"try", L"typeof",
			L"uint", L"ulong", L"unchecked", L"unsafe", L"ushort", L"using", L"virtual", L"void", L"volatile", L"while",
		};
		return keywords.contains(word);
	}

	// A name as C# can write it: a keyword gets an '@'
	std::wstring Identifier(std::wstring const& name) {
		return IsKeyword(name) ? L"@" + name : name;
	}

	// a type's name without the number of its generic parameters ("List`1")
	std::wstring WithoutArity(std::wstring const& name) {
		return name.substr(0, name.find(L'`'));
	}

	//
	// Expressions
	//
	enum class E : uint8_t {
		Literal,	// Text
		Var,		// a local, parameter, this, a temporary: Var
		Binary,		// Args[0] Text Args[1]
		Unary,		// Text Args[0] (Args[0] Text if Postfix)
		Cast,		// (Text)Args[0]
		As,			// Args[0] as Text
		Is,			// Args[0] is Text
		Checked,	// checked(Args[0])
		Call,		// [Args[0].]Text(Args...)
		New,		// new Text(Args)
		Member,		// Args[0].Text or Owner.Text
		Index,		// Args[0][Args[1...]]
		NewArray,	// new Text[Args[0]]
		AddressOf,	// &Args[0]
		Deref,		// *Args[0]
		Conditional,	// Args[0] ? Args[1] : Args[2]
		Assign,		// Args[0] Text Args[1] ("=", "+=" ...)
		Typeof,		// typeof(Text)
		Default,	// default(Text)
		Raw,		// Text, then (Args) if there are any
		Exception,	// what a catch caught
		MethodGroup,	// [Args[0].]Text: what a delegate is made of
		Base,		// base
	};

	struct Expr;
	using ExprPtr = std::shared_ptr<Expr>;

	struct Expr {
		E Kind{};
		std::wstring Text;
		std::wstring Owner;
		std::vector<ExprPtr> Args;
		bool HasTarget{ false };
		bool Postfix{ false };
		ClrTypeInfo Type;
		uint32_t Token{ 0 };
		int Var{ -1 };
		bool IsInt{ false };
		int64_t Int{ 0 };
		bool Unordered{ false };	// a comparison of reals that is also true when a value is NaN (bge.un...)
		std::vector<std::wstring> GenericArgs;
	};

	ExprPtr Make(E kind, std::wstring text = {}, std::vector<ExprPtr> args = {}, ClrTypeInfo type = {}) {
		auto e = std::make_shared<Expr>();
		e->Kind = kind;
		e->Text = std::move(text);
		e->Args = std::move(args);
		e->Type = std::move(type);
		return e;
	}

	ExprPtr IntLiteral(int64_t value, ClrTypeInfo type = { L"int", K::Integer }) {
		auto text = std::to_wstring(value);
		if (type.Name == L"long")
			text += L"L";
		else if (type.Name == L"ulong")
			text = std::to_wstring((uint64_t)value) + L"UL";
		auto e = Make(E::Literal, text, {}, type);
		e->IsInt = true;
		e->Int = value;
		return e;
	}

	ExprPtr BoolLiteral(bool value) {
		auto e = Make(E::Literal, value ? L"true" : L"false", {}, { L"bool", K::Bool });
		e->IsInt = true;
		e->Int = value;
		return e;
	}

	ExprPtr NullLiteral() {
		return Make(E::Literal, L"null", {}, { L"object", K::Object });
	}

	bool IsLiteral(ExprPtr const& e, int64_t value) {
		return e && e->Kind == E::Literal && e->IsInt && e->Int == value;
	}
	bool IsNull(ExprPtr const& e) {
		return e && e->Kind == E::Literal && e->Text == L"null";
	}

	// what reading an expression involves
	void ForEach(ExprPtr const& e, std::function<void(Expr const&)> const& fn) {
		if (!e)
			return;
		fn(*e);
		for (auto& a : e->Args)
			ForEach(a, fn);
	}
	bool ReadsVar(ExprPtr const& e, int var) {
		bool reads = false;
		ForEach(e, [&](Expr const& x) { reads |= x.Kind == E::Var && x.Var == var; });
		return reads;
	}
	// it calls something, or writes something: it cannot be moved over other code, or dropped
	bool HasSideEffects(ExprPtr const& e) {
		bool effects = false;
		ForEach(e, [&](Expr const& x) {
			effects |= x.Kind == E::Call || x.Kind == E::New || x.Kind == E::Assign || x.Kind == E::NewArray ||
				(x.Kind == E::Raw && !x.Args.empty()) || (x.Kind == E::Unary && (x.Text == L"++" || x.Text == L"--"));
		});
		return effects;
	}
	// what it reads may be changed by what is written to memory (fields, arrays, through pointers)
	bool ReadsMemory(ExprPtr const& e) {
		bool reads = false;
		ForEach(e, [&](Expr const& x) {
			reads |= x.Kind == E::Member || x.Kind == E::Index || x.Kind == E::Deref || x.Kind == E::Call || x.Kind == E::New;
		});
		return reads;
	}
	bool Same(ExprPtr const& a, ExprPtr const& b) {
		if (!a || !b || a->Kind != b->Kind || a->Text != b->Text || a->Var != b->Var || a->Owner != b->Owner ||
			a->Token != b->Token || a->Args.size() != b->Args.size() || a->HasTarget != b->HasTarget)
			return false;
		switch (a->Kind) {
			case E::Var: case E::Literal: case E::Member: case E::Index: case E::Deref: case E::Base: case E::AddressOf:
				break;
			default:
				return false;
		}
		for (size_t i = 0; i < a->Args.size(); i++)
			if (!Same(a->Args[i], b->Args[i]))
				return false;
		return true;
	}

	ExprPtr Not(ExprPtr const& e);

	// the operators of comparisons, and their opposites
	PCWSTR Opposite(std::wstring const& op) {
		static const std::pair<PCWSTR, PCWSTR> pairs[] = {
			{ L"==", L"!=" }, { L"!=", L"==" }, { L"<", L">=" }, { L">=", L"<" }, { L">", L"<=" }, { L"<=", L">" },
		};
		for (auto& [a, b] : pairs)
			if (op == a)
				return b;
		return nullptr;
	}

	ExprPtr Not(ExprPtr const& e) {
		if (e->Kind == E::Literal && e->Type.Kind == K::Bool)
			return BoolLiteral(e->Int == 0);
		if (e->Kind == E::Unary && e->Text == L"!")
			return e->Args[0];
		if (e->Kind == E::Binary) {
			if (auto op = Opposite(e->Text)) {
				// a comparison of reals is not the opposite of the other one when a value is NaN, but for one that is
				// true for NaN (as the compiler writes the negated conditions of ifs)
				bool real = e->Args[0]->Type.Kind == K::Float || e->Args[1]->Type.Kind == K::Float;
				if (!real || e->Text == L"==" || e->Text == L"!=" || e->Unordered)
					return Make(E::Binary, op, e->Args, e->Type);
			}
			if (e->Text == L"&&" || e->Text == L"||")
				return Make(E::Binary, e->Text == L"&&" ? L"||" : L"&&", { Not(e->Args[0]), Not(e->Args[1]) }, e->Type);
		}
		return Make(E::Unary, L"!", { e }, { L"bool", K::Bool });
	}

	//
	// Statements
	//
	enum class S : uint8_t { Expr, Return, Throw, If, While, DoWhile, For, Foreach, Lock, Using, Switch, Try, Break, Continue, Goto, Label, Comment };

	struct Stmt;
	using StmtPtr = std::shared_ptr<Stmt>;
	using StmtList = std::vector<StmtPtr>;

	struct Case {
		std::vector<int64_t> Values;
		bool Default{ false };
		StmtList Body;
	};

	struct Handler {
		enum class Kinds { Catch, Filter, Finally, Fault } Kind{};
		std::wstring Type;
		uint32_t TypeToken{ 0 };
		int Var{ -1 };
		ExprPtr Filter;
		std::wstring FilterText;	// a filter that is not one expression: where it is
		StmtList Body;
	};

	struct Stmt {
		S Kind{};
		ExprPtr Value;		// Expr: the expression; Return, Throw: the value; If, While, Switch: the condition, value
		StmtList Body, Else;
		ExprPtr Init, Step;	// For; Foreach: the variable in Init; Using: the variable's assignment in Init
		std::vector<Case> Cases;
		std::vector<Handler> Handlers;
		int Target{ -1 };	// Goto, Label: the block
		std::wstring Text;	// Comment
		bool Spill{ false };	// the assignment of a value on the stack at the end of a block
		bool Declares{ false };	// an assignment that declares its variable
	};

	StmtPtr MakeStmt(S kind, ExprPtr value = nullptr) {
		auto s = std::make_shared<Stmt>();
		s->Kind = kind;
		s->Value = std::move(value);
		return s;
	}

	// a jump: what follows it in the same list is not reached
	bool EndsWithJump(StmtList const& list) {
		if (list.empty())
			return false;
		switch (list.back()->Kind) {
			case S::Return: case S::Throw: case S::Break: case S::Continue: case S::Goto:
				return true;
			case S::If:
				return EndsWithJump(list.back()->Body) && EndsWithJump(list.back()->Else);
		}
		return false;
	}

	void ForEachStmt(StmtList const& list, std::function<void(Stmt&)> const& fn) {
		for (auto& s : list) {
			fn(*s);
			ForEachStmt(s->Body, fn);
			ForEachStmt(s->Else, fn);
			for (auto& c : s->Cases)
				ForEachStmt(c.Body, fn);
			for (auto& h : s->Handlers)
				ForEachStmt(h.Body, fn);
		}
	}
	// the expressions of a statement (not of the statements in it)
	void ForEachExprOf(Stmt& s, std::function<void(ExprPtr&)> const& fn) {
		for (auto e : { &s.Value, &s.Init, &s.Step })
			if (*e)
				fn(*e);
		for (auto& h : s.Handlers)
			if (h.Filter)
				fn(h.Filter);
	}

	// replaces a variable's reads in an expression (the first 'limit' of them)
	ExprPtr Substitute(ExprPtr const& e, int var, ExprPtr const& with, int& count) {
		if (!e)
			return e;
		if (e->Kind == E::Var && e->Var == var && count > 0) {
			count--;
			return with;
		}
		for (auto& a : e->Args)
			a = Substitute(a, var, with, count);
		return e;
	}

	struct Variable {
		enum class Roles { This, Param, Local, Temp, Stack } Role{};
		std::wstring Name;
		ClrTypeInfo Type;
		int Index{ 0 };
		bool Declared{ false };	// by its first assignment, a catch or a for
	};

	//
	// A block of IL: from a leader to the next one, its statements and how it ends
	//
	enum class Exit : uint8_t { Goto, Cond, Switch, Return, Throw, EndFinally, EndFilter };

	struct Block {
		uint32_t Start{}, End{};	// offsets
		size_t First{}, Last{};	// instructions [First, Last)
		StmtList Stmts;
		Exit Kind{ Exit::Goto };
		ExprPtr Value;			// the condition (true goes to Succ[0]), the switch's value, the filter's result
		std::vector<int> Succ;	// Cond: true, false; Switch: the cases, then the default
		std::vector<int> Pred;
		int Depth{ -1 };		// of the stack when it starts (-1: not reached)
		bool Removed{ false };
		bool Root{ false };		// the start of the method, of a handler or filter
		int Region{ -1 };		// the innermost try, handler or filter it is in (an index into the regions)
	};

	struct Region {
		uint32_t Start, End;
	};

	// The IL opcodes the decompiler handles by number (ECMA-335 III)
	namespace Op {
		enum : uint16_t {
			Nop = 0x00, Break = 0x01, LdArg0 = 0x02, LdArg3 = 0x05, LdLoc0 = 0x06, LdLoc3 = 0x09, StLoc0 = 0x0A, StLoc3 = 0x0D,
			LdArgS = 0x0E, LdArgaS = 0x0F, StArgS = 0x10, LdLocS = 0x11, LdLocaS = 0x12, StLocS = 0x13, LdNull = 0x14,
			LdcI4M1 = 0x15, LdcI4_0 = 0x16, LdcI4_8 = 0x1E, LdcI4S = 0x1F, LdcI4 = 0x20, LdcI8 = 0x21, LdcR4 = 0x22, LdcR8 = 0x23,
			Dup = 0x25, Pop = 0x26, Jmp = 0x27, Call = 0x28, Calli = 0x29, Ret = 0x2A, BrS = 0x2B, BrFalseS = 0x2C, BrTrueS = 0x2D,
			BeqS = 0x2E, BltUnS = 0x37, Br = 0x38, BrFalse = 0x39, BrTrue = 0x3A, Beq = 0x3B, BltUn = 0x44, Switch = 0x45,
			LdIndI1 = 0x46, LdIndRef = 0x50, StIndRef = 0x51, StIndR8 = 0x57, Add = 0x58, Sub = 0x59, Mul = 0x5A, Div = 0x5B,
			DivUn = 0x5C, Rem = 0x5D, RemUn = 0x5E, And = 0x5F, Or = 0x60, Xor = 0x61, Shl = 0x62, Shr = 0x63, ShrUn = 0x64,
			Neg = 0x65, Not = 0x66, ConvI1 = 0x67, ConvI2 = 0x68, ConvI4 = 0x69, ConvI8 = 0x6A, ConvR4 = 0x6B, ConvR8 = 0x6C,
			ConvU4 = 0x6D, ConvU8 = 0x6E, CallVirt = 0x6F, CpObj = 0x70, LdObj = 0x71, LdStr = 0x72, NewObj = 0x73,
			CastClass = 0x74, IsInst = 0x75, ConvRUn = 0x76, Unbox = 0x79, Throw = 0x7A, LdFld = 0x7B, LdFlda = 0x7C,
			StFld = 0x7D, LdSFld = 0x7E, LdSFlda = 0x7F, StSFld = 0x80, StObj = 0x81, ConvOvfI1Un = 0x82, ConvOvfUUn = 0x8B,
			Box = 0x8C, NewArr = 0x8D, LdLen = 0x8E, LdElema = 0x8F, LdElemI1 = 0x90, LdElemRef = 0x9A, StElemI = 0x9B,
			StElemRef = 0xA2, LdElem = 0xA3, StElem = 0xA4, UnboxAny = 0xA5, ConvOvfI1 = 0xB3, ConvOvfU8 = 0xBA,
			RefAnyVal = 0xC2, CkFinite = 0xC3, MkRefAny = 0xC6, LdToken = 0xD0, ConvU2 = 0xD1, ConvU1 = 0xD2, ConvI = 0xD3,
			ConvOvfI = 0xD4, ConvOvfU = 0xD5, AddOvf = 0xD6, AddOvfUn = 0xD7, MulOvf = 0xD8, MulOvfUn = 0xD9, SubOvf = 0xDA,
			SubOvfUn = 0xDB, EndFinally = 0xDC, Leave = 0xDD, LeaveS = 0xDE, StIndI = 0xDF, ConvU = 0xE0,
			ArgList = 0xFE00, Ceq = 0xFE01, Cgt = 0xFE02, CgtUn = 0xFE03, Clt = 0xFE04, CltUn = 0xFE05, LdFtn = 0xFE06,
			LdVirtFtn = 0xFE07, LdArg = 0xFE09, LdArga = 0xFE0A, StArg = 0xFE0B, LdLoc = 0xFE0C, LdLoca = 0xFE0D, StLoc = 0xFE0E,
			LocAlloc = 0xFE0F, EndFilter = 0xFE11, Unaligned = 0xFE12, Volatile = 0xFE13, Tail = 0xFE14, InitObj = 0xFE15,
			Constrained = 0xFE16, CpBlk = 0xFE17, InitBlk = 0xFE18, No = 0xFE19, Rethrow = 0xFE1A, SizeOf = 0xFE1C,
			RefAnyType = 0xFE1D, Readonly = 0xFE1E,
		};
	}

	bool IsConditionalBranch(uint16_t op) {
		return (op >= Op::BrFalseS && op <= Op::BltUnS) || (op >= Op::BrFalse && op <= Op::BltUn);
	}
	bool EndsBlock(uint16_t op) {
		switch (op) {
			case Op::Ret: case Op::Throw: case Op::Rethrow: case Op::EndFinally: case Op::EndFilter: case Op::Jmp:
			case Op::Br: case Op::BrS: case Op::Leave: case Op::LeaveS: case Op::Switch:
				return true;
		}
		return IsConditionalBranch(op);
	}

	// the comparison of a conditional branch (beq, bge...), or of a comparison instruction
	PCWSTR BranchComparison(uint16_t op) {
		if (op >= Op::BeqS && op <= Op::BltUnS)
			op = (uint16_t)(op - Op::BeqS + Op::Beq);
		static const PCWSTR ops[] = { L"==", L">=", L">", L"<=", L"<", L"!=", L">=", L">", L"<=", L"<" };
		return op >= Op::Beq && op <= Op::BltUn ? ops[op - Op::Beq] : nullptr;
	}

	//
	// Decompiles one method's body into statements
	//
	class MethodDecompiler {
	public:
		MethodDecompiler(PEFile const& pe, ClrMetadata const& md, uint32_t method) : m_PE(pe), m_Md(md), m_Method(method),
			m_Def(md.Methods()[method - 1]) {
			m_Info = m_Md.MemberInfo(MethodToken(m_Method), {});
		}

		bool Run(std::wstring& error);
		StmtList const& Body() const { return m_Result; }
		std::vector<Variable>& Variables() { return m_Vars; }
		ClrMemberInfo const& Info() const { return m_Info; }
		std::vector<int> const& Params() const { return m_ParamVars; }

	private:
		void SetUp();
		bool BuildBlocks();
		void Simulate(int b);
		void Instruction(IlInstruction const& inst, Block& block);

		// the stack
		ExprPtr Pop();
		void Push(ExprPtr e) { m_Stack.push_back(std::move(e)); }
		void Add(StmtPtr s);
		void Assign(ExprPtr target, ExprPtr value);
		ExprPtr Spill(ExprPtr e);
		int NewVar(Variable::Roles role, std::wstring const& base, ClrTypeInfo type = {});
		ExprPtr VarExpr(int var);

		// what instructions are
		ExprPtr Arg(uint32_t n);
		ExprPtr Local(uint32_t n);
		ExprPtr Coerce(ExprPtr e, ClrTypeInfo const& type);
		ExprPtr EnumValue(ClrTypeInfo const& type, int64_t value);
		ExprPtr Condition(ExprPtr e);
		ExprPtr Compare(std::wstring const& op, ExprPtr a, ExprPtr b, bool unordered = false);
		ExprPtr Arithmetic(std::wstring const& op, ExprPtr a, ExprPtr b);
		ExprPtr Convert(std::wstring const& type, K kind, ExprPtr e, bool overflow);
		void CallInstruction(IlInstruction const& inst, bool newobj);
		ExprPtr Field(uint32_t token, ExprPtr target);
		ExprPtr Element(ExprPtr array, ExprPtr index, ClrTypeInfo type);
		ExprPtr Indirect(ExprPtr address, ClrTypeInfo type);
		ExprPtr TypeExprOf(uint32_t token);

		// the graph
		void Simplify();
		bool MergeTernary(int b);
		bool MergeCondition(int b);
		bool MergeCoalesce(int b);
		bool MergeLine(int b);
		void Bypass();
		void Connect();
		void Dominators();
		bool Dominates(int a, int b) const;
		void FindLoops();

		// structured code
		struct Context {
			uint32_t RangeStart{ 0 }, RangeEnd{ UINT32_MAX };
			int Follow{ -1 };
			int After{ -1 };
			int LoopHeader{ -1 }, LoopExit{ -1 };
			std::vector<bool> const* LoopBody{ nullptr };
			bool InSwitch{ false };
			std::vector<uint64_t> ActiveTries;
		};
		void Emit(int b, Context const& ctx, StmtList& out, int header = -1);
		StmtList Branch(int b, int follow, Context const& ctx);
		int EmitLoop(int h, Context const& ctx, StmtList& out);
		int EmitTry(int b, uint64_t key, Context const& ctx, StmtList& out);
		uint64_t TryAt(int b, Context const& ctx) const;
		void EmitSwitch(int b, Context const& ctx, StmtList& out, int follow);
		bool InRange(int b, Context const& ctx) const;
		int BlockAt(uint32_t offset) const;
		StmtPtr GotoBlock(int b);

		// the code after it is structured
		void Clean(StmtList& list, bool top);
		bool Foreach(StmtList& list, size_t i);
		bool Lock(StmtList& list, size_t i);
		bool Using(StmtList& list, size_t i);
		void InlineLocals(StmtList& list);
		void Compound(StmtList& list);
		void ForLoops(StmtList& list);
		void Declarations();

		PEFile const& m_PE;
		ClrMetadata const& m_Md;
		uint32_t m_Method;
		ClrMethodDef const& m_Def;
		ClrMemberInfo m_Info;
		ClrGenericContext m_Context;
		std::wstring m_TypeName;
		IlMethodBody m_Body;
		std::vector<IlInstruction> m_Code;
		bool m_Complete{ true };

		std::vector<Variable> m_Vars;
		std::vector<int> m_ParamVars, m_LocalVars, m_StackVars;
		int m_This{ -1 };
		std::set<std::wstring> m_Names;

		std::vector<Block> m_Blocks;
		std::map<uint32_t, int> m_BlockAt;
		std::vector<Region> m_Regions;

		// the simulation of a block
		std::vector<ExprPtr> m_Stack;
		Block* m_Current{ nullptr };
		uint32_t m_Constrained{ 0 };

		// the structure
		std::vector<int> m_Idom, m_Ipdom;
		std::vector<bool> m_Emitted, m_Header;
		std::map<int, std::vector<bool>> m_LoopBody;
		std::map<int, int> m_LoopExit;
		std::vector<int> m_Order;	// reverse postorder
		int m_Depth{ 0 };
		bool m_CatchEx{ false };	// "ex" is the name of the variables of catches
		StmtList m_Result;
	};

	int MethodDecompiler::NewVar(Variable::Roles role, std::wstring const& base, ClrTypeInfo type) {
		auto name = base;
		for (int n = 2; m_Names.contains(name) || IsKeyword(name); n++)
			name = base + std::to_wstring(n);
		m_Names.insert(name);
		Variable v;
		v.Role = role;
		v.Name = name;
		v.Type = std::move(type);
		m_Vars.push_back(std::move(v));
		return (int)m_Vars.size() - 1;
	}

	ExprPtr MethodDecompiler::VarExpr(int var) {
		auto e = Make(E::Var, {}, {}, m_Vars[var].Type);
		e->Var = var;
		return e;
	}

	// a name for a local from its type: "num", "text", "list", "stringBuilder"...
	std::wstring NameOfType(ClrTypeInfo const& type) {
		auto name = type.Name;
		if (name.starts_with(L"ref "))
			name = name.substr(4);
		if (name.ends_with(L"]"))
			return L"array";
		if (name.ends_with(L"*"))
			return L"ptr";
		static const std::pair<PCWSTR, PCWSTR> names[] = {
			{ L"int", L"num" }, { L"uint", L"num" }, { L"long", L"num" }, { L"ulong", L"num" }, { L"short", L"num" },
			{ L"ushort", L"num" }, { L"byte", L"b" }, { L"sbyte", L"b" }, { L"bool", L"flag" }, { L"string", L"text" },
			{ L"object", L"obj" }, { L"char", L"c" }, { L"float", L"num" }, { L"double", L"num" }, { L"decimal", L"num" },
			{ L"IntPtr", L"ptr" }, { L"UIntPtr", L"ptr" },
		};
		for (auto& [t, n] : names)
			if (name == t)
				return n;
		if (name.find(L"DisplayClass") != std::wstring::npos)
			return L"displayClass";
		if (auto lt = name.find(L'<'); lt != std::wstring::npos && lt > 0)
			name = name.substr(0, lt);
		if (name.ends_with(L'?'))
			name.pop_back();
		if (auto dot = name.rfind(L'.'); dot != std::wstring::npos)
			name = name.substr(dot + 1);
		if (name.size() > 2 && name[0] == L'I' && iswupper(name[1]))
			name = name.substr(1);
		std::wstring result;
		for (auto c : name)
			if (iswalnum(c) || c == L'_')
				result += c;
		if (result.empty() || iswdigit(result[0]))
			return L"v";
		result[0] = towlower(result[0]);
		return result;
	}

	void MethodDecompiler::SetUp() {
		m_Info = m_Md.MemberInfo(MethodToken(m_Method), {});
		m_Context = m_Md.ContextOf(m_Method);
		m_TypeName = m_Md.CSharpTypeDefName(m_Def.Type);
		bool hasThis = (m_Def.Flags & 0x10) == 0;
		if (hasThis) {
			Variable v;
			v.Role = Variable::Roles::This;
			v.Name = L"this";
			v.Type = m_Md.CSharpType(TypeToken(m_Def.Type));
			m_Vars.push_back(std::move(v));
			m_This = (int)m_Vars.size() - 1;
		}
		auto& params = m_Info.Signature.Params;
		for (uint32_t i = 0; i < params.size(); i++) {
			auto name = m_Md.ParamName(m_Method, i + 1);
			bool valid = !name.empty() && std::all_of(name.begin(), name.end(), [](wchar_t c) { return iswalnum(c) || c == L'_'; });
			auto var = NewVar(Variable::Roles::Param, valid ? Identifier(name) : std::format(L"arg{}", i), params[i]);
			m_Vars[var].Index = (int)i;
			m_ParamVars.push_back(var);
		}
		// the locals are named after the parameters, which keep their names
		for (auto const& type : m_Md.CSharpLocalTypes(m_Body.LocalVarSigToken, m_Method)) {
			auto var = NewVar(Variable::Roles::Local, NameOfType(type), type);
			m_Vars[var].Index = (int)m_LocalVars.size();
			m_LocalVars.push_back(var);
		}
	}

	ExprPtr MethodDecompiler::Arg(uint32_t n) {
		if (m_This >= 0) {
			if (n == 0)
				return VarExpr(m_This);
			n--;
		}
		if (n < m_ParamVars.size())
			return VarExpr(m_ParamVars[n]);
		return Make(E::Raw, std::format(L"__arg{}", n));
	}

	ExprPtr MethodDecompiler::Local(uint32_t n) {
		if (n < m_LocalVars.size())
			return VarExpr(m_LocalVars[n]);
		return Make(E::Raw, std::format(L"__local{}", n));
	}

	ExprPtr MethodDecompiler::Pop() {
		if (m_Stack.empty())
			return Make(E::Raw, L"/* stack is empty */");
		auto e = m_Stack.back();
		m_Stack.pop_back();
		return e;
	}

	// a statement: the values on the stack that it could change are read before it (into temporaries)
	void MethodDecompiler::Add(StmtPtr s) {
		std::set<int> written;
		bool memory = false;
		auto value = s->Value;
		if (value) {
			ForEach(value, [&](Expr const& x) {
				if (x.Kind == E::Assign && x.Args[0]->Kind == E::Var)
					written.insert(x.Args[0]->Var);
				if (x.Kind == E::AddressOf && x.Args[0]->Kind == E::Var)
					written.insert(x.Args[0]->Var);
			});
			memory = HasSideEffects(value);
		}
		for (auto& e : m_Stack) {
			if (e->Kind == E::Literal || e->Kind == E::Typeof || e->Kind == E::Default || e->Kind == E::Exception)
				continue;
			bool spill = memory && (ReadsMemory(e) || HasSideEffects(e));
			for (auto v : written)
				spill |= ReadsVar(e, v);
			if (spill) {
				// the stack's own variables are not temporaries
				if (e->Kind == E::Var && m_Vars[e->Var].Role == Variable::Roles::Stack && !written.contains(e->Var))
					continue;
				auto temp = NewVar(Variable::Roles::Temp, L"temp", e->Type);
				auto assign = MakeStmt(S::Expr, Make(E::Assign, L"=", { VarExpr(temp), e }, e->Type));
				m_Current->Stmts.push_back(assign);
				e = VarExpr(temp);
			}
		}
		m_Current->Stmts.push_back(std::move(s));
	}

	void MethodDecompiler::Assign(ExprPtr target, ExprPtr value) {
		value = Coerce(value, target->Type);
		Add(MakeStmt(S::Expr, Make(E::Assign, L"=", { target, value }, target->Type)));
	}

	// a value used twice (dup): read once, into a temporary, unless it is simple
	ExprPtr MethodDecompiler::Spill(ExprPtr e) {
		if (e->Kind == E::Literal || e->Kind == E::Var || e->Kind == E::Typeof || e->Kind == E::Exception || e->Kind == E::Base)
			return e;
		auto temp = NewVar(Variable::Roles::Temp, L"temp", e->Type);
		Add(MakeStmt(S::Expr, Make(E::Assign, L"=", { VarExpr(temp), e }, e->Type)));
		return VarExpr(temp);
	}

	ExprPtr MethodDecompiler::EnumValue(ClrTypeInfo const& type, int64_t value) {
		if (TableOf(type.Token) == ClrTableId::TypeDef) {
			for (auto f : m_Md.FieldsOf(RowOf(type.Token)))
				if (auto v = m_Md.ConstantInteger(FieldToken(f)); v && *v == value) {
					auto e = Make(E::Member, m_Md.Fields()[f - 1].Name, {}, type);
					e->Owner = type.Name;
					e->Token = FieldToken(f);
					return e;
				}
		}
		return Make(E::Cast, type.Name, { IntLiteral(value) }, type);
	}

	// a value where a type is expected: the integer literals of IL as booleans, characters and the values of enums
	ExprPtr MethodDecompiler::Coerce(ExprPtr e, ClrTypeInfo const& type) {
		if (!e)
			return e;
		if (e->Kind == E::Literal && e->IsInt && e->Type.Kind != K::Bool) {
			switch (type.Kind) {
				case K::Bool:
					if (e->Int == 0 || e->Int == 1)
						return BoolLiteral(e->Int != 0);
					break;
				case K::Char:
					if (e->Int >= 0 && e->Int <= 0xFFFF)
						return Make(E::Literal, CSharpChar((wchar_t)e->Int), {}, type);
					break;
				case K::Enum:
					return EnumValue(type, e->Int);
				case K::Integer:
					if ((type.Name == L"long" || type.Name == L"ulong") && e->Type.Name != type.Name)
						return IntLiteral(e->Int, type);
					// an unsigned value that IL has as a negative int
					if (type.Name == L"uint" && e->Int < 0 && e->Int >= INT32_MIN) {
						auto u = Make(E::Literal, std::to_wstring((uint32_t)e->Int) + L"u", {}, type);
						u->IsInt = true;
						u->Int = (uint32_t)e->Int;
						return u;
					}
					break;
			}
		}
		// an integer where an enum or a character is: a cast; where a boolean is: a comparison
		if (e->Kind != E::Literal && e->Kind != E::Conditional && e->Type.Kind == K::Integer) {
			if (type.Kind == K::Enum || type.Kind == K::Char)
				return Make(E::Cast, type.Name, { e }, type);
			if (type.Kind == K::Bool)
				return Make(E::Binary, L"!=", { e, IntLiteral(0) }, type);
		}
		if (e->Kind == E::Conditional && (type.Kind == K::Bool || type.Kind == K::Char || type.Kind == K::Enum)) {
			auto c = Make(E::Conditional, {}, { e->Args[0], Coerce(e->Args[1], type), Coerce(e->Args[2], type) }, type);
			// c ? true : false is c, c ? x : false is c && x...
			if (type.Kind == K::Bool) {
				auto& a = c->Args[1];
				auto& b = c->Args[2];
				if (a->Kind == E::Literal && b->Kind == E::Literal && a->Type.Kind == K::Bool && b->Type.Kind == K::Bool)
					return a->Int == b->Int ? a : a->Int ? c->Args[0] : Not(c->Args[0]);
				if (b->Kind == E::Literal && b->Type.Kind == K::Bool && b->Int == 0)
					return Make(E::Binary, L"&&", { c->Args[0], a }, type);
				if (a->Kind == E::Literal && a->Type.Kind == K::Bool && a->Int == 1)
					return Make(E::Binary, L"||", { c->Args[0], b }, type);
				if (a->Kind == E::Literal && a->Type.Kind == K::Bool && a->Int == 0)
					return Make(E::Binary, L"&&", { Not(c->Args[0]), b }, type);
				if (b->Kind == E::Literal && b->Type.Kind == K::Bool && b->Int == 1)
					return Make(E::Binary, L"||", { Not(c->Args[0]), a }, type);
			}
			return c;
		}
		return e;
	}

	// a value tested by brtrue: true when it is not zero or null
	ExprPtr MethodDecompiler::Condition(ExprPtr e) {
		switch (e->Type.Kind) {
			case K::Bool:
				return e;
			case K::String: case K::Object: case K::Pointer:
				if (e->Kind == E::As)
					return Make(E::Is, e->Text, e->Args, { L"bool", K::Bool });
				return Make(E::Binary, L"!=", { e, NullLiteral() }, { L"bool", K::Bool });
			case K::Integer: case K::Char:
				return Make(E::Binary, L"!=", { e, IntLiteral(0) }, { L"bool", K::Bool });
			case K::Enum:
				return Make(E::Binary, L"!=", { e, EnumValue(e->Type, 0) }, { L"bool", K::Bool });
		}
		if (e->Kind == E::Literal && e->IsInt)
			return BoolLiteral(e->Int != 0);
		return e;
	}

	ExprPtr MethodDecompiler::Compare(std::wstring const& op, ExprPtr a, ExprPtr b, bool unordered) {
		ClrTypeInfo boolean{ L"bool", K::Bool };
		// b > 0 (cgt.un, as the compiler writes b != false) of a boolean is the boolean
		if (op == L">" && a->Type.Kind == K::Bool && b->Kind == E::Literal && b->IsInt && b->Int == 0)
			return a;
		// a boolean compared with 0 or 1 is itself or its opposite
		if (op == L"==" || op == L"!=") {
			for (int i = 0; i < 2; i++) {
				auto& x = i ? b : a;
				auto& y = i ? a : b;
				if (x->Type.Kind == K::Bool && y->Kind == E::Literal && y->IsInt && (y->Int == 0 || y->Int == 1))
					return (y->Int == 1) == (op == L"==") ? x : Not(x);
				if (y->Kind == E::Literal && y->IsInt && y->Int == 0 && (x->Type.Kind == K::Object || x->Type.Kind == K::String))
					y = NullLiteral();
			}
		}
		// a literal is of the type of the other side (a character, an enum's value)
		if (a->Kind == E::Literal)
			a = Coerce(a, b->Type);
		else
			b = Coerce(b, a->Type);
		auto e = Make(E::Binary, op, { a, b }, boolean);
		e->Unordered = unordered && (a->Type.Kind == K::Float || b->Type.Kind == K::Float);
		return e;
	}

	ExprPtr MethodDecompiler::Arithmetic(std::wstring const& op, ExprPtr a, ExprPtr b) {
		ClrTypeInfo type{ L"int", K::Integer };
		auto wide = [](ClrTypeInfo const& t) { return t.Name == L"long" || t.Name == L"ulong" || t.Name == L"uint"; };
		if ((op == L"&" || op == L"|" || op == L"^") && (a->Type.Kind == K::Bool || b->Type.Kind == K::Bool)) {
			type = { L"bool", K::Bool };
			a = Coerce(a, type);
			b = Coerce(b, type);
		}
		else if (a->Type.Kind == K::Enum && (op == L"&" || op == L"|" || op == L"^")) {
			type = a->Type;
			b = Coerce(b, type);
		}
		else if (a->Type.Kind == K::Float)
			type = a->Type;
		else if (b->Type.Kind == K::Float)
			type = b->Type;
		else if (a->Type.Kind == K::Pointer)
			type = a->Type;
		else if (wide(a->Type))
			type = a->Type;
		else if (wide(b->Type))
			type = b->Type;
		// x + -1 is x - 1
		if (op == L"+" && b->Kind == E::Literal && b->IsInt && b->Int < 0 && b->Int != INT64_MIN)
			return Make(E::Binary, L"-", { a, IntLiteral(-b->Int, b->Type) }, type);
		return Make(E::Binary, op, { a, b }, type);
	}

	ExprPtr MethodDecompiler::Convert(std::wstring const& name, K kind, ExprPtr e, bool overflow) {
		ClrTypeInfo type{ name, kind };
		// the length of an array, and literals, need no conversion
		if (e->Kind == E::Member && e->Text == L"Length" && (name == L"int" || name == L"IntPtr"))
			return e;
		if (e->Kind == E::Literal && e->IsInt && !overflow) {
			// conv.u8 extends an int32 with zeros: 3000000000 is loaded as int32 and made long so
			if (name == L"ulong" && e->Type.Name == L"int")
				return IntLiteral((uint32_t)e->Int, { L"long", K::Integer });
			if (name == L"long" || name == L"ulong")
				return IntLiteral(e->Int, type);
			if (name == L"double" || name == L"float")
				return Make(E::Literal, CSharpReal((double)e->Int, name == L"float"), {}, type);
		}
		if (e->Type.Name == name && !overflow)
			return e;
		auto cast = Make(E::Cast, name, { e }, type);
		return overflow ? Make(E::Checked, {}, { cast }, type) : cast;
	}

	ExprPtr MethodDecompiler::TypeExprOf(uint32_t token) {
		auto e = Make(E::Typeof, m_Md.CSharpType(token, m_Context).Name, {}, { L"Type", K::Object });
		e->Token = token;
		return e;
	}

	ExprPtr MethodDecompiler::Field(uint32_t token, ExprPtr target) {
		auto info = m_Md.MemberInfo(token, m_Context);
		auto e = Make(E::Member, info.Name, {}, info.FieldType);
		e->Token = token;
		if (target) {
			if (target->Kind == E::AddressOf)
				target = target->Args[0];
			e->Args = { target };
			e->HasTarget = true;
		}
		else
			e->Owner = info.Owner.Name;
		return e;
	}

	ExprPtr MethodDecompiler::Element(ExprPtr array, ExprPtr index, ClrTypeInfo type) {
		if (type.Name.empty() && array->Type.Name.ends_with(L"[]")) {
			type.Name = array->Type.Name.substr(0, array->Type.Name.size() - 2);
			type.Kind = type.Name == L"string" ? K::String : K::Unknown;
		}
		return Make(E::Index, {}, { array, index }, type);
	}

	ExprPtr MethodDecompiler::Indirect(ExprPtr address, ClrTypeInfo type) {
		if (address->Kind == E::AddressOf)
			return address->Args[0];
		// *(p + i * sizeof(T)) is p[i]
		if (address->Kind == E::Binary && address->Text == L"+" && address->Args[0]->Type.Kind == K::Pointer) {
			auto& pointer = address->Args[0];
			auto element = pointer->Type.Name.substr(0, pointer->Type.Name.size() - 1);
			static const std::pair<PCWSTR, int> sizes[] = {
				{ L"sbyte", 1 }, { L"byte", 1 }, { L"bool", 1 }, { L"short", 2 }, { L"ushort", 2 }, { L"char", 2 }, { L"int", 4 },
				{ L"uint", 4 }, { L"float", 4 }, { L"long", 8 }, { L"ulong", 8 }, { L"double", 8 },
			};
			int size = 0;
			for (auto& [name, n] : sizes)
				if (element == name)
					size = n;
			auto& offset = address->Args[1];
			ExprPtr index;
			if (size == 1)
				index = offset;
			else if (size && offset->Kind == E::Binary && offset->Text == L"*" && IsLiteral(offset->Args[1], size))
				index = offset->Args[0];
			else if (size && offset->Kind == E::Literal && offset->IsInt && offset->Int % size == 0)
				index = IntLiteral(offset->Int / size);
			if (index)
				return Make(E::Index, {}, { pointer, index }, { element, element == L"float" || element == L"double" ? K::Float : K::Integer });
		}
		// a ref (or out) parameter is the variable it refers to
		if (address->Kind == E::Var && address->Type.Kind == K::ByRef) {
			auto e = std::make_shared<Expr>(*address);
			auto name = address->Type.Name.substr(4);
			e->Type = { name, name == L"bool" ? K::Bool : name == L"char" ? K::Char : name == L"string" ? K::String :
				name == L"double" || name == L"float" ? K::Float : type.Kind != K::Unknown ? type.Kind : K::Unknown };
			return e;
		}
		if (type.Name.empty() && address->Type.Kind == K::ByRef)
			type = { address->Type.Name.substr(4) };
		return Make(E::Deref, {}, { address }, type);
	}

	// call, callvirt and newobj: properties, events, operators and string concatenation as C# writes them
	void MethodDecompiler::CallInstruction(IlInstruction const& inst, bool newobj) {
		auto token = (uint32_t)inst.Value;
		auto info = m_Md.MemberInfo(token, m_Context);
		auto& sig = info.Signature;
		std::vector<ExprPtr> args(sig.Params.size());
		for (size_t i = args.size(); i-- > 0; )
			args[i] = Coerce(Pop(), sig.Params[i]);
		ExprPtr target;
		if (sig.HasThis && !newobj)
			target = Pop();
		m_Constrained = 0;
		auto result = sig.Return;
		auto& name = info.Name;

		auto finish = [&](ExprPtr e) {
			if (!newobj && sig.Return.Kind == K::Void)
				Add(MakeStmt(S::Expr, e));
			else
				Push(e);
		};

		if (newobj) {
			// a delegate made of a method
			if (args.size() == 2 && args[1]->Kind == E::MethodGroup) {
				auto group = args[1];
				if (!IsNull(args[0]) && !group->HasTarget) {
					group = std::make_shared<Expr>(*group);
					group->Args = { args[0] };
					group->HasTarget = true;
					group->Owner.clear();
				}
				auto e = Make(E::New, info.Owner.Name, { group }, info.Owner);
				e->Token = token;
				Push(e);
				return;
			}
			auto type = info.Owner;
			if (type.Kind == K::Unknown)
				type.Kind = K::Object;
			auto e = Make(E::New, type.Name, args, type);
			e->Token = token;
			Push(e);
			return;
		}

		bool isThis = target && target->Kind == E::Var && target->Var == m_This && m_This >= 0;
		bool ownMember = TableOf(info.Definition) == ClrTableId::MethodDef && m_Md.Methods()[RowOf(info.Definition) - 1].Type == m_Def.Type;
		// a constructor's call of another constructor: of the base type or of this one
		if (name == L".ctor" && target) {
			bool own = ownMember;
			if (isThis && !own && info.Owner.Name == L"object" && args.empty())
				return;
			auto e = Make(E::Call, isThis ? (own ? L"this" : L"base") : L".ctor", std::move(args), { L"void", K::Void });
			e->Token = token;
			if (!isThis) {
				e->Args.insert(e->Args.begin(), target);
				e->HasTarget = true;
			}
			Add(MakeStmt(S::Expr, e));
			return;
		}
		if (target && target->Kind == E::AddressOf)
			target = target->Args[0];
		// a call (not callvirt) of another type's method on this: of the base type's
		if (isThis && inst.Opcode == Op::Call && !ownMember)
			target = Make(E::Base, L"base", {}, info.Owner);

		// what kind of accessor it is: of this file, as the metadata says; of another, by its name
		ClrAccessor accessor = ClrAccessor::None;
		if (TableOf(info.Definition) == ClrTableId::MethodDef)
			accessor = m_Md.AccessorOf(RowOf(info.Definition)).second;
		else if (name.starts_with(L"get_"))
			accessor = ClrAccessor::Getter;
		else if (name.starts_with(L"set_"))
			accessor = ClrAccessor::Setter;
		else if (name.starts_with(L"add_"))
			accessor = ClrAccessor::Adder;
		else if (name.starts_with(L"remove_"))
			accessor = ClrAccessor::Remover;

		auto member = [&](std::wstring const& memberName, ClrTypeInfo const& type) {
			auto e = Make(E::Member, memberName, {}, type);
			e->Token = token;
			if (target) {
				e->Args = { target };
				e->HasTarget = true;
			}
			else
				e->Owner = info.Owner.Name;
			return e;
		};
		auto index = [&](std::vector<ExprPtr> indexArgs, ClrTypeInfo const& type) {
			indexArgs.insert(indexArgs.begin(), target);
			auto e = Make(E::Index, {}, std::move(indexArgs), type);
			e->Token = token;
			return e;
		};
		if (accessor == ClrAccessor::Getter && name.size() > 4) {
			if (args.empty()) {
				Push(member(name.substr(4), result));
				return;
			}
			if (target && (name == L"get_Item" || (name == L"get_Chars" && info.Owner.Name == L"string"))) {
				Push(index(args, result));
				return;
			}
		}
		if (accessor == ClrAccessor::Setter && name.size() > 4 && sig.Return.Kind == K::Void && !args.empty()) {
			if (args.size() == 1) {
				Assign(member(name.substr(4), sig.Params[0]), args[0]);
				return;
			}
			if (target && name == L"set_Item") {
				auto value = args.back();
				args.pop_back();
				Assign(index(args, sig.Params.back()), value);
				return;
			}
		}
		if ((accessor == ClrAccessor::Adder || accessor == ClrAccessor::Remover) && args.size() == 1) {
			auto event = member(name.substr(accessor == ClrAccessor::Adder ? 4 : 7), sig.Params[0]);
			Add(MakeStmt(S::Expr, Make(E::Assign, accessor == ClrAccessor::Adder ? L"+=" : L"-=", { event, args[0] }, sig.Params[0])));
			return;
		}

		// operators
		if (!sig.HasThis && name.starts_with(L"op_")) {
			static const std::pair<PCWSTR, PCWSTR> binary[] = {
				{ L"op_Addition", L"+" }, { L"op_Subtraction", L"-" }, { L"op_Multiply", L"*" }, { L"op_Division", L"/" },
				{ L"op_Modulus", L"%" }, { L"op_BitwiseAnd", L"&" }, { L"op_BitwiseOr", L"|" }, { L"op_ExclusiveOr", L"^" },
				{ L"op_LeftShift", L"<<" }, { L"op_RightShift", L">>" }, { L"op_Equality", L"==" }, { L"op_Inequality", L"!=" },
				{ L"op_LessThan", L"<" }, { L"op_GreaterThan", L">" }, { L"op_LessThanOrEqual", L"<=" },
				{ L"op_GreaterThanOrEqual", L">=" },
			};
			static const std::pair<PCWSTR, PCWSTR> unary[] = {
				{ L"op_UnaryNegation", L"-" }, { L"op_UnaryPlus", L"+" }, { L"op_LogicalNot", L"!" }, { L"op_OnesComplement", L"~" },
			};
			if (args.size() == 2)
				for (auto& [op, text] : binary)
					if (name == op) {
						Push(Make(E::Binary, text, args, result));
						return;
					}
			if (args.size() == 1) {
				for (auto& [op, text] : unary)
					if (name == op) {
						Push(Make(E::Unary, text, args, result));
						return;
					}
				if (name == L"op_Implicit") {
					auto e = args[0];
					if (e->Kind == E::Literal)	// a literal of another type (decimal d = 5)
						e = std::make_shared<Expr>(*e), e->Type = result;
					Push(e);
					return;
				}
				if (name == L"op_Explicit") {
					Push(Make(E::Cast, result.Name, args, result));
					return;
				}
			}
		}
		// string.Concat(a, b...) of strings is a + b
		if (!sig.HasThis && info.Owner.Name == L"string" && name == L"Concat" && args.size() >= 2 && args.size() <= 4 &&
			(args[0]->Type.Kind == K::String || args[1]->Type.Kind == K::String)) {
			auto e = args[0];
			for (size_t i = 1; i < args.size(); i++)
				e = Make(E::Binary, L"+", { e, args[i] }, { L"string", K::String });
			Push(e);
			return;
		}
		// Type.GetTypeFromHandle(typeof(T)) is typeof(T)
		if (!sig.HasThis && info.Owner.Name == L"Type" && name == L"GetTypeFromHandle" && args.size() == 1 && args[0]->Kind == E::Typeof) {
			Push(args[0]);
			return;
		}

		// a delegate's Invoke: the delegate is called
		if (name == L"Invoke" && target && sig.HasThis) {
			auto owner = info.Owner.Name;
			bool isDelegate = TableOf(info.Owner.Token) == ClrTableId::TypeDef ? m_Md.TypeKind(RowOf(info.Owner.Token)) == L"delegate" :
				owner.starts_with(L"Func<") || owner.starts_with(L"Action") || owner.ends_with(L"Handler") ||
				owner.starts_with(L"Predicate<") || owner.starts_with(L"Comparison<") || owner.starts_with(L"Converter<") ||
				owner.find(L"EventHandler") != std::wstring::npos;
			if (isDelegate) {
				auto e = Make(E::Call, {}, std::move(args), result);
				e->Token = token;
				e->Args.insert(e->Args.begin(), target);
				e->HasTarget = true;
				finish(e);
				return;
			}
		}

		auto e = Make(E::Call, name, std::move(args), result);
		e->Token = token;
		e->GenericArgs = info.GenericArgs;
		if (target) {
			e->Args.insert(e->Args.begin(), target);
			e->HasTarget = true;
		}
		else
			e->Owner = info.Owner.Name;
		finish(e);
	}

	void MethodDecompiler::Instruction(IlInstruction const& inst, Block& block) {
		auto op = inst.Opcode;
		auto token = (uint32_t)inst.Value;
		ClrTypeInfo intType{ L"int", K::Integer };
		auto binary = [&](PCWSTR text) {
			auto b = Pop();
			auto a = Pop();
			Push(Arithmetic(text, a, b));
		};
		auto checkedBinary = [&](PCWSTR text) {
			auto b = Pop();
			auto a = Pop();
			auto e = Arithmetic(text, a, b);
			Push(Make(E::Checked, {}, { e }, e->Type));
		};
		auto compare = [&](PCWSTR text) {
			auto b = Pop();
			auto a = Pop();
			// x > null (cgt.un) is x != null
			if (std::wstring(text) == L">" && IsNull(b))
				text = L"!=";
			Push(Compare(text, a, b, op == Op::CgtUn || op == Op::CltUn));
		};
		auto branchTo = [&](int64_t target) { return BlockAt((uint32_t)target); };
		auto next = [&]() { return BlockAt(inst.Offset + inst.Size); };

		switch (op) {
			case Op::Nop: case Op::Volatile: case Op::Tail: case Op::Readonly: case Op::Unaligned: case Op::No:
				return;
			case Op::Constrained:
				m_Constrained = token;
				return;
			case Op::Break:
				Add(MakeStmt(S::Expr, Make(E::Raw, L"Debugger.Break()", {}, { L"void", K::Void })));
				return;

			case Op::LdArgS: case Op::LdArg:
				Push(Arg((uint32_t)inst.Value));
				return;
			case Op::LdArgaS: case Op::LdArga:
			{
				auto a = Arg((uint32_t)inst.Value);
				Push(a->Var == m_This && m_This >= 0 ? a : Make(E::AddressOf, {}, { a }, { L"ref " + a->Type.Name, K::ByRef }));
				return;
			}
			case Op::StArgS: case Op::StArg:
			{
				auto v = Pop();
				Assign(Arg((uint32_t)inst.Value), v);
				return;
			}
			case Op::LdLocS: case Op::LdLoc:
				Push(Local((uint32_t)inst.Value));
				return;
			case Op::LdLocaS: case Op::LdLoca:
			{
				auto l = Local((uint32_t)inst.Value);
				Push(Make(E::AddressOf, {}, { l }, { L"ref " + l->Type.Name, K::ByRef }));
				return;
			}
			case Op::StLocS: case Op::StLoc:
			{
				auto v = Pop();
				Assign(Local((uint32_t)inst.Value), v);
				return;
			}
			case Op::LdNull:
				Push(NullLiteral());
				return;
			case Op::LdcI4M1:
				Push(IntLiteral(-1));
				return;
			case Op::LdcI4S: case Op::LdcI4:
				Push(IntLiteral((int32_t)inst.Value));
				return;
			case Op::LdcI8:
				Push(IntLiteral(inst.Value, { L"long", K::Integer }));
				return;
			case Op::LdcR4:
				Push(Make(E::Literal, CSharpReal(inst.Real, true), {}, { L"float", K::Float }));
				return;
			case Op::LdcR8:
				Push(Make(E::Literal, CSharpReal(inst.Real, false), {}, { L"double", K::Float }));
				return;
			case Op::LdStr:
			{
				auto text = m_Md.UserString(token);
				Push(Make(E::Literal, text ? CSharpQuote(*text) : std::format(L"/* string 0x{:08X} */", token), {}, { L"string", K::String }));
				return;
			}
			case Op::Dup:
			{
				auto v = Spill(Pop());
				Push(v);
				Push(v);
				return;
			}
			case Op::Pop:
			{
				auto v = Pop();
				if (HasSideEffects(v))
					Add(MakeStmt(S::Expr, v));
				return;
			}
			case Op::Jmp:
				Add(MakeStmt(S::Expr, Make(E::Raw, L"/* jmp */ " + m_Md.MemberName(token))));
				return;
			case Op::Call: case Op::CallVirt:
				CallInstruction(inst, false);
				return;
			case Op::NewObj:
				CallInstruction(inst, true);
				return;
			case Op::Calli:
			{
				// the signature says how many arguments there are
				auto fp = Pop();
				Push(Make(E::Raw, L"/* calli */ (" + m_Md.TokenText(token) + L")", { fp }));
				return;
			}
			case Op::Ret:
				if (m_Info.Signature.Return.Kind != K::Void && !m_Stack.empty())
					Add(MakeStmt(S::Return, Coerce(Pop(), m_Info.Signature.Return)));
				else
					Add(MakeStmt(S::Return));
				block.Kind = Exit::Return;
				return;
			case Op::Throw:
				Add(MakeStmt(S::Throw, Pop()));
				block.Kind = Exit::Throw;
				return;
			case Op::Rethrow:
				Add(MakeStmt(S::Throw));
				block.Kind = Exit::Throw;
				return;
			case Op::EndFinally:
				block.Kind = Exit::EndFinally;
				return;
			case Op::EndFilter:
				block.Value = Pop();
				block.Kind = Exit::EndFilter;
				return;
			case Op::Br: case Op::BrS:
				block.Kind = Exit::Goto;
				block.Succ = { branchTo(inst.Value) };
				return;
			case Op::Leave: case Op::LeaveS:
				m_Stack.clear();
				block.Kind = Exit::Goto;
				block.Succ = { branchTo(inst.Value) };
				return;
			case Op::BrTrue: case Op::BrTrueS: case Op::BrFalse: case Op::BrFalseS:
			{
				auto c = Condition(Pop());
				block.Value = op == Op::BrTrue || op == Op::BrTrueS ? c : Not(c);
				block.Kind = Exit::Cond;
				block.Succ = { branchTo(inst.Value), next() };
				return;
			}
			case Op::Switch:
			{
				block.Value = Pop();
				block.Kind = Exit::Switch;
				for (auto t : inst.Targets)
					block.Succ.push_back(BlockAt(t));
				block.Succ.push_back(next());
				return;
			}

			case Op::Add: binary(L"+"); return;
			case Op::Sub: binary(L"-"); return;
			case Op::Mul: binary(L"*"); return;
			case Op::Div: case Op::DivUn: binary(L"/"); return;
			case Op::Rem: case Op::RemUn: binary(L"%"); return;
			case Op::And: binary(L"&"); return;
			case Op::Or: binary(L"|"); return;
			case Op::Xor: binary(L"^"); return;
			case Op::Shl: binary(L"<<"); return;
			case Op::Shr: case Op::ShrUn: binary(L">>"); return;
			case Op::AddOvf: case Op::AddOvfUn: checkedBinary(L"+"); return;
			case Op::SubOvf: case Op::SubOvfUn: checkedBinary(L"-"); return;
			case Op::MulOvf: case Op::MulOvfUn: checkedBinary(L"*"); return;
			case Op::Neg:
			{
				auto a = Pop();
				if (a->Kind == E::Literal && a->IsInt)
					Push(IntLiteral(-a->Int, a->Type));
				else
					Push(Make(E::Unary, L"-", { a }, a->Type));
				return;
			}
			case Op::Not:
			{
				auto a = Pop();
				Push(Make(E::Unary, L"~", { a }, a->Type));
				return;
			}
			case Op::Ceq: compare(L"=="); return;
			case Op::Cgt: case Op::CgtUn: compare(L">"); return;
			case Op::Clt: case Op::CltUn: compare(L"<"); return;

			case Op::ConvI1: Push(Convert(L"sbyte", K::Integer, Pop(), false)); return;
			case Op::ConvI2: Push(Convert(L"short", K::Integer, Pop(), false)); return;
			case Op::ConvI4: Push(Convert(L"int", K::Integer, Pop(), false)); return;
			case Op::ConvI8: Push(Convert(L"long", K::Integer, Pop(), false)); return;
			case Op::ConvR4: Push(Convert(L"float", K::Float, Pop(), false)); return;
			case Op::ConvR8: case Op::ConvRUn: Push(Convert(L"double", K::Float, Pop(), false)); return;
			case Op::ConvU4: Push(Convert(L"uint", K::Integer, Pop(), false)); return;
			case Op::ConvU8: Push(Convert(L"ulong", K::Integer, Pop(), false)); return;
			case Op::ConvU2:
			{
				auto a = Pop();
				Push(a->Type.Kind == K::Char ? a : Convert(L"ushort", K::Integer, a, false));
				return;
			}
			case Op::ConvU1: Push(Convert(L"byte", K::Integer, Pop(), false)); return;
			case Op::ConvI:
			{
				auto a = Pop();
				// pointer arithmetic: an index made a native integer
				Push(a->Type.Kind == K::Pointer || a->Kind == E::Literal || a->Type.Kind == K::Integer ? a : Convert(L"IntPtr", K::Integer, a, false));
				return;
			}
			case Op::ConvU:
			{
				auto a = Pop();
				Push(a->Type.Kind == K::Pointer || a->Type.Kind == K::ByRef || a->Kind == E::Literal || a->Type.Kind == K::Integer ? a :
					Convert(L"UIntPtr", K::Integer, a, false));
				return;
			}
			case Op::ConvOvfI: Push(Convert(L"IntPtr", K::Integer, Pop(), true)); return;
			case Op::ConvOvfU: Push(Convert(L"UIntPtr", K::Integer, Pop(), true)); return;
			case Op::CkFinite: return;

			case Op::CastClass:
			{
				auto type = m_Md.CSharpType(token, m_Context);
				auto e = Make(E::Cast, type.Name, { Pop() }, type);
				e->Token = token;
				Push(e);
				return;
			}
			case Op::IsInst:
			{
				auto type = m_Md.CSharpType(token, m_Context);
				if (type.Kind == K::Unknown)
					type.Kind = K::Object;
				auto e = Make(E::As, type.Name, { Pop() }, type);
				e->Token = token;
				e->Type.Kind = K::Object;
				Push(e);
				return;
			}
			case Op::Box:
			{
				auto type = m_Md.CSharpType(token, m_Context);
				auto v = Coerce(Pop(), type);
				Push(v);
				return;
			}
			case Op::Unbox: case Op::UnboxAny:
			{
				auto type = m_Md.CSharpType(token, m_Context);
				auto e = Make(E::Cast, type.Name, { Pop() }, type);
				e->Token = token;
				Push(e);
				return;
			}
			case Op::LdFld:
			{
				auto target = Pop();
				Push(Field(token, target));
				return;
			}
			case Op::LdFlda:
			{
				auto f = Field(token, Pop());
				Push(Make(E::AddressOf, {}, { f }, { L"ref " + f->Type.Name, K::ByRef }));
				return;
			}
			case Op::StFld:
			{
				auto v = Pop();
				auto target = Pop();
				Assign(Field(token, target), v);
				return;
			}
			case Op::LdSFld:
				Push(Field(token, nullptr));
				return;
			case Op::LdSFlda:
			{
				auto f = Field(token, nullptr);
				Push(Make(E::AddressOf, {}, { f }, { L"ref " + f->Type.Name, K::ByRef }));
				return;
			}
			case Op::StSFld:
				Assign(Field(token, nullptr), Pop());
				return;
			case Op::LdObj:
				Push(Indirect(Pop(), m_Md.CSharpType(token, m_Context)));
				return;
			case Op::StObj:
			{
				auto v = Pop();
				auto address = Pop();
				Assign(Indirect(address, m_Md.CSharpType(token, m_Context)), v);
				return;
			}
			case Op::CpObj:
			{
				auto source = Pop();
				auto destination = Pop();
				auto type = m_Md.CSharpType(token, m_Context);
				Assign(Indirect(destination, type), Indirect(source, type));
				return;
			}
			case Op::InitObj:
			{
				auto type = m_Md.CSharpType(token, m_Context);
				auto address = Pop();
				Assign(Indirect(address, type), Make(E::Default, type.Name, {}, type));
				return;
			}
			case Op::NewArr:
			{
				auto type = m_Md.CSharpType(token, m_Context);
				auto e = Make(E::NewArray, type.Name, { Pop() }, { type.Name + L"[]", K::Object });
				e->Token = token;
				Push(e);
				return;
			}
			case Op::LdLen:
				Push(Make(E::Member, L"Length", { Pop() }, intType));
				m_Stack.back()->HasTarget = true;
				return;
			case Op::LdElema:
			{
				auto index = Pop();
				auto array = Pop();
				auto e = Element(array, index, m_Md.CSharpType(token, m_Context));
				Push(Make(E::AddressOf, {}, { e }, { L"ref " + e->Type.Name, K::ByRef }));
				return;
			}
			case Op::LdElem:
			{
				auto index = Pop();
				auto array = Pop();
				Push(Element(array, index, m_Md.CSharpType(token, m_Context)));
				return;
			}
			case Op::StElem:
			{
				auto v = Pop();
				auto index = Pop();
				auto array = Pop();
				Assign(Element(array, index, m_Md.CSharpType(token, m_Context)), v);
				return;
			}
			case Op::LocAlloc:
				Push(Make(E::Raw, L"stackalloc byte", { Pop() }, { L"byte*", K::Pointer }));
				return;
			case Op::SizeOf:
				Push(Make(E::Raw, L"sizeof(" + m_Md.CSharpType(token, m_Context).Name + L")", {}, intType));
				return;
			case Op::LdToken:
				switch (TableOf(token)) {
					case ClrTableId::TypeDef: case ClrTableId::TypeRef: case ClrTableId::TypeSpec:
						Push(TypeExprOf(token));
						return;
					case ClrTableId::Field:
					case ClrTableId::MemberRef:
					{
						auto info = m_Md.MemberInfo(token, m_Context);
						auto e = Make(E::Raw, (info.IsField ? L"fieldof(" : L"methodof(") + info.Owner.Name + L"." + info.Name + L")");
						e->Token = token;
						Push(e);
						return;
					}
				}
				Push(Make(E::Raw, L"methodof(" + m_Md.TokenText(token) + L")"));
				return;
			case Op::LdFtn: case Op::LdVirtFtn:
			{
				auto info = m_Md.MemberInfo(token, m_Context);
				auto e = Make(E::MethodGroup, info.Name, {}, { L"IntPtr", K::Integer });
				e->Token = token;
				e->Owner = info.Owner.Name;
				if (op == Op::LdVirtFtn) {
					e->Args = { Pop() };
					e->HasTarget = true;
				}
				Push(e);
				return;
			}
			case Op::ArgList:
				Push(Make(E::Raw, L"__arglist"));
				return;
			case Op::MkRefAny:
				Push(Make(E::Raw, L"__makeref", { Pop() }, { L"TypedReference", K::Struct }));
				return;
			case Op::RefAnyVal:
				Push(Make(E::Raw, L"__refvalue", { Pop() }));
				return;
			case Op::RefAnyType:
				Push(Make(E::Raw, L"__reftype", { Pop() }, { L"Type", K::Object }));
				return;
			case Op::CpBlk: case Op::InitBlk:
			{
				auto size = Pop();
				auto value = Pop();
				auto address = Pop();
				Add(MakeStmt(S::Expr, Make(E::Raw, op == Op::CpBlk ? L"Unsafe.CopyBlock" : L"Unsafe.InitBlock", { address, value, size })));
				return;
			}
		}

		if (op >= Op::LdArg0 && op <= Op::LdArg3) {
			Push(Arg(op - Op::LdArg0));
			return;
		}
		if (op >= Op::LdLoc0 && op <= Op::LdLoc3) {
			Push(Local(op - Op::LdLoc0));
			return;
		}
		if (op >= Op::StLoc0 && op <= Op::StLoc3) {
			auto v = Pop();
			Assign(Local(op - Op::StLoc0), v);
			return;
		}
		if (op >= Op::LdcI4_0 && op <= Op::LdcI4_8) {
			Push(IntLiteral(op - Op::LdcI4_0));
			return;
		}
		if (auto cmp = BranchComparison(op)) {
			auto b = Pop();
			auto a = Pop();
			bool unordered = (op >= 0x34 && op <= 0x37) || (op >= 0x41 && op <= 0x44);	// bge.un... blt.un
			block.Value = Compare(cmp, a, b, unordered);
			block.Kind = Exit::Cond;
			block.Succ = { branchTo(inst.Value), next() };
			return;
		}
		if (op >= Op::LdIndI1 && op <= Op::LdIndRef) {
			static const std::pair<PCWSTR, K> types[] = {
				{ L"sbyte", K::Integer }, { L"byte", K::Integer }, { L"short", K::Integer }, { L"ushort", K::Integer },
				{ L"int", K::Integer }, { L"uint", K::Integer }, { L"long", K::Integer }, { L"IntPtr", K::Integer },
				{ L"float", K::Float }, { L"double", K::Float }, { L"", K::Unknown },
			};
			auto& [name, kind] = types[op - Op::LdIndI1];
			Push(Indirect(Pop(), { name, kind }));
			return;
		}
		if ((op >= Op::StIndRef && op <= Op::StIndR8) || op == Op::StIndI) {
			auto v = Pop();
			auto address = Pop();
			Assign(Indirect(address, {}), v);
			return;
		}
		if ((op >= Op::LdElemI1 && op <= Op::LdElemRef)) {
			static const std::pair<PCWSTR, K> types[] = {
				{ L"sbyte", K::Integer }, { L"byte", K::Integer }, { L"short", K::Integer }, { L"ushort", K::Integer },
				{ L"int", K::Integer }, { L"uint", K::Integer }, { L"long", K::Integer }, { L"IntPtr", K::Integer },
				{ L"float", K::Float }, { L"double", K::Float }, { L"", K::Unknown },
			};
			auto& [name, kind] = types[op - Op::LdElemI1];
			auto index = Pop();
			auto array = Pop();
			Push(Element(array, index, { name, kind }));
			return;
		}
		if (op >= Op::StElemI && op <= Op::StElemRef) {
			auto v = Pop();
			auto index = Pop();
			auto array = Pop();
			Assign(Element(array, index, {}), v);
			return;
		}
		if ((op >= Op::ConvOvfI1Un && op <= Op::ConvOvfUUn) || (op >= Op::ConvOvfI1 && op <= Op::ConvOvfU8)) {
			static const PCWSTR un[] = { L"sbyte", L"short", L"int", L"long", L"byte", L"ushort", L"uint", L"ulong", L"IntPtr", L"UIntPtr" };
			static const PCWSTR ovf[] = { L"sbyte", L"byte", L"short", L"ushort", L"int", L"uint", L"long", L"ulong" };
			auto name = op <= Op::ConvOvfUUn ? un[op - Op::ConvOvfI1Un] : ovf[op - Op::ConvOvfI1];
			Push(Convert(name, K::Integer, Pop(), true));
			return;
		}
		auto opcode = GetIlOpcode(op);
		Add(MakeStmt(S::Comment));
		m_Current->Stmts.back()->Text = std::format(L"IL_{:04x}: {} (not decompiled)", inst.Offset, opcode ? opcode->Name : L"?");
	}

	int MethodDecompiler::BlockAt(uint32_t offset) const {
		auto it = m_BlockAt.find(offset);
		return it == m_BlockAt.end() ? -1 : it->second;
	}

	bool MethodDecompiler::BuildBlocks() {
		m_Complete = DecodeIl(m_Body.Code, m_Code);
		if (m_Code.empty())
			return false;
		std::set<uint32_t> offsets;
		for (auto& inst : m_Code)
			offsets.insert(inst.Offset);
		std::set<uint32_t> leaders{ 0 };
		for (auto& inst : m_Code) {
			auto next = inst.Offset + inst.Size;
			if (inst.Operand == IlOperand::Branch8 || inst.Operand == IlOperand::Branch32)
				leaders.insert((uint32_t)inst.Value);
			for (auto t : inst.Targets)
				leaders.insert(t);
			if (EndsBlock(inst.Opcode))
				leaders.insert(next);
		}
		for (auto& c : m_Body.Clauses) {
			leaders.insert(c.TryOffset);
			leaders.insert(c.TryOffset + c.TryLength);
			leaders.insert(c.HandlerOffset);
			leaders.insert(c.HandlerOffset + c.HandlerLength);
			if ((c.Flags & 7) == 1)
				leaders.insert(c.FilterOffset);
			m_Regions.push_back({ c.TryOffset, c.TryOffset + c.TryLength });
			m_Regions.push_back({ c.HandlerOffset, c.HandlerOffset + c.HandlerLength });
			if ((c.Flags & 7) == 1)
				m_Regions.push_back({ c.FilterOffset, c.HandlerOffset });
		}
		size_t i = 0;
		while (i < m_Code.size()) {
			Block block;
			block.Start = m_Code[i].Offset;
			block.First = i;
			do {
				i++;
			} while (i < m_Code.size() && !leaders.contains(m_Code[i].Offset));
			block.Last = i;
			block.End = i < m_Code.size() ? m_Code[i].Offset : m_Code.back().Offset + m_Code.back().Size;
			m_BlockAt[block.Start] = (int)m_Blocks.size();
			m_Blocks.push_back(std::move(block));
		}
		// the innermost region of each block
		for (auto& b : m_Blocks) {
			uint32_t size = UINT32_MAX;
			for (size_t r = 0; r < m_Regions.size(); r++)
				if (b.Start >= m_Regions[r].Start && b.Start < m_Regions[r].End && m_Regions[r].End - m_Regions[r].Start < size) {
					size = m_Regions[r].End - m_Regions[r].Start;
					b.Region = (int)r;
				}
		}
		return true;
	}

	void MethodDecompiler::Simulate(int b) {
		auto& block = m_Blocks[b];
		m_Current = &block;
		m_Stack.clear();
		m_Constrained = 0;
		bool handler = false;
		for (auto& c : m_Body.Clauses)
			if (((c.Flags & 7) == 0 && c.HandlerOffset == block.Start) || ((c.Flags & 7) == 1 && (c.FilterOffset == block.Start || c.HandlerOffset == block.Start)))
				handler = true;
		for (int k = 0; k < block.Depth; k++) {
			if (k == 0 && handler) {
				Push(Make(E::Exception, L"ex", {}, { L"Exception", K::Object }));
				continue;
			}
			while ((int)m_StackVars.size() <= k)
				m_StackVars.push_back(NewVar(Variable::Roles::Stack, std::format(L"stack{}", m_StackVars.size())));
			Push(VarExpr(m_StackVars[k]));
		}
		block.Kind = Exit::Goto;
		bool ended = false;
		for (auto i = block.First; i < block.Last; i++) {
			Instruction(m_Code[i], block);
			if (EndsBlock(m_Code[i].Opcode))
				ended = true;
		}
		if (!ended) {
			// it goes on into the next block
			auto next = BlockAt(block.End);
			block.Kind = Exit::Goto;
			block.Succ = { next };
			if (next < 0) {
				block.Succ.clear();
				block.Kind = Exit::Return;
			}
		}
		// what is left on the stack goes on to the next blocks
		if (!block.Succ.empty() && !m_Stack.empty()) {
			for (size_t k = 0; k < m_Stack.size(); k++) {
				while (m_StackVars.size() <= k)
					m_StackVars.push_back(NewVar(Variable::Roles::Stack, std::format(L"stack{}", m_StackVars.size())));
				auto& e = m_Stack[k];
				if (e->Kind == E::Var && e->Var == m_StackVars[k])
					continue;
				auto s = MakeStmt(S::Expr, Make(E::Assign, L"=", { VarExpr(m_StackVars[k]), e }, e->Type));
				s->Spill = true;
				block.Stmts.push_back(s);
				if (m_Vars[m_StackVars[k]].Type.Name.empty())
					m_Vars[m_StackVars[k]].Type = e->Type;
			}
		}
		auto exitDepth = (int)m_Stack.size();
		for (auto s : block.Succ)
			if (s >= 0 && m_Blocks[s].Depth < 0)
				m_Blocks[s].Depth = exitDepth;
	}

	void MethodDecompiler::Connect() {
		for (auto& b : m_Blocks)
			b.Pred.clear();
		for (int i = 0; i < (int)m_Blocks.size(); i++) {
			auto& b = m_Blocks[i];
			if (b.Removed)
				continue;
			std::erase(b.Succ, -1);
			for (auto s : b.Succ)
				if (std::find(m_Blocks[s].Pred.begin(), m_Blocks[s].Pred.end(), i) == m_Blocks[s].Pred.end())
					m_Blocks[s].Pred.push_back(i);
		}
	}

	// c ? a : b: the two blocks of a and b each give the stack's value, and go on to the same block
	bool MethodDecompiler::MergeTernary(int b) {
		auto& block = m_Blocks[b];
		if (block.Kind != Exit::Cond || block.Succ.size() != 2 || block.Succ[0] == block.Succ[1])
			return false;
		int t = block.Succ[0], f = block.Succ[1];
		auto valueOnly = [&](int x) {
			auto& v = m_Blocks[x];
			return !v.Root && v.Pred.size() == 1 && v.Kind == Exit::Goto && v.Succ.size() == 1 && v.Stmts.size() == 1 &&
				v.Stmts[0]->Spill && v.Region == block.Region;
		};
		if (!valueOnly(t) || !valueOnly(f) || m_Blocks[t].Succ[0] != m_Blocks[f].Succ[0])
			return false;
		auto& ta = m_Blocks[t].Stmts[0]->Value;
		auto& fa = m_Blocks[f].Stmts[0]->Value;
		if (ta->Args[0]->Var != fa->Args[0]->Var)
			return false;
		auto a = ta->Args[1], c = fa->Args[1];
		auto type = a->Type.Kind != K::Unknown && a->Kind != E::Literal ? a->Type : c->Type;
		if (a->Kind == E::Literal && c->Kind == E::Literal && a->IsInt && c->IsInt && (a->Int == 0 || a->Int == 1) && (c->Int == 0 || c->Int == 1))
			type = { L"bool", K::Bool };
		auto value = Coerce(Make(E::Conditional, {}, { block.Value, a, c }, type), type);
		auto s = MakeStmt(S::Expr, Make(E::Assign, L"=", { ta->Args[0], value }, type));
		s->Spill = true;
		block.Stmts.push_back(s);
		block.Kind = Exit::Goto;
		block.Value = nullptr;
		block.Succ = { m_Blocks[t].Succ[0] };
		m_Blocks[t].Removed = m_Blocks[f].Removed = true;
		m_Blocks[t].Succ.clear();
		m_Blocks[f].Succ.clear();
		if (m_Vars[ta->Args[0]->Var].Type.Kind == K::Unknown || type.Kind == K::Bool)
			m_Vars[ta->Args[0]->Var].Type = type;
		Connect();
		return true;
	}

	// x ?? y: a value is tested for null (after dup), and the block that is reached when it is null gives another one
	bool MethodDecompiler::MergeCoalesce(int b) {
		auto& block = m_Blocks[b];
		if (block.Kind != Exit::Cond || block.Succ.size() != 2 || block.Stmts.empty() || !block.Stmts.back()->Spill)
			return false;
		auto& c = block.Value;
		if (c->Kind != E::Binary || c->Text != L"!=" || c->Args[0]->Kind != E::Var || !IsNull(c->Args[1]))
			return false;
		int tested = c->Args[0]->Var;
		auto& spill = block.Stmts.back()->Value;
		if (spill->Args[1]->Kind != E::Var || spill->Args[1]->Var != tested)
			return false;
		int slot = spill->Args[0]->Var;
		int m = block.Succ[0], f = block.Succ[1];
		auto& fb = m_Blocks[f];
		if (fb.Root || fb.Pred.size() != 1 || fb.Kind != Exit::Goto || fb.Succ.size() != 1 || fb.Succ[0] != m || fb.Region != block.Region ||
			fb.Stmts.empty() || !fb.Stmts.back()->Spill || fb.Stmts.back()->Value->Args[0]->Var != slot)
			return false;
		auto other = fb.Stmts.back()->Value->Args[1];
		// t = new ...; x = t; (spill t): the value is the assignment x = new ...
		if (fb.Stmts.size() == 3) {
			auto& first = fb.Stmts[0]->Value;
			auto& second = fb.Stmts[1]->Value;
			if (fb.Stmts[0]->Kind != S::Expr || fb.Stmts[1]->Kind != S::Expr || first->Kind != E::Assign || second->Kind != E::Assign ||
				first->Args[0]->Kind != E::Var || other->Kind != E::Var || other->Var != first->Args[0]->Var ||
				second->Args[1]->Kind != E::Var || second->Args[1]->Var != other->Var)
				return false;
			other = Make(E::Assign, L"=", { second->Args[0], first->Args[1] }, second->Args[0]->Type);
		}
		else if (fb.Stmts.size() != 1)
			return false;
		block.Stmts.back()->Value = Make(E::Assign, L"=", { spill->Args[0], Make(E::Binary, L"??", { spill->Args[1], other }, spill->Args[1]->Type) },
			spill->Args[1]->Type);
		block.Kind = Exit::Goto;
		block.Value = nullptr;
		block.Succ = { m };
		fb.Removed = true;
		fb.Succ.clear();
		fb.Stmts.clear();
		Connect();
		return true;
	}

	// if (a) if (b): a block that only tests a condition, after another one, is an && or an ||
	bool MethodDecompiler::MergeCondition(int b) {
		auto& b1 = m_Blocks[b];
		if (b1.Kind != Exit::Cond || b1.Succ.size() != 2)
			return false;
		for (int which = 0; which < 2; which++) {
			int x = b1.Succ[which];
			auto& b2 = m_Blocks[x];
			if (x == b || b2.Root || b2.Kind != Exit::Cond || !b2.Stmts.empty() || b2.Pred.size() != 1 || b2.Region != b1.Region)
				continue;
			int t1 = b1.Succ[0], f1 = b1.Succ[1], t2 = b2.Succ[0], f2 = b2.Succ[1];
			auto c1 = b1.Value, c2 = b2.Value;
			ClrTypeInfo boolean{ L"bool", K::Bool };
			ExprPtr c;
			if (which == 0 && f1 == f2)
				c = Make(E::Binary, L"&&", { c1, c2 }, boolean);
			else if (which == 0 && f1 == t2)
				c = Make(E::Binary, L"||", { Not(c1), c2 }, boolean);
			else if (which == 1 && t1 == t2)
				c = Make(E::Binary, L"||", { c1, c2 }, boolean);
			else if (which == 1 && t1 == f2)
				c = Make(E::Binary, L"&&", { Not(c1), c2 }, boolean);
			else
				continue;
			b1.Value = c;
			b1.Succ = { t2, f2 };
			b2.Removed = true;
			b2.Succ.clear();
			Connect();
			return true;
		}
		return false;
	}

	// a block that only goes on to the next one, which only it goes to: one block (the values it leaves on the stack read
	// by the next one's first statement)
	bool MethodDecompiler::MergeLine(int b) {
		auto& block = m_Blocks[b];
		if (block.Kind != Exit::Goto || block.Succ.size() != 1)
			return false;
		int n = block.Succ[0];
		auto& next = m_Blocks[n];
		if (n == b || next.Root || next.Pred.size() != 1 || next.Region != block.Region)
			return false;
		// a try or a handler that starts there stays where it is
		for (auto& c : m_Body.Clauses)
			if (c.TryOffset == next.Start || c.HandlerOffset == next.Start)
				return false;
		// the values: into the first statement of the next block that reads them (or its condition)
		while (!block.Stmts.empty() && block.Stmts.back()->Spill) {
			auto spill = block.Stmts.back();
			int var = spill->Value->Args[0]->Var;
			auto reads = [&](ExprPtr const& e) {
				int found = 0;
				ForEach(e, [&](Expr const& x) { found += x.Kind == E::Var && x.Var == var; });
				return found;
			};
			ExprPtr* use = nullptr;
			if (!next.Stmts.empty() && next.Stmts[0]->Value)
				use = &next.Stmts[0]->Value;
			else if (next.Stmts.empty() && next.Value)
				use = &next.Value;
			// nothing else may read it: not the rest of the block, and not the blocks after it (that it would pass it on to)
			int total = next.Value ? reads(next.Value) : 0;
			for (auto& s : next.Stmts)
				if (s->Value)
					total += reads(s->Value);
			auto slot = std::find(m_StackVars.begin(), m_StackVars.end(), var) - m_StackVars.begin();
			bool passed = false;
			for (int s : next.Succ)
				passed |= m_Blocks[s].Depth > slot;
			for (auto& s : next.Stmts)
				if (s->Spill && s->Value->Args[0]->Var == var)
					passed = false;
			if (!use || reads(*use) != 1 || total != 1 || passed)
				break;
			int one = 1;
			*use = Substitute(*use, var, spill->Value->Args[1], one);
			block.Stmts.pop_back();
		}
		for (auto& s : next.Stmts)
			block.Stmts.push_back(s);
		block.Kind = next.Kind;
		block.Value = next.Value;
		block.Succ = next.Succ;
		block.End = next.End;
		block.Last = next.Last;
		next.Removed = true;
		next.Succ.clear();
		next.Stmts.clear();
		Connect();
		return true;
	}

	// an empty block that only goes on: its predecessors go there directly
	void MethodDecompiler::Bypass() {
		for (int i = 0; i < (int)m_Blocks.size(); i++) {
			auto& b = m_Blocks[i];
			if (b.Removed || b.Root || b.Kind != Exit::Goto || !b.Stmts.empty() || b.Succ.size() != 1 || b.Succ[0] == i)
				continue;
			bool start = false;
			for (auto& c : m_Body.Clauses)
				start |= c.TryOffset == b.Start || c.HandlerOffset == b.Start || ((c.Flags & 7) == 1 && c.FilterOffset == b.Start);
			if (start)
				continue;
			int target = b.Succ[0];
			for (auto p : std::vector<int>(b.Pred)) {
				if (m_Blocks[p].Region != b.Region && m_Blocks[target].Region != m_Blocks[p].Region)
					continue;
				for (auto& s : m_Blocks[p].Succ)
					if (s == i)
						s = target;
			}
			Connect();
			if (m_Blocks[i].Pred.empty()) {
				m_Blocks[i].Removed = true;
				m_Blocks[i].Succ.clear();
				Connect();
			}
		}
	}

	void MethodDecompiler::Simplify() {
		Connect();
		for (bool changed = true; changed; ) {
			changed = false;
			Bypass();
			for (int i = 0; i < (int)m_Blocks.size(); i++) {
				if (m_Blocks[i].Removed)
					continue;
				changed |= MergeTernary(i) || MergeCoalesce(i) || MergeCondition(i) || MergeLine(i);
			}
		}
	}

	// The dominators (of the blocks reached from the start and the handlers) and post-dominators (Cooper, Harvey, Kennedy)
	void MethodDecompiler::Dominators() {
		int n = (int)m_Blocks.size();
		int root = n;
		auto succ = [&](int x) -> std::vector<int> {
			if (x == root) {
				std::vector<int> roots;
				for (int i = 0; i < n; i++)
					if (m_Blocks[i].Root && !m_Blocks[i].Removed)
						roots.push_back(i);
				return roots;
			}
			return m_Blocks[x].Succ;
		};
		auto compute = [&](int start, std::function<std::vector<int>(int)> const& next, std::function<std::vector<int>(int)> const& prev, std::vector<int>& idom) {
			std::vector<int> order, index(n + 1, -1);
			std::vector<bool> seen(n + 1);
			// an iterative postorder
			std::vector<std::pair<int, size_t>> stack{ { start, 0 } };
			seen[start] = true;
			std::vector<std::vector<int>> nexts(n + 1);
			nexts[start] = next(start);
			while (!stack.empty()) {
				auto& [x, i] = stack.back();
				if (i < nexts[x].size()) {
					int y = nexts[x][i++];
					if (y >= 0 && !seen[y]) {
						seen[y] = true;
						nexts[y] = next(y);
						stack.push_back({ y, 0 });
					}
					continue;
				}
				index[x] = (int)order.size();
				order.push_back(x);
				stack.pop_back();
			}
			idom.assign(n + 1, -1);
			idom[start] = start;
			for (bool changed = true; changed; ) {
				changed = false;
				for (auto it = order.rbegin(); it != order.rend(); ++it) {
					int x = *it;
					if (x == start)
						continue;
					int dom = -1;
					for (int p : prev(x)) {
						if (p < 0 || idom[p] < 0)
							continue;
						if (dom < 0) {
							dom = p;
							continue;
						}
						int a = p, b = dom;
						while (a != b) {
							while (index[a] < index[b])
								a = idom[a];
							while (index[b] < index[a])
								b = idom[b];
						}
						dom = a;
					}
					if (dom >= 0 && idom[x] != dom) {
						idom[x] = dom;
						changed = true;
					}
				}
			}
			return order;
		};
		auto pred = [&](int x) -> std::vector<int> {
			if (x == root)
				return {};
			auto p = m_Blocks[x].Pred;
			if (m_Blocks[x].Root)
				p.push_back(root);
			return p;
		};
		auto order = compute(root, succ, pred, m_Idom);
		m_Order.assign(order.rbegin(), order.rend());
		std::erase(m_Order, root);

		// post-dominators: the reverse graph, from an exit that the blocks that end the method (or a handler) go to
		int exit = n;
		auto exits = [&](int x) { return !m_Blocks[x].Removed && m_Blocks[x].Succ.empty() && m_Idom[x] >= 0; };
		auto rsucc = [&](int x) -> std::vector<int> {
			if (x == exit) {
				std::vector<int> ends;
				for (int i = 0; i < n; i++)
					if (exits(i))
						ends.push_back(i);
				return ends;
			}
			return m_Blocks[x].Pred;
		};
		auto rpred = [&](int x) -> std::vector<int> {
			if (x == exit)
				return {};
			auto s = m_Blocks[x].Succ;
			if (exits(x))
				s.push_back(exit);
			return s;
		};
		compute(exit, rsucc, rpred, m_Ipdom);
		for (auto& d : m_Ipdom)
			if (d == exit)
				d = -1;
	}

	bool MethodDecompiler::Dominates(int a, int b) const {
		int n = (int)m_Blocks.size();
		for (int x = b, steps = 0; x >= 0 && x < n && steps <= n; steps++) {
			if (x == a)
				return true;
			if (m_Idom[x] == x || m_Idom[x] >= n)
				return false;
			x = m_Idom[x];
		}
		return false;
	}

	void MethodDecompiler::FindLoops() {
		int n = (int)m_Blocks.size();
		m_Header.assign(n, false);
		auto dominates = [&](int a, int b) {
			for (int x = b, steps = 0; x >= 0 && x < n && steps <= n; steps++) {
				if (x == a)
					return true;
				if (m_Idom[x] == x || m_Idom[x] >= n)
					return false;
				x = m_Idom[x];
			}
			return false;
		};
		for (int u : m_Order) {
			for (int h : m_Blocks[u].Succ) {
				if (!dominates(h, u))
					continue;
				// the body: what reaches the back edge without passing the header
				auto& body = m_LoopBody[h];
				if (body.empty())
					body.assign(n, false);
				body[h] = true;
				std::vector<int> work{ u };
				while (!work.empty()) {
					int x = work.back();
					work.pop_back();
					if (body[x])
						continue;
					body[x] = true;
					for (int p : m_Blocks[x].Pred)
						if (!body[p] && dominates(h, p))
							work.push_back(p);
				}
				m_Header[h] = true;
			}
		}
		for (auto& [h, body] : m_LoopBody) {
			// the handlers of the tries in it are in it
			for (bool changed = true; changed; ) {
				changed = false;
				for (auto& c : m_Body.Clauses) {
					int start = BlockAt(c.TryOffset);
					if (start < 0 || !body[start])
						continue;
					for (int i = 0; i < n; i++) {
						auto& b = m_Blocks[i];
						bool inside = (b.Start >= c.HandlerOffset && b.Start < c.HandlerOffset + c.HandlerLength) ||
							(b.Start >= c.TryOffset && b.Start < c.TryOffset + c.TryLength) ||
							((c.Flags & 7) == 1 && b.Start >= c.FilterOffset && b.Start < c.HandlerOffset);
						if (inside && !b.Removed && !body[i]) {
							body[i] = changed = true;
						}
					}
				}
			}
			// the exit: where the header's condition (or a latch's) leaves the loop; else the first block it leaves to
			std::vector<int> exits;
			for (int x = 0; x < n; x++)
				if (body[x])
					for (int s : m_Blocks[x].Succ)
						if (!body[s] && std::find(exits.begin(), exits.end(), s) == exits.end())
							exits.push_back(s);
			int exit = -1;
			auto& hb = m_Blocks[h];
			if (hb.Kind == Exit::Cond && hb.Succ.size() == 2 && body[hb.Succ[0]] != body[hb.Succ[1]])
				exit = body[hb.Succ[0]] ? hb.Succ[1] : hb.Succ[0];
			if (exit < 0)
				for (int x = 0; x < n && exit < 0; x++)
					if (body[x] && m_Blocks[x].Kind == Exit::Cond && std::find(m_Blocks[x].Succ.begin(), m_Blocks[x].Succ.end(), h) != m_Blocks[x].Succ.end())
						for (int s : m_Blocks[x].Succ)
							if (!body[s])
								exit = s;
			if (exit < 0 && !exits.empty())
				exit = *std::min_element(exits.begin(), exits.end(), [&](int a, int b) { return m_Blocks[a].Start < m_Blocks[b].Start; });
			m_LoopExit[h] = exit;
			// the blocks that only the loop goes to and that go on to its exit, or end the method, are in it: they are the
			// code before a break (or a return in the loop)
			for (bool changed = true; changed; ) {
				changed = false;
				for (int x = 0; x < n; x++) {
					auto& b = m_Blocks[x];
					if (body[x] || x == exit || b.Removed || b.Pred.empty() || !dominates(h, x) || b.Region != hb.Region)
						continue;
					bool fromLoop = std::all_of(b.Pred.begin(), b.Pred.end(), [&](int p) { return body[p]; });
					bool toExit = !b.Succ.empty() && std::all_of(b.Succ.begin(), b.Succ.end(), [&](int s) { return body[s] || s == exit; });
					bool ends = b.Succ.empty() && (b.Kind == Exit::Return || b.Kind == Exit::Throw);
					if (fromLoop && (toExit || ends)) {
						body[x] = true;
						changed = true;
					}
				}
			}
		}
	}

	bool MethodDecompiler::InRange(int b, Context const& ctx) const {
		return m_Blocks[b].Start >= ctx.RangeStart && m_Blocks[b].Start < ctx.RangeEnd;
	}

	StmtPtr MethodDecompiler::GotoBlock(int b) {
		auto s = MakeStmt(S::Goto);
		s->Target = b;
		return s;
	}

	// the outermost try that starts at a block and is not being written (its offset and length as a key), 0 if none
	uint64_t MethodDecompiler::TryAt(int b, Context const& ctx) const {
		uint64_t best = 0;
		uint32_t length = 0;
		for (auto& c : m_Body.Clauses) {
			if (c.TryOffset != m_Blocks[b].Start)
				continue;
			uint64_t key = ((uint64_t)c.TryOffset << 32) | c.TryLength;
			if (std::find(ctx.ActiveTries.begin(), ctx.ActiveTries.end(), key) != ctx.ActiveTries.end())
				continue;
			if (c.TryOffset + c.TryLength > ctx.RangeEnd)
				continue;
			if (c.TryLength > length) {
				length = c.TryLength;
				best = key;
			}
		}
		return best;
	}

	StmtList MethodDecompiler::Branch(int b, int follow, Context const& ctx) {
		Context sub = ctx;
		if (follow >= 0)
			sub.Follow = follow;
		StmtList list;
		Emit(b, sub, list);
		return list;
	}

	void MethodDecompiler::Emit(int b, Context const& ctx, StmtList& out, int header) {
		if (++m_Depth > 400) {
			if (b >= 0)
				out.push_back(GotoBlock(b));
			m_Depth--;
			return;
		}
		for (bool first = true; b >= 0; first = false) {
			if (b == ctx.Follow)
				break;
			bool inRange = InRange(b, ctx);
			if (b == ctx.LoopHeader && !(first && header == b)) {
				out.push_back(MakeStmt(S::Continue));
				break;
			}
			if (b == ctx.LoopExit) {
				out.push_back(ctx.InSwitch ? GotoBlock(b) : MakeStmt(S::Break));
				break;
			}
			if (!inRange && b == ctx.After)
				break;
			// a small block that returns or throws: written again, rather than gone to
			auto& target = m_Blocks[b];
			bool outside = !inRange || (ctx.LoopBody && !(*ctx.LoopBody)[b]);
			if ((m_Emitted[b] || outside) && target.Succ.empty() && (target.Kind == Exit::Return || target.Kind == Exit::Throw) &&
				target.Stmts.size() <= 3 && !target.Root) {
				for (auto& st : target.Stmts)
					out.push_back(st);
				m_Emitted[b] = true;
				break;
			}
			if (!inRange || m_Emitted[b] || (ctx.LoopBody && !(*ctx.LoopBody)[b])) {
				out.push_back(GotoBlock(b));
				break;
			}
			if (auto key = TryAt(b, ctx)) {
				b = EmitTry(b, key, ctx, out);
				continue;
			}
			if (m_Header[b] && !(first && header == b)) {
				b = EmitLoop(b, ctx, out);
				continue;
			}
			m_Emitted[b] = true;
			auto label = MakeStmt(S::Label);
			label->Target = b;
			out.push_back(label);
			auto& block = m_Blocks[b];
			for (auto& s : block.Stmts)
				out.push_back(s);
			if (block.Kind == Exit::Goto && block.Succ.size() == 1) {
				b = block.Succ[0];
				continue;
			}
			if (block.Kind == Exit::Cond && block.Succ.size() == 2) {
				int m = m_Ipdom[b];
				auto usable = [&](int x) {
					return x >= 0 && InRange(x, ctx) && !(ctx.LoopBody && !(*ctx.LoopBody)[x]) && x != ctx.LoopHeader && !m_Emitted[x];
				};
				if (!usable(m))
					m = -1;
				// no block where both ways meet (one of them returns): if one way's block is only gone to otherwise from the
				// other way, the if is the other way, and goes on there (if (!c) { ...return; } x)
				if (m < 0)
					for (int i = 0; i < 2 && m < 0; i++) {
						int x = block.Succ[i], other = block.Succ[1 - i];
						auto& xb = m_Blocks[x];
						if (!usable(x) || xb.Pred.size() < 2 || x == other)
							continue;
						if (std::all_of(xb.Pred.begin(), xb.Pred.end(), [&](int p) { return p == b || Dominates(other, p); }))
							m = x;
					}
				auto s = MakeStmt(S::If, block.Value);
				s->Body = Branch(block.Succ[0], m, ctx);
				s->Else = Branch(block.Succ[1], m, ctx);
				out.push_back(s);
				if (m < 0)
					break;
				b = m;
				continue;
			}
			if (block.Kind == Exit::Switch) {
				int m = m_Ipdom[b];
				if (m >= 0 && (!InRange(m, ctx) || (ctx.LoopBody && !(*ctx.LoopBody)[m]) || m == ctx.LoopHeader))
					m = -1;
				EmitSwitch(b, ctx, out, m);
				if (m < 0)
					break;
				b = m;
				continue;
			}
			break;	// return, throw, the end of a handler
		}
		m_Depth--;
	}

	int MethodDecompiler::EmitLoop(int h, Context const& ctx, StmtList& out) {
		Context sub = ctx;
		sub.LoopHeader = h;
		sub.LoopExit = m_LoopExit[h];
		sub.LoopBody = &m_LoopBody[h];
		sub.Follow = -1;
		sub.InSwitch = false;
		auto loop = MakeStmt(S::While, BoolLiteral(true));
		Emit(h, sub, loop->Body, h);
		out.push_back(loop);
		int exit = m_LoopExit[h];
		return exit;
	}

	void MethodDecompiler::EmitSwitch(int b, Context const& ctx, StmtList& out, int follow) {
		auto& block = m_Blocks[b];
		auto s = MakeStmt(S::Switch, block.Value);
		// switch (x - 1) with cases 0, 1... is switch (x) with cases 1, 2...
		int64_t bias = 0;
		if (s->Value->Kind == E::Binary && s->Value->Text == L"-" && s->Value->Args[1]->Kind == E::Literal && s->Value->Args[1]->IsInt) {
			bias = s->Value->Args[1]->Int;
			s->Value = s->Value->Args[0];
		}
		Context sub = ctx;
		sub.Follow = follow;
		sub.InSwitch = true;
		int fallback = block.Succ.back();
		std::vector<int> targets;
		for (size_t i = 0; i + 1 < block.Succ.size(); i++) {
			int t = block.Succ[i];
			if (t == fallback)
				continue;
			auto it = std::find(targets.begin(), targets.end(), t);
			if (it == targets.end()) {
				targets.push_back(t);
				s->Cases.push_back({});
				it = targets.end() - 1;
			}
			s->Cases[it - targets.begin()].Values.push_back((int64_t)i + bias);
		}
		for (size_t i = 0; i < targets.size(); i++) {
			auto& c = s->Cases[i];
			if (targets[i] != follow)
				Emit(targets[i], sub, c.Body);
			if (!EndsWithJump(c.Body))
				c.Body.push_back(MakeStmt(S::Break));
		}
		if (fallback != follow) {
			Case c;
			c.Default = true;
			Emit(fallback, sub, c.Body);
			if (!EndsWithJump(c.Body))
				c.Body.push_back(MakeStmt(S::Break));
			s->Cases.push_back(std::move(c));
		}
		// the values the switch is on are of the value's type (characters, an enum's)
		out.push_back(s);
	}

	int MethodDecompiler::EmitTry(int b, uint64_t key, Context const& ctx, StmtList& out) {
		auto tryStart = (uint32_t)(key >> 32), tryEnd = tryStart + (uint32_t)key;
		std::vector<IlExceptionClause const*> clauses;
		for (auto& c : m_Body.Clauses)
			if (c.TryOffset == tryStart && c.TryOffset + c.TryLength == tryEnd)
				clauses.push_back(&c);
		auto inHandlers = [&](uint32_t offset) {
			for (auto c : clauses)
				if ((offset >= c->HandlerOffset && offset < c->HandlerOffset + c->HandlerLength) ||
					((c->Flags & 7) == 1 && offset >= c->FilterOffset && offset < c->HandlerOffset))
					return true;
			return false;
		};
		// where it goes on: where the try and its catches leave to (the most of them)
		std::map<int, int> targets;
		for (int i = 0; i < (int)m_Blocks.size(); i++) {
			auto& block = m_Blocks[i];
			if (block.Removed)
				continue;
			bool inTry = block.Start >= tryStart && block.Start < tryEnd;
			if (!inTry && !inHandlers(block.Start))
				continue;
			for (int s : block.Succ) {
				auto offset = m_Blocks[s].Start;
				if (!(offset >= tryStart && offset < tryEnd) && !inHandlers(offset))
					targets[s]++;
			}
		}
		int after = -1;
		for (auto [t, count] : targets)
			if (after < 0 || count > targets[after] || (count == targets[after] && m_Blocks[t].Start < m_Blocks[after].Start))
				after = t;

		auto s = MakeStmt(S::Try);
		Context body = ctx;
		body.RangeStart = tryStart;
		body.RangeEnd = tryEnd;
		body.Follow = -1;
		body.After = after;
		body.ActiveTries.push_back(key);
		Emit(b, body, s->Body);

		for (auto c : clauses) {
			Handler h;
			int start = BlockAt(c->HandlerOffset);
			Context hctx = ctx;
			hctx.RangeStart = c->HandlerOffset;
			hctx.RangeEnd = c->HandlerOffset + c->HandlerLength;
			hctx.Follow = -1;
			hctx.After = after;
			switch (c->Flags & 7) {
				case 0:
					h.Kind = Handler::Kinds::Catch;
					h.TypeToken = c->ClassToken;
					h.Type = m_Md.CSharpType(c->ClassToken, m_Context).Name;
					break;
				case 1:
				{
					h.Kind = Handler::Kinds::Filter;
					h.Type = L"object";
					int filter = BlockAt(c->FilterOffset);
					// one block that ends with endfilter: its value is the condition
					if (filter >= 0 && m_Blocks[filter].Kind == Exit::EndFilter && m_Blocks[filter].Stmts.size() <= 1) {
						auto& fb = m_Blocks[filter];
						h.Filter = fb.Value;
						m_Emitted[filter] = true;
						// e = ex as T; (e != null && c): catch (T e) when (c)
						if (fb.Stmts.size() == 1 && fb.Stmts[0]->Kind == S::Expr && fb.Stmts[0]->Value->Kind == E::Assign) {
							auto& assign = fb.Stmts[0]->Value;
							auto& value = assign->Args[1];
							auto& f = h.Filter;
							int var = assign->Args[0]->Kind == E::Var ? assign->Args[0]->Var : -1;
							auto tests = [&](ExprPtr const& x) {
								return x->Kind == E::Binary && x->Text == L"!=" && x->Args[0]->Kind == E::Var && x->Args[0]->Var == var && IsNull(x->Args[1]);
							};
							if (var >= 0 && value->Kind == E::As && value->Args[0]->Kind == E::Exception) {
								if (f->Kind == E::Binary && f->Text == L"&&" && tests(f->Args[0])) {
									h.Type = value->Text;
									h.TypeToken = value->Token;
									h.Var = var;
									h.Filter = f->Args[1];
								}
								else if (tests(f)) {
									h.Kind = Handler::Kinds::Catch;
									h.Type = value->Text;
									h.TypeToken = value->Token;
									h.Var = var;
									h.Filter = nullptr;
								}
								if (h.Var >= 0) {
									m_Vars[var].Declared = true;
									if (!m_Names.contains(L"ex") || m_CatchEx) {
										m_Names.insert(L"ex");
										m_CatchEx = true;
										m_Vars[var].Name = L"ex";
									}
								}
							}
							if (h.Var < 0)
								h.Body.push_back(fb.Stmts[0]);
						}
					}
					else
						h.FilterText = std::format(L"/* the filter at IL_{:04x} */", c->FilterOffset);
					break;
				}
				case 2: h.Kind = Handler::Kinds::Finally; break;
				default: h.Kind = Handler::Kinds::Fault; break;
			}
			if (start >= 0)
				Emit(start, hctx, h.Body);
			// the exception: a variable of the catch, if the handler keeps it (stloc), or none (pop)
			if ((h.Kind == Handler::Kinds::Catch || h.Kind == Handler::Kinds::Filter) && h.Var < 0) {
				size_t first = 0;
				while (first < h.Body.size() && h.Body[first]->Kind == S::Label)
					first++;
				if (first < h.Body.size()) {
					auto& st = h.Body[first];
					if (st->Kind == S::Expr && st->Value->Kind == E::Assign && st->Value->Args[1]->Kind == E::Exception &&
						st->Value->Args[0]->Kind == E::Var) {
						h.Var = st->Value->Args[0]->Var;
						m_Vars[h.Var].Declared = true;
						// a local that only holds what is caught is the usual "ex" (each catch has its own)
						if (m_Vars[h.Var].Role == Variable::Roles::Local && (!m_Names.contains(L"ex") || m_CatchEx)) {
							m_Names.insert(L"ex");
							m_CatchEx = true;
							m_Vars[h.Var].Name = L"ex";
						}
						h.Body.erase(h.Body.begin() + first);
						// the filter's own variable is the same one
						if (h.Kind == Handler::Kinds::Filter && first < h.Body.size()) {
							auto& next = h.Body[first];
							if (next->Kind == S::Expr && next->Value->Kind == E::Assign && next->Value->Args[1]->Kind == E::Exception)
								h.Body.erase(h.Body.begin() + first);
						}
					}
				}
				bool usesException = false;
				ForEachStmt(h.Body, [&](Stmt& st) {
					ForEachExprOf(st, [&](ExprPtr& e) { ForEach(e, [&](Expr const& x) { usesException |= x.Kind == E::Exception; }); });
				});
				if (h.Filter)
					ForEach(h.Filter, [&](Expr const& x) { usesException |= x.Kind == E::Exception; });
				if (h.Var < 0 && usesException) {
					h.Var = NewVar(Variable::Roles::Temp, L"ex", { h.Type, K::Object });
					if (m_Vars[h.Var].Name != L"ex" && m_CatchEx)
						m_Vars[h.Var].Name = L"ex";
					m_CatchEx |= m_Vars[h.Var].Name == L"ex";
					m_Vars[h.Var].Declared = true;
					auto var = h.Var;
					std::function<void(ExprPtr&)> replace = [&](ExprPtr& e) {
						if (!e)
							return;
						if (e->Kind == E::Exception)
							e = VarExpr(var);
						for (auto& a : e->Args)
							replace(a);
					};
					ForEachStmt(h.Body, [&](Stmt& st) { ForEachExprOf(st, replace); });
					replace(h.Filter);
				}
			}
			s->Handlers.push_back(std::move(h));
		}
		out.push_back(s);
		return after;
	}

	//
	// After the structure: the C# that the compiler's IL came from
	//

	// how many times each variable is read, and written
	struct Uses {
		std::map<int, int> Reads, Writes;
	};
	void CountUses(StmtList const& list, Uses& uses) {
		ForEachStmt(list, [&](Stmt& s) {
			ForEachExprOf(s, [&](ExprPtr& e) {
				std::function<void(ExprPtr const&, bool)> walk = [&](ExprPtr const& x, bool target) {
					if (!x)
						return;
					if (x->Kind == E::Var) {
						(target ? uses.Writes : uses.Reads)[x->Var]++;
						return;
					}
					if (x->Kind == E::Assign) {
						walk(x->Args[0], x->Text == L"=");
						if (x->Text != L"=")
							walk(x->Args[0], false);
						walk(x->Args[1], false);
						return;
					}
					if (x->Kind == E::AddressOf && x->Args[0]->Kind == E::Var) {
						uses.Reads[x->Args[0]->Var]++;
						uses.Writes[x->Args[0]->Var]++;
						return;
					}
					for (auto& a : x->Args)
						walk(a, false);
				};
				walk(e, false);
			});
			for (auto& h : s.Handlers)
				if (h.Var >= 0)
					uses.Writes[h.Var]++;
		});
	}

	bool MethodDecompiler::Foreach(StmtList& list, size_t i) {
		auto& init = list[i - 1];
		auto& t = list[i];
		if (init->Kind != S::Expr || init->Value->Kind != E::Assign || init->Value->Args[0]->Kind != E::Var)
			return false;
		auto get = init->Value->Args[1];
		int enumerator = init->Value->Args[0]->Var;
		if (get->Kind != E::Call || get->Text != L"GetEnumerator" || !get->HasTarget || get->Args.size() != 1)
			return false;
		if (t->Handlers.size() != 1 || t->Handlers[0].Kind != Handler::Kinds::Finally || t->Body.size() != 1 || t->Body[0]->Kind != S::While)
			return false;
		auto isEnumerator = [&](ExprPtr const& e) {
			auto x = e && e->Kind == E::AddressOf ? e->Args[0] : e;
			return x && x->Kind == E::Var && x->Var == enumerator;
		};
		auto calls = [&](ExprPtr const& e, PCWSTR name) {
			return e && e->Kind == E::Call && e->Text == name && e->HasTarget && e->Args.size() == 1 && isEnumerator(e->Args[0]);
		};
		// the finally only disposes of it (if it is not null)
		auto& fin = t->Handlers[0].Body;
		bool disposes = (fin.size() == 1 && fin[0]->Kind == S::Expr && calls(fin[0]->Value, L"Dispose")) ||
			(fin.size() == 1 && fin[0]->Kind == S::If && fin[0]->Else.empty() && fin[0]->Body.size() == 1 &&
				fin[0]->Body[0]->Kind == S::Expr && calls(fin[0]->Body[0]->Value, L"Dispose"));
		if (!disposes)
			return false;
		auto loop = t->Body[0];
		if (!calls(loop->Value, L"MoveNext") || loop->Body.empty())
			return false;
		auto& first = loop->Body[0];
		if (first->Kind != S::Expr || first->Value->Kind != E::Assign || first->Value->Args[0]->Kind != E::Var)
			return false;
		auto current = first->Value->Args[1];
		if (current->Kind != E::Member || current->Text != L"Current" || !current->HasTarget || !isEnumerator(current->Args[0]))
			return false;
		// the variable is the loop's: it is not used elsewhere, and the enumerator only by the loop
		int var = first->Value->Args[0]->Var;
		Uses all, inside;
		CountUses(m_Result, all);
		CountUses(loop->Body, inside);
		if (all.Reads[var] != inside.Reads[var] || all.Writes[var] != inside.Writes[var])
			return false;
		auto f = MakeStmt(S::Foreach, get->Args[0]);
		f->Init = first->Value->Args[0];
		f->Body.assign(loop->Body.begin() + 1, loop->Body.end());
		m_Vars[var].Declared = true;
		m_Vars[enumerator].Declared = true;
		list[i - 1] = f;
		list.erase(list.begin() + i);
		return true;
	}

	namespace {
		// v = x;
		int Assigned(StmtPtr const& s, ExprPtr* value = nullptr) {
			if (s->Kind != S::Expr || s->Value->Kind != E::Assign || s->Value->Text != L"=" || s->Value->Args[0]->Kind != E::Var)
				return -1;
			if (value)
				*value = s->Value->Args[1];
			return s->Value->Args[0]->Var;
		}
		bool IsVar(ExprPtr const& e, int var) {
			auto x = e && e->Kind == E::AddressOf ? e->Args[0] : e;
			return x && x->Kind == E::Var && x->Var == var;
		}
		// a call of a method of a name, on a variable (or with it as its first argument, for a static method)
		bool CallOn(ExprPtr const& e, PCWSTR name, int var) {
			if (!e || e->Kind != E::Call || e->Text != name || e->Args.empty())
				return false;
			if (IsVar(e->Args[0], var))
				return true;
			// ((IDisposable)v).Dispose()
			return e->Args[0]->Kind == E::Cast && IsVar(e->Args[0]->Args[0], var);
		}
		// the one statement of a finally, or of an if that is all of it
		StmtPtr FinallyCall(Stmt const& t, ExprPtr* condition) {
			if (t.Handlers.size() != 1 || t.Handlers[0].Kind != Handler::Kinds::Finally || t.Handlers[0].Body.size() != 1)
				return nullptr;
			auto& only = t.Handlers[0].Body[0];
			if (only->Kind == S::If && only->Else.empty() && only->Body.size() == 1) {
				*condition = only->Value;
				return only->Body[0];
			}
			*condition = nullptr;
			return only;
		}
	}

	// lock (x): f = false; then o = x before the try (C# 6 and later) or as its first statement (C# 5: Monitor.Enter(o = x, ref f))
	bool MethodDecompiler::Lock(StmtList& list, size_t i) {
		auto& t = *list[i];
		ExprPtr object, initial, condition;
		int flag = Assigned(list[i - 1], &initial);
		if (flag < 0 || !IsLiteral(initial, 0) || t.Body.empty())
			return false;
		bool inside = i < 2 || Assigned(list[i - 2]) < 0;
		int obj = inside ? Assigned(t.Body[0], &object) : Assigned(list[i - 2], &object);
		size_t enterAt = inside ? 1 : 0;
		if (obj < 0 || t.Body.size() <= enterAt)
			return false;
		auto exit = FinallyCall(t, &condition);
		if (!exit || !condition || !IsVar(condition, flag) || exit->Kind != S::Expr || !CallOn(exit->Value, L"Exit", obj))
			return false;
		auto& enter = t.Body[enterAt]->Value;
		if (t.Body[enterAt]->Kind != S::Expr || !enter || enter->Kind != E::Call || enter->Text != L"Enter" || enter->Args.size() != 2 ||
			!(IsVar(enter->Args[0], obj) || Same(enter->Args[0], object)) || !IsVar(enter->Args[1], flag))
			return false;
		// the variables are the lock's own
		Uses uses;
		CountUses(m_Result, uses);
		if (uses.Reads[obj] > 2 || uses.Writes[obj] != 1 || uses.Writes[flag] != 2)
			return false;
		auto lock = MakeStmt(S::Lock, object);
		lock->Body.assign(t.Body.begin() + enterAt + 1, t.Body.end());
		if (inside) {
			list[i - 1] = lock;
			list.erase(list.begin() + i);
		}
		else {
			list[i - 2] = lock;
			list.erase(list.begin() + i - 1, list.begin() + i + 1);
		}
		return true;
	}

	bool MethodDecompiler::Using(StmtList& list, size_t i) {
		auto& t = *list[i];
		ExprPtr value, condition;
		int var = Assigned(list[i - 1], &value);
		if (var < 0 || m_Vars[var].Role == Variable::Roles::Param || m_Vars[var].Role == Variable::Roles::This)
			return false;
		auto dispose = FinallyCall(t, &condition);
		if (!dispose || dispose->Kind != S::Expr || !CallOn(dispose->Value, L"Dispose", var))
			return false;
		if (condition && !(condition->Kind == E::Binary && condition->Text == L"!=" && IsVar(condition->Args[0], var) && IsNull(condition->Args[1])))
			return false;
		// it is not set again in the using
		Uses inside;
		CountUses(t.Body, inside);
		if (inside.Writes[var] != 0)
			return false;
		auto u = MakeStmt(S::Using);
		u->Init = list[i - 1]->Value;
		u->Body = t.Body;
		m_Vars[var].Declared = true;
		list[i - 1] = u;
		list.erase(list.begin() + i);
		return true;
	}

	// !(a < b) is a >= b, !!x is x
	void Normalize(ExprPtr& e) {
		if (!e)
			return;
		for (auto& a : e->Args)
			Normalize(a);
		if (e->Kind == E::Unary && e->Text == L"!") {
			auto inner = e->Args[0];
			if ((inner->Kind == E::Unary && inner->Text == L"!") || (inner->Kind == E::Binary && (Opposite(inner->Text) || inner->Text == L"&&" || inner->Text == L"||")) ||
				(inner->Kind == E::Literal && inner->Type.Kind == K::Bool)) {
				auto n = Not(inner);
				if (!(n->Kind == E::Unary && n->Text == L"!" && n->Args[0] == inner))
					e = n;
			}
		}
	}

	void MethodDecompiler::Clean(StmtList& list, bool top) {
		for (auto& s : list)
			ForEachExprOf(*s, [](ExprPtr& e) { Normalize(e); });
		for (size_t i = 0; i < list.size(); i++) {
			auto& s = list[i];
			Clean(s->Body, false);
			Clean(s->Else, false);
			for (auto& c : s->Cases)
				Clean(c.Body, false);
			for (auto& h : s->Handlers)
				Clean(h.Body, false);
		}
		for (bool changed = true; changed; ) {
			changed = false;
			for (size_t i = 0; i < list.size(); i++) {
				auto s = list[i];
				// goto the next statement
				if (s->Kind == S::Goto && i + 1 < list.size() && list[i + 1]->Kind == S::Label && list[i + 1]->Target == s->Target) {
					list.erase(list.begin() + i);
					changed = true;
					break;
				}
				if (s->Kind == S::If) {
					auto isJump = [](StmtPtr const& x) {
						return x->Kind == S::Break || x->Kind == S::Continue || x->Kind == S::Goto || x->Kind == S::Return || x->Kind == S::Throw;
					};
					// if (c) { } else { x } is if (!c) { x }
					if (s->Body.empty() && !s->Else.empty()) {
						s->Value = Not(s->Value);
						std::swap(s->Body, s->Else);
						changed = true;
					}
					// if (c) { x } else { break; } is if (!c) { break; } x
					if (s->Else.size() == 1 && isJump(s->Else[0]) && !(s->Body.size() == 1 && isJump(s->Body[0]))) {
						s->Value = Not(s->Value);
						std::swap(s->Body, s->Else);
						list.insert(list.begin() + i + 1, s->Else.begin(), s->Else.end());
						s->Else.clear();
						changed = true;
						break;
					}
					// if (c) { x; return; } else { y } is if (c) { x; return; } y
					if (!s->Else.empty() && EndsWithJump(s->Body)) {
						list.insert(list.begin() + i + 1, s->Else.begin(), s->Else.end());
						s->Else.clear();
						changed = true;
						break;
					}
					// if (c) { x } else { y; return; } is if (!c) { y; return; } x
					if (!s->Else.empty() && EndsWithJump(s->Else) && !EndsWithJump(s->Body)) {
						s->Value = Not(s->Value);
						std::swap(s->Body, s->Else);
						list.insert(list.begin() + i + 1, s->Else.begin(), s->Else.end());
						s->Else.clear();
						changed = true;
						break;
					}
					if (s->Body.empty() && s->Else.empty() && !HasSideEffects(s->Value)) {
						list.erase(list.begin() + i);
						changed = true;
						break;
					}
					auto elseIf = [](StmtList const& l) { return l.size() == 1 && l[0]->Kind == S::If; };
					// if (c) { if (d) ... } else { x } reads better as if (!c) { x } else if (d) ...
					if (!s->Else.empty() && elseIf(s->Body) && !elseIf(s->Else)) {
						s->Value = Not(s->Value);
						std::swap(s->Body, s->Else);
						changed = true;
					}
					// if (x != y) { a } else { b } is if (x == y) { b } else { a }
					else if (!s->Else.empty() && !elseIf(s->Else) && s->Value->Kind == E::Binary && s->Value->Text == L"!=") {
						s->Value = Not(s->Value);
						std::swap(s->Body, s->Else);
						changed = true;
					}
				}
				// try { try { } catch { } } finally { } is one try (as C# writes it)
				if (s->Kind == S::Try && s->Handlers.size() == 1 && s->Handlers[0].Kind == Handler::Kinds::Finally &&
					s->Body.size() == 1 && s->Body[0]->Kind == S::Try) {
					auto inner = s->Body[0];
					bool finally = false;
					for (auto& h : inner->Handlers)
						finally |= h.Kind == Handler::Kinds::Finally || h.Kind == Handler::Kinds::Fault;
					if (!finally) {
						auto handlers = inner->Handlers;
						handlers.push_back(s->Handlers[0]);
						s->Handlers = std::move(handlers);
						s->Body = inner->Body;
						changed = true;
					}
				}
				// e = x.GetEnumerator(); try { while (e.MoveNext()) { v = e.Current; ... } } finally { e.Dispose(); } is foreach
				if (s->Kind == S::Try && i > 0 && Foreach(list, i)) {
					changed = true;
					break;
				}
				// o = x; f = false; try { Monitor.Enter(o, ref f); ... } finally { if (f) Monitor.Exit(o); } is lock (x)
				if (s->Kind == S::Try && i > 0 && Lock(list, i)) {
					changed = true;
					break;
				}
				// v = x; try { ... } finally { if (v != null) v.Dispose(); } is using (v = x)
				if (s->Kind == S::Try && i > 0 && Using(list, i)) {
					changed = true;
					break;
				}
				if (s->Kind == S::While) {
					auto& body = s->Body;
					// while (true) { if (c) break; ... } is while (!c) { ... }
					while (!body.empty() && body.back()->Kind == S::Continue)
						body.pop_back(), changed = true;
					size_t first = 0;
					while (first < body.size() && body[first]->Kind == S::Label)
						first++;
					if (IsLiteral(s->Value, 1) && first < body.size() && body[first]->Kind == S::If && body[first]->Else.empty() &&
						body[first]->Body.size() == 1 && body[first]->Body[0]->Kind == S::Break) {
						s->Value = Not(body[first]->Value);
						body.erase(body.begin() + first);
						changed = true;
					}
					// while (true) { ...; if (c) continue; break; }, while (true) { ...; if (!c) break; } are do { ... } while (c)
					else if (IsLiteral(s->Value, 1) && body.size() >= 2 && body.back()->Kind == S::Break &&
						body[body.size() - 2]->Kind == S::If && body[body.size() - 2]->Else.empty() &&
						body[body.size() - 2]->Body.size() == 1 && body[body.size() - 2]->Body[0]->Kind == S::Continue) {
						s->Kind = S::DoWhile;
						s->Value = body[body.size() - 2]->Value;
						body.resize(body.size() - 2);
						changed = true;
					}
					else if (IsLiteral(s->Value, 1) && !body.empty() && body.back()->Kind == S::If && body.back()->Else.empty() &&
						body.back()->Body.size() == 1 && body.back()->Body[0]->Kind == S::Break) {
						s->Kind = S::DoWhile;
						s->Value = Not(body.back()->Value);
						body.pop_back();
						changed = true;
					}
				}
				if (s->Kind == S::DoWhile)
					while (!s->Body.empty() && s->Body.back()->Kind == S::Continue)
						s->Body.pop_back(), changed = true;
				// for (...) { if (c) continue; x } is for (...) { if (!c) { x } }
				if (s->Kind == S::While || s->Kind == S::For || s->Kind == S::Foreach) {
					auto& body = s->Body;
					for (size_t k = 0; k + 1 < body.size(); k++) {
						auto& x = body[k];
						if (x->Kind == S::If && x->Else.empty() && x->Body.size() == 1 && x->Body[0]->Kind == S::Continue &&
							!EndsWithJump(body) && std::none_of(body.begin() + k + 1, body.end(), [](StmtPtr const& y) { return y->Kind == S::Label; })) {
							x->Value = Not(x->Value);
							x->Body.assign(body.begin() + k + 1, body.end());
							body.resize(k + 1);
							changed = true;
							break;
						}
					}
				}
				// if (c) { return false; } return x; is return !c && x;
				if (s->Kind == S::If && s->Else.empty() && s->Body.size() == 1 && s->Body[0]->Kind == S::Return && i + 1 < list.size() &&
					list[i + 1]->Kind == S::Return && s->Body[0]->Value && list[i + 1]->Value) {
					auto& first = s->Body[0]->Value;
					auto& second = list[i + 1]->Value;
					if (first->Kind == E::Literal && first->Type.Kind == K::Bool && second->Type.Kind == K::Bool) {
						ClrTypeInfo boolean{ L"bool", K::Bool };
						auto value = first->Int ? Make(E::Binary, L"||", { s->Value, second }, boolean) :
							Make(E::Binary, L"&&", { Not(s->Value), second }, boolean);
						list[i + 1]->Value = value;
						list.erase(list.begin() + i);
						changed = true;
						break;
					}
				}
			}
		}
		if (top) {
			while (!list.empty() && list.back()->Kind == S::Return && !list.back()->Value)
				list.pop_back();
		}
	}

	// a local that is read once, right after it is set: the value goes where it is read (as debug builds write
	// "bool flag = x > 5; if (flag)")
	void MethodDecompiler::InlineLocals(StmtList& list) {
		// the places where a variable is set and, right after, read: (the list, the statement that sets it, where it is read)
		struct Site {
			StmtList* List;
			size_t Index;
			ExprPtr* Use;
		};
		std::map<int, std::vector<Site>> sites;
		auto readsOnce = [](ExprPtr const& e, int var) {
			int n = 0;
			ForEach(e, [&](Expr const& x) { n += x.Kind == E::Var && x.Var == var; });
			return n == 1;
		};
		auto assigned = [](StmtPtr const& s) {
			return s->Kind == S::Expr && s->Value->Kind == E::Assign && s->Value->Text == L"=" && s->Value->Args[0]->Kind == E::Var ?
				s->Value->Args[0]->Var : -1;
		};
		std::function<void(StmtList&)> visit = [&](StmtList& stmts) {
			for (auto& s : stmts) {
				visit(s->Body);
				visit(s->Else);
				for (auto& c : s->Cases)
					visit(c.Body);
				for (auto& h : s->Handlers)
					visit(h.Body);
				// do { ...; flag = c; } while (flag)
				if (s->Kind == S::DoWhile && !s->Body.empty())
					if (int var = assigned(s->Body.back()); var >= 0 && readsOnce(s->Value, var))
						sites[var].push_back({ &s->Body, s->Body.size() - 1, &s->Value });
			}
			for (size_t i = 0; i + 1 < stmts.size(); i++) {
				int var = assigned(stmts[i]);
				if (var < 0)
					continue;
				auto& next = stmts[i + 1];
				// what is evaluated once, first: not a loop's condition
				switch (next->Kind) {
					case S::Expr: case S::Return: case S::Throw: case S::If: case S::Switch:
						if (next->Value && readsOnce(next->Value, var))
							sites[var].push_back({ &stmts, i, &next->Value });
						break;
				}
			}
		};
		visit(list);
		Uses uses;
		CountUses(m_Result, uses);
		// the statements to remove, by list (from the last, so that the indexes stay right)
		std::map<StmtList*, std::vector<size_t>> removed;
		for (auto& [var, list] : sites) {
			auto role = m_Vars[var].Role;
			if (role == Variable::Roles::Param || role == Variable::Roles::This)
				continue;
			// every value it is given is read once, right after: it is not needed
			if (uses.Writes[var] != (int)list.size() || uses.Reads[var] != (int)list.size())
				continue;
			bool ok = true;
			for (auto& site : list) {
				auto value = (*site.List)[site.Index]->Value->Args[1];
				// the value moves over what is evaluated before it there: only when that cannot matter
				if (HasSideEffects(value) || ReadsMemory(value)) {
					bool other = false;
					ForEach(*site.Use, [&](Expr const& x) {
						other |= x.Kind == E::Call || x.Kind == E::New || x.Kind == E::Assign;
					});
					if (other && !((*site.Use)->Kind == E::Call || (*site.Use)->Kind == E::New))
						ok = false;
				}
			}
			if (!ok)
				continue;
			for (auto& site : list) {
				int one = 1;
				*site.Use = Substitute(*site.Use, var, (*site.List)[site.Index]->Value->Args[1], one);
				removed[site.List].push_back(site.Index);
			}
		}
		for (auto& [stmts, indexes] : removed) {
			std::sort(indexes.rbegin(), indexes.rend());
			for (auto i : indexes)
				stmts->erase(stmts->begin() + i);
		}
	}

	// x = x + 1 is x++, x = x * 2 is x *= 2
	void MethodDecompiler::Compound(StmtList& list) {
		ForEachStmt(list, [&](Stmt& s) {
			auto compound = [&](ExprPtr& e) {
				if (!e || e->Kind != E::Assign || e->Text != L"=")
					return;
				auto& target = e->Args[0];
				auto& value = e->Args[1];
				if (value->Kind != E::Binary || !Same(target, value->Args[0]))
					return;
				auto& op = value->Text;
				static const std::set<std::wstring> ops = { L"+", L"-", L"*", L"/", L"%", L"&", L"|", L"^", L"<<", L">>" };
				if (!ops.contains(op))
					return;
				// a cast of the result (x = (short)(x + 1)) is not here: x += 1 converts as well
				if ((op == L"+" || op == L"-") && IsLiteral(value->Args[1], 1) && target->Type.Kind != K::Float) {
					auto u = Make(E::Unary, op == L"+" ? L"++" : L"--", { target }, target->Type);
					u->Postfix = true;
					e = u;
					return;
				}
				e = Make(E::Assign, op + L"=", { target, value->Args[1] }, target->Type);
			};
			if (s.Kind == S::Expr)
				compound(s.Value);
			if (s.Kind == S::For)
				compound(s.Step);
		});
	}

	// i = 0; while (i < n) { ...; i++; } is for (i = 0; i < n; i++) { ... }
	void MethodDecompiler::ForLoops(StmtList& list) {
		for (size_t i = 0; i < list.size(); i++) {
			auto& s = list[i];
			ForLoops(s->Body);
			ForLoops(s->Else);
			for (auto& c : s->Cases)
				ForLoops(c.Body);
			for (auto& h : s->Handlers)
				ForLoops(h.Body);
			if (s->Kind != S::While || i == 0 || s->Body.size() < 2)
				continue;
			auto& init = list[i - 1];
			auto& step = s->Body.back();
			if (init->Kind != S::Expr || init->Value->Kind != E::Assign || init->Value->Args[0]->Kind != E::Var || step->Kind != S::Expr)
				continue;
			int var = init->Value->Args[0]->Var;
			auto& sv = step->Value;
			bool steps = (sv->Kind == E::Unary && sv->Postfix && sv->Args[0]->Kind == E::Var && sv->Args[0]->Var == var) ||
				(sv->Kind == E::Assign && sv->Args[0]->Kind == E::Var && sv->Args[0]->Var == var);
			if (!steps || !ReadsVar(s->Value, var))
				continue;
			// a continue would skip the step
			bool continues = false;
			std::function<void(StmtList const&)> find = [&](StmtList const& stmts) {
				for (auto& x : stmts) {
					continues |= x->Kind == S::Continue;
					if (x->Kind != S::While && x->Kind != S::DoWhile && x->Kind != S::For) {
						find(x->Body);
						find(x->Else);
						for (auto& c : x->Cases)
							find(c.Body);
						for (auto& h : x->Handlers)
							find(h.Body);
					}
				}
			};
			find(s->Body);
			if (continues)
				continue;
			s->Kind = S::For;
			s->Init = init->Value;
			s->Step = sv;
			s->Body.pop_back();
			list.erase(list.begin() + i - 1);
			i--;
		}
	}

	// A variable is declared where it is first set, if that is in the method's own list of statements (not in a block),
	// before it is used; the others are declared at the start
	void MethodDecompiler::Declarations() {
		std::set<int> seen;
		std::function<void(ExprPtr const&)> see = [&](ExprPtr const& e) {
			ForEach(e, [&](Expr const& x) {
				if (x.Kind == E::Var)
					seen.insert(x.Var);
			});
		};
		std::function<void(StmtList const&)> seeAll = [&](StmtList const& list) {
			ForEachStmt(list, [&](Stmt& s) {
				ForEachExprOf(s, [&](ExprPtr& e) { see(e); });
				for (auto& h : s.Handlers)
					if (h.Var >= 0)
						seen.insert(h.Var);
			});
		};
		for (auto& s : m_Result) {
			ExprPtr assign;
			if (s->Kind == S::Expr && s->Value->Kind == E::Assign && s->Value->Text == L"=")
				assign = s->Value;
			if (s->Kind == S::For && s->Init && s->Init->Kind == E::Assign && s->Init->Text == L"=")
				assign = s->Init;
			if (assign && assign->Args[0]->Kind == E::Var) {
				int var = assign->Args[0]->Var;
				auto& v = m_Vars[var];
				see(assign->Args[1]);
				if (!seen.contains(var) && !v.Declared && v.Role != Variable::Roles::Param && v.Role != Variable::Roles::This) {
					s->Declares = true;
					v.Declared = true;
				}
				seen.insert(var);
				if (s->Kind == S::For) {
					see(s->Value);
					see(s->Step);
					seeAll(s->Body);
				}
				continue;
			}
			ForEachExprOf(*s, [&](ExprPtr& e) { see(e); });
			seeAll(s->Body);
			seeAll(s->Else);
			for (auto& c : s->Cases)
				seeAll(c.Body);
			for (auto& h : s->Handlers)
				seeAll(h.Body);
		}
	}

	bool MethodDecompiler::Run(std::wstring& error) {
		if (m_Def.Rva == 0 || (m_Def.ImplFlags & 3) != 0)
			return false;
		if (!ReadIlMethodBody(m_PE, m_Def.Rva, m_Body)) {
			error = std::format(L"the body at RVA 0x{:X} cannot be read", m_Def.Rva);
			return false;
		}
		SetUp();
		if (!BuildBlocks()) {
			error = L"the code is not IL";
			return false;
		}
		// the stack: from the start, and from each handler (whose stack has the exception)
		std::vector<int> work;
		auto root = [&](uint32_t offset, int depth) {
			int b = BlockAt(offset);
			if (b >= 0 && m_Blocks[b].Depth < 0) {
				m_Blocks[b].Depth = depth;
				m_Blocks[b].Root = true;
				work.push_back(b);
			}
		};
		root(0, 0);
		for (auto& c : m_Body.Clauses) {
			auto kind = c.Flags & 7;
			root(c.HandlerOffset, kind == 0 || kind == 1 ? 1 : 0);
			if (kind == 1)
				root(c.FilterOffset, 1);
		}
		std::vector<bool> done(m_Blocks.size());
		// in the order of the code, so that a block's stack is known from the blocks before it
		for (bool more = true; more; ) {
			more = false;
			for (int b = 0; b < (int)m_Blocks.size(); b++) {
				if (done[b] || m_Blocks[b].Depth < 0)
					continue;
				Simulate(b);
				done[b] = true;
				more = true;
			}
		}
		for (int b = 0; b < (int)m_Blocks.size(); b++)
			if (!done[b])
				m_Blocks[b].Removed = true;	// not reached

		Simplify();
		Dominators();
		FindLoops();
		m_Emitted.assign(m_Blocks.size(), false);
		Context top;
		Emit(0, top, m_Result);
		// what was not written (code that is only reached through what could not be structured)
		for (int b : m_Order)
			if (!m_Emitted[b] && !m_Blocks[b].Removed && m_Blocks[b].Pred.size() + (m_Blocks[b].Root ? 1 : 0) > 0) {
				bool handler = false;
				for (auto& c : m_Body.Clauses)
					handler |= m_Blocks[b].Start >= c.HandlerOffset && m_Blocks[b].Start < c.HandlerOffset + c.HandlerLength;
				if (handler && m_Blocks[b].Root)
					continue;
				Emit(b, top, m_Result);
			}
		// the labels that are gone to
		std::set<int> targets;
		ForEachStmt(m_Result, [&](Stmt& s) {
			if (s.Kind == S::Goto)
				targets.insert(s.Target);
		});
		std::function<void(StmtList&)> labels = [&](StmtList& list) {
			std::erase_if(list, [&](StmtPtr const& s) { return s->Kind == S::Label && !targets.contains(s->Target); });
			for (auto& s : list) {
				labels(s->Body);
				labels(s->Else);
				for (auto& c : s->Cases)
					labels(c.Body);
				for (auto& h : s->Handlers)
					labels(h.Body);
			}
		};
		labels(m_Result);
		Clean(m_Result, true);
		InlineLocals(m_Result);
		Clean(m_Result, true);
		Compound(m_Result);
		ForLoops(m_Result);
		Clean(m_Result, true);
		Declarations();
		if (!m_Complete)
			error = L"the code ends with bytes that are not IL";
		return true;
	}

	//
	// Writing C#
	//
	class Writer {
	public:
		Writer(ClrMetadata const& md) : m_Md(md) {}

		void Line(std::wstring const& text, uint32_t token = 0) {
			m_Lines.push_back(std::wstring(m_Indent, L'\t') + text);
			m_Tokens.push_back(token);
		}
		void Open() { Line(L"{"); m_Indent++; }
		void Close(std::wstring const& after = {}) { m_Indent--; Line(L"}" + after); }
		void Blank() {
			if (!m_Lines.empty() && !m_Lines.back().empty() && m_Lines.back().back() != L'{')
				Line(L"");
		}
		int& Indent() { return m_Indent; }

		std::wstring Text(std::vector<uint32_t>* tokens) {
			std::wstring text;
			for (auto& line : m_Lines)
				text += line + L"\r\n";
			if (tokens)
				*tokens = m_Tokens;
			return text;
		}

		// the token of a line: the first one that has a definition in this file
		uint32_t Pick(std::vector<uint32_t> const& tokens) const {
			for (auto t : tokens)
				if (t && m_Md.Definition(t))
					return t;
			return 0;
		}

	private:
		ClrMetadata const& m_Md;
		std::vector<std::wstring> m_Lines;
		std::vector<uint32_t> m_Tokens;
		int m_Indent{ 0 };
	};

	// how tightly an expression binds (C#'s precedence, higher is tighter)
	int Precedence(Expr const& e) {
		switch (e.Kind) {
			case E::Assign: return 1;
			case E::Conditional: return 2;
			case E::Binary:
			{
				static const std::pair<PCWSTR, int> ops[] = {
					{ L"??", 3 }, { L"||", 4 }, { L"&&", 5 }, { L"|", 6 }, { L"^", 7 }, { L"&", 8 }, { L"==", 9 }, { L"!=", 9 },
					{ L"<", 10 }, { L">", 10 }, { L"<=", 10 }, { L">=", 10 }, { L"<<", 11 }, { L">>", 11 }, { L"+", 12 }, { L"-", 12 },
					{ L"*", 13 }, { L"/", 13 }, { L"%", 13 },
				};
				for (auto& [op, p] : ops)
					if (e.Text == op)
						return p;
				return 12;
			}
			case E::As: case E::Is: return 10;
			case E::Unary: return e.Postfix ? 16 : 14;
			case E::Cast: case E::Deref: case E::AddressOf: return 14;
			case E::Literal: return !e.Text.empty() && e.Text[0] == L'-' ? 14 : 17;
		}
		return 17;
	}

	class CSharpPrinter {
	public:
		CSharpPrinter(ClrMetadata const& md, Writer& w, uint32_t type) : m_Md(md), m_W(w), m_Type(type) {
			if (type) {
				// the names of the type and those it is in: their static members need no type before them
				for (auto t = type; t && m_Own.size() < 32; t = md.Types()[t - 1].Enclosing)
					m_Own.insert(md.CSharpTypeDefName(t));
			}
		}

		void SetVariables(std::vector<Variable> const* vars) { m_Vars = vars; }

		std::wstring Expression(ExprPtr const& e, int min = 0);
		void Statements(StmtList const& list);
		void Statement(Stmt const& s);
		std::vector<uint32_t>& Tokens() { return m_Tokens; }

	private:
		std::wstring Name(int var) const {
			return m_Vars && var >= 0 && var < (int)m_Vars->size() ? (*m_Vars)[var].Name : std::format(L"v{}", var);
		}
		std::wstring Owner(std::wstring const& owner) const {
			// Box<T>.Count in Box<T> is Count
			return owner.empty() || m_Own.contains(owner) || m_Own.contains(owner.substr(0, owner.find(L'<'))) ? L"" : TypeText(owner) + L".";
		}
	public:
		// a type's name, without the types it is nested in when the code is in them
		std::wstring TypeText(std::wstring const& name) const {
			size_t best = 0;
			for (auto& own : m_Own)
				if (own.size() > best && name.size() > own.size() + 1 && name.starts_with(own) && name[own.size()] == L'.')
					best = own.size() + 1;
			return name.substr(best);
		}
	private:
		std::wstring Target(ExprPtr const& e) {
			auto t = e;
			if (t->Kind == E::AddressOf)
				t = t->Args[0];
			// this.x is x
			if (t->Kind == E::Var && m_Vars && t->Var >= 0 && (*m_Vars)[t->Var].Role == Variable::Roles::This)
				return L"";
			return Expression(t, 17) + L".";
		}
		std::wstring Arguments(std::vector<ExprPtr> const& args, size_t first, uint32_t method = 0) {
			std::wstring text;
			// a method of this file says which of its parameters are out
			uint32_t definition = method ? m_Md.Definition(method) : 0;
			if (TableOf(definition) != ClrTableId::MethodDef)
				definition = 0;
			for (size_t i = first; i < args.size(); i++) {
				if (i > first)
					text += L", ";
				auto& a = args[i];
				if (a->Kind == E::AddressOf && a->Args[0]->Kind != E::Index) {
					bool out = definition && (m_Md.ParamFlags(RowOf(definition), (uint32_t)(i - first + 1)) & 3) == 2;
					text += (out ? L"out " : L"ref ") + Expression(a->Args[0], 1);
				}
				else
					text += Expression(a, 1);
			}
			return text;
		}
		void Line(std::wstring const& text) {
			m_W.Line(text, m_W.Pick(m_Tokens));
			m_Tokens.clear();
		}
		std::wstring Declaration(Stmt const& s, ExprPtr const& assign);
		void Body(StmtList const& list) {
			m_W.Open();
			Statements(list);
			m_W.Close();
		}

		ClrMetadata const& m_Md;
		Writer& m_W;
		uint32_t m_Type;
		std::set<std::wstring> m_Own;
		std::vector<Variable> const* m_Vars{ nullptr };
		std::vector<uint32_t> m_Tokens;
	};

	std::wstring CSharpPrinter::Expression(ExprPtr const& e, int min) {
		if (!e)
			return L"";
		if (e->Token)
			m_Tokens.push_back(e->Token);
		std::wstring text;
		int p = Precedence(*e);
		auto& a = e->Args;
		switch (e->Kind) {
			case E::Literal: text = e->Text; break;
			case E::Var: text = Name(e->Var); break;
			case E::Base: text = L"base"; break;
			case E::Exception: text = L"ex"; break;
			case E::Binary:
				text = Expression(a[0], p) + L" " + e->Text + L" " + Expression(a[1], p + 1);
				break;
			case E::Unary:
				if (e->Postfix)
					text = Expression(a[0], p) + e->Text;
				else {
					auto operand = Expression(a[0], p);
					text = e->Text + (!operand.empty() && (operand[0] == e->Text[0]) ? L" " : L"") + operand;
				}
				break;
			case E::Cast: text = L"(" + TypeText(e->Text) + L")" + Expression(a[0], p); break;
			case E::As: text = Expression(a[0], p) + L" as " + TypeText(e->Text); break;
			case E::Is: text = Expression(a[0], p) + L" is " + TypeText(e->Text); break;
			case E::Checked: text = L"checked(" + Expression(a[0], 0) + L")"; break;
			case E::Call:
			{
				std::wstring generic;
				if (!e->GenericArgs.empty()) {
					generic = L"<";
					for (size_t i = 0; i < e->GenericArgs.size(); i++)
						generic += (i ? L", " : L"") + e->GenericArgs[i];
					generic += L">";
				}
				if (e->Text.empty() && e->HasTarget) {	// a delegate, called
					text = Expression(a[0], 17) + L"(" + Arguments(a, 1) + L")";
					break;
				}
				auto prefix = e->HasTarget ? Target(a[0]) : Owner(e->Owner);
				// a constructor's call of another one
				bool chain = !e->HasTarget && e->Owner.empty() && (e->Text == L"base" || e->Text == L"this");
				text = prefix + (chain ? e->Text : Identifier(e->Text)) + generic + L"(" + Arguments(a, e->HasTarget ? 1 : 0, e->Token) + L")";
				break;
			}
			case E::New: text = L"new " + TypeText(e->Text) + L"(" + Arguments(a, 0) + L")"; break;
			case E::Member:
				text = (e->HasTarget ? Target(a[0]) : Owner(e->Owner)) + Identifier(e->Text);
				break;
			case E::MethodGroup:
				text = (e->HasTarget ? Target(a[0]) : Owner(e->Owner)) + Identifier(e->Text);
				break;
			case E::Index:
			{
				auto target = a[0]->Kind == E::AddressOf ? a[0]->Args[0] : a[0];
				text = Expression(target, 17) + L"[" + Arguments(a, 1) + L"]";
				break;
			}
			case E::NewArray:
			{
				// new int[5], new int[5][] (an array of arrays)
				auto name = e->Text;
				auto brackets = name.find(L'[');
				text = L"new " + (brackets == std::wstring::npos ? name : name.substr(0, brackets)) + L"[" + Expression(a[0], 0) + L"]" +
					(brackets == std::wstring::npos ? L"" : name.substr(brackets));
				break;
			}
			case E::AddressOf: text = L"&" + Expression(a[0], p); break;
			case E::Deref: text = L"*" + Expression(a[0], p); break;
			case E::Conditional:
				text = Expression(a[0], 3) + L" ? " + Expression(a[1], 2) + L" : " + Expression(a[2], 2);
				break;
			case E::Assign:
				text = Expression(a[0], 2) + L" " + e->Text + L" " + Expression(a[1], 1);
				break;
			case E::Typeof: text = L"typeof(" + TypeText(e->Text) + L")"; break;
			case E::Default: text = L"default(" + TypeText(e->Text) + L")"; break;
			case E::Raw:
				text = e->Text;
				if (!a.empty())
					text += (e->Text.ends_with(L"byte") ? L"[" : L"(") + Arguments(a, 0) + (e->Text.ends_with(L"byte") ? L"]" : L")");
				break;
		}
		return p < min ? L"(" + text + L")" : text;
	}

	std::wstring CSharpPrinter::Declaration(Stmt const& s, ExprPtr const& assign) {
		auto& var = assign->Args[0];
		auto type = m_Vars ? (*m_Vars)[var->Var].Type : ClrTypeInfo{};
		auto name = type.Name.empty() ? L"var" : TypeText(type.Name);
		if ((*m_Vars)[var->Var].Role == Variable::Roles::Temp || (*m_Vars)[var->Var].Role == Variable::Roles::Stack)
			name = L"var";
		return name + L" " + Expression(assign, 0);
	}

	void CSharpPrinter::Statements(StmtList const& list) {
		for (auto& s : list)
			Statement(*s);
	}

	void CSharpPrinter::Statement(Stmt const& s) {
		switch (s.Kind) {
			case S::Expr:
				Line((s.Declares ? Declaration(s, s.Value) : Expression(s.Value, 0)) + L";");
				break;
			case S::Return:
				Line(s.Value ? L"return " + Expression(s.Value, 0) + L";" : L"return;");
				break;
			case S::Throw:
				Line(s.Value ? L"throw " + Expression(s.Value, 0) + L";" : L"throw;");
				break;
			case S::Break: Line(L"break;"); break;
			case S::Continue: Line(L"continue;"); break;
			case S::Goto: Line(std::format(L"goto IL_{:04x};", s.Target >= 0 ? s.Target : 0)); break;
			case S::Label:
			{
				auto& indent = m_W.Indent();
				indent = std::max(0, indent - 1);
				Line(std::format(L"IL_{:04x}:", s.Target));
				indent++;
				break;
			}
			case S::Comment: Line(L"// " + s.Text); break;
			case S::If:
			{
				Line(L"if (" + Expression(s.Value, 0) + L")");
				Body(s.Body);
				auto* other = &s.Else;
				while (!other->empty()) {
					if (other->size() == 1 && (*other)[0]->Kind == S::If) {
						auto& next = *(*other)[0];
						Line(L"else if (" + Expression(next.Value, 0) + L")");
						Body(next.Body);
						other = &next.Else;
						continue;
					}
					Line(L"else");
					Body(*other);
					break;
				}
				break;
			}
			case S::While:
				Line(L"while (" + Expression(s.Value, 0) + L")");
				Body(s.Body);
				break;
			case S::DoWhile:
				Line(L"do");
				m_W.Open();
				Statements(s.Body);
				m_W.Indent()--;
				Line(L"} while (" + Expression(s.Value, 0) + L");");
				break;
			case S::Lock:
				Line(L"lock (" + Expression(s.Value, 0) + L")");
				Body(s.Body);
				break;
			case S::Using:
				Line(L"using (" + Declaration(s, s.Init) + L")");
				Body(s.Body);
				break;
			case S::Foreach:
			{
				auto var = s.Init->Var;
				auto type = m_Vars && var >= 0 ? TypeText((*m_Vars)[var].Type.Name) : L"var";
				Line(L"foreach (" + (type.empty() ? L"var" : type) + L" " + Name(var) + L" in " + Expression(s.Value, 0) + L")");
				Body(s.Body);
				break;
			}
			case S::For:
			{
				auto init = s.Declares ? Declaration(s, s.Init) : Expression(s.Init, 0);
				Line(L"for (" + init + L"; " + Expression(s.Value, 0) + L"; " + Expression(s.Step, 0) + L")");
				Body(s.Body);
				break;
			}
			case S::Switch:
			{
				Line(L"switch (" + Expression(s.Value, 0) + L")");
				m_W.Open();
				auto type = s.Value->Type;
				for (auto& c : s.Cases) {
					for (auto v : c.Values) {
						std::wstring value;
						if (type.Kind == K::Char && v >= 0 && v <= 0xFFFF)
							value = CSharpChar((wchar_t)v);
						else
							value = std::to_wstring(v);
						Line(L"case " + value + L":");
					}
					if (c.Default)
						Line(L"default:");
					m_W.Indent()++;
					Statements(c.Body);
					m_W.Indent()--;
				}
				m_W.Close();
				break;
			}
			case S::Try:
			{
				Line(L"try");
				Body(s.Body);
				for (auto& h : s.Handlers) {
					std::wstring var = h.Var >= 0 ? L" " + Name(h.Var) : L"";
					switch (h.Kind) {
						case Handler::Kinds::Catch:
							if (h.TypeToken)
								m_Tokens.push_back(h.TypeToken);
							Line(h.Type == L"object" && var.empty() ? L"catch" : L"catch (" + h.Type + var + L")");
							break;
						case Handler::Kinds::Filter:
						{
							auto filter = h.Filter ? Expression(h.Filter, 0) : h.FilterText;
							Line(L"catch (" + h.Type + var + L") when (" + filter + L")");
							break;
						}
						case Handler::Kinds::Finally: Line(L"finally"); break;
						case Handler::Kinds::Fault: Line(L"fault // runs only when an exception leaves the try"); break;
					}
					Body(h.Body);
				}
				break;
			}
		}
	}

	//
	// Declarations
	//
	std::wstring Access(uint32_t flags) {
		static const PCWSTR names[] = { L"", L"private ", L"private protected ", L"internal ", L"protected ", L"protected internal ", L"public ", L"" };
		return names[flags & 7];
	}

	// the modifiers of a method: access, static, abstract, virtual, override, sealed
	std::wstring MethodModifiers(ClrMethodDef const& m, bool inInterface) {
		if (inInterface)
			return (m.Flags & 0x10) ? L"static " : L"";
		std::wstring text = Access(m.Flags);
		if (m.Flags & 0x10)
			text += L"static ";
		if (m.Flags & 0x2000)
			text += L"extern ";
		bool isVirtual = (m.Flags & 0x40) != 0, newSlot = (m.Flags & 0x100) != 0, final = (m.Flags & 0x20) != 0;
		if (m.Flags & 0x400)
			text += newSlot ? L"abstract " : L"abstract override ";
		else if (isVirtual && !newSlot)
			text += final ? L"sealed override " : L"override ";
		else if (isVirtual && newSlot && !final)
			text += L"virtual ";
		return text;
	}

	bool HasAttribute(ClrMetadata const& md, uint32_t token, std::wstring_view name) {
		for (auto& a : md.CustomAttributes(token))
			if (md.CSharpType(a.AttributeType).Name == name)
				return true;
		return false;
	}

	class TypePrinter {
	public:
		TypePrinter(PEFile const* pe, ClrMetadata const& md, Writer& w) : m_PE(pe), m_Md(md), m_W(w) {}

		// a type's name, without the types it is nested in when the declaration is in them
		std::wstring TypeText(std::wstring const& name, uint32_t type) const {
			Writer none(m_Md);
			return CSharpPrinter(m_Md, none, type).TypeText(name);
		}

		void Attributes(uint32_t token, uint32_t type, PCWSTR prefix = L"") {
			// the attributes that are flags in the metadata
			auto row = RowOf(token);
			switch (TableOf(token)) {
				case ClrTableId::TypeDef:
					if (m_Md.Types()[row - 1].Flags & 0x2000)
						m_W.Line(L"[Serializable]");
					break;
				case ClrTableId::Field:
					if (m_Md.Fields()[row - 1].Flags & 0x80)
						m_W.Line(L"[NonSerialized]");
					break;
				case ClrTableId::MethodDef:
					if (auto native = m_Md.PInvoke(row)) {
						std::wstring text = L"[DllImport(" + CSharpQuote(native->Module);
						if (!native->EntryPoint.empty() && native->EntryPoint != m_Md.Methods()[row - 1].Name)
							text += L", EntryPoint = " + CSharpQuote(native->EntryPoint);
						static const PCWSTR charsets[] = { L"", L", CharSet = CharSet.Ansi", L", CharSet = CharSet.Unicode", L", CharSet = CharSet.Auto" };
						text += charsets[(native->Flags >> 1) & 3];
						if (native->Flags & 1)
							text += L", ExactSpelling = true";
						if (native->Flags & 0x40)
							text += L", SetLastError = true";
						static const PCWSTR conventions[] = { L"", L"", L", CallingConvention = CallingConvention.Cdecl",
							L", CallingConvention = CallingConvention.StdCall", L", CallingConvention = CallingConvention.ThisCall",
							L", CallingConvention = CallingConvention.FastCall", L"", L"" };
						text += conventions[(native->Flags >> 8) & 7];
						m_W.Line(text + L")]");
					}
					break;
			}
			for (auto& a : m_Md.CustomAttributes(token)) {
				auto name = m_Md.CSharpType(a.AttributeType).Name;
				// what the compiler writes for what C# says otherwise
				if (name == L"ExtensionAttribute" || name == L"ParamArrayAttribute" || name == L"IsReadOnlyAttribute" ||
					name == L"DefaultMemberAttribute")
					continue;
				m_W.Line(std::wstring(L"[") + prefix + m_Md.AttributeCSharp(a, m_Md.ContextOfType(type)) + L"]", a.Constructor);
			}
		}

		std::wstring Parameters(uint32_t method, ClrMemberInfo const& info, MethodDecompiler* decompiler) {
			std::wstring text;
			bool extension = HasAttribute(m_Md, MethodToken(method), L"ExtensionAttribute");
			for (size_t i = 0; i < info.Signature.Params.size(); i++) {
				auto& p = info.Signature.Params[i];
				if (i)
					text += L", ";
				auto token = m_Md.ParamToken(method, (uint32_t)i + 1);
				auto flags = m_Md.ParamFlags(method, (uint32_t)i + 1);
				if (i == 0 && extension)
					text += L"this ";
				if (token && HasAttribute(m_Md, token, L"ParamArrayAttribute"))
					text += L"params ";
				auto type = TypeText(p.Name, m_Md.Methods()[method - 1].Type);
				if (p.Kind == K::ByRef && (flags & 3) == 2)
					type = L"out " + type.substr(4);
				else if (p.Kind == K::ByRef && (flags & 3) == 1 && token && HasAttribute(m_Md, token, L"IsReadOnlyAttribute"))
					type = L"in " + type.substr(4);
				std::wstring name;
				if (decompiler && i < decompiler->Params().size())
					name = decompiler->Variables()[decompiler->Params()[i]].Name;
				else {
					name = m_Md.ParamName(method, (uint32_t)i + 1);
					name = name.empty() ? std::format(L"arg{}", i) : Identifier(name);
				}
				text += type + L" " + name;
				if (token)
					if (auto value = m_Md.ConstantCSharp(token); !value.empty())
						text += L" = " + value;
			}
			return text;
		}

		// public C() { } (with : base()), the only constructor: C# writes it when there is none
		bool IsDefaultConstructor(uint32_t method) {
			auto& m = m_Md.Methods()[method - 1];
			if (m.Name != L".ctor" || (m.Flags & 7) != 6 || !m_Md.CustomAttributes(MethodToken(method)).empty())
				return false;
			MethodDecompiler decompiler(*m_PE, m_Md, method);
			std::wstring error;
			if (!decompiler.Run(error) || !decompiler.Info().Signature.Params.empty())
				return false;
			auto& body = decompiler.Body();
			return body.empty() || (body.size() == 1 && body[0]->Kind == S::Expr && body[0]->Value->Kind == E::Call &&
				body[0]->Value->Text == L"base" && body[0]->Value->Args.empty());
		}

		std::wstring GenericParams(uint32_t token) {
			auto names = m_Md.GenericParamNames(token);
			// a nested type has those of the types it is in first: C# does not say them again
			if (TableOf(token) == ClrTableId::TypeDef)
				if (auto enclosing = m_Md.Types()[RowOf(token) - 1].Enclosing) {
					auto outer = m_Md.GenericParamNames(TypeToken(enclosing)).size();
					names.erase(names.begin(), names.begin() + std::min(outer, names.size()));
				}
			if (names.empty())
				return {};
			std::wstring text = L"<";
			for (size_t i = 0; i < names.size(); i++)
				text += (i ? L", " : L"") + names[i];
			return text + L">";
		}

		// a method: its attributes, declaration and body; 'name' and 'modifiers' given for accessors
		void Method(uint32_t method, bool accessor = false, std::wstring const& accessorName = {}) {
			auto& m = m_Md.Methods()[method - 1];
			auto type = m.Type;
			bool inInterface = type && (m_Md.Types()[type - 1].Flags & 0x20);
			MethodDecompiler decompiler(*m_PE, m_Md, method);
			std::wstring error;
			bool decompiled = false;
			try {
				decompiled = decompiler.Run(error);
			}
			catch (std::exception const&) {
				error = L"the decompiler failed";
			}
			auto& info = decompiler.Info();
			Attributes(MethodToken(method), type);
			if (auto ret = m_Md.ParamToken(method, 0))
				Attributes(ret, type, L"return: ");

			std::wstring header;
			auto typeName = m_Md.CSharpTypeDefName(type);
			if (auto dot = typeName.rfind(L'.'); dot != std::wstring::npos)
				typeName = typeName.substr(dot + 1);
			if (accessor)
				header = accessorName;
			else if (m.Name == L".ctor")
				header = Access(m.Flags) + typeName + L"(" + Parameters(method, info, &decompiler) + L")";
			else if (m.Name == L".cctor")
				header = L"static " + typeName + L"()";
			else if (m.Name == L"Finalize" && (m.Flags & 0x40) && info.Signature.Params.empty() && !inInterface)
				header = L"~" + typeName + L"()";
			else {
				auto name = Identifier(m.Name);
				auto modifiers = MethodModifiers(m, inInterface);
				bool pointers = info.Signature.Return.Kind == K::Pointer ||
					std::any_of(info.Signature.Params.begin(), info.Signature.Params.end(), [](ClrTypeInfo const& t) { return t.Kind == K::Pointer; });
				if (pointers)
					modifiers += L"unsafe ";
				static const std::pair<PCWSTR, PCWSTR> operators[] = {
					{ L"op_Addition", L"+" }, { L"op_Subtraction", L"-" }, { L"op_Multiply", L"*" }, { L"op_Division", L"/" },
					{ L"op_Modulus", L"%" }, { L"op_BitwiseAnd", L"&" }, { L"op_BitwiseOr", L"|" }, { L"op_ExclusiveOr", L"^" },
					{ L"op_LeftShift", L"<<" }, { L"op_RightShift", L">>" }, { L"op_Equality", L"==" }, { L"op_Inequality", L"!=" },
					{ L"op_LessThan", L"<" }, { L"op_GreaterThan", L">" }, { L"op_LessThanOrEqual", L"<=" },
					{ L"op_GreaterThanOrEqual", L">=" }, { L"op_UnaryNegation", L"-" }, { L"op_UnaryPlus", L"+" },
					{ L"op_LogicalNot", L"!" }, { L"op_OnesComplement", L"~" }, { L"op_Increment", L"++" }, { L"op_Decrement", L"--" },
					{ L"op_True", L"true" }, { L"op_False", L"false" },
				};
				bool special = (m.Flags & 0x800) != 0;
				if (special && m.Name == L"op_Implicit")
					header = modifiers + L"implicit operator " + info.Signature.Return.Name;
				else if (special && m.Name == L"op_Explicit")
					header = modifiers + L"explicit operator " + info.Signature.Return.Name;
				else {
					for (auto& [op, text] : operators)
						if (special && m.Name == op)
							name = std::wstring(L"operator ") + text;
					header = modifiers + TypeText(info.Signature.Return.Name, type) + L" " + name + GenericParams(MethodToken(method));
				}
				header += L"(" + Parameters(method, info, &decompiler) + L")";
			}

			if (!decompiled) {
				// abstract, extern, an interface's, implemented by the runtime or native code
				if (!error.empty()) {
					m_W.Line(header, MethodToken(method));
					m_W.Open();
					m_W.Line(L"// " + error);
					m_W.Close();
				}
				else
					m_W.Line(header + L";", MethodToken(method));
				return;
			}
			auto body = decompiler.Body();
			// a constructor's call of the base type's (or another of its own) goes in its declaration
			if (m.Name == L".ctor" && !body.empty() && body[0]->Kind == S::Expr && body[0]->Value->Kind == E::Call &&
				(body[0]->Value->Text == L"base" || body[0]->Value->Text == L"this") && !body[0]->Value->HasTarget) {
				// : base() is said by saying nothing
				if (body[0]->Value->Text != L"base" || body[0]->Value->Args.size() > 0) {
					CSharpPrinter args(m_Md, m_W, type);
					args.SetVariables(&decompiler.Variables());
					header += L" : " + args.Expression(body[0]->Value);
				}
				body.erase(body.begin());
			}
			m_W.Line(header, MethodToken(method));
			m_W.Open();
			if (!error.empty())
				m_W.Line(L"// " + error);
			CSharpPrinter printer(m_Md, m_W, type);
			printer.SetVariables(&decompiler.Variables());
			// the variables that are not declared where they are first set
			auto& vars = decompiler.Variables();
			std::set<int> used;
			ForEachStmt(body, [&](Stmt& s) {
				ForEachExprOf(s, [&](ExprPtr& e) {
					ForEach(e, [&](Expr const& x) {
						if (x.Kind == E::Var)
							used.insert(x.Var);
					});
				});
			});
			bool declared = false;
			for (int v = 0; v < (int)vars.size(); v++) {
				auto& var = vars[v];
				if (!used.contains(v) || var.Declared || var.Role == Variable::Roles::Param || var.Role == Variable::Roles::This)
					continue;
				auto typeText = var.Type.Name.empty() ? L"object" : printer.TypeText(var.Type.Name);
				m_W.Line(typeText + L" " + var.Name + L";");
				declared = true;
			}
			if (declared && !body.empty())
				m_W.Line(L"");
			printer.Statements(body);
			m_W.Close();
		}

		void FieldDecl(uint32_t field) {
			auto& f = m_Md.Fields()[field - 1];
			Attributes(FieldToken(field), f.Type);
			auto info = m_Md.MemberInfo(FieldToken(field));
			std::wstring text = Access(f.Flags);
			auto value = m_Md.ConstantCSharp(FieldToken(field));
			if ((f.Flags & 0x40) && !value.empty())
				text += L"const ";
			else {
				if (f.Flags & 0x10)
					text += L"static ";
				if (f.Flags & 0x20)
					text += L"readonly ";
			}
			text += TypeText(info.FieldType.Name, f.Type) + L" " + Identifier(f.Name);
			if (!value.empty())
				text += L" = " + value;
			m_W.Line(text + L";", FieldToken(field));
		}

		void Property(uint32_t property) {
			auto& p = m_Md.Properties()[property - 1];
			auto type = p.Type;
			bool inInterface = type && (m_Md.Types()[type - 1].Flags & 0x20);
			Attributes((ClrTableId::Property << 24) | property, type);
			std::vector<ClrTypeInfo> params;
			auto propertyType = m_Md.CSharpPropertyType(property, &params);
			auto accessor = p.Getter ? p.Getter : p.Setter;
			std::wstring modifiers = accessor ? MethodModifiers(m_Md.Methods()[accessor - 1], inInterface) : L"";
			std::wstring name = Identifier(p.Name);
			if (!params.empty()) {
				// an indexer: its parameters are the getter's (or the setter's, without the value)
				name = L"this[";
				auto info = m_Md.MemberInfo(MethodToken(accessor));
				auto count = p.Getter ? info.Signature.Params.size() : info.Signature.Params.size() - 1;
				for (size_t i = 0; i < count && i < info.Signature.Params.size(); i++) {
					auto pn = m_Md.ParamName(accessor, (uint32_t)i + 1);
					name += (i ? L", " : L"") + info.Signature.Params[i].Name + L" " + (pn.empty() ? std::format(L"arg{}", i) : Identifier(pn));
				}
				name += L"]";
			}
			auto declaration = modifiers + propertyType.Name + L" " + name;
			// an automatic property: its backing field is there
			bool automatic = false;
			for (auto f : m_Md.FieldsOf(type))
				automatic |= m_Md.Fields()[f - 1].Name == L"<" + p.Name + L">k__BackingField";
			bool bodies = !automatic && !inInterface;
			auto hasBody = [&](uint32_t m) { return m && m_Md.Methods()[m - 1].Rva != 0; };
			if (!bodies || (!hasBody(p.Getter) && !hasBody(p.Setter))) {
				std::wstring text = L" {";
				auto access = [&](uint32_t m) {
					// an accessor less accessible than the property says so
					auto mine = Access(m_Md.Methods()[m - 1].Flags);
					return accessor && m != accessor && mine != Access(m_Md.Methods()[accessor - 1].Flags) && !inInterface ? mine : L"";
				};
				if (p.Getter)
					text += L" " + access(p.Getter) + L"get;";
				if (p.Setter)
					text += L" " + access(p.Setter) + L"set;";
				m_W.Line(declaration + text + L" }", (ClrTableId::Property << 24) | property);
				return;
			}
			m_W.Line(declaration, (ClrTableId::Property << 24) | property);
			m_W.Open();
			if (p.Getter)
				Method(p.Getter, true, L"get");
			if (p.Setter)
				Method(p.Setter, true, L"set");
			m_W.Close();
		}

		void Event(uint32_t event) {
			auto& e = m_Md.Events()[event - 1];
			auto type = e.Type;
			bool inInterface = type && (m_Md.Types()[type - 1].Flags & 0x20);
			Attributes((ClrTableId::Event << 24) | event, type);
			auto accessor = e.Adder ? e.Adder : e.Remover;
			std::wstring modifiers = accessor ? MethodModifiers(m_Md.Methods()[accessor - 1], inInterface) : L"";
			auto eventType = e.EventType ? m_Md.CSharpType(e.EventType, m_Md.ContextOfType(type)).Name : L"Delegate";
			auto text = modifiers + L"event " + eventType + L" " + Identifier(e.Name);
			// a field-like event: the compiler's field has its name
			bool fieldLike = inInterface;
			for (auto f : m_Md.FieldsOf(type))
				fieldLike |= m_Md.Fields()[f - 1].Name == e.Name;
			if (fieldLike || !e.Adder || m_Md.Methods()[e.Adder - 1].Rva == 0) {
				m_W.Line(text + L";", (ClrTableId::Event << 24) | event);
				return;
			}
			m_W.Line(text, (ClrTableId::Event << 24) | event);
			m_W.Open();
			if (e.Adder)
				Method(e.Adder, true, L"add");
			if (e.Remover)
				Method(e.Remover, true, L"remove");
			m_W.Close();
		}

		// "public class Circle : IShape", "public delegate int Compare(int a, int b);"
		std::wstring Header(uint32_t type, bool& isDelegate) {
			auto& t = m_Md.Types()[type - 1];
			auto token = TypeToken(type);
			auto kind = m_Md.TypeKind(type);
			static const PCWSTR visibility[] = { L"internal ", L"public ", L"public ", L"private ", L"protected ", L"internal ",
				L"private protected ", L"protected internal " };
			std::wstring text = visibility[t.Flags & 7];
			bool isAbstract = (t.Flags & 0x80) != 0, isSealed = (t.Flags & 0x100) != 0;
			auto name = WithoutArity(t.Name);
			isDelegate = false;
			if (kind == L"delegate") {
				// its Invoke method says what it is
				for (auto m : m_Md.MethodsOf(type))
					if (m_Md.Methods()[m - 1].Name == L"Invoke") {
						auto info = m_Md.MemberInfo(MethodToken(m));
						isDelegate = true;
						return text + L"delegate " + TypeText(info.Signature.Return.Name, type) + L" " + name + GenericParams(token) + L"(" +
							Parameters(m, info, nullptr) + L");";
					}
			}
			if (kind == L"class") {
				if (isAbstract && isSealed)
					text += L"static ";
				else if (isAbstract)
					text += L"abstract ";
				else if (isSealed)
					text += L"sealed ";
			}
			text += (kind == L"delegate" ? L"class" : kind) + L" " + name + GenericParams(token);
			std::vector<std::wstring> bases;
			if (t.Extends && kind == L"class") {
				auto base = m_Md.CSharpType(t.Extends, m_Md.ContextOfType(type)).Name;
				if (base != L"object")
					bases.push_back(base);
			}
			if (kind == L"enum") {
				auto underlying = m_Md.EnumUnderlyingType(type);
				if (underlying.Name != L"int")
					bases.push_back(underlying.Name);
			}
			for (auto i : m_Md.Interfaces(type))
				bases.push_back(m_Md.CSharpType(i, m_Md.ContextOfType(type)).Name);
			for (size_t i = 0; i < bases.size(); i++)
				text += (i ? L", " : L" : ") + bases[i];
			return text;
		}

		void Type(uint32_t type) {
			auto& t = m_Md.Types()[type - 1];
			auto token = TypeToken(type);
			auto kind = m_Md.TypeKind(type);
			Attributes(token, type);
			bool isDelegate;
			auto text = Header(type, isDelegate);
			if (isDelegate) {
				m_W.Line(text, token);
				return;
			}
			m_W.Line(text, t.Extends ? t.Extends : token);
			m_W.Open();

			if (kind == L"enum") {
				for (auto f : m_Md.FieldsOf(type)) {
					auto& field = m_Md.Fields()[f - 1];
					if (!(field.Flags & 0x10))
						continue;	// value__
					Attributes(FieldToken(f), type);
					auto value = m_Md.ConstantCSharp(FieldToken(f));
					m_W.Line(Identifier(field.Name) + (value.empty() ? L"" : L" = " + value) + L",", FieldToken(f));
				}
				m_W.Close();
				return;
			}
			// the members: fields (not the backing fields of properties and events), properties, events, methods (not
			// accessors), nested types
			std::set<std::wstring> hidden;
			for (auto p : m_Md.PropertiesOf(type))
				hidden.insert(L"<" + m_Md.Properties()[p - 1].Name + L">k__BackingField");
			for (auto e : m_Md.EventsOf(type))
				hidden.insert(m_Md.Events()[e - 1].Name);
			bool any = false;
			for (auto f : m_Md.FieldsOf(type)) {
				if (hidden.contains(m_Md.Fields()[f - 1].Name))
					continue;
				FieldDecl(f);
				any = true;
			}
			for (auto e : m_Md.EventsOf(type)) {
				if (any)
					m_W.Blank();
				Event(e);
				any = true;
			}
			for (auto p : m_Md.PropertiesOf(type)) {
				if (any)
					m_W.Blank();
				Property(p);
				any = true;
			}
			int constructors = 0;
			for (auto m : m_Md.MethodsOf(type))
				constructors += m_Md.Methods()[m - 1].Name == L".ctor";
			for (auto m : m_Md.MethodsOf(type)) {
				if (m_Md.AccessorOf(m).second != ClrAccessor::None)
					continue;
				if (constructors == 1 && IsDefaultConstructor(m))
					continue;
				if (any)
					m_W.Blank();
				Method(m);
				any = true;
			}
			for (auto n : m_Md.NestedTypes(type)) {
				if (any)
					m_W.Blank();
				Type(n);
				any = true;
			}
			m_W.Close();
		}

	private:
		PEFile const* m_PE;	// none for declarations alone
		ClrMetadata const& m_Md;
		Writer& m_W;
	};
}

std::wstring DecompileClrMethod(PEFile const& pe, ClrMetadata const& md, uint32_t method, std::vector<uint32_t>* tokens) {
	if (method == 0 || method > md.Methods().size())
		return {};
	Writer w(md);
	TypePrinter printer(&pe, md, w);
	auto [owner, accessor] = md.AccessorOf(method);
	(void)owner;
	static const PCWSTR names[] = { L"", L"get", L"set", L"add", L"remove", L"raise", L"" };
	if (accessor != ClrAccessor::None && accessor != ClrAccessor::Other)
		printer.Method(method, true, names[(int)accessor]);
	else
		printer.Method(method);
	return w.Text(tokens);
}

std::wstring DecompileClrType(PEFile const& pe, ClrMetadata const& md, uint32_t type, std::vector<uint32_t>* tokens) {
	if (type == 0 || type > md.Types().size())
		return {};
	Writer w(md);
	auto& t = md.Types()[type - 1];
	if (!t.Namespace.empty() && t.Enclosing == 0) {
		w.Line(L"namespace " + t.Namespace);
		w.Open();
	}
	TypePrinter(&pe, md, w).Type(type);
	if (!t.Namespace.empty() && t.Enclosing == 0)
		w.Close();
	return w.Text(tokens);
}

std::wstring DecompileClrProperty(PEFile const& pe, ClrMetadata const& md, uint32_t property, std::vector<uint32_t>* tokens) {
	if (property == 0 || property > md.Properties().size())
		return {};
	Writer w(md);
	TypePrinter(&pe, md, w).Property(property);
	return w.Text(tokens);
}

std::wstring DecompileClrEvent(PEFile const& pe, ClrMetadata const& md, uint32_t event, std::vector<uint32_t>* tokens) {
	if (event == 0 || event > md.Events().size())
		return {};
	Writer w(md);
	TypePrinter(&pe, md, w).Event(event);
	return w.Text(tokens);
}

std::wstring CSharpTypeDeclaration(ClrMetadata const& md, uint32_t type) {
	if (type == 0 || type > md.Types().size())
		return {};
	Writer w(md);
	bool isDelegate;
	return TypePrinter(nullptr, md, w).Header(type, isDelegate);
}

std::wstring DecompileClrField(ClrMetadata const& md, uint32_t field, std::vector<uint32_t>* tokens) {
	if (field == 0 || field > md.Fields().size())
		return {};
	Writer w(md);
	auto& f = md.Fields()[field - 1];
	auto info = md.MemberInfo(FieldToken(field));
	std::wstring text;
	for (auto& a : md.CustomAttributes(FieldToken(field)))
		w.Line(L"[" + md.AttributeCSharp(a) + L"]", a.Constructor);
	text = Access(f.Flags);
	auto value = md.ConstantCSharp(FieldToken(field));
	if ((f.Flags & 0x40) && !value.empty())
		text += L"const ";
	else {
		if (f.Flags & 0x10)
			text += L"static ";
		if (f.Flags & 0x20)
			text += L"readonly ";
	}
	text += info.FieldType.Name + L" " + Identifier(f.Name);
	if (!value.empty())
		text += L" = " + value;
	w.Line(text + L";", FieldToken(field));
	w.Line(L"");
	w.Line(L"// in " + md.CSharpTypeDefName(f.Type), TypeToken(f.Type));
	return w.Text(tokens);
}
