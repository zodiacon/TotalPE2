// View.cpp : implementation of the CView class
//
/////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "resource.h"
#include "ScintillaView.h"
#include <lexilla/Lexilla.h>
#include <lexilla/SciLexer.h>
#include <lexilla/lexlib/LexerModule.h>
#include <WTLHelper.h>
#include "PEStrings.h"
#include "PEFile.h"
#include "CodeAnalysis.h"
#include "InfSyntax.h"

// "address mnemonic operands (name of the target) ; bytes": an instruction that refers to a known address gets its name:
// a branch target, a RIP-relative or absolute operand (x86, x64), the address that adrp and add or ldr make (ARM64)
static CStringA FormatInstruction(const cs_insn& inst, InstructionRefs const& refs, IMainFrame* frame) {
	CStringA text, extra;
	if (frame) {
		if (refs.Branch)
			extra = CStringA(frame->ResolveVa(*refs.Branch).c_str());
		if (extra.IsEmpty() && refs.Memory)
			extra = CStringA(frame->ResolveVa(*refs.Memory).c_str());
	}

	if (!extra.IsEmpty())
		extra = std::format("{} ({})", inst.op_str, (PCSTR)extra).c_str();
	text.Format("%llX %-10s %-55s;", inst.address, inst.mnemonic, !extra.IsEmpty() ? (PCSTR)extra : inst.op_str);
	for (int i = 0; i < inst.size; i++)
		text += std::format(" {:02X}", inst.bytes[i]).c_str();
	return text;
}


using namespace Lexilla;
using namespace Scintilla;

