// View.cpp : implementation of the CView class
//
/////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "resource.h"
#include "ScintillaView.h"
#include "InputDlg.h"
#include <Annotations.h>
#include <unordered_set>
#include <regex>
#include <lexilla/Lexilla.h>
#include <lexilla/SciLexer.h>
#include "ClrIL.h"
#include <lexilla/lexlib/LexerModule.h>
#include <WTLHelper.h>
#include "PEStrings.h"
#include "PEFile.h"
#include "CodeAnalysis.h"
#include "InfSyntax.h"
#include "CodeColors.h"
#include "Fonts.h"

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
	ui.UIEnable(ID_EDIT_CUT, !m_Sci.IsReadOnly() && !m_Sci.IsSelectionEmpty());
	ui.UIEnable(ID_EDIT_PASTE, !m_Sci.IsReadOnly());
	ui.UIEnable(ID_EDIT_UNDO, !m_Sci.IsReadOnly());
	ui.UIEnable(ID_EDIT_REDO, !m_Sci.IsReadOnly());
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
	ui.UIEnable(ID_ASSEMBLY_CALLGRAPH, FlowGraphAddress().has_value());
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
		text += FormatInstruction(*inst, info, Frame());
		if (auto comment = Frame()->CommentAt(inst->address); !comment.empty()) {
			// one line (a comment of several is shown on one)
			for (auto& ch : comment)
				if (ch == L'\r' || ch == L'\n')
					ch = L' ';
			text += "  ; " + CStringA(CW2A(comment.c_str(), CP_UTF8));
		}
		text += "\r\n";

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
	m_Code = code;
	m_CodeAddress = address;
	m_Lines.clear();
	m_LineOfVa.clear();
	m_Sci.SetText(Disassemble(code, address));
	if (!m_Function)
		m_Function = address;
	MarkBookmarks();
	return true;
}

void CScintillaView::XrefsChanged() {
	if (m_Language != LexLanguage::Asm || m_Code.empty())
		return;
	// the instructions of the caret and of the first line shown, which the labels move
	auto caret = GetNavigationPosition();
	auto top = GetLine(InstructionLine((int)m_Sci.SendMessage(SCI_DOCLINEFROMVISIBLE, m_Sci.SendMessage(SCI_GETFIRSTVISIBLELINE))));
	uint64_t topVa = top ? top->Va : 0;
	bool readOnly = m_Sci.IsReadOnly();
	m_Sci.SetReadOnly(false);
	SetAsmCode(m_Code, m_CodeAddress, m_Arch);
	m_Sci.SetReadOnly(readOnly);
	m_Sci.EmptyUndoBuffer();
	if (auto it = m_LineOfVa.find(topVa); it != m_LineOfVa.end())
		m_Sci.SendMessage(SCI_SETFIRSTVISIBLELINE, m_Sci.SendMessage(SCI_VISIBLEFROMDOCLINE, it->second));
	if (caret > 0)
		if (auto it = m_LineOfVa.find((uint64_t)caret); it != m_LineOfVa.end())
			m_Sci.GotoPos(m_Sci.PositionFromLine(it->second));
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
	auto row = (int)m_Sci.SendMessage(SCI_VISIBLEFROMDOCLINE, line);
	m_Sci.SendMessage(SCI_SETFIRSTVISIBLELINE, std::max(0, row - onScreen / 3));
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
	if (pMsg->message == WM_KEYDOWN && m_LineAction && m_Sci.m_hWnd && pMsg->hwnd == m_Sci.m_hWnd && pMsg->wParam == VK_RETURN &&
		::GetKeyState(VK_CONTROL) >= 0 && ::GetKeyState(VK_MENU) >= 0 && ::GetKeyState(VK_SHIFT) >= 0) {
		if (!m_LineAction(CurrentLine()))
			::MessageBeep(MB_ICONWARNING);
		return TRUE;
	}
	// ';' comments the instruction of the caret
	if (pMsg->message == WM_CHAR && pMsg->wParam == L';' && HasAnnotations() && m_Sci.m_hWnd && pMsg->hwnd == m_Sci.m_hWnd &&
		m_Sci.SendMessage(SCI_GETREADONLY)) {
		BOOL handled;
		OnAnnotate(0, ID_NAV_COMMENT, nullptr, handled);
		return TRUE;
	}
	// N renames the variable or function at the caret, Y changes the variable's type
	if (pMsg->message == WM_KEYDOWN && m_Language == LexLanguage::C && m_Function && m_Sci.m_hWnd && pMsg->hwnd == m_Sci.m_hWnd &&
		(pMsg->wParam == 'N' || pMsg->wParam == 'Y') && m_Sci.SendMessage(SCI_GETREADONLY) &&
		::GetKeyState(VK_CONTROL) >= 0 && ::GetKeyState(VK_MENU) >= 0 && ::GetKeyState(VK_SHIFT) >= 0) {
		if (!Rename(NameAt(m_Sci.GetCurrentPos()), pMsg->wParam == 'Y'))
			::MessageBeep(MB_ICONWARNING);
		return TRUE;
	}
	// G shows the flow graph of the function at the caret, or of the view's
	if (pMsg->message == WM_KEYDOWN && m_Language == LexLanguage::C && m_Function && m_Sci.m_hWnd && pMsg->hwnd == m_Sci.m_hWnd &&
		pMsg->wParam == 'G' && m_Sci.SendMessage(SCI_GETREADONLY) &&
		::GetKeyState(VK_CONTROL) >= 0 && ::GetKeyState(VK_MENU) >= 0 && ::GetKeyState(VK_SHIFT) >= 0) {
		m_ContextPos = -1;
		if (auto va = FlowGraphAddress(); !va || !Frame()->ShowFlowGraph(*va))
			::MessageBeep(MB_ICONWARNING);
		return TRUE;
	}
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
	handled = m_LineAction ? m_LineAction(line) : Follow(line);
	return 0;
}

