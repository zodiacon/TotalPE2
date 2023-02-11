// View.cpp : implementation of the CView class
//
/////////////////////////////////////////////////////////////////////////////

#include "pch.h"
#include "resource.h"
#include "ScintillaView.h"
#include "SciLexer.h"
#include "LexerModule.h"
#include <ThemeHelper.h>
#include <Theme.h>
#include "..\External\Capstone\capstone.h"
#include "PEStrings.h"
#include "PEFile.h"

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

extern Lexilla::LexerModule lmXML;
extern Lexilla::LexerModule lmAsm;

CScintillaView::CScintillaView(IMainFrame* frame, PEFile const& pe, PCWSTR title) : CViewBase(frame), m_PE(pe), m_Title(title) {
}

CString CScintillaView::GetTitle() const {
	return m_Title;
}

CScintillaCtrl& CScintillaView::GetCtrl() {
	return m_Sci;
}

void CScintillaView::UpdateUI(bool first) {
	auto& ui = Frame()->GetUI();
	ui.UIEnable(ID_EDIT_COPY, !m_Sci.IsSelectionEmpty());
	auto text = m_Sci.GetSelText();
	auto address = text.starts_with("0x");
	ui.UIEnable(ID_ASSEMBLY_GOTOADDRESS, address);
	ui.UIEnable(ID_ASSEMBLY_DISASSEMBLEATTHEEND, address);
	ui.UIEnable(ID_ASSEMBLY_DISASSEMBLEINANEWTAB, address);
}

bool CScintillaView::SetAsmCode(std::span<const std::byte> code, uint64_t address, bool is32Bit) {
	m_Is32Bit = is32Bit;
	csh handle;
	if (cs_open(CS_ARCH_X86, is32Bit ? CS_MODE_32 : CS_MODE_64, &handle) != CS_ERR_OK)
		return false;
	auto bytes = (const uint8_t*)code.data();
	auto size = code.size();
	cs_insn inst{};
	CStringA text;
	while (cs_disasm_iter(handle, &bytes, &size, &address, &inst)) {
		text += PEStrings::FormatInstruction(inst, Frame()->GetSymbols()) + L"\r\n";
		if (_strcmpi(inst.mnemonic, "ret") == 0)
			break;
	}

	m_Sci.SetText(text);
	cs_close(&handle);

	return true;
}

void CScintillaView::SetText(PCWSTR text) {
	m_Sci.SetText(CStringA(text));
}

void CScintillaView::SetText(PCSTR text) {
	m_Sci.SetText(text);
}

void CScintillaView::SetLanguage(LexLanguage lang) {
	m_Language = lang;
	switch (lang) {
		case LexLanguage::Asm:
		{
			auto lexer = lmAsm.Create();
			for(int i = 0; i < _countof(KeyWords_ASM); i++)
				lexer->WordListSet(i, KeyWords_ASM[i]);
			m_Sci.SetLexer(lexer);
			break;
		}

		case LexLanguage::Xml:
			m_Sci.SetLexer(lmXML.Create());
			break;
	}
	UpdateColors();
}

void CScintillaView::UpdateColors() {
	// TEMPORARY
	auto dark = !ThemeHelper::IsDefault();
	auto theme = ThemeHelper::GetCurrentTheme();

	m_Sci.StyleSetFore(STYLE_DEFAULT, theme->TextColor);
	m_Sci.StyleSetBack(STYLE_DEFAULT, theme->BackColor);
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
			m_Sci.StyleSetFore(SCE_H_TAG, dark ? RGB(0, 128, 255) : RGB(0, 0, 240));
			m_Sci.StyleSetFore(SCE_H_ATTRIBUTE, dark ? RGB(240, 128, 128) : RGB(128, 0, 0));
			break;
	}
}

LRESULT CScintillaView::OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
	m_Sci.Focus();
	m_Sci.SetFocus();

	return 0;
}

LRESULT CScintillaView::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	m_hWndClient = m_Sci.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN);

	m_Sci.StyleSetFont(STYLE_DEFAULT, "Consolas");
	m_Sci.StyleSetSize(STYLE_DEFAULT, 11);
	m_Sci.UsePopup(SC_POPUP_NEVER);

	return 0;
}

LRESULT CScintillaView::OnUpdateTheme(UINT, WPARAM, LPARAM, BOOL&) {
	UpdateColors();
	return 0;
}

LRESULT CScintillaView::OnContextMenu(UINT, WPARAM, LPARAM lp, BOOL&) {
	UpdateUI();
	CMenu menu;
	menu.LoadMenuW(IDR_CONTEXT);
	return Frame()->TrackPopupMenu(menu.GetSubMenu(m_Language == LexLanguage::Xml ? 0 : 6), 0, 
		GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
}

LRESULT CScintillaView::OnGoToAddress(WORD, WORD, HWND, BOOL&) {
	auto text = m_Sci.GetSelText();

	auto pos = m_Sci.FindTextW(Scintilla::FindOption::WordStart | Scintilla::FindOption::WholeWord, text.substr(2).c_str());
	if (pos >= 0)
		m_Sci.GotoPos(pos);
	else
		AtlMessageBox(m_hWnd, L"Address not found", IDR_MAINFRAME, MB_ICONWARNING);
	return 0;
}

LRESULT CScintillaView::OnDisassembleNewTab(WORD, WORD, HWND, BOOL&) {
	auto selection = m_Sci.GetSelText();
	auto address = strtoull(selection.substr(2).c_str(), nullptr, 16);

	return 0;
}

LRESULT CScintillaView::OnDisassembleAtEnd(WORD, WORD, HWND, BOOL&) {
	auto selection = m_Sci.GetSelText();
	auto address = strtoull(selection.substr(2).c_str(), nullptr, 16);
	csh handle;
	if (cs_open(CS_ARCH_X86, m_Is32Bit ? CS_MODE_32 : CS_MODE_64, &handle) != CS_ERR_OK)
		return false;

	auto bytes = (const uint8_t*)m_PE.GetData() + m_PE->GetOffsetFromVA(address);
	size_t size = 0x1000;
	cs_insn inst{};
	CStringA text;
	while (cs_disasm_iter(handle, &bytes, &size, &address, &inst)) {
		text += PEStrings::FormatInstruction(inst, Frame()->GetSymbols()) + L"\r\n";
		if (_strcmpi(inst.mnemonic, "ret") == 0)
			break;
	}
	cs_close(&handle);

	m_Sci.SetReadOnly(false);
	m_Sci.AppendText(text.GetLength() + 1, "\n" + text);
	m_Sci.SetReadOnly(true);

	return 0;
}