const char* KeyWords_ASM[] = {
	"aaa aad aam aas adc add and arpl blsr bnd bndcl bndcn bndcu bndmov bndstx bound bsf bsr bswap bt btc btr bts call cbw cdq cflush clc cld cli clts "
	"cmc cmova cmovae cmovb cmovbe cmovc cmove cmovg cmovge cmovl cmovle cmovna cmovnae cmovnb cmovnbe cmovnc "
	"cmovne cmovng cmovnge cmovnl cmovnle cmovno cmovnp cmovns cmovnz cmovo cmovp cmovpe cmovpo cmovs cmovz "
	"cmp cmps cmpsb cmpsd cmpsq cmpsw cmpxchg cmpxchg486 cmpxchg8b cpuid cwd cwde daa das dec div emms enter "
	"esc femms hlt ibts icebp idiv imul in inc ins insb insd insw int int01 int03 int1 int3 into invd invlpg "
	"iret iretd iretdf iretf iretw ja jae jb jbe jc jcxz je jecxz jg jge jl jle jmp jna jnae jnb jnbe jnc jne "
	"jng jnge jnl jnle jno jnp jns jnz jo jp jpe jpo js jz lahf lar lds lea leave les lfs lgdt lgs lidt lldt "
	"lmsw loadall loadall286 lock lods lodsb lodsd lodsq lodsw loop loopd loope looped loopew loopne loopned "
	"loopnew loopnz loopnzd loopnzw loopw loopz loopzd loopzw lsl lss ltr mov movs movsb movsd movsq movsw "
	"movsx movsxd movzx mul neg nop not or out outs outsb outsd outsw pop popa popad popaw popf popfd popfw "
	"push pusha pushad pushaw pushd pushf pushfd pushfw pushw rcl rcr rdmsr rdpmc rdshr rdtsc rep repe repne "
	"repnz repz ret retf retn rol ror rsdc rsldt rsm rsts sahf sal salc sar sbb scas scasb scasd scasq scasw "
	"seta setae setb setbe setc sete setg setge setl setle setna setnae setnb setnbe setnc setne setng setnge "
	"setnl setnle setno setnp setns setnz seto setp setpe setpo sets setz sgdt shl shld shr shrd sidt sldt smi "
	"smint smintold smsw stc std sti stos stosb stosd stosq stosw str sub svdc svldt svts syscall sysenter "
	"sysexit sysret test ud0 ud1 ud2 umov verr verw wait wbinvd wrmsr wrshr xadd xbts xchg xlat xlatb xor",
	"f2xm1 fabs fadd faddp fbld fbstp fchs fclex fcmovb fcmovbe fcmove fcmovnb fcmovnbe fcmovne fcmovnu fcmovu "
	"fcom fcomi fcomip fcomp fcompp fcos fdecstp fdisi fdiv fdivp fdivr fdivrp feni ffree ffreep fiadd ficom "
	"ficomp fidiv fidivr fild fimul fincstp finit fist fistp fisub fisubr fld fld1 fldcw fldenv fldenvd "
	"fldenvw fldl2e fldl2t fldlg2 fldln2 fldpi fldz fmul fmulp fnclex fndisi fneni fninit fnop fnsave fnsaved "
	"fnsavew fnstcw fnstenv fnstenvd fnstenvw fnstsw fpatan fprem fprem1 fptan frndint frstor frstord frstorw "
	"fsave fsaved fsavew fscale fsetpm fsin fsincos fsqrt fst fstcw fstenv fstenvd fstenvw fstp fstsw fsub "
	"fsubp fsubr fsubrp ftst fucom fucomp fucompp fwait fxam fxch fxtract fyl2x fyl2xp1",
	"ah al ax bh bl bp bx ch cl cr0 cr2 cr3 cr4 cs cx dh di dl dr0 dr1 dr2 dr3 dr6 dr7 ds dx eax ebp ebx ecx edi "
	"edx eip es esi esp fs gs mm0 mm1 mm2 mm3 mm4 mm5 mm6 mm7 r10 r10b r10d r10w r11 r11b r11d r11w r12 r12b "
	"r12d r12w r13 r13b r13d r13w r14 r14b r14d r14w r15 r15b r15d r15w r8 r8b r8d r8w r9 r9b r9d r9w rax rbp "
	"rbx rcx rdi rdx rip rsi rsp si sp ss st st0 st1 st2 st3 st4 st5 st6 st7 tr3 tr4 tr5 tr6 tr7 xmm0 xmm1 "
	"xmm10 xmm11 xmm12 xmm13 xmm14 xmm15 xmm2 xmm3 xmm4 xmm5 xmm6 xmm7 xmm8 xmm9 ymm0 ymm1 ymm10 ymm11 ymm12 "
	"ymm13 ymm14 ymm15 ymm2 ymm3 ymm4 ymm5 ymm6 ymm7 ymm8 ymm9",
	"%arg %assign %define %elif %elifctk %elifdef %elifid %elifidn %elifidni %elifmacro %elifnctk %elifndef "
	"%elifnid %elifnidn %elifnidni %elifnmacro %elifnnum %elifnstr %elifnum %elifstr %else %endif %endmacro "
	"%endrep %error %exitrep %iassign %idefine %if %ifctk %ifdef %ifid %ifidn %ifidni %ifmacro %ifnctk %ifndef "
	"%ifnid %ifnidn %ifnidni %ifnmacro %ifnnum %ifnstr %ifnum %ifstr %imacro %include %line %local %macro %out "
	"%pop %push %rep %repl %rotate %stacksize %strlen %substr %undef %xdefine %xidefine .186 .286 .286c .286p "
	".287 .386 .386c .386p .387 .486 .486p .8086 .8087 .alpha .break .code .const .continue .cref .data .data? "
	".dosseg .else .elseif .endif .endw .err .err1 .err2 .errb .errdef .errdif .errdifi .erre .erridn .erridni "
	".errnb .errndef .errnz .exit .fardata .fardata? .if .lall .lfcond .list .listall .listif .listmacro "
	".listmacroall .model .msfloat .no87 .nocref .nolist .nolistif .nolistmacro .radix .repeat .sall .seq "
	".sfcond .stack .startup .tfcond .type .until .untilcxz .while .xall .xcref .xlist absolute alias align "
	"alignb assume at bits catstr comm comment common cpu db dd df dosseg dq dt dup dw echo else elseif "
	"elseif1 elseif2 elseifb elseifdef elseifdif elseifdifi elseife elseifidn elseifidni elseifnb elseifndef "
	"end endif endm endp ends endstruc eq equ even exitm export extern externdef extrn for forc ge global goto "
	"group gt high highword iend if if1 if2 ifb ifdef ifdif ifdifi ife ifidn ifidni ifnb ifndef import incbin "
	"include includelib instr invoke irp irpc istruc label le length lengthof local low lowword lroffset lt "
	"macro mask mod name ne offset opattr option org page popcontext proc proto ptr public purge pushcontext "
	"record repeat rept resb resd resq rest resw section seg segment short size sizeof sizestr struc struct "
	"substr subtitle subttl textequ this times title type typedef union use16 use32 while width",
	"$ $$ %0 %1 %2 %3 %4 %5 %6 %7 %8 %9 .bss .data .text ? @b @f a16 a32 abs addr all assumes at basic byte c "
	"carry? casemap common compact cpu dotname dword emulator epilogue error export expr16 expr32 far far16 "
	"far32 farstack flat forceframe fortran fword huge language large listing ljmp loadds m510 medium memory "
	"near near16 near32 nearstack nodotname noemulator nokeyword noljmp nom510 none nonunique nooldmacros "
	"nooldstructs noreadonly noscoped nosignextend nosplit nothing notpublic o16 o32 oldmacros oldstructs "
	"os_dos overflow? para parity? pascal private prologue qword radix readonly real10 real4 real8 req sbyte "
	"scoped sdword seq setif2 sign? small smallstack stdcall sword syscall tbyte tiny use16 use32 uses vararg "
	"word wrt zero?",
	"addpd addps addsd addss andnpd andnps andpd andps blendpd blendps blendvpd blendvps cmpeqpd cmpeqps cmpeqsd "
	"cmpeqss cmplepd cmpleps cmplesd cmpless cmpltpd cmpltps cmpltsd cmpltss cmpnepd cmpneps cmpnesd cmpness "
	"cmpnlepd cmpnleps cmpnlesd cmpnless cmpnltpd cmpnltps cmpnltsd cmpnltss cmpordpd cmpordps cmpordsd "
	"cmpordss cmpunordpd cmpunordps cmpunordsd cmpunordss comisd comiss crc32 cvtdq2pd cvtdq2ps cvtpd2dq "
	"cvtpd2pi cvtpd2ps cvtpi2pd cvtpi2ps cvtps2dq cvtps2pd cvtps2pi cvtsd2si cvtsd2ss cvtsi2sd cvtsi2ss "
	"cvtss2sd cvtss2si cvttpd2dq cvttpd2pi cvttps2dq cvttps2pi cvttsd2si cvttss2si divpd divps divsd divss "
	"dppd dpps extractps fxrstor fxsave insertps ldmxscr lfence maskmovdq maskmovdqu maxpd maxps maxss mfence "
	"minpd minps minsd minss movapd movaps movd movdq2q movdqa movdqu movhlps movhpd movhps movlhps movlpd "
	"movlps movmskpd movmskps movntdq movntdqa movnti movntpd movntps movntq movq movq2dq movsd movss movupd "
	"movups mpsadbw mulpd mulps mulsd mulss orpd orps packssdw packsswb packusdw packuswb paddb paddd paddq "
	"paddsb paddsiw paddsw paddusb paddusw paddw pand pandn pause paveb pavgb pavgusb pavgw paxsd pblendvb "
	"pblendw pcmpeqb pcmpeqd pcmpeqq pcmpeqw pcmpestri pcmpestrm pcmpgtb pcmpgtd pcmpgtq pcmpgtw pcmpistri "
	"pcmpistrm pdistib pextrb pextrd pextrq pextrw pf2id pf2iw pfacc pfadd pfcmpeq pfcmpge pfcmpgt pfmax pfmin "
	"pfmul pfnacc pfpnacc pfrcp pfrcpit1 pfrcpit2 pfrsqit1 pfrsqrt pfsub pfsubr phminposuw pi2fd pinsrb pinsrd "
	"pinsrq pinsrw pmachriw pmaddwd pmagw pmaxsb pmaxsd pmaxsw pmaxub pmaxud pmaxuw pminsb pminsd pminsw "
	"pminub pminud pminuw pmovmskb pmovsxbd pmovsxbq pmovsxbw pmovsxdq pmovsxwd pmovsxwq pmovzxbd pmovzxbq "
	"pmovzxbw pmovzxdq pmovzxwd pmovzxwq pmuldq pmulhriw pmulhrwa pmulhrwc pmulhuw pmulhw pmulld pmullw "
	"pmuludq pmvgezb pmvlzb pmvnzb pmvzb popcnt por prefetch prefetchnta prefetcht0 prefetcht1 prefetcht2 "
	"prefetchw psadbw pshufd pshufhw pshuflw pshufw pslld pslldq psllq psllw psrad psraw psrld psrldq psrlq "
	"psrlw psubb psubd psubq psubsb psubsiw psubsw psubusb psubusw psubw pswapd ptest punpckhbw punpckhdq "
	"punpckhqdq punpckhwd punpcklbw punpckldq punpcklqdq punpcklwd pxor rcpps rcpss roundpd roundps roundsd "
	"roundss rsqrtps rsqrtss sfence shufpd shufps sqrtpd sqrtps sqrtsd sqrtss stmxcsr subpd subps subsd subss "
	"ucomisd ucomiss unpckhpd unpckhps unpcklpd unpcklps xorpd xorps",
};