LRESULT CScintillaView::OnGoToDefinition(WORD, WORD, HWND, BOOL&) {
	if (m_LineAction && !m_LineAction(m_ContextLine >= 0 ? m_ContextLine : CurrentLine()))
		::MessageBeep(MB_ICONWARNING);
	return 0;
}

void CScintillaView::SetLineAction(LineAction action) {
	m_LineAction = std::move(action);
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

// the flow graph, or the call graph, of the function
LRESULT CScintillaView::OnFlowGraph(WORD, WORD id, HWND, BOOL&) {
	if (auto va = FlowGraphAddress())
		id == ID_ASSEMBLY_CALLGRAPH ? Frame()->ShowCallGraph(*va) : Frame()->ShowFlowGraph(*va);
	return 0;
}

// What the flow graph is of: an address that is selected (in the image), the instruction of the line (or the one that a label
// is above), and otherwise (an empty line, below the code) the function that the view shows. Flow graphs are of PE files.
std::optional<uint64_t> CScintillaView::FlowGraphAddress() {
	if (!m_PE)
		return std::nullopt;
	uint64_t va;
	auto base = m_PE.GetImageBase();
	// decompiled code: the function (of the image) whose name is under the mouse, or that of the view
	if (m_Language == LexLanguage::C && m_Function) {
		auto inImage = [&](uint64_t address) {
			return address >= base && address - base <= 0xFFFFFFFFULL && m_PE.GetOffsetFromRVA((uint32_t)(address - base)) != 0;
		};
		if (auto function = FunctionOf(NameAt(m_ContextPos >= 0 ? m_ContextPos : m_Sci.GetCurrentPos())); function && inImage(function))
			return function;
		return m_Function;
	}
	if (m_Language != LexLanguage::Asm)
		return std::nullopt;
	if (ParseHexAddress(m_Sci.GetSelText(), va) && va >= base && va - base <= 0xFFFFFFFFULL && m_PE.GetOffsetFromRVA((uint32_t)(va - base)) != 0)
		return va;
	if (auto line = GetLine(InstructionLine(m_ContextLine >= 0 ? m_ContextLine : CurrentLine())); line && line->Va)
		return line->Va;
	if (auto first = GetLine(InstructionLine(0)); first && first->Va)
		return first->Va;
	return std::nullopt;
}

void CScintillaView::SetText(PCWSTR text) {
	SetText(CStringA(text).GetString());
}

void CScintillaView::SetText(PCSTR text) {
	m_Sci.SetText(text);
	// IDL: a GUID in one color (the lexer of C takes its parts for numbers and names)
	if (m_Language == LexLanguage::Idl) {
		static std::regex const guid(R"([0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12})");
		m_Sci.SendMessage(SCI_SETINDICATORCURRENT, IndicatorGuid);
		std::string_view all(text);
		for (std::cregex_iterator it(all.data(), all.data() + all.size(), guid), end; it != end; ++it)
			m_Sci.SendMessage(SCI_INDICATORFILLRANGE, it->position(), it->length());
	}
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
	extern LexerModule lmIl;
	extern const LexerModule lmCPP;	// C, C++ and C# (from the Lexilla library, where it is const): the keywords tell them apart

	m_Language = lang;
	// the code of the image is ASCII, with the user's comments (and C strings) in UTF-8
	if (lang == LexLanguage::Asm || lang == LexLanguage::C)
		m_Sci.SetCodePage(SC_CP_UTF8);
	// (a constant of Windows the mouse rests on: the names of its value)
	if (lang == LexLanguage::C)
		m_Sci.SendMessage(SCI_SETMOUSEDWELLTIME, 600);

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

		case LexLanguage::IL:
			m_Sci.SetLexer(lmIl.Create());
			break;

		case LexLanguage::Idl:
		{
			// the words of IDL (of what a type library describes); the attributes in brackets and the types of OLE as the second set
			auto lexer = lmCPP.Create();
			m_Sci.SetLexer(lexer);
			lexer->WordListSet(0, "library interface dispinterface coclass module typedef enum struct union const import importlib "
				"properties methods cpp_quote midl_pragma");
			lexer->WordListSet(1,
				// attributes
				"uuid version lcid helpstring helpcontext helpfile helpstringcontext helpstringdll dual oleautomation object "
				"odl hidden restricted nonextensible noncreatable appobject control licensed aggregatable replaceable "
				"predeclid public local default defaultvalue defaultbind defaultcollelem displaybind bindable requestedit "
				"immediatebind source in out retval optional lcid vararg id propget propput propputref readonly "
				"uidefault usesgetlasterror custom entry call_as "
				// types
				"void char short int long hyper float double unsigned signed boolean byte wchar_t BSTR VARIANT VARIANT_BOOL "
				"HRESULT SCODE DATE CURRENCY CY DECIMAL GUID IUnknown IDispatch SAFEARRAY LPSTR LPWSTR VARTYPE INT_PTR "
				"UINT_PTR int64 uint64 __int64 BYTE WORD DWORD BOOL LONG ULONG USHORT UINT INT OLE_COLOR");
			break;
		}

		case LexLanguage::C:
		case LexLanguage::Cpp:
		case LexLanguage::CSharp:
		{
			static const char* const keywords[] = {
				// C
				"auto break case char const continue default do double else enum extern float for goto if inline int long "
				"register restrict return short signed sizeof static struct switch typedef union unsigned void volatile while "
				"_Bool _Static_assert",
				// C++
				"alignas alignof bool break case catch char char8_t char16_t char32_t class const constexpr continue decltype "
				"default delete do double else enum explicit extern false float for friend goto if inline int long mutable "
				"namespace new noexcept nullptr operator private protected public return short signed sizeof static "
				"static_assert struct switch template this throw true try typedef typename union unsigned using virtual void "
				"volatile wchar_t while override final",
				// C#
				"abstract as base bool break byte case catch char checked class const continue decimal default delegate do "
				"double else enum event explicit extern false finally fixed float for foreach get goto if implicit in int "
				"interface internal is lock long namespace new null object operator out override params private protected "
				"public readonly ref return sbyte sealed set short sizeof stackalloc static string struct switch this throw "
				"true try typeof uint ulong unchecked unsafe ushort using value virtual void volatile while",
			};
			// the types of <stdint.h> and <stddef.h> (and C#'s attributes) as the second set
			static const char* const types[] = {
				"int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t size_t ptrdiff_t wchar_t",
				"int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t size_t ptrdiff_t",
				"StructLayout LayoutKind Explicit FieldOffset Size",
			};
			auto index = lang == LexLanguage::C ? 0 : lang == LexLanguage::Cpp ? 1 : 2;
			auto lexer = lmCPP.Create();
			m_Sci.SetLexer(lexer);
			lexer->WordListSet(0, keywords[index]);
			lexer->WordListSet(1, types[index]);
			break;
		}
	}
	UpdateColors();
}