CScintillaView::CScintillaView(IMainFrame* frame, PEFile const& pe, PCWSTR title) : CViewBase(frame), m_PE(pe), m_Title(title) {
}

CString CScintillaView::GetTitle() const {
	return m_Title;
}

CScintillaCtrl& CScintillaView::GetCtrl() {
	return m_Sci;
}

namespace {
	// "0x401000", "401000" or "00000001400010A0", as a number
	bool ParseHexAddress(std::string text, uint64_t& value) {
		if (text.starts_with("0x") || text.starts_with("0X"))
			text = text.substr(2);
		if (text.empty() || text.size() > 16 || !std::all_of(text.begin(), text.end(), [](unsigned char c) { return isxdigit(c) != 0; }))
			return false;
		value = strtoull(text.c_str(), nullptr, 16);
		return true;
	}
}

void CScintillaView::UpdateUI(bool first) {
	auto& ui = Frame()->GetUI();
	ui.UIEnable(ID_EDIT_COPY, !m_Sci.IsSelectionEmpty());
	auto text = m_Sci.GetSelText();
	uint64_t value;
	auto address = ParseHexAddress(text, value);
	ui.UIEnable(ID_ASSEMBLY_GOTOADDRESS, address);
	ui.UIEnable(ID_ASSEMBLY_DISASSEMBLEATTHEEND, address);
	ui.UIEnable(ID_ASSEMBLY_DISASSEMBLEINANEWTAB, address);

	auto line = GetLine(m_ContextLine >= 0 ? m_ContextLine : CurrentLine());
	ui.UIEnable(ID_ASSEMBLY_FOLLOW, line && line->Target());
	ui.UIEnable(ID_ASSEMBLY_XREFS_HERE, line && line->Va);
	ui.UIEnable(ID_ASSEMBLY_XREFS_TARGET, line && line->Target());
	ui.UIEnable(ID_ASSEMBLY_FLOWGRAPH, FlowGraphAddress().has_value());
}

namespace {
	// "; name -- 12 xrefs (10 calls, 2 jumps)", the label above an instruction that something refers to
	CStringA XrefLabel(std::wstring const& name, std::span<const Xref> refs) {
		int calls = 0, jumps = 0, data = 0;
		for (auto const& x : refs) {
			switch (x.Kind) {
				case XrefKind::Call: calls++; break;
				case XrefKind::Data: data++; break;
				default: jumps++; break;
			}
		}
		std::string detail;
		auto add = [&](int count, char const* what) {
			if (count > 0)
				detail += std::format("{}{} {}{}", detail.empty() ? "" : ", ", count, what, count == 1 ? "" : "s");
		};
		add(calls, "call");
		add(jumps, "jump");
		add(data, "data reference");
		std::string prefix = name.empty() ? std::string() : std::string(CStringA(name.c_str())) + " -- ";
		auto text = std::format("; {}{} xref{} ({})", prefix, refs.size(), refs.size() == 1 ? "" : "s", detail);
		return text.c_str();
	}

	bool IsJumpTarget(XrefMap const& xrefs, uint64_t va) {
		for (auto const& x : xrefs.To(va))
			if (x.Kind == XrefKind::Jump || x.Kind == XrefKind::ConditionalJump)
				return true;
		return false;
	}
}