void CScintillaView::UpdateColors() {
	auto dark = WTLHelper::IsDarkMode();
	// the user's colors of the theme (Options > Colors): the color of an element, and whether it is bold
	auto style = [&](int id, char const* key) {
		auto c = CodeColors::Get(key, dark);
		m_Sci.StyleSetFore(id, c.Color);
		m_Sci.StyleSetBold(id, c.Bold);
	};

	// the code font (Options > Code Font)
	auto font = Fonts::Code();
	m_Sci.StyleSetFont(STYLE_DEFAULT, CW2A(font.lfFaceName, CP_UTF8));
	m_Sci.StyleSetSize(STYLE_DEFAULT, Fonts::Points(font));
	m_Sci.StyleSetBold(STYLE_DEFAULT, font.lfWeight >= FW_BOLD);
	m_Sci.StyleSetItalic(STYLE_DEFAULT, font.lfItalic != 0);
	m_Sci.StyleSetFore(STYLE_DEFAULT, ::GetSysColor(COLOR_WINDOWTEXT));
	m_Sci.StyleSetBack(STYLE_DEFAULT, ::GetSysColor(COLOR_WINDOW));
	m_Sci.StyleClearAll();

	// the scroll bars, of the theme (dark mode's themes are of the controls it knows, which Scintilla is not one of)
	::SetWindowTheme(m_Sci, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
	m_Sci.SetWindowPos(nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

	// the lines marked as related to the other side of a side-by-side view
	m_Sci.SendMessage(SCI_MARKERDEFINE, MarkerRelated, SC_MARK_BACKGROUND);
	m_Sci.SendMessage(SCI_MARKERSETBACK, MarkerRelated, CodeColors::Get("Common.Related", dark).Color);
	// a bookmark: in the margin
	auto bookmark = CodeColors::Get("Common.Bookmark", dark).Color;
	m_Sci.SendMessage(SCI_MARKERDEFINE, MarkerBookmark, SC_MARK_BOOKMARK);
	m_Sci.SendMessage(SCI_MARKERSETFORE, MarkerBookmark, RGB(GetRValue(bookmark) / 2, GetGValue(bookmark) * 2 / 3, GetBValue(bookmark) * 7 / 8));
	m_Sci.SendMessage(SCI_MARKERSETBACK, MarkerBookmark, bookmark);

	// the selection and the caret: Scintilla's own colors are for a light background
	using Scintilla::Element;
	auto opaque = [](COLORREF color) { return (int)(color | 0xFF000000); };
	if (dark) {
		for (auto element : { Element::SelectionBack, Element::SelectionAdditionalBack, Element::SelectionSecondaryBack })
			m_Sci.Execute(SCI_SETELEMENTCOLOUR, (WPARAM)element, opaque(RGB(38, 79, 120)));
		for (auto element : { Element::SelectionInactiveBack })
			m_Sci.Execute(SCI_SETELEMENTCOLOUR, (WPARAM)element, opaque(RGB(58, 61, 65)));
		m_Sci.Execute(SCI_SETELEMENTCOLOUR, (WPARAM)Element::Caret, opaque(RGB(240, 240, 240)));
	}
	else {
		for (auto element : { Element::SelectionBack, Element::SelectionAdditionalBack, Element::SelectionSecondaryBack,
			Element::SelectionInactiveBack, Element::Caret })
			m_Sci.Execute(SCI_RESETELEMENTCOLOUR, (WPARAM)element);
	}

	switch (m_Language) {
		case LexLanguage::Asm:
			style(SCE_ASM_COMMENT, "Asm.Comment");
			style(SCE_ASM_CPUINSTRUCTION, "Asm.Instruction");
			style(SCE_ASM_NUMBER, "Asm.Number");
			style(SCE_ASM_STRING, "Asm.String");
			style(SCE_ASM_REGISTER, "Asm.Register");
			style(SCE_ASM_DIRECTIVE, "Asm.Directive");
			break;

		case LexLanguage::Xml:
		case LexLanguage::Html:
			style(SCE_H_TAG, "Xml.Tag");
			style(SCE_H_ATTRIBUTE, "Xml.Attribute");
			break;

		case LexLanguage::Inf:
			style((int)InfStyle::Comment, "Inf.Comment");
			style((int)InfStyle::Section, "Inf.Section");
			style((int)InfStyle::Key, "Inf.Key");
			style((int)InfStyle::Operator, "Inf.Operator");
			style((int)InfStyle::String, "Inf.String");
			style((int)InfStyle::StringKey, "Inf.StringKey");
			style((int)InfStyle::Number, "Inf.Number");
			style((int)InfStyle::RootKey, "Inf.RootKey");
			break;

		case LexLanguage::IL:
			style((int)IlStyle::Comment, "IL.Comment");
			style((int)IlStyle::String, "IL.String");
			style((int)IlStyle::Label, "IL.Label");
			style((int)IlStyle::Opcode, "IL.Opcode");
			style((int)IlStyle::Directive, "IL.Directive");
			style((int)IlStyle::Keyword, "IL.Keyword");
			style((int)IlStyle::Type, "IL.Type");
			style((int)IlStyle::Number, "IL.Number");
			break;

		case LexLanguage::C:
		case LexLanguage::Cpp:
		case LexLanguage::CSharp:
		case LexLanguage::Idl:
			// (IDL's GUIDs: an indicator over the lexer's colors)
			m_Sci.SendMessage(SCI_INDICSETSTYLE, IndicatorGuid, INDIC_TEXTFORE);
			m_Sci.SendMessage(SCI_INDICSETFORE, IndicatorGuid, CodeColors::Get("C.Guid", dark).Color);
			for (auto id : { SCE_C_COMMENT, SCE_C_COMMENTLINE, SCE_C_COMMENTDOC, SCE_C_COMMENTLINEDOC })
				style(id, "C.Comment");
			style(SCE_C_WORD, "C.Keyword");
			style(SCE_C_WORD2, "C.Type");
			style(SCE_C_NUMBER, "C.Number");
			style(SCE_C_STRING, "C.String");
			style(SCE_C_CHARACTER, "C.String");
			style(SCE_C_PREPROCESSOR, "C.Preprocessor");
			style(SCE_C_OPERATOR, "C.Operator");
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

	auto font = Fonts::Code();
	m_Sci.StyleSetFont(STYLE_DEFAULT, CW2A(font.lfFaceName, CP_UTF8));
	m_Sci.StyleSetSize(STYLE_DEFAULT, Fonts::Points(font));
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
	intptr_t pos = -1;
	if (x != -1 && y != -1) {
		CPoint pt(x, y);
		::ScreenToClient(m_Sci, &pt);
		pos = (intptr_t)m_Sci.SendMessage(SCI_POSITIONFROMPOINT, pt.x, pt.y);
	}
	else {
		// from the keyboard: at the caret
		pos = m_Sci.GetCurrentPos();
		CPoint pt((int)m_Sci.SendMessage(SCI_POINTXFROMPOSITION, 0, pos), (int)m_Sci.SendMessage(SCI_POINTYFROMPOSITION, 0, pos));
		::ClientToScreen(m_Sci, &pt);
		x = pt.x;
		y = pt.y;
	}
	if ((m_Language == LexLanguage::Asm || m_Language == LexLanguage::C || m_LineAction) && pos >= 0)
		m_ContextLine = (int)m_Sci.LineFromPosition(pos);
	m_ContextPos = pos;
	UpdateUI();
	CMenu menu;
	menu.LoadMenuW(IDR_CONTEXT);
	CMenuHandle sub = menu.GetSubMenu(m_Language == LexLanguage::Asm ? 6 : 0);

	// a function under the mouse: it can be decompiled or disassembled
	m_LinkAddress = 0;
	if (!m_Links.empty() && pos >= 0) {
		auto name = NameAt(pos);
		if (auto address = FunctionOf(name)) {
			m_LinkAddress = address;
			auto shown = name.size() > 60 ? name.substr(0, 57) + L"..." : name;
			for (auto& ch : shown)
				if (ch == L'&')
					ch = L'+';	// not a mnemonic
			sub.InsertMenu(0, MF_BYPOSITION | MF_SEPARATOR);
			sub.InsertMenu(0, MF_BYPOSITION | MF_STRING, ID_CODE_DISASSEMBLE_HERE, (L"Disassemble " + shown + L" in sa&me window").c_str());
			sub.InsertMenu(0, MF_BYPOSITION | MF_STRING, ID_CODE_DECOMPILE_HERE, (L"Decompile " + shown + L" in &same window").c_str());
			sub.InsertMenu(0, MF_BYPOSITION | MF_STRING, ID_CODE_DISASSEMBLE, (L"Dis&assemble " + shown).c_str());
			sub.InsertMenu(0, MF_BYPOSITION | MF_STRING, ID_CODE_DECOMPILE, (L"&Decompile " + shown).c_str());
		}
	}
	if (m_LineAction) {
		sub.InsertMenu(0, MF_BYPOSITION | MF_SEPARATOR);
		sub.InsertMenu(0, MF_BYPOSITION | MF_STRING, ID_CODE_GOTODEFINITION, L"&Go to Definition\tEnter");
	}
	// a function or type of Windows under the mouse: its declaration, its documentation (of a constant: those of its type)
	m_ContextApi.clear();
	m_ContextDocs.clear();
	if (m_Language == LexLanguage::C && pos >= 0) {
		auto name = NameAt(pos);
		auto api = std::string(CW2A(name.c_str()));
		std::wstring shown = name;
		if (!name.empty() && !Frame()->IsWin32Api(api)) {
			uint64_t value = 0;
			std::string type;
			if (!Frame()->ConstantNames(api, value, type).empty() && !type.empty() && Frame()->IsWin32Api(type)) {
				shown = std::wstring(CA2W(type.c_str()));
				api = type;
			}
			else
				api.clear();
		}
		if (!name.empty() && !api.empty()) {
			m_ContextApi = CA2W(api.c_str());
			// a constant whose type has no page: that of the function of Windows it is passed to (its page has its values)
			m_ContextDocs = m_ContextApi;
			std::wstring docs = shown;
			if (shown != name && !Frame()->IsWin32Documented(api))
				if (auto callee = CalleeAt(pos); !callee.empty() && Frame()->IsWin32Documented(std::string(CW2A(callee.c_str())))) {
					m_ContextDocs = callee;
					docs = callee + L" (" + name + L")";
				}
			sub.InsertMenu(0, MF_BYPOSITION | MF_SEPARATOR);
			sub.InsertMenu(0, MF_BYPOSITION | MF_STRING, ID_CODE_WIN32DOCS, (L"D&ocumentation of " + docs).c_str());
			sub.InsertMenu(0, MF_BYPOSITION | MF_STRING, ID_CODE_WIN32API, (L"Show " + shown + L" in &Windows API").c_str());
		}
	}
	// IDL: the declaration under the mouse (by its GUID) can be shown in the COM view
	m_ContextGuid.clear();
	if (m_Language == LexLanguage::Idl && pos >= 0)
		if (m_ContextGuid = GuidOfLine((int)m_Sci.LineFromPosition(pos)); !m_ContextGuid.empty()) {
			sub.InsertMenu(0, MF_BYPOSITION | MF_SEPARATOR);
			sub.InsertMenu(0, MF_BYPOSITION | MF_STRING, ID_IDL_SHOWINCOM, L"Show in &COM View");
		}
	// a constant of Windows whose value has other names: one of them can be shown
	m_ContextNames.clear();
	if (m_Language == LexLanguage::C && pos >= 0) {
		auto name = std::string(CW2A(NameAt(pos).c_str()));
		uint64_t value = 0;
		std::string type;
		auto names = name.empty() ? std::vector<std::string>() : Frame()->ConstantNames(name, value, type);
		if (names.size() > 1) {
			CMenu showAs;
			showAs.CreatePopupMenu();
			for (auto const& n : names)
				if (n != names[0] && m_ContextNames.size() <= ID_CODE_SHOWAS_LAST - ID_CODE_SHOWAS_FIRST) {
					showAs.AppendMenu(MF_STRING, ID_CODE_SHOWAS_FIRST + (UINT)m_ContextNames.size(), CA2W(n.c_str()));
					m_ContextNames.push_back(n);
				}
			sub.InsertMenu(0, MF_BYPOSITION | MF_SEPARATOR);
			sub.InsertMenu(0, MF_BYPOSITION | MF_POPUP, (UINT_PTR)showAs.Detach(),
				std::format(L"Show {} (0x{:X}) &as", std::wstring(CA2W(names[0].c_str())), value).c_str());
		}
	}
	// a variable (or a function) of the decompiled code under the mouse: it can be renamed, and a variable retyped
	m_ContextName.clear();
	if (m_Language == LexLanguage::C && m_Function && pos >= 0) {
		auto name = NameAt(pos);
		auto variable = VariableNamed(name);
		if (variable || FunctionOf(name)) {
			m_ContextName = name;
			auto shown = name.size() > 60 ? name.substr(0, 57) + L"..." : name;
			for (auto& ch : shown)
				if (ch == L'&')
					ch = L'+';
			sub.InsertMenu(0, MF_BYPOSITION | MF_SEPARATOR);
			if (variable)
				sub.InsertMenu(0, MF_BYPOSITION | MF_STRING, ID_CODE_RETYPE, (L"Change &Type of " + shown + L"...\tY").c_str());
			sub.InsertMenu(0, MF_BYPOSITION | MF_STRING, ID_CODE_RENAME, (L"&Rename " + shown + L"...\tN").c_str());
		}
	}
	// the line's bookmark and comment
	if (HasAnnotations() && m_ContextLine >= 0 && AddressOfLine(m_ContextLine)) {
		sub.AppendMenu(MF_SEPARATOR);
		sub.AppendMenu(MF_STRING, ID_CODE_BOOKMARK, Frame()->IsBookmarked(AddressOfLine(m_ContextLine)) ? L"Remove &Bookmark\tCtrl+B" : L"Add &Bookmark...\tCtrl+B");
		sub.AppendMenu(MF_STRING, ID_CODE_COMMENT, L"Co&mment...\t;");
	}
	// decompiled code: the flow graph and the call graph of the function under the mouse, or of the view's
	if (m_Language == LexLanguage::C && m_Function) {
		if (auto va = FlowGraphAddress()) {
			std::wstring of;
			if (*va != m_Function) {
				auto name = NameAt(pos);
				of = L" of " + (name.size() > 60 ? name.substr(0, 57) + L"..." : name);
				for (auto& ch : of)
					if (ch == L'&')
						ch = L'+';
			}
			sub.AppendMenu(MF_SEPARATOR);
			sub.AppendMenu(MF_STRING, ID_ASSEMBLY_FLOWGRAPH, (L"Function Flow &Graph" + of + L"\tG").c_str());
			sub.AppendMenu(MF_STRING, ID_ASSEMBLY_CALLGRAPH, (L"Function Ca&ll Graph" + of).c_str());
		}
	}
	// the function of the view: its disassembly and C side by side
	if (m_Function && !m_Notify && (m_Language == LexLanguage::Asm || m_Language == LexLanguage::C)) {
		sub.AppendMenu(MF_SEPARATOR);
		sub.AppendMenu(MF_STRING, ID_CODE_SIDEBYSIDE, m_Language == LexLanguage::Asm ? L"Side by Side with &C" : L"Side by Side with Disassembl&y");
	}
	auto result = Frame()->ShowContextMenu(sub, 0, x, y);
	return result;
}

std::string CScintillaView::GuidOfLine(int line) const {
	// (the attributes are before the declaration: from the line up, to the one that ends the previous declaration)
	static std::regex const guid(R"(uuid\(([0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12})\))");
	auto& sci = const_cast<CScintillaCtrl&>(m_Sci);
	for (int l = line; l >= 0 && l > line - 40; l--) {
		auto start = sci.PositionFromLine(l), end = (intptr_t)sci.SendMessage(SCI_GETLINEENDPOSITION, l);
		std::string text(end - start, '\0');
		Sci_TextRangeFull range{ { start, end }, text.data() };
		if (end > start)
			sci.SendMessage(SCI_GETTEXTRANGEFULL, 0, (LPARAM)&range);
		text.resize(strlen(text.c_str()));
		std::smatch m;
		if (std::regex_search(text, m, guid)) {
			GUID value;
			if (FAILED(::CLSIDFromString(CA2W(("{" + m[1].str() + "}").c_str()), &value)))
				return {};
			return std::string((char const*)&value, 16);
		}
		if (l < line && text.find('}') != std::string::npos)
			break;
	}
	return {};
}

LRESULT CScintillaView::OnShowInCom(WORD, WORD, HWND, BOOL&) {
	// (of the context menu's line, else of the caret's)
	auto guid = m_ContextGuid;
	m_ContextGuid.clear();
	if (guid.empty() && m_Language == LexLanguage::Idl)
		guid = GuidOfLine((int)m_Sci.LineFromPosition(m_Sci.GetCurrentPos()));
	if (!guid.empty() && !Frame()->ShowInComView(guid))
		AtlMessageBox(m_hWnd, L"The COM view has nothing of this GUID: the code neither serves nor passes it.", IDR_MAINFRAME, MB_ICONINFORMATION);
	return 0;
}

bool CScintillaView::ShowDeclaration(std::vector<std::string> const& keywords, std::string const& name) {
	auto length = m_Sci.GetTextLength();
	std::string text(length + 1, '\0');
	m_Sci.GetText(length + 1, text.data());
	text.resize(length);
	for (auto const& keyword : keywords) {
		std::regex declaration("(^|\n)[ \t]*" + keyword + "[ \t]+(" + name + R"()\b)");
		std::smatch m;
		if (std::regex_search(text, m, declaration)) {
			auto at = (intptr_t)m.position(2);
			auto line = m_Sci.LineFromPosition(at);
			m_Sci.SendMessage(SCI_SETFIRSTVISIBLELINE, std::max<intptr_t>(0, line - 8));
			m_Sci.SendMessage(SCI_SETSEL, at, at + (intptr_t)name.size());
			m_Sci.SetFocus();
			return true;
		}
	}
	return false;
}

void CScintillaView::SetFunctionLinks(std::unordered_map<std::wstring, uint64_t> links) {
	m_Links = std::move(links);
}

void CScintillaView::SetFunctionAddress(uint64_t address) {
	m_Function = address;
}

void CScintillaView::SetVariables(GhidraDecompiler::Variables variables) {
	m_Variables = std::move(variables);
}

void CScintillaView::SetLineAddresses(GhidraDecompiler::LineAddresses lines) {
	m_LineAddresses = std::move(lines);
	MarkBookmarks();
}

// code of the image: a disassembly, or the C of one of its functions
bool CScintillaView::HasAnnotations() const {
	return m_Language == LexLanguage::Asm ? !m_Lines.empty() : m_Language == LexLanguage::C && m_Function != 0;
}

uint64_t CScintillaView::AddressOfLine(int line) const {
	if (m_Language == LexLanguage::Asm) {
		auto info = GetLine(InstructionLine(line));
		return info ? info->Va : 0;
	}
	if (m_Language != LexLanguage::C || !m_Function)
		return 0;
	if (line >= 0 && line < (int)m_LineAddresses.size() && !m_LineAddresses[line].empty())
		return *std::ranges::min_element(m_LineAddresses[line]);
	return m_Function;
}

void CScintillaView::MarkBookmarks() {
	if (!m_Sci.m_hWnd)
		return;
	m_Sci.SendMessage(SCI_MARKERDELETEALL, MarkerBookmark);
	if (!HasAnnotations() || Frame()->GetAnnotations().Bookmarks.empty())
		return;
	if (m_Language == LexLanguage::Asm) {
		for (auto const& [va, line] : m_LineOfVa)
			if (Frame()->IsBookmarked(va))
				m_Sci.SendMessage(SCI_MARKERADD, line, MarkerBookmark);
		return;
	}
	// a bookmark at an instruction of a line of the C: on the first such line, a statement rather than the comment before it
	auto commentOnly = [&](int line) {
		auto text = m_Sci.GetLine(line);
		auto first = text.find_first_not_of(" \t");
		return first != std::string::npos && text.compare(first, 2, "/*") == 0;
	};
	std::unordered_set<uint64_t> marked;
	for (bool comments : { false, true })
		for (int line = 0; line < (int)m_LineAddresses.size(); line++)
			for (auto va : m_LineAddresses[line])
				if (!marked.contains(va) && commentOnly(line) == comments && Frame()->IsBookmarked(va)) {
					marked.insert(va);
					m_Sci.SendMessage(SCI_MARKERADD, line, MarkerBookmark);
				}
}

void CScintillaView::AnnotationsChanged() {
	if (m_Language == LexLanguage::Asm)
		XrefsChanged();		// (disassembled again: with the comments)
	MarkBookmarks();
}

LRESULT CScintillaView::OnAnnotate(WORD, WORD id, HWND, BOOL&) {
	auto line = (id == ID_CODE_BOOKMARK || id == ID_CODE_COMMENT) && m_ContextLine >= 0 ? m_ContextLine : CurrentLine();
	auto va = HasAnnotations() ? AddressOfLine(line) : 0;
	bool done = false;
	if (va)
		done = id == ID_NAV_COMMENT || id == ID_CODE_COMMENT ? Frame()->EditComment(va) : Frame()->ToggleBookmark(va);
	if (!done)
		::MessageBeep(MB_ICONWARNING);
	return 0;
}

// the next (or previous) line with a bookmark, around the end
LRESULT CScintillaView::OnNextBookmark(WORD, WORD id, HWND, BOOL&) {
	auto mask = 1 << MarkerBookmark;
	auto lines = (int)m_Sci.SendMessage(SCI_GETLINECOUNT);
	auto current = CurrentLine();
	int line = id == ID_HEX_BOOKMARK_NEXT ? (int)m_Sci.SendMessage(SCI_MARKERNEXT, current + 1, mask) : (int)m_Sci.SendMessage(SCI_MARKERPREVIOUS, current - 1, mask);
	if (line < 0)
		line = id == ID_HEX_BOOKMARK_NEXT ? (int)m_Sci.SendMessage(SCI_MARKERNEXT, 0, mask) : (int)m_Sci.SendMessage(SCI_MARKERPREVIOUS, lines - 1, mask);
	if (line < 0) {
		::MessageBeep(MB_ICONWARNING);
		return 0;
	}
	ShowLine(line);
	m_Sci.GotoPos(m_Sci.PositionFromLine(line));
	return 0;
}

GhidraDecompiler::Variable const* CScintillaView::VariableNamed(std::wstring const& name) const {
	if (name.empty())
		return nullptr;
	auto narrow = std::string(CW2A(name.c_str()));
	for (auto const& v : m_Variables)
		if (v.Name == narrow)
			return &v;
	return nullptr;
}

LRESULT CScintillaView::OnShowAs(WORD, WORD id, HWND, BOOL&) {
	auto index = id - ID_CODE_SHOWAS_FIRST;
	if (index < m_ContextNames.size())
		Frame()->PreferConstantName(m_ContextNames[index]);
	return 0;
}

// The names of the value of a constant of Windows the mouse rests on (the others it has)
LRESULT CScintillaView::OnDwell(int, LPNMHDR hdr, BOOL&) {
	auto scn = (SCNotification*)hdr;
	if (hdr->code == SCN_DWELLEND) {
		m_Sci.SendMessage(SCI_CALLTIPCANCEL);
		return 0;
	}
	if (m_Language != LexLanguage::C || scn->position < 0)
		return 0;
	auto name = std::string(CW2A(NameAt(scn->position).c_str()));
	uint64_t value = 0;
	std::string type;
	auto names = name.empty() ? std::vector<std::string>() : Frame()->ConstantNames(name, value, type);
	if (names.empty())
		return 0;
	auto tip = std::format("{} = 0x{:X} ({})", name, value, type);
	if (names.size() > 1) {
		std::string others;
		for (auto const& n : names)
			if (n != name)
				others += (others.empty() ? "" : ", ") + n;
		tip += "\nAlso: " + others + "\n(the context menu shows another of them)";
	}
	m_Sci.SendMessage(SCI_CALLTIPSHOW, scn->position, (LPARAM)tip.c_str());
	return 0;
}

// The function whose call has the text at a position among its arguments: the name before the "(" that is open there (on
// its line, or the lines of the statement before it); empty if there is none
std::wstring CScintillaView::CalleeAt(intptr_t pos) {
	auto line = m_Sci.LineFromPosition(pos);
	auto start = m_Sci.PositionFromLine(std::max<intptr_t>(0, line - 8));
	auto charAt = [&](intptr_t at) { return (char)m_Sci.SendMessage(SCI_GETCHARAT, at); };
	int depth = 0;
	for (auto at = pos - 1; at >= start; at--) {
		auto c = (char)charAt(at);
		if (c == ';' || c == '{' || c == '}')
			break;
		if (c == ')')
			depth++;
		else if (c == '(' && depth-- == 0) {
			auto end = at;
			while (end > start && charAt(end - 1) == ' ')
				end--;
			auto begin = end;
			while (begin > start && (isalnum((unsigned char)charAt(begin - 1)) || charAt(begin - 1) == '_'))
				begin--;
			if (begin == end)
				return {};
			std::string name;
			for (auto i = begin; i < end; i++)
				name += (char)charAt(i);
			return std::wstring(CA2W(name.c_str()));
		}
	}
	return {};
}

LRESULT CScintillaView::OnWin32Api(WORD, WORD id, HWND, BOOL&) {
	auto const& name = id == ID_CODE_WIN32DOCS ? m_ContextDocs : m_ContextApi;
	if (name.empty() || !Frame()->ShowWin32Api(std::string(CW2A(name.c_str())), id == ID_CODE_WIN32DOCS))
		::MessageBeep(MB_ICONWARNING);
	return 0;
}

LRESULT CScintillaView::OnRename(WORD, WORD id, HWND, BOOL&) {
	if (!Rename(m_ContextName, id == ID_CODE_RETYPE))
		::MessageBeep(MB_ICONWARNING);
	return 0;
}

namespace {
	// a C identifier; of a function also a C++ name ("ns::Class::Method")
	bool IsIdentifier(std::wstring const& text, bool qualified) {
		if (text.empty() || iswdigit(text[0]))
			return false;
		for (size_t i = 0; i < text.size(); i++) {
			auto ch = text[i];
			if (ch < 128 && (iswalnum(ch) || ch == L'_'))
				continue;
			if (qualified && ch == L':' && i + 1 < text.size() && text[i + 1] == L':' && i > 0) {
				i++;
				continue;
			}
			if (qualified && ch == L'~')
				continue;
			return false;
		}
		return true;
	}
}

bool CScintillaView::Rename(std::wstring const& name, bool type) {
	auto variable = VariableNamed(name);
	auto function = variable ? 0 : FunctionOf(name);
	if (!variable && (!function || type))
		return false;
	// (copies: the variables are replaced when the code is decompiled again)
	auto variables = m_Variables;
	std::wstring text;
	if (type) {
		text = std::wstring(CA2W(variable->Type.c_str()));
		auto hint = L"In C: \"int\", \"char *\", \"BYTE[16]\", \"MY_STRUCT *\" (a type of the symbols). Empty: the type the decompiler finds.";
		CInputDlg dlg(L"Change Type", (L"&Type of " + name + L":").c_str(), hint, text, [](std::wstring const& t) {
			return t.find_first_of(L"(){};,=") != std::wstring::npos ? std::wstring(L"That is not a type") : std::wstring();
			});
		if (dlg.DoModal() != IDOK || text == std::wstring(CA2W(variable->Type.c_str())))
			return true;
		auto copy = *variable;
		return Frame()->ChangeVariable(m_Function, copy, std::nullopt, std::string(CW2A(text.c_str())), variables);
	}
	text = name;
	auto hint = variable ? L"Empty: the name (and type) the decompiler gives it." : L"Empty: the name the function had.";
	CInputDlg dlg(L"Rename", variable ? L"&Name of the variable:" : L"&Name of the function:", hint, text, [&](std::wstring const& t) {
		if (t.empty())
			return std::wstring();
		if (!IsIdentifier(t, !variable))
			return std::wstring(L"A name is made of letters, digits and '_', and does not start with a digit");
		if (variable && t != name && VariableNamed(t))
			return L"Another variable is named " + t;
		return std::wstring();
		});
	if (dlg.DoModal() != IDOK || text == name)
		return true;
	if (variable) {
		auto copy = *variable;
		return Frame()->ChangeVariable(m_Function, copy, std::string(CW2A(text.c_str())), std::nullopt, variables);
	}
	return Frame()->RenameFunction(function, text);
}

uint64_t CScintillaView::LineAddress(int line) const {
	auto info = GetLine(line);
	return info ? info->Va : 0;
}

int CScintillaView::LineOfAddress(uint64_t va) const {
	auto it = m_LineOfVa.find(va);
	return it == m_LineOfVa.end() ? -1 : it->second;
}

void CScintillaView::MarkLines(std::vector<int> const& lines) {
	m_Sci.SendMessage(SCI_MARKERDELETEALL, MarkerRelated);
	for (auto line : lines)
		m_Sci.SendMessage(SCI_MARKERADD, line, MarkerRelated);
}

void CScintillaView::SetNotify(Notify notify) {
	m_Notify = std::move(notify);
}

LRESULT CScintillaView::OnSciNotify(int, LPNMHDR hdr, BOOL& handled) {
	if (m_Notify)
		m_Notify(*this, *(SCNotification*)hdr);
	handled = FALSE;
	return 0;
}

LRESULT CScintillaView::OnSideBySide(WORD, WORD, HWND, BOOL&) {
	if (m_Function)
		Frame()->ShowSideBySide(m_Function);
	return 0;
}

// A name around a position: letters, digits and the characters of C++ names (scopes, destructors, decorated parts),
// with balanced template arguments ("std::min<unsigned __int64>")
std::wstring CScintillaView::NameAt(intptr_t pos) const {
	auto line = m_Sci.LineFromPosition(pos);
	auto text = m_Sci.GetLine(line);
	auto index = (size_t)(pos - m_Sci.PositionFromLine(line));
	if (index > text.size())
		return {};
	auto isName = [](char ch) {
		return isalnum((unsigned char)ch) || ch == '_' || ch == ':' || ch == '~' || ch == '$' || ch == '@' || ch == '`' || ch == '\'';
	};
	// from a '<' (or '>') to the one that closes (opens) it
	auto match = [&](size_t at, int step) -> size_t {
		int depth = 0;
		for (auto i = (intptr_t)at; i >= 0 && i < (intptr_t)text.size(); i += step) {
			if (text[i] == '<')
				depth += step;
			else if (text[i] == '>')
				depth -= step;
			if (depth == 0)
				return (size_t)i;
		}
		return std::string::npos;
	};
	// inside template arguments ("std::pair<int, char>" at "char"): from the name they belong to (the outermost)
	for (auto i = (intptr_t)index - 1, depth = (intptr_t)0; i >= 0; i--) {
		auto ch = text[i];
		if (ch == '>')
			depth++;
		else if (ch == '<') {
			if (depth > 0)
				depth--;
			else if (i > 0 && isName(text[i - 1]))
				index = (size_t)i - 1;		// the name before the arguments
			else
				break;
		}
		else if (depth == 0 && (ch == '(' || ch == ')' || ch == ';' || ch == '=' || ch == '{' || ch == '}'))
			break;
	}
	size_t start = index, end = index;
	while (start > 0) {
		if (isName(text[start - 1]))
			start--;
		else if (text[start - 1] == '>' && (start = match(start - 1, -1)) != std::string::npos)
			continue;
		else
			break;
		if (start == std::string::npos)
			return {};
	}
	while (end < text.size()) {
		if (isName(text[end]))
			end++;
		else if (text[end] == '<') {
			auto close = match(end, 1);
			if (close == std::string::npos)
				break;
			end = close + 1;
		}
		else
			break;
	}
	if (start == std::string::npos || end <= start)
		return {};
	return (PCWSTR)CA2W(text.substr(start, end - start).c_str(), CP_UTF8);
}

uint64_t CScintillaView::FunctionOf(std::wstring const& name) const {
	if (name.empty())
		return 0;
	if (auto it = m_Links.find(name); it != m_Links.end())
		return it->second;
	// a function without a name: its address is in it
	if (name.starts_with(L"func_0x"))
		return wcstoull(name.c_str() + 7, nullptr, 16);
	return 0;
}

// In a view of its own, or in place of this one ("in same window": this view is destroyed then, so nothing of it is used
// after the frame is called)
LRESULT CScintillaView::OnCodeLink(WORD, WORD id, HWND, BOOL&) {
	auto address = m_LinkAddress;
	auto frame = Frame();
	if (!address)
		return 0;
	switch (id) {
		case ID_CODE_DECOMPILE: frame->DecompileFunction(address); break;
		case ID_CODE_DISASSEMBLE: frame->DisassembleFunction(address); break;
		case ID_CODE_DECOMPILE_HERE: frame->ReplaceView(m_hWnd, [=] { return frame->DecompileFunction(address); }); break;
		case ID_CODE_DISASSEMBLE_HERE: frame->ReplaceView(m_hWnd, [=] { return frame->DisassembleFunction(address); }); break;
	}
	return 0;
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

// Editing, when the text is not read only (the frame's accelerators send these: the keys do not reach the control)
LRESULT CScintillaView::OnEdit(WORD, WORD id, HWND, BOOL&) {
	if (m_Sci.IsReadOnly())
		return 0;
	switch (id) {
		case ID_EDIT_CUT: m_Sci.Cut(); break;
		case ID_EDIT_PASTE: m_Sci.Paste(); break;
		case ID_EDIT_UNDO: m_Sci.Undo(); break;
		case ID_EDIT_REDO: m_Sci.Redo(); break;
	}
	return 0;
}

LRESULT CScintillaView::OnEditCopy(WORD, WORD, HWND, BOOL&) {
	m_Sci.Copy();
	return 0;
}