// Disassembles until the end of the function: after an instruction that ends the flow of control, unless the next
// instruction is the target of a jump (the function goes on there). Every piece of text ends with a line break and has
// one entry in m_Lines, so that line numbers and entries correspond.
CStringA CScintillaView::Disassemble(std::span<const std::byte> code, uint64_t address) {
	size_t h;
	if (!OpenDisassembler(m_Arch, h))	// with the details of the operands: they are needed to resolve symbols
		return "";
	csh handle = h;
	auto const& xrefs = Frame()->GetXrefs();
	AddressTracker tracker(m_Arch);

	auto bytes = (const uint8_t*)code.data();
	auto size = code.size();
	auto inst = cs_malloc(handle);
	CStringA text;
	while (cs_disasm_iter(handle, &bytes, &size, &address, inst)) {
		auto refs = xrefs.To(inst->address);
		if (!refs.empty()) {
			if (!m_Lines.empty()) {
				text += "\r\n";
				m_Lines.push_back({});
			}
			text += XrefLabel(Frame()->ResolveVa(inst->address), refs) + "\r\n";
			m_Lines.push_back({});
		}

		auto info = tracker.Refs(*inst);
		Line line;
		line.Va = inst->address;
		line.Branch = info.Branch;
		line.Memory = info.Memory;
		m_LineOfVa.insert({ inst->address, (int)m_Lines.size() });
		m_Lines.push_back(line);
		text += FormatInstruction(*inst, info, Frame()) + "\r\n";

		if (EndsFlow(*inst, m_Arch) && !IsJumpTarget(xrefs, address))
			break;
	}
	cs_free(inst, 1);
	cs_close(&handle);
	return text;
}

bool CScintillaView::SetAsmCode(std::span<const std::byte> code, uint64_t address, CpuArch arch) {
	if (arch != m_Arch) {
		m_Arch = arch;
		if (m_Language == LexLanguage::Asm)
			SetLanguage(LexLanguage::Asm);	// the words of the instruction set
	}
	m_Lines.clear();
	m_LineOfVa.clear();
	m_Sci.SetText(Disassemble(code, address));
	return true;
}

int CScintillaView::CurrentLine() const {
	return (int)m_Sci.LineFromPosition(m_Sci.GetCurrentPos());
}

CScintillaView::Line const* CScintillaView::GetLine(int line) const {
	return line >= 0 && line < (int)m_Lines.size() ? &m_Lines[line] : nullptr;
}

// the first line at or below 'line' that is an instruction, or -1
int CScintillaView::InstructionLine(int line) const {
	for (int i = std::max(line, 0); i < (int)m_Lines.size(); i++)
		if (m_Lines[i].Va)
			return i;
	return -1;
}

int64_t CScintillaView::GetNavigationPosition() const {
	if (m_Language != LexLanguage::Asm)
		return -1;
	auto line = GetLine(InstructionLine(CurrentLine()));
	return line ? (int64_t)line->Va : -1;
}

void CScintillaView::SetNavigationPosition(int64_t va) {
	GoToAddress((uint64_t)va);
}

void CScintillaView::ShowLine(int line) {
	auto start = (intptr_t)m_Sci.SendMessage(SCI_POSITIONFROMLINE, line);
	auto onScreen = (int)m_Sci.SendMessage(SCI_LINESONSCREEN);
	m_Sci.SendMessage(SCI_SETFIRSTVISIBLELINE, std::max(0, line - onScreen / 3));
	m_Sci.SendMessage(SCI_SETSEL, start, m_Sci.SendMessage(SCI_GETLINEENDPOSITION, line));
}

bool CScintillaView::GoToAddress(uint64_t va) {
	auto it = m_LineOfVa.find(va);
	if (it == m_LineOfVa.end())
		return false;
	ShowLine(it->second);
	return true;
}

// in this view if it shows the address, otherwise wherever the main window finds a place for it
bool CScintillaView::NavigateTo(uint64_t va) {
	Frame()->RecordNavigation();
	if (GoToAddress(va)) {
		Frame()->RecordNavigation();
		return true;
	}
	return Frame()->GoToVa(va);
}

bool CScintillaView::Follow(int line) {
	auto info = GetLine(line);
	if (info == nullptr || !info->Target())
		return false;
	return NavigateTo(*info->Target());
}

BOOL CScintillaView::PreTranslateMessage(MSG* pMsg) {
	if (pMsg->message == WM_KEYDOWN && m_Language == LexLanguage::Asm && m_Sci.m_hWnd && pMsg->hwnd == m_Sci.m_hWnd &&
		(pMsg->wParam == VK_RETURN || pMsg->wParam == 'X' || pMsg->wParam == 'G') &&
		::GetKeyState(VK_CONTROL) >= 0 && ::GetKeyState(VK_MENU) >= 0 && ::GetKeyState(VK_SHIFT) >= 0) {
		m_ContextLine = -1;		// the line of the caret
		if (pMsg->wParam == VK_RETURN) {
			Follow(CurrentLine());
		}
		else if (pMsg->wParam == 'G') {
			if (auto va = FlowGraphAddress(); !va || !Frame()->ShowFlowGraph(*va))
				::MessageBeep(MB_ICONWARNING);
		}
		else if (auto line = GetLine(CurrentLine()); line && line->Va) {
			Frame()->ShowXrefs(line->Va);
		}
		return TRUE;
	}
	return CViewBase::PreTranslateMessage(pMsg);
}

LRESULT CScintillaView::OnDoubleClick(int, LPNMHDR pnmh, BOOL& handled) {
	auto line = (int)m_Sci.LineFromPosition(((SCNotification*)pnmh)->position);
	handled = Follow(line);
	return 0;
}

LRESULT CScintillaView::OnFollow(WORD, WORD, HWND, BOOL&) {
	Follow(m_ContextLine >= 0 ? m_ContextLine : CurrentLine());
	return 0;
}

LRESULT CScintillaView::OnXrefsHere(WORD, WORD, HWND, BOOL&) {
	if (auto line = GetLine(m_ContextLine >= 0 ? m_ContextLine : CurrentLine()); line && line->Va)
		Frame()->ShowXrefs(line->Va);
	return 0;
}

LRESULT CScintillaView::OnXrefsTarget(WORD, WORD, HWND, BOOL&) {
	if (auto line = GetLine(m_ContextLine >= 0 ? m_ContextLine : CurrentLine()); line && line->Target())
		Frame()->ShowXrefs(*line->Target());
	return 0;
}

LRESULT CScintillaView::OnFlowGraph(WORD, WORD, HWND, BOOL&) {
	if (auto va = FlowGraphAddress())
		Frame()->ShowFlowGraph(*va);
	return 0;
}

// What the flow graph is of: an address that is selected (in the image), the instruction of the line (or the one that a label
// is above), and otherwise (an empty line, below the code) the function that the view shows. Flow graphs are of PE files.
std::optional<uint64_t> CScintillaView::FlowGraphAddress() {
	if (!m_PE || m_Language != LexLanguage::Asm)
		return std::nullopt;
	uint64_t va;
	auto base = m_PE.GetImageBase();
	if (ParseHexAddress(m_Sci.GetSelText(), va) && va >= base && va - base <= 0xFFFFFFFFULL && m_PE.GetOffsetFromRVA((uint32_t)(va - base)) != 0)
		return va;
	if (auto line = GetLine(InstructionLine(m_ContextLine >= 0 ? m_ContextLine : CurrentLine())); line && line->Va)
		return line->Va;
	if (auto first = GetLine(InstructionLine(0)); first && first->Va)
		return first->Va;
	return std::nullopt;
}

void CScintillaView::SetText(PCWSTR text) {
	m_Sci.SetText(CStringA(text));
}

void CScintillaView::SetText(PCSTR text) {
	m_Sci.SetText(text);
}

// The words of ARM64 code for the assembly lexer: the instructions, (no FPU instructions), the registers
static std::vector<std::string> const& KeyWords_ARM64() {
	static std::vector<std::string> const words = [] {
		std::string registers = "sp wsp xzr wzr lr fp pc nzcv fpcr fpsr";
		for (int i = 0; i <= 31; i++) {
			for (auto prefix : { "x", "w", "v", "q", "d", "s", "h", "b" }) {
				if (i == 31 && (prefix[0] == 'x' || prefix[0] == 'w'))
					continue;
				registers += std::format(" {}{}", prefix, i);
			}
		}
		std::string instructions =
			"adc adcs add adds adr adrp and ands asr at autia autib b bfi bfm bfxil bic bics bl blr blraa br brk bti "
			"cas casa casal casl cbnz cbz ccmn ccmp cinc cinv clrex cls clz cmn cmp cneg crc32b crc32cb crc32ch crc32cw crc32cx "
			"crc32h crc32w crc32x csel cset csetm csinc csinv csneg dc dmb dsb eon eor eret extr fabs fadd fccmp fcmp fcsel "
			"fcvt fcvtzs fcvtzu fdiv fmadd fmax fmin fmov fmsub fmul fneg fnmadd fnmul frinta frintm frintn frintp frintz "
			"fsqrt fsub hint hlt hvc ic isb ld1 ld2 ld3 ld4 ldadd ldaddal ldaddl ldar ldarb ldarh ldaxp ldaxr ldaxrb ldaxrh "
			"ldclr ldclral ldeor ldnp ldp ldpsw ldr ldrb ldrh ldrsb ldrsh ldrsw ldset ldsetal ldswp ldswpal ldtr ldur ldurb "
			"ldurh ldursb ldursh ldursw ldxp ldxr ldxrb ldxrh lsl lsr madd mneg mov movi movk movn movz mrs msr msub mul mvn "
			"neg negs ngc ngcs nop orn orr pacia pacib paciasp pacibsp autiasp autibsp prfm rbit ret retaa retab rev rev16 rev32 ror sbc sbcs sbfiz "
			"sbfm sbfx scvtf sdiv sev sevl smaddl smc smnegl smsubl smulh smull st1 st2 st3 st4 stlr stlrb stlrh stlxp stlxr "
			"stlxrb stlxrh stnp stp str strb strh sttr stur sturb sturh stxp stxr stxrb stxrh sub subs svc swp sxtb sxth sxtw "
			"sys sysl tbnz tbz tlbi tst ubfiz ubfm ubfx ucvtf udf udiv umaddl umnegl umsubl umulh umull uxtb uxth wfe wfi yield "
			"b.eq b.ne b.cs b.hs b.cc b.lo b.mi b.pl b.vs b.vc b.hi b.ls b.ge b.lt b.gt b.le b.al";
		return std::vector<std::string>{ instructions, "", registers, "", "", "" };
	}();
	return words;
}

void CScintillaView::SetLanguage(LexLanguage lang) {
	extern LexerModule lmAsm;
	extern LexerModule lmXML;
	extern LexerModule lmInf;

	m_Language = lang;

	switch (lang) {
		case LexLanguage::Asm:
		{
			auto lexer = lmAsm.Create();
			m_Sci.SetLexer(lexer);
			if (m_Arch == CpuArch::Arm64) {
				auto const& words = KeyWords_ARM64();
				for (int i = 0; i < (int)words.size(); i++)
					lexer->WordListSet(i, words[i].c_str());
				break;
			}
			auto count = _countof(KeyWords_ASM);
			for(int i = 0; i < count; i++)
				lexer->WordListSet(i, KeyWords_ASM[i]);
			break;
		}

		case LexLanguage::Xml:
		case LexLanguage::Html:	// the XML lexer colors the tags and attributes of HTML as well
			m_Sci.SetLexer(lmXML.Create());
			break;

		case LexLanguage::Inf:
			m_Sci.SetLexer(lmInf.Create());
			break;
	}
	UpdateColors();
}

void CScintillaView::UpdateColors() {
	// TEMPORARY
	auto dark = WTLHelper::IsDarkMode();

	m_Sci.StyleSetFore(STYLE_DEFAULT, ::GetSysColor(COLOR_WINDOWTEXT));
	m_Sci.StyleSetBack(STYLE_DEFAULT, ::GetSysColor(COLOR_WINDOW));
	m_Sci.StyleClearAll();

	switch (m_Language) {
		case LexLanguage::Asm:
			m_Sci.StyleSetFore(SCE_ASM_COMMENT, RGB(0, 128, 0));
			m_Sci.StyleSetFore(SCE_ASM_CPUINSTRUCTION, dark ? RGB(240, 0, 0) : RGB(160, 0, 0));
			m_Sci.StyleSetFore(SCE_ASM_NUMBER, dark ? RGB(0, 255, 255) : RGB(0, 0, 255));
			m_Sci.StyleSetFore(SCE_ASM_STRING, dark ? RGB(128, 0, 128) : RGB(128, 0, 64));
			m_Sci.StyleSetFore(SCE_ASM_REGISTER, dark ? RGB(128, 128, 0) : RGB(255, 128, 0));
			m_Sci.StyleSetFore(SCE_ASM_DIRECTIVE, RGB(128, 128, 128));
			break;

		case LexLanguage::Xml:
		case LexLanguage::Html:
			m_Sci.StyleSetFore(SCE_H_TAG, dark ? RGB(0, 128, 255) : RGB(0, 0, 240));
			m_Sci.StyleSetFore(SCE_H_ATTRIBUTE, dark ? RGB(240, 128, 128) : RGB(128, 0, 0));
			break;

		case LexLanguage::Inf:
			m_Sci.StyleSetFore((int)InfStyle::Comment, RGB(0, 128, 0));
			m_Sci.StyleSetFore((int)InfStyle::Section, dark ? RGB(0, 128, 255) : RGB(0, 0, 240));
			m_Sci.StyleSetBold((int)InfStyle::Section, true);
			m_Sci.StyleSetFore((int)InfStyle::Key, dark ? RGB(240, 128, 128) : RGB(128, 0, 0));
			m_Sci.StyleSetFore((int)InfStyle::Operator, RGB(128, 128, 128));
			m_Sci.StyleSetFore((int)InfStyle::String, dark ? RGB(214, 157, 133) : RGB(163, 21, 21));
			m_Sci.StyleSetFore((int)InfStyle::StringKey, dark ? RGB(200, 120, 255) : RGB(128, 0, 128));
			m_Sci.StyleSetFore((int)InfStyle::Number, dark ? RGB(0, 255, 255) : RGB(0, 0, 255));
			m_Sci.StyleSetFore((int)InfStyle::RootKey, dark ? RGB(255, 160, 0) : RGB(192, 96, 0));
			m_Sci.StyleSetBold((int)InfStyle::RootKey, true);
			break;
	}
}

LRESULT CScintillaView::OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
	m_Sci.Focus();
	m_Sci.SetFocus();

	return 0;
}

LRESULT CScintillaView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	m_hWndClient = m_Sci.Create(m_hWnd);

	m_Sci.StyleSetFont(STYLE_DEFAULT, "Consolas");
	m_Sci.StyleSetSize(STYLE_DEFAULT, 11);
	m_Sci.UsePopup(SC_POPUP_NEVER);

	bHandled = FALSE;	// the base class registers the view for the message filter (PreTranslateMessage) and idle handling
	return 0;
}

LRESULT CScintillaView::OnUpdateTheme(UINT, WPARAM, LPARAM, BOOL&) {
	UpdateColors();
	return 0;
}

LRESULT CScintillaView::OnContextMenu(UINT, WPARAM, LPARAM lp, BOOL&) {
	// the line under the mouse: a right click does not move the caret
	m_ContextLine = -1;
	int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
	if (m_Language == LexLanguage::Asm && x != -1 && y != -1) {
		CPoint pt(x, y);
		::ScreenToClient(m_Sci, &pt);
		auto pos = (intptr_t)m_Sci.SendMessage(SCI_POSITIONFROMPOINT, pt.x, pt.y);
		if (pos >= 0)
			m_ContextLine = (int)m_Sci.LineFromPosition(pos);
	}
	UpdateUI();
	CMenu menu;
	menu.LoadMenuW(IDR_CONTEXT);
	auto result = Frame()->ShowContextMenu(menu.GetSubMenu(m_Language == LexLanguage::Asm ? 6 : 0), 0,
		x, y);
	return result;
}

LRESULT CScintillaView::OnGoToAddress(WORD, WORD, HWND, BOOL&) {
	uint64_t va;
	if (!ParseHexAddress(m_Sci.GetSelText(), va) || !NavigateTo(va))
		AtlMessageBox(m_hWnd, L"Address not found", IDR_MAINFRAME, MB_ICONWARNING);
	return 0;
}

LRESULT CScintillaView::OnDisassembleNewTab(WORD, WORD, HWND, BOOL&) {
	uint64_t va;
	if (ParseHexAddress(m_Sci.GetSelText(), va))
		Frame()->GoToVa(va);
	return 0;
}

LRESULT CScintillaView::OnDisassembleAtEnd(WORD, WORD, HWND, BOOL&) {
	uint64_t address;
	if (!ParseHexAddress(m_Sci.GetSelText(), address) || address < m_PE.GetImageBase())
		return 0;
	auto offset = m_PE.GetOffsetFromRVA(address - m_PE.GetImageBase());
	if (offset == 0 || offset >= m_PE.GetFileSize())
		return 0;

	// the text ends with a line break, so the new lines start at the line that is now empty
	auto size = std::min<uint32_t>(0x1000, m_PE.GetFileSize() - (uint32_t)offset);
	auto text = Disassemble(m_PE.GetSpan((uint32_t)offset, size), address);

	m_Sci.SetReadOnly(false);
	m_Sci.AppendText(text.GetLength(), text);
	m_Sci.SetReadOnly(true);

	return 0;
}

LRESULT CScintillaView::OnEditCopy(WORD, WORD, HWND, BOOL&) {
	m_Sci.Copy();
	return 0;
}


