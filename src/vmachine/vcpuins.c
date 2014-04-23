/* This file is a part of NXVM project. */

#include "stdio.h"
#include "memory.h"

#include "vapi.h"

#include "vport.h"
#include "vram.h"
#include "vcpu.h"
#include "vpic.h"
#include "bios/qdbios.h"
#include "vcpuins.h"

#include "debug/aasm.h"
#include "debug/dasm.h"

#define MOD ((modrm&0xc0)>>6)
#define REG ((modrm&0x38)>>3)
#define RM  ((modrm&0x07)>>0)

#define ADD_FLAG  (VCPU_EFLAGS_OF | VCPU_EFLAGS_SF | VCPU_EFLAGS_ZF | \
                   VCPU_EFLAGS_AF | VCPU_EFLAGS_CF | VCPU_EFLAGS_PF)
#define	 OR_FLAG  (VCPU_EFLAGS_SF | VCPU_EFLAGS_ZF | VCPU_EFLAGS_PF)
#define ADC_FLAG  (VCPU_EFLAGS_OF | VCPU_EFLAGS_SF | VCPU_EFLAGS_ZF | \
                   VCPU_EFLAGS_AF | VCPU_EFLAGS_CF | VCPU_EFLAGS_PF)
#define SBB_FLAG  (VCPU_EFLAGS_OF | VCPU_EFLAGS_SF | VCPU_EFLAGS_ZF | \
                   VCPU_EFLAGS_AF | VCPU_EFLAGS_CF | VCPU_EFLAGS_PF)
#define AND_FLAG  (VCPU_EFLAGS_SF | VCPU_EFLAGS_ZF | VCPU_EFLAGS_PF)
#define SUB_FLAG  (VCPU_EFLAGS_OF | VCPU_EFLAGS_SF | VCPU_EFLAGS_ZF | \
                   VCPU_EFLAGS_AF | VCPU_EFLAGS_CF | VCPU_EFLAGS_PF)
#define XOR_FLAG  (VCPU_EFLAGS_SF | VCPU_EFLAGS_ZF | VCPU_EFLAGS_PF)
#define CMP_FLAG  (VCPU_EFLAGS_OF | VCPU_EFLAGS_SF | VCPU_EFLAGS_ZF | \
                   VCPU_EFLAGS_AF | VCPU_EFLAGS_CF | VCPU_EFLAGS_PF)
#define INC_FLAG  (VCPU_EFLAGS_OF | VCPU_EFLAGS_SF | VCPU_EFLAGS_ZF | \
                   VCPU_EFLAGS_AF | VCPU_EFLAGS_PF)
#define DEC_FLAG  (VCPU_EFLAGS_OF | VCPU_EFLAGS_SF | VCPU_EFLAGS_ZF | \
                   VCPU_EFLAGS_AF | VCPU_EFLAGS_PF)
#define TEST_FLAG (VCPU_EFLAGS_SF | VCPU_EFLAGS_ZF | VCPU_EFLAGS_PF)
#define SHL_FLAG  (VCPU_EFLAGS_SF | VCPU_EFLAGS_ZF | VCPU_EFLAGS_PF)
#define SHR_FLAG  (VCPU_EFLAGS_SF | VCPU_EFLAGS_ZF | VCPU_EFLAGS_PF)
#define SAL_FLAG  (VCPU_EFLAGS_SF | VCPU_EFLAGS_ZF | VCPU_EFLAGS_PF)
#define SAR_FLAG  (VCPU_EFLAGS_SF | VCPU_EFLAGS_ZF | VCPU_EFLAGS_PF)
#define AAM_FLAG  (VCPU_EFLAGS_SF | VCPU_EFLAGS_ZF | VCPU_EFLAGS_PF)
#define AAD_FLAG  (VCPU_EFLAGS_SF | VCPU_EFLAGS_ZF | VCPU_EFLAGS_PF)
#define DAA_FLAG  (VCPU_EFLAGS_SF | VCPU_EFLAGS_ZF | VCPU_EFLAGS_PF)
#define DAS_FLAG  (VCPU_EFLAGS_SF | VCPU_EFLAGS_ZF | VCPU_EFLAGS_PF)

t_cpuins vcpuins;

#define GetCPL         (_GetSegSel_RPL(vcpu.cs.selector))
#define GetOperandSize ((vcpu.cs.d_b ^ vcpuins.prefix_oprsize) ? 32 : 16)
#define GetAddressSize ((vcpu.cs.d_b ^ vcpuins.prefix_addrsize) ? 32 : 16)

#define bugfix if

#define NOTIMP void
#define ASMCMP void
#define EXCEPT void

static void CaseError(const char *str)
{
	vapiPrint("The NXVM CPU has encountered an internal case error: \n%s\n",str);
	vapiCallBackMachineStop();
}

static t_nubit32 GetPhysicalFromLinear(t_nubit32 linear, t_nubit8 cpl, t_bool write)
{
	/* possible exception: pf */
	t_nubit32 physical = linear;
	t_vaddrcc ppdtitem, ppetitem; /* page table entries */
	if (_GetCR0_PG) {
		ppdtitem = vramAddr(_GetCR3_BASE + _GetLinear_DIR(linear) * 4);
		if (!_GetPageEntry_P(d_nubit32(ppdtitem))) {
			/* #PF(excode) */
			SetBit(vcpuins.except, VCPUINS_EXCEPT_PF);
			vcpuins.excode = _MakePageFaultErrorCode(0, write, (cpl == 3));
			return physical;
		}
		if (cpl == 0x03) {
			if (!_GetPageEntry_US(d_nubit32(ppdtitem))) {
				/* #PF(excode) */
				SetBit(vcpuins.except, VCPUINS_EXCEPT_PF);
				vcpuins.excode = _MakePageFaultErrorCode(1, write, 1);
				return physical;
			}
			if (!_GetPageEntry_RW(d_nubit32(ppdtitem)) && write) {
				/* #PF(excode) */
				SetBit(vcpuins.except, VCPUINS_EXCEPT_PF);
				vcpuins.excode = _MakePageFaultErrorCode(1, 1, 1);
				return physical;
			}
		}
		_SetPageEntry_A(d_nubit32(ppdtitem));
		ppetitem = vramAddr(_GetPageEntry_BASE(d_nubit32(ppdtitem)) + _GetLinear_PAGE(linear) * 4);
		if (!_GetPageEntry_P(d_nubit32(ppetitem))) {
			/* #PF(excode) */
			SetBit(vcpuins.except, VCPUINS_EXCEPT_PF);
			vcpuins.excode = _MakePageFaultErrorCode(0, write, (cpl == 3));
			return physical;
		}
		if (cpl == 0x03) {
			if (!_GetPageEntry_US(d_nubit32(ppetitem))) {
				/* #PF(excode) */
				SetBit(vcpuins.except, VCPUINS_EXCEPT_PF);
				vcpuins.excode = _MakePageFaultErrorCode(1, write, 1);
				return physical;
			}
			if (!_GetPageEntry_RW(d_nubit32(ppetitem)) && write) {
				/* #PF(excode) */
				SetBit(vcpuins.except, VCPUINS_EXCEPT_PF);
				vcpuins.excode = _MakePageFaultErrorCode(1, 1, 1);
				return physical;
			}
		}
		_SetPageEntry_A(d_nubit32(ppetitem));
		if (write) _SetPageEntry_D(d_nubit32(ppetitem));
		physical = _GetPageEntry_BASE(d_nubit32(ppetitem)) + _GetLinear_OFFSET(linear);
	}
	return physical;
}
static t_nubit64 GetDescriptorFromGDT(t_nubit16 selector)
{
	/* possible exception: gp/limit; pf/lost */
	t_nubit64 descriptor = 0x0000000000000000;
	t_nubit32 linear = _GetGDTR_BASE + _GetSelector_OFFSET(selector);
	if (_GetSelector_OFFSET(selector) + 7 > _GetGDTR_LIMIT) {
		/* #GP(selector) */
		SetBit(vcpuins.except, VCPUINS_EXCEPT_GP);
		vcpuins.excode = selector;
		return descriptor;
	}
	descriptor = vramQWord(GetPhysicalFromLinear(linear, 0x00, 0x00));
	return descriptor;
}
static t_nubit64 GetDescriptorFromLDT(t_nubit16 selector)
{
	/* possible exception: gp/limit; pf/lost */
	t_nubit64 descriptor = 0x0000000000000000;
	t_nubit32 linear = vcpu.ldtr.base + _GetSelector_OFFSET(selector);
	if (linear + 7 > vcpu.ldtr.limit) {
		/* #GP(selector) */
		SetBit(vcpuins.except, VCPUINS_EXCEPT_GP);
		vcpuins.excode = selector;
		return descriptor;
	}
	descriptor = vramQWord(GetPhysicalFromLinear(linear, 0x00, 0x00));
	return descriptor;
}
static t_nubit64 GetSegmentDescriptor(t_nubit16 selector)
{
	/* possible exception: gp/limit; pf/lost */
	t_nubit64 descriptor = 0x0000000000000000;
	if (_GetCR0_PE) {
		/* protected mode */
		if (_GetSelector_TI(selector))
			if (_GetSelector_INDEX(selector))
				descriptor = GetDescriptorFromLDT(selector);
			else
				CaseError("GetSegmentDescriptor::selector(null)");
		else
			descriptor = GetDescriptorFromGDT(selector);
	} else
		CaseError("GetSegmentDescriptor::CR0_PE(0)");
	return descriptor;
}
//static void LoadCS(t_nubit16 selector)
//{
//	t_nubit64 descriptor = 0x0000000000000000;
//	/* check for protected mode */
//	if (!_GetCR0_PE) {
//		vcpu.cs.base = (selector << 4);
//		return;
//	}
//	/* check for v86 mode */
//	if (_GetVM) {
//		vcpu.cs.base = (selector << 4);
//		return;
//	}
//	/* check for null selector */
//	if (_IsSelectorNull(selector)) {
//		/* #GP(0) */
//		SetBit(vcpuins.except, VCPUINS_EXCEPT_GP);
//		vcpuins.excode = 0;
//		return;
//	}
//	descriptor = GetSegmentDescriptor(selector);
//	if (vcpuins.except) return;
///*#define VCPU_SEGDESC_BASE_0  0x000000ffffff0000
//#define VCPU_SEGDESC_BASE_1  0xff00000000000000
//#define VCPU_SEGDESC_LIMIT_0 0x000000000000ffff
//#define VCPU_SEGDESC_LIMIT_1 0x000f000000000000
//#define VCPU_SEGDESC_TYPE_A  0x0000010000000000 * descriptor type: accessed *
//#define VCPU_SEGDESC_TYPE_R  0x0000020000000000 DISCARD!
//#define VCPU_SEGDESC_TYPE_C  0x0000040000000000 * descriptor type: conforming  (code) *
//#define VCPU_SEGDESC_TYPE_CD 0x0000080000000000 DONE!
//#define VCPU_SEGDESC_S       0x0000100000000000 DONE!
//#define VCPU_SEGDESC_DPL     0x0000600000000000 * descriptor previlege level*
//#define VCPU_SEGDESC_P       0x0000800000000000 DONE!
//#define VCPU_SEGDESC_AVL     0x0010000000000000 DISCARD!
//#define VCPU_SEGDESC_DB      0x0040000000000000 * default size / b *
//#define VCPU_SEGDESC_G       0x0080000000000000 * granularity */
//	/* check for non-system descriptor */
//	if (!_GetSegDesc_S(descriptor)) {
//		/* #GP(selector) */
//		SetBit(vcpuins.except, VCPUINS_EXCEPT_GP);
//		vcpuins.excode = selector;
//		return;
//	}
//	/* check for executable segment */
//	if (!_GetSegDesc_TYPE_CD(descriptor)) {
//		/* #GP(selector) */
//		SetBit(vcpuins.except, VCPUINS_EXCEPT_GP);
//		vcpuins.excode = selector;
//		return;
//	}
//	/* check for presence */
//	if (!_GetSegDesc_P(descriptor)) {
//		/* #NP(selector) */
//		SetBit(vcpuins.except, VCPUINS_EXCEPT_NP);
//		vcpuins.excode = selector;
//		return;
//	}
//}

static t_cpu acpu;
static t_nubit8 ub1,ub2,ub3;
static t_nsbit8 sb1,sb2,sb3;
static t_nubit16 uw1,uw2,uw3;
static t_nsbit16 sw1,sw2,sw3;
#ifdef VCPUASM

#define AFLAGS0   (VCPU_EFLAGS_OF | VCPU_EFLAGS_SF | VCPU_EFLAGS_ZF | \
                   VCPU_EFLAGS_AF | VCPU_EFLAGS_CF | VCPU_EFLAGS_PF | \
                   VCPU_EFLAGS_DF | VCPU_EFLAGS_TF | VCPU_EFLAGS_IF)
#define AFLAGS1   (VCPU_EFLAGS_OF | VCPU_EFLAGS_SF | VCPU_EFLAGS_ZF | \
                   VCPU_EFLAGS_AF | VCPU_EFLAGS_CF | VCPU_EFLAGS_PF | \
                   VCPU_EFLAGS_DF | VCPU_EFLAGS_TF)
#define AFLAGS2   (VCPU_EFLAGS_SF | VCPU_EFLAGS_ZF | \
                   VCPU_EFLAGS_AF | VCPU_EFLAGS_CF | VCPU_EFLAGS_PF | \
                   VCPU_EFLAGS_DF | VCPU_EFLAGS_TF)

static void acpuPrintRegs(t_cpu *xcpu)
{
	if (xcpu == &vcpu) vapiPrint("VCPU REGISTERS\n");
	else               vapiPrint("ACPU REGISTERS\n");
	vapiPrint(  "AX=%04X", xcpu->ax);
	vapiPrint("  BX=%04X", xcpu->bx);
	vapiPrint("  CX=%04X", xcpu->cx);
	vapiPrint("  DX=%04X", xcpu->dx);
	vapiPrint("  SP=%04X", xcpu->sp);
	vapiPrint("  BP=%04X", xcpu->bp);
	vapiPrint("  SI=%04X", xcpu->si);
	vapiPrint("  DI=%04X", xcpu->di);
	vapiPrint("\nDS=%04X", xcpu->ds);
	vapiPrint("  ES=%04X", xcpu->es);
	vapiPrint("  SS=%04X", xcpu->ss);
	vapiPrint("  CS=%04X", xcpu->cs);
	vapiPrint("  IP=%04X", xcpu->ip);
	vapiPrint("   ");
	if(xcpu->flags & VCPU_EFLAGS_OF) vapiPrint("OV ");
	else                           vapiPrint("NV ");
	if(xcpu->flags & VCPU_EFLAGS_DF) vapiPrint("DN ");
	else                           vapiPrint("UP ");
	if(xcpu->flags & VCPU_EFLAGS_IF) vapiPrint("E_ ");
	else                           vapiPrint("D_ ");
	if(xcpu->flags & VCPU_EFLAGS_SF) vapiPrint("NG ");
	else                           vapiPrint("PL ");
	if(xcpu->flags & VCPU_EFLAGS_ZF) vapiPrint("ZR ");
	else                           vapiPrint("NZ ");
	if(xcpu->flags & VCPU_EFLAGS_AF) vapiPrint("AC ");
	else                           vapiPrint("NA ");
	if(xcpu->flags & VCPU_EFLAGS_PF) vapiPrint("PE ");
	else                           vapiPrint("PO ");
	if(xcpu->flags & VCPU_EFLAGS_CF) vapiPrint("CY ");
	else                           vapiPrint("NC ");
	vapiPrint("\n");
}
static t_bool acpuCheck(t_nubit16 flags)
{
	t_bool flagdiff = 0x00;
	if (acpu.ax != vcpu.ax)       {vapiPrint("diff ax\n");flagdiff = 0x01;}
	if (acpu.bx != vcpu.bx)       {vapiPrint("diff bx\n");flagdiff = 0x01;}
	if (acpu.cx != vcpu.cx)       {vapiPrint("diff cx\n");flagdiff = 0x01;}
	if (acpu.dx != vcpu.dx)       {vapiPrint("diff dx\n");flagdiff = 0x01;}
	if (acpu.sp != vcpu.sp)       {vapiPrint("diff sp\n");flagdiff = 0x01;}
	if (acpu.bp != vcpu.bp)       {vapiPrint("diff bp\n");flagdiff = 0x01;}
	if (acpu.si != vcpu.si)       {vapiPrint("diff si\n");flagdiff = 0x01;}
	if (acpu.di != vcpu.di)       {vapiPrint("diff di\n");flagdiff = 0x01;}
	if (acpu.ip != vcpu.eip)       {vapiPrint("diff ip\n");flagdiff = 0x01;}
	if (acpu.cs != vcpu.cs.selector)       {vapiPrint("diff cs\n");flagdiff = 0x01;}
	if (acpu.ds != vcpu.ds.selector)       {vapiPrint("diff ds\n");flagdiff = 0x01;}
	if (acpu.es != vcpu.es.selector)       {vapiPrint("diff es\n");flagdiff = 0x01;}
	if (acpu.ss != vcpu.ss.selector)       {vapiPrint("diff ss\n");flagdiff = 0x01;}
	if ((acpu.flags & flags) != (vcpu.eflags & flags))
		                          {vapiPrint("diff fg\n");flagdiff = 0x01;}
	if (flagdiff) {
		acpuPrintRegs(&acpu);
		acpuPrintRegs(&vcpu);
		vapiCallBackMachineStop();
	}
	return flagdiff;
}
#define async    if(acpu = vcpu,1)
#define aexec    __asm
#define acheck(f)   if(acpuCheck(f))
#define async8   if(1) {\
	acpu = vcpu;ub1 = d_nubit8(dest);ub2 = d_nubit8(src);} else
#define async12  if(1) {\
	acpu = vcpu;uw1 = d_nubit16(dest);uw2 = d_nsbit8(src);} else
#define async16  if(1) {\
	acpu = vcpu;uw1 = d_nubit16(dest);uw2 = d_nubit16(src);} else
#define asreg8   if(!vramIsAddrInMem(dest)) {\
	d_nubit8((t_vaddrcc)dest - (t_vaddrcc)&vcpu + (t_vaddrcc)&acpu) = ub3;} else
#define asreg16  if(!vramIsAddrInMem(dest)) {\
	d_nubit16((t_vaddrcc)dest - (t_vaddrcc)&vcpu + (t_vaddrcc)&acpu) = uw3;} else
#define asreg12  asreg16
#define acheck8(f)  if(acpuCheck(f) || d_nubit8(dest) != ub3) {\
	vapiPrint("ub1=%02X,ub2=%02X,ub3=%02X,dest=%02X\n",\
		ub1,ub2,ub3,d_nubit8(dest));} else
#define acheck16(f) if(acpuCheck(f) || d_nubit16(dest) != uw3) {\
	vapiPrint("uw1=%04X,uw2=%04X,uw3=%04X,dest=%04X\n",\
		uw1,uw2,uw3,d_nubit16(dest));} else
#define acheck12(f) acheck16(f)
#define aexec8  if(1) {\
	__asm pushfd\
	__asm push eax\
	__asm push acpu.flags\
	__asm popf\
	__asm mov al,ub1\
	__asm op  al,ub2\
	__asm mov ub3,al\
	__asm pushf\
	__asm pop acpu.flags\
	__asm pop eax\
	__asm popfd} else
#define aexec16  if(1) {\
	__asm pushfd\
	__asm push eax\
	__asm push acpu.flags\
	__asm popf\
	__asm mov ax,uw1\
	__asm op  ax,uw2\
	__asm mov uw3,ax\
	__asm pushf\
	__asm pop acpu.flags\
	__asm pop eax\
	__asm popfd} else
#define aexec12 aexec16
#define asyncall switch(len) {\
	case 8:  async8; break;\
	case 12: async12;break;\
	case 16: async16;break;}
#define aexecall switch(len) {\
	case 8: aexec8; break;\
	case 12:aexec12;break;\
	case 16:aexec16;break;}
#define asregall switch(len) {\
	case 8: asreg8; break;\
	case 12:asreg12;break;\
	case 16:asreg16;break;}
#define acheckall(f) switch(len) {\
	case 8: acheck8(f); break;\
	case 12:acheck12(f);break;\
	case 16:acheck16(f);break;}

#define aexec_csf if(1) {\
	__asm pushfd\
	__asm push acpu.flags\
	__asm popf\
	__asm op\
	__asm pushf\
	__asm pop acpu.flags\
	__asm popfd} else
#define aexec_idc8 if(1) {\
	__asm pushfd\
	__asm push eax\
	__asm push acpu.flags\
	__asm popf\
	__asm mov al,ub1\
	__asm op al\
	__asm mov ub3,al\
	__asm pushf\
	__asm pop acpu.flags\
	__asm pop eax\
	__asm popfd} else
#define aexec_idc16 if(1) {\
	__asm pushfd\
	__asm push eax\
	__asm push acpu.flags\
	__asm popf\
	__asm mov ax,uw1\
	__asm op ax\
	__asm mov uw3,ax\
	__asm pushf\
	__asm pop acpu.flags\
	__asm pop eax\
	__asm popfd} else
#define aexec_srd8 if(1) {\
	__asm pushfd\
	__asm push eax\
	__asm push ecx\
	__asm push acpu.flags\
	__asm popf\
	__asm mov al,ub1\
	__asm mov cl,ub2\
	__asm op al,cl\
	__asm mov ub3,al\
	__asm pushf\
	__asm pop acpu.flags\
	__asm pop ecx\
	__asm pop eax\
	__asm popfd} else
#define aexec_srd16 if(1) {\
	__asm pushfd\
	__asm push eax\
	__asm push ecx\
	__asm push acpu.flags\
	__asm popf\
	__asm mov ax,uw1\
	__asm mov cl,ub2\
	__asm op ax,cl\
	__asm mov uw3,ax\
	__asm pushf\
	__asm pop acpu.flags\
	__asm pop ecx\
	__asm pop eax\
	__asm popfd} else
#define aexec_cax8 if(1) {\
	__asm pushfd\
	__asm push eax\
	__asm mov al, acpu.al\
	__asm push acpu.flags\
	__asm popf\
	__asm op\
	__asm pushf\
	__asm pop acpu.flags\
	__asm mov acpu.al, al\
	__asm pop eax\
	__asm popfd} else
#define aexec_cax16 if(1) {\
	__asm pushfd\
	__asm push eax\
	__asm push edx\
	__asm mov ax, acpu.ax\
	__asm mov dx, acpu.dx\
	__asm push acpu.flags\
	__asm popf\
	__asm op\
	__asm pushf\
	__asm pop acpu.flags\
	__asm mov acpu.dx, dx\
	__asm mov acpu.ax, ax\
	__asm pop edx\
	__asm pop eax\
	__asm popfd} else
#define aexec_nxx8 if(1) {\
	__asm pushfd\
	__asm push eax\
	__asm mov al, ub1\
	__asm push acpu.flags\
	__asm popf\
	__asm op al\
	__asm pushf\
	__asm pop acpu.flags\
	__asm mov ub3, al\
	__asm pop eax\
	__asm popfd} else
#define aexec_nxx16 if(1) {\
	__asm pushfd\
	__asm push eax\
	__asm mov ax, uw1\
	__asm push acpu.flags\
	__asm popf\
	__asm op ax\
	__asm pushf\
	__asm pop acpu.flags\
	__asm mov uw3, ax\
	__asm pop eax\
	__asm popfd} else
#define aexec_mdx8 if(1) {\
	__asm pushfd\
	__asm push eax\
	__asm mov ax, acpu.ax\
	__asm push acpu.flags\
	__asm popf\
	__asm op ub2\
	__asm pushf\
	__asm pop acpu.flags\
	__asm mov acpu.ax, ax\
	__asm pop eax\
	__asm popfd} else
#define aexec_mdx16 if(1) {\
	__asm pushfd\
	__asm push eax\
	__asm mov ax, acpu.ax\
	__asm mov dx, acpu.dx\
	__asm push acpu.flags\
	__asm popf\
	__asm op uw2\
	__asm pushf\
	__asm pop acpu.flags\
	__asm mov acpu.dx, dx\
	__asm mov acpu.ax, ax\
	__asm pop eax\
	__asm popfd} else
#else
#define async     if(0)
#define aexec     __asm
#define acheck(f) if(0)
#define async8    if(0)
#define async12   if(0)
#define async16   if(0)
#define asreg8    if(0)
#define asreg12   if(0)
#define asreg16   if(0)
#define acheck8(f)  if(0)
#define acheck12(f) if(0)
#define acheck16(f) if(0)
#define aexec8    if(0)
#define aexec12   if(0)
#define aexec16   if(0)
#define asyncall
#define aexecall
#define asregall
#define acheckall(f)
#define aexec_csf   if(0)
#define aexec_idc8  if(0)
#define aexec_idc16 if(0)
#define aexec_srd8  if(0)
#define aexec_srd16 if(0)
#define aexec_cax8  if(0)
#define aexec_cax16 if(0)
#define aexec_nxx8  if(0)
#define aexec_nxx16 if(0)
#define aexec_mdx8  if(0)
#define aexec_mdx16 if(0)
#endif

static void CalcCF()
{
	switch(vcpuins.type) {
	case ADD8:
	case ADD16:
		MakeBit(vcpu.eflags,VCPU_EFLAGS_CF,(vcpuins.result < vcpuins.opr1) || (vcpuins.result < vcpuins.opr2));
		break;
	case ADC8:
		bugfix(21)
			MakeBit(vcpu.eflags, VCPU_EFLAGS_CF, (
				(_GetCF && vcpuins.opr2 == 0xff) ?
				1 : ((vcpuins.result < vcpuins.opr1) || (vcpuins.result < vcpuins.opr2))));
		else
			bugfix(1) MakeBit(vcpu.eflags,VCPU_EFLAGS_CF,vcpuins.result < vcpuins.opr1);
			else MakeBit(vcpu.eflags,VCPU_EFLAGS_CF,vcpuins.result <= vcpuins.opr1);
		break;
	case ADC16:
		bugfix(21)
			MakeBit(vcpu.eflags, VCPU_EFLAGS_CF, (
				(_GetCF && vcpuins.opr2 == 0xffff) ?
				1 : ((vcpuins.result < vcpuins.opr1) || (vcpuins.result < vcpuins.opr2))));
		else
			bugfix(1) MakeBit(vcpu.eflags,VCPU_EFLAGS_CF,vcpuins.result < vcpuins.opr1);
			else MakeBit(vcpu.eflags,VCPU_EFLAGS_CF,vcpuins.result <= vcpuins.opr1);
		break;
	case SBB8:
		bugfix(20)
			MakeBit(vcpu.eflags, VCPU_EFLAGS_CF, (vcpuins.opr1 < vcpuins.result) ||
				(_GetCF && (vcpuins.opr2 == 0xff)));
		else
			MakeBit(vcpu.eflags, VCPU_EFLAGS_CF, (vcpuins.opr1 < vcpuins.result) ||
				(vcpuins.opr2 == 0xff));
		break;
	case SBB16:
		bugfix(20)
			MakeBit(vcpu.eflags, VCPU_EFLAGS_CF, (vcpuins.opr1 < vcpuins.result) ||
				(_GetCF && (vcpuins.opr2 == 0xffff)));
		else
			MakeBit(vcpu.eflags, VCPU_EFLAGS_CF, (vcpuins.opr1 < vcpuins.result) ||
				(vcpuins.opr2 == 0xffff));
		break;
	case SUB8:
	case SUB16:
	case CMP8:
	case CMP16:
		MakeBit(vcpu.eflags,VCPU_EFLAGS_CF,vcpuins.opr1 < vcpuins.opr2);
		break;
	default:CaseError("CalcCF::vcpuins.type");break;}
}
static void CalcOF()
{
	switch(vcpuins.type) {
	case ADD8:
	case ADC8:
		MakeBit(vcpu.eflags,VCPU_EFLAGS_OF,((vcpuins.opr1&0x0080) == (vcpuins.opr2&0x0080)) && ((vcpuins.opr1&0x0080) != (vcpuins.result&0x0080)));
		break;
	case ADD16:
	case ADC16:
		MakeBit(vcpu.eflags,VCPU_EFLAGS_OF,((vcpuins.opr1&0x8000) == (vcpuins.opr2&0x8000)) && ((vcpuins.opr1&0x8000) != (vcpuins.result&0x8000)));
		break;
	case SBB8:
	case SUB8:
	case CMP8:
		MakeBit(vcpu.eflags,VCPU_EFLAGS_OF,((vcpuins.opr1&0x0080) != (vcpuins.opr2&0x0080)) && ((vcpuins.opr2&0x0080) == (vcpuins.result&0x0080)));
		break;
	case SBB16:
	case SUB16:
	case CMP16:
		MakeBit(vcpu.eflags,VCPU_EFLAGS_OF,((vcpuins.opr1&0x8000) != (vcpuins.opr2&0x8000)) && ((vcpuins.opr2&0x8000) == (vcpuins.result&0x8000)));
		break;
	default:CaseError("CalcOF::vcpuins.type");break;}
}
static void CalcAF()
{
	MakeBit(vcpu.eflags,VCPU_EFLAGS_AF,((vcpuins.opr1^vcpuins.opr2)^vcpuins.result)&0x10);
}
static void CalcPF()
{
	t_nubit8 res8 = vcpuins.result & 0xff;
	t_nubitcc count = 0;
	while(res8)
	{
		res8 &= res8-1; 
		count++;
	}
	MakeBit(vcpu.eflags,VCPU_EFLAGS_PF,!(count&0x01));
}
static void CalcZF()
{
	MakeBit(vcpu.eflags,VCPU_EFLAGS_ZF,!vcpuins.result);
}
static void CalcSF()
{
	switch(vcpuins.bit) {
	case 8:	MakeBit(vcpu.eflags,VCPU_EFLAGS_SF,!!(vcpuins.result&0x80));break;
	case 16:MakeBit(vcpu.eflags,VCPU_EFLAGS_SF,!!(vcpuins.result&0x8000));break;
	default:CaseError("CalcSF::vcpuins.bit");break;}
}
static void CalcTF() {}
static void CalcIF() {}
static void CalcDF() {}

static void SetFlags(t_nubit16 flags)
{
	if(flags & VCPU_EFLAGS_CF) CalcCF();
	if(flags & VCPU_EFLAGS_PF) CalcPF();
	if(flags & VCPU_EFLAGS_AF) CalcAF();
	if(flags & VCPU_EFLAGS_ZF) CalcZF();
	if(flags & VCPU_EFLAGS_SF) CalcSF();
	if(flags & VCPU_EFLAGS_TF) CalcTF();
	if(flags & VCPU_EFLAGS_IF) CalcIF();
	if(flags & VCPU_EFLAGS_DF) CalcDF();
	if(flags & VCPU_EFLAGS_OF) CalcOF();
}
static void GetMem()
{
	/* returns vcpuins.rm */
	vcpuins.rm = vramGetRealAddress(vcpuins.overds.selector,vramVarWord(vcpu.cs.selector,vcpu.eip));
	vcpu.eip += 2;
}
static void GetImm(t_nubitcc immbit)
{
	// returns vcpuins.imm
	vcpuins.imm = vramGetRealAddress(vcpu.cs.selector,vcpu.eip);
	switch(immbit) {
	case 8:  vcpu.eip += 1;break;
	case 16: vcpu.eip += 2;break;
	case 32: vcpu.eip += 4;break;
	default: CaseError("GetImm::immbit");break;}
}
static void GetModRegRM(t_nubitcc regbit,t_nubitcc rmbit)
{
	// returns vcpuins.rm and vcpuins.r
	t_nsbit8 disp8 = 0x00;
	t_nubit16 disp16 = 0x0000;
	t_nubit8 modrm = vramVarByte(vcpu.cs.selector,vcpu.eip++);
	vcpuins.rm = vcpuins.r = (t_vaddrcc)NULL;
	switch(MOD) {
	case 0:
		switch(RM) {
		case 0:	vcpuins.rm = vramGetRealAddress(vcpuins.overds.selector,vcpu.bx+vcpu.si);break;
		case 1:	vcpuins.rm = vramGetRealAddress(vcpuins.overds.selector,vcpu.bx+vcpu.di);break;
		case 2:	vcpuins.rm = vramGetRealAddress(vcpuins.overss.selector,vcpu.bp+vcpu.si);break;
		case 3:	vcpuins.rm = vramGetRealAddress(vcpuins.overss.selector,vcpu.bp+vcpu.di);break;
		case 4:	vcpuins.rm = vramGetRealAddress(vcpuins.overds.selector,vcpu.si);break;
		case 5:	vcpuins.rm = vramGetRealAddress(vcpuins.overds.selector,vcpu.di);break;
		case 6: disp16 = vramVarWord(vcpu.cs.selector,vcpu.eip);vcpu.eip += 2;
			    vcpuins.rm = vramGetRealAddress(vcpuins.overds.selector,disp16); break;
		case 7:	vcpuins.rm = vramGetRealAddress(vcpuins.overds.selector,vcpu.bx);break;
		default:CaseError("GetModRegRM::MOD0::RM");break;}
		break;
	case 1:
		bugfix(23) {disp8 = (t_nsbit8)vramVarByte(vcpu.cs.selector,vcpu.eip);vcpu.eip += 1;}
		switch(RM) {
		case 0:	vcpuins.rm = vramGetRealAddress(vcpuins.overds.selector,vcpu.bx+vcpu.si+disp8);break;
		case 1:	vcpuins.rm = vramGetRealAddress(vcpuins.overds.selector,vcpu.bx+vcpu.di+disp8);break;
		case 2:	vcpuins.rm = vramGetRealAddress(vcpuins.overss.selector,vcpu.bp+vcpu.si+disp8);break;
		case 3:	vcpuins.rm = vramGetRealAddress(vcpuins.overss.selector,vcpu.bp+vcpu.di+disp8);break;
		case 4:	vcpuins.rm = vramGetRealAddress(vcpuins.overds.selector,vcpu.si+disp8);break;
		case 5:	vcpuins.rm = vramGetRealAddress(vcpuins.overds.selector,vcpu.di+disp8);break;
		case 6:	vcpuins.rm = vramGetRealAddress(vcpuins.overss.selector,vcpu.bp+disp8);break;
		case 7:	vcpuins.rm = vramGetRealAddress(vcpuins.overds.selector,vcpu.bx+disp8);break;
		default:CaseError("GetModRegRM::MOD1::RM");break;}
		bugfix(23) ;
		else {
			bugfix(3) vcpuins.rm += (t_nsbit8)vramVarByte(vcpu.cs.selector,vcpu.eip);
			else vcpuins.rm += vramVarByte(vcpu.cs.selector,vcpu.eip);
			vcpu.eip += 1;
		}
		break;
	case 2:
		bugfix(23) {disp16 = vramVarWord(vcpu.cs.selector,vcpu.eip);vcpu.eip += 2;}
		switch(RM) {
		case 0:	vcpuins.rm = vramGetRealAddress(vcpuins.overds.selector,vcpu.bx+vcpu.si+disp16);break;
		case 1:	vcpuins.rm = vramGetRealAddress(vcpuins.overds.selector,vcpu.bx+vcpu.di+disp16);break;
		case 2:	vcpuins.rm = vramGetRealAddress(vcpuins.overss.selector,vcpu.bp+vcpu.si+disp16);break;
		case 3:	vcpuins.rm = vramGetRealAddress(vcpuins.overss.selector,vcpu.bp+vcpu.di+disp16);break;
		case 4:	vcpuins.rm = vramGetRealAddress(vcpuins.overds.selector,vcpu.si+disp16);break;
		case 5:	vcpuins.rm = vramGetRealAddress(vcpuins.overds.selector,vcpu.di+disp16);break;
		case 6:	vcpuins.rm = vramGetRealAddress(vcpuins.overss.selector,vcpu.bp+disp16);break;
		case 7:	vcpuins.rm = vramGetRealAddress(vcpuins.overds.selector,vcpu.bx+disp16);break;
		default:CaseError("GetModRegRM::MOD2::RM");break;}
		bugfix(23) ;
		else {vcpuins.rm += vramVarWord(vcpu.cs.selector,vcpu.eip);vcpu.eip += 2;}
		break;
	case 3:
		switch(RM) {
		case 0:	if(rmbit == 8) vcpuins.rm = (t_vaddrcc)(&vcpu.al); else vcpuins.rm = (t_vaddrcc)(&vcpu.ax); break;
		case 1:	if(rmbit == 8) vcpuins.rm = (t_vaddrcc)(&vcpu.cl); else vcpuins.rm = (t_vaddrcc)(&vcpu.cx); break;
		case 2:	if(rmbit == 8) vcpuins.rm = (t_vaddrcc)(&vcpu.dl); else vcpuins.rm = (t_vaddrcc)(&vcpu.dx); break;
		case 3:	if(rmbit == 8) vcpuins.rm = (t_vaddrcc)(&vcpu.bl); else vcpuins.rm = (t_vaddrcc)(&vcpu.bx); break;
		case 4:	if(rmbit == 8) vcpuins.rm = (t_vaddrcc)(&vcpu.ah); else vcpuins.rm = (t_vaddrcc)(&vcpu.sp); break;
		case 5:	if(rmbit == 8) vcpuins.rm = (t_vaddrcc)(&vcpu.ch); else vcpuins.rm = (t_vaddrcc)(&vcpu.bp); break;
		case 6:	if(rmbit == 8) vcpuins.rm = (t_vaddrcc)(&vcpu.dh); else vcpuins.rm = (t_vaddrcc)(&vcpu.si); break;
		case 7:	if(rmbit == 8) vcpuins.rm = (t_vaddrcc)(&vcpu.bh); else vcpuins.rm = (t_vaddrcc)(&vcpu.di); break;
		default:CaseError("GetModRegRM::MOD3::RM");break;}
		break;
	default:CaseError("GetModRegRM::MOD");break;}
	switch(regbit) {
	case 0:		vcpuins.r = REG;					break;
	case 4:
		switch(REG) {
		case 0:	vcpuins.r = (t_vaddrcc)(&vcpu.es.selector);	break;
		case 1:	vcpuins.r = (t_vaddrcc)(&vcpu.cs.selector);	break;
		case 2:	vcpuins.r = (t_vaddrcc)(&vcpu.ss.selector);	break;
		case 3:	vcpuins.r = (t_vaddrcc)(&vcpu.ds.selector);	break;
		case 4:	vcpuins.r = (t_vaddrcc)(&vcpu.fs.selector);	break;
		case 5:	vcpuins.r = (t_vaddrcc)(&vcpu.gs.selector);	break;
		default:CaseError("GetModRegRM::regbit4::REG");break;}
		break;
	case 8:
		switch(REG) {
		case 0:	vcpuins.r = (t_vaddrcc)(&vcpu.al);	break;
		case 1:	vcpuins.r = (t_vaddrcc)(&vcpu.cl);	break;
		case 2:	vcpuins.r = (t_vaddrcc)(&vcpu.dl);	break;
		case 3:	vcpuins.r = (t_vaddrcc)(&vcpu.bl);	break;
		case 4:	vcpuins.r = (t_vaddrcc)(&vcpu.ah);	break;
		case 5:	vcpuins.r = (t_vaddrcc)(&vcpu.ch);	break;
		case 6:	vcpuins.r = (t_vaddrcc)(&vcpu.dh);	break;
		case 7:	vcpuins.r = (t_vaddrcc)(&vcpu.bh);	break;
		default:CaseError("GetModRegRM::regbit8::REG");break;}
		break;
	case 16:
		switch(REG) {
		case 0: vcpuins.r = (t_vaddrcc)(&vcpu.ax);	break;
		case 1:	vcpuins.r = (t_vaddrcc)(&vcpu.cx);	break;
		case 2:	vcpuins.r = (t_vaddrcc)(&vcpu.dx);	break;
		case 3:	vcpuins.r = (t_vaddrcc)(&vcpu.bx);	break;
		case 4:	vcpuins.r = (t_vaddrcc)(&vcpu.sp);	break;
		case 5:	vcpuins.r = (t_vaddrcc)(&vcpu.bp);	break;
		case 6:	vcpuins.r = (t_vaddrcc)(&vcpu.si);	break;
		case 7: vcpuins.r = (t_vaddrcc)(&vcpu.di);	break;
		default:CaseError("GetModRegRM::regbit16::REG");break;}
		break;
	default:CaseError("GetModRegRM::regbit");break;}
}
static void GetModRegRMEA()
{
	t_nsbit8 disp8 = 0x00;
	t_nubit16 disp16 = 0x0000;
	t_nubit8 modrm = vramVarByte(vcpu.cs.selector,vcpu.eip++);
	vcpuins.rm = vcpuins.r = (t_vaddrcc)NULL;
	switch(MOD) {
	case 0:
		switch(RM) {
		case 0:	vcpuins.rm = vcpu.bx+vcpu.si;break;
		case 1:	vcpuins.rm = vcpu.bx+vcpu.di;break;
		case 2:	vcpuins.rm = vcpu.bp+vcpu.si;break;
		case 3:	vcpuins.rm = vcpu.bp+vcpu.di;break;
		case 4:	vcpuins.rm = vcpu.si;break;
		case 5:	vcpuins.rm = vcpu.di;break;
		case 6:	vcpuins.rm = vramVarWord(vcpu.cs.selector,vcpu.eip);vcpu.eip += 2;break;
		case 7:	vcpuins.rm = vcpu.bx;break;
		default:CaseError("GetModRegRMEA::MOD0::RM");break;}
		break;
	case 1:
		bugfix(23) {disp8 = (t_nsbit8)vramVarByte(vcpu.cs.selector,vcpu.eip);vcpu.eip += 1;}
		switch(RM) {
		case 0:	vcpuins.rm = vcpu.bx+vcpu.si+disp8;break;
		case 1:	vcpuins.rm = vcpu.bx+vcpu.di+disp8;break;
		case 2:	vcpuins.rm = vcpu.bp+vcpu.si+disp8;break;
		case 3:	vcpuins.rm = vcpu.bp+vcpu.di+disp8;break;
		case 4:	vcpuins.rm = vcpu.si+disp8;break;
		case 5:	vcpuins.rm = vcpu.di+disp8;break;
		case 6:	vcpuins.rm = vcpu.bp+disp8;break;
		case 7:	vcpuins.rm = vcpu.bx+disp8;break;
		default:CaseError("GetModRegRMEA::MOD1::RM");break;}
		bugfix(23) {vcpuins.rm %= 0x10000;}
		else {
			bugfix(3) vcpuins.rm += (t_nsbit8)vramVarByte(vcpu.cs.selector,vcpu.eip);
			else vcpuins.rm += vramVarByte(vcpu.cs.selector,vcpu.eip);
			vcpu.eip += 1;
		}
		break;
	case 2:
		bugfix(23) {disp16 = vramVarWord(vcpu.cs.selector,vcpu.eip);vcpu.eip += 2;}
		switch(RM) {
		case 0:	vcpuins.rm = vcpu.bx+vcpu.si+disp16;break;
		case 1:	vcpuins.rm = vcpu.bx+vcpu.di+disp16;break;
		case 2:	vcpuins.rm = vcpu.bp+vcpu.si+disp16;break;
		case 3:	vcpuins.rm = vcpu.bp+vcpu.di+disp16;break;
		case 4:	vcpuins.rm = vcpu.si+disp16;break;
		case 5:	vcpuins.rm = vcpu.di+disp16;break;
		case 6:
			bugfix(14) vcpuins.rm = vcpu.bp+disp16;
			else vcpuins.rm = vramGetRealAddress(vcpuins.overss.selector,vcpu.bp);
			break;
		case 7:	vcpuins.rm = vcpu.bx+disp16;break;
		default:CaseError("GetModRegRMEA::MOD2::RM");break;}
		bugfix(23) {vcpuins.rm %= 0x10000;}
		else {vcpuins.rm += vramVarWord(vcpu.cs.selector,vcpu.eip);vcpu.eip += 2;}
		break;
	default:CaseError("GetModRegRMEA::MOD");break;}
	switch(REG) {
	case 0: vcpuins.r = (t_vaddrcc)(&vcpu.ax);	break;
	case 1:	vcpuins.r = (t_vaddrcc)(&vcpu.cx);	break;
	case 2:	vcpuins.r = (t_vaddrcc)(&vcpu.dx);	break;
	case 3:	vcpuins.r = (t_vaddrcc)(&vcpu.bx);	break;
	case 4:	vcpuins.r = (t_vaddrcc)(&vcpu.sp);	break;
	case 5:	vcpuins.r = (t_vaddrcc)(&vcpu.bp);	break;
	case 6:	vcpuins.r = (t_vaddrcc)(&vcpu.si);	break;
	case 7: vcpuins.r = (t_vaddrcc)(&vcpu.di);	break;
	default:CaseError("GetModRegRMEA::REG");break;}
}

static void INT(t_nubit8 intid);
static ASMCMP ADD(void *dest, void *src, t_nubit8 len)
{
	asyncall
	switch(len) {
	case 8:
		vcpuins.bit = 8;
		vcpuins.type = ADD8;
		vcpuins.opr1 = d_nubit8(dest) & 0xff;
		vcpuins.opr2 = d_nubit8(src) & 0xff;
		vcpuins.result = (vcpuins.opr1+vcpuins.opr2) & 0xff;
		d_nubit8(dest) = (t_nubit8)vcpuins.result;
		bugfix(6) break;
		else ;
	case 12:
		vcpuins.bit = 16;
		vcpuins.type = ADD16;
		vcpuins.opr1 = d_nubit16(dest) & 0xffff;
		bugfix(22) vcpuins.opr2 = d_nsbit8(src) & 0xffff;
		else vcpuins.opr2 = d_nsbit8(src); /* in this case opr2 could be 0xffffffff */
		vcpuins.result = (vcpuins.opr1+vcpuins.opr2) & 0xffff;
		d_nubit16(dest) = (t_nubit16)vcpuins.result;
		break;
	case 16:
		vcpuins.bit = 16;
		vcpuins.type = ADD16;
		vcpuins.opr1 = d_nubit16(dest) & 0xffff;
		vcpuins.opr2 = d_nubit16(src) & 0xffff;
		vcpuins.result = (vcpuins.opr1+vcpuins.opr2) & 0xffff;
		d_nubit16(dest) = (t_nubit16)vcpuins.result;
		break;
	default:CaseError("ADD::len");break;}
	SetFlags(ADD_FLAG);
#define op add
	aexecall
#undef op
	asregall
	acheckall(AFLAGS1)
}
static ASMCMP OR(void *dest, void *src, t_nubit8 len)
{
	asyncall
	switch(len) {
	case 8:
		vcpuins.bit = 8;
		//vcpuins.type = OR8;
		vcpuins.opr1 = d_nubit8(dest) & 0xff;
		vcpuins.opr2 = d_nubit8(src) & 0xff;
		vcpuins.result = (vcpuins.opr1|vcpuins.opr2) & 0xff;
		d_nubit8(dest) = (t_nubit8)vcpuins.result;
		break;
	case 12:
		vcpuins.bit = 16;
		//vcpuins.type = OR16;
		vcpuins.opr1 = d_nubit16(dest) & 0xffff;
		vcpuins.opr2 = d_nsbit8(src) & 0xffff;
		vcpuins.result = (vcpuins.opr1|vcpuins.opr2) & 0xffff;
		d_nubit16(dest) = (t_nubit16)vcpuins.result;
		break;
	case 16:
		vcpuins.bit = 16;
		//vcpuins.type = OR16;
		vcpuins.opr1 = d_nubit16(dest) & 0xffff;
		vcpuins.opr2 = d_nubit16(src) & 0xffff;
		vcpuins.result = (vcpuins.opr1|vcpuins.opr2) & 0xffff;
		d_nubit16(dest) = (t_nubit16)vcpuins.result;
		break;
	default:CaseError("OR::len");break;}
	_ClrOF;
	_ClrCF;
	_ClrAF;
	SetFlags(OR_FLAG);
#define op or
	aexecall
#undef op
	asregall
	acheckall(AFLAGS1)
}
static ASMCMP ADC(void *dest, void *src, t_nubit8 len)
{
	asyncall
	switch(len) {
	case 8:
		vcpuins.bit = 8;
		vcpuins.type = ADC8;
		vcpuins.opr1 = d_nubit8(dest) & 0xff;
		vcpuins.opr2 = d_nubit8(src) & 0xff;
		vcpuins.result = (vcpuins.opr1+vcpuins.opr2+_GetCF) & 0xff;
		d_nubit8(dest) = (t_nubit8)vcpuins.result;
		break;
	case 12:
		vcpuins.bit = 16;
		vcpuins.type = ADC16;
		vcpuins.opr1 = d_nubit16(dest) & 0xffff;
		vcpuins.opr2 = d_nsbit8(src) & 0xffff;
		vcpuins.result = (vcpuins.opr1+vcpuins.opr2+_GetCF) & 0xffff;
		d_nubit16(dest) = (t_nubit16)vcpuins.result;
		break;
	case 16:
		vcpuins.bit = 16;
		vcpuins.type = ADC16;
		vcpuins.opr1 = d_nubit16(dest) & 0xffff;
		vcpuins.opr2 = d_nubit16(src) & 0xffff;
		vcpuins.result = (vcpuins.opr1+vcpuins.opr2+_GetCF) & 0xffff;
		d_nubit16(dest) = (t_nubit16)vcpuins.result;
		break;
	default:CaseError("ADC::len");break;}
	SetFlags(ADC_FLAG);
#define op adc
	aexecall
#undef op
	asregall
	acheckall(AFLAGS1)
}
static ASMCMP SBB(void *dest, void *src, t_nubit8 len)
{
	asyncall
	switch(len) {
	case 8:
		vcpuins.bit = 8;
		vcpuins.type = SBB8;
		vcpuins.opr1 = d_nubit8(dest) & 0xff;
		vcpuins.opr2 = d_nubit8(src) & 0xff;
		vcpuins.result = (vcpuins.opr1-(vcpuins.opr2+_GetCF)) & 0xff;
		d_nubit8(dest) = (t_nubit8)vcpuins.result;
		break;
	case 12:
		vcpuins.bit = 16;
		vcpuins.type = SBB16;
		vcpuins.opr1 = d_nubit16(dest) & 0xffff;
		vcpuins.opr2 = d_nsbit8(src) & 0xffff;
		vcpuins.result = (vcpuins.opr1-(vcpuins.opr2+_GetCF)) & 0xffff;
		d_nubit16(dest) = (t_nubit16)vcpuins.result;
		break;
	case 16:
		vcpuins.bit = 16;
		vcpuins.type = SBB16;
		vcpuins.opr1 = d_nubit16(dest) & 0xffff;
		vcpuins.opr2 = d_nubit16(src) & 0xffff;
		vcpuins.result = (vcpuins.opr1-(vcpuins.opr2+_GetCF)) & 0xffff;
		d_nubit16(dest) = (t_nubit16)vcpuins.result;
		break;
	default:CaseError("SBB::len");break;}
	SetFlags(SBB_FLAG);
#define op sbb
	aexecall
#undef op
	asregall
	acheckall(AFLAGS1)
}
static ASMCMP AND(void *dest, void *src, t_nubit8 len)
{
	asyncall
	switch(len) {
	case 8:
		vcpuins.bit = 8;
		//vcpuins.type = AND8;
		vcpuins.opr1 = d_nubit8(dest) & 0xff;
		vcpuins.opr2 = d_nubit8(src) & 0xff;
		vcpuins.result = (vcpuins.opr1&vcpuins.opr2) & 0xff;
		d_nubit8(dest) = (t_nubit8)vcpuins.result;
		break;
	case 12:
		vcpuins.bit = 16;
		//vcpuins.type = AND16;
		vcpuins.opr1 = d_nubit16(dest) & 0xffff;
		vcpuins.opr2 = d_nsbit8(src) & 0xffff;
		vcpuins.result = (vcpuins.opr1&vcpuins.opr2) & 0xffff;
		d_nubit16(dest) = (t_nubit16)vcpuins.result;
		break;
	case 16:
		vcpuins.bit = 16;
		//vcpuins.type = AND16;
		vcpuins.opr1 = d_nubit16(dest) & 0xffff;
		vcpuins.opr2 = d_nubit16(src) & 0xffff;
		vcpuins.result = (vcpuins.opr1&vcpuins.opr2) & 0xffff;
		d_nubit16(dest) = (t_nubit16)vcpuins.result;
		break;
	default:CaseError("AND::len");break;}
	_ClrOF;
	_ClrCF;
	_ClrAF;
	SetFlags(AND_FLAG);
#define op and
	aexecall
#undef op
	asregall
	acheckall(AFLAGS1)
}
static void SUB(void *dest, void *src, t_nubit8 len)
{
	switch(len) {
	case 8:
		vcpuins.bit = 8;
		vcpuins.type = SUB8;
		vcpuins.opr1 = d_nubit8(dest) & 0xff;
		vcpuins.opr2 = d_nubit8(src) & 0xff;
		vcpuins.result = (vcpuins.opr1-vcpuins.opr2)&0xff;
		d_nubit8(dest) = (t_nubit8)vcpuins.result;
		break;
	case 12:
		vcpuins.bit = 16;
		vcpuins.type = SUB16;
		vcpuins.opr1 = d_nubit16(dest) & 0xffff;
		vcpuins.opr2 = d_nsbit8(src) & 0xffff;
		vcpuins.result = (vcpuins.opr1-vcpuins.opr2)&0xffff;
		d_nubit16(dest) = (t_nubit16)vcpuins.result;
		break;
	case 16:
		vcpuins.bit = 16;
		vcpuins.type = SUB16;
		vcpuins.opr1 = d_nubit16(dest) & 0xffff;
		vcpuins.opr2 = d_nubit16(src) & 0xffff;
		vcpuins.result = (vcpuins.opr1-vcpuins.opr2)&0xffff;
		d_nubit16(dest) = (t_nubit16)vcpuins.result;
		break;
	default:CaseError("SUB::len");break;}
	SetFlags(SUB_FLAG);
}
static ASMCMP XOR(void *dest, void *src, t_nubit8 len)
{
	asyncall
	switch(len) {
	case 8:
		vcpuins.bit = 8;
		//vcpuins.type = XOR8;
		vcpuins.opr1 = d_nubit8(dest) & 0xff;
		vcpuins.opr2 = d_nubit8(src) & 0xff;
		vcpuins.result = (vcpuins.opr1^vcpuins.opr2)&0xff;
		d_nubit8(dest) = (t_nubit8)vcpuins.result;
		break;
	case 12:
		vcpuins.bit = 16;
		//vcpuins.type = XOR16;
		vcpuins.opr1 = d_nubit16(dest) & 0xffff;
		vcpuins.opr2 = d_nsbit8(src) & 0xffff;
		vcpuins.result = (vcpuins.opr1^vcpuins.opr2)&0xffff;
		d_nubit16(dest) = (t_nubit16)vcpuins.result;
		break;
	case 16:
		vcpuins.bit = 16;
		//vcpuins.type = XOR16;
		vcpuins.opr1 = d_nubit16(dest) & 0xffff;
		vcpuins.opr2 = d_nubit16(src) & 0xffff;
		vcpuins.result = (vcpuins.opr1^vcpuins.opr2)&0xffff;
		d_nubit16(dest) = (t_nubit16)vcpuins.result;
		break;
	default:CaseError("XOR::len");break;}
	_ClrOF;
	_ClrCF;
	_ClrAF;
	SetFlags(XOR_FLAG);
#define op xor
	aexecall
#undef op
	asregall
	acheckall(AFLAGS1)
}
static ASMCMP CMP(void *dest, void *src, t_nubit8 len)
{
	asyncall
	switch(len) {
	case 8:
		vcpuins.bit = 8;
		vcpuins.type = CMP8;
		vcpuins.opr1 = d_nubit8(dest) & 0xff;
		vcpuins.opr2 = d_nsbit8(src) & 0xff;
		bugfix(7) vcpuins.result = ((t_nubit8)vcpuins.opr1-(t_nsbit8)vcpuins.opr2)&0xff;
		else vcpuins.result = (vcpuins.opr1-vcpuins.opr2)&0xff;
		break;
	case 12:
		vcpuins.bit = 16;
		vcpuins.type = CMP16;
		vcpuins.opr1 = d_nubit16(dest) & 0xffff;
		vcpuins.opr2 = d_nsbit8(src) & 0xffff;
		vcpuins.result = ((t_nubit16)vcpuins.opr1-(t_nsbit8)vcpuins.opr2)&0xffff;
		break;
	case 16:
		vcpuins.bit = 16;
		vcpuins.type = CMP16;
		vcpuins.opr1 = d_nubit16(dest) & 0xffff;
		vcpuins.opr2 = d_nsbit16(src) & 0xffff;
		bugfix(7) vcpuins.result = ((t_nubit16)vcpuins.opr1-(t_nsbit16)vcpuins.opr2)&0xffff;
		else vcpuins.result = (vcpuins.opr1-vcpuins.opr2)&0xffff;
		break;
	default:CaseError("CMP::len");break;}
	SetFlags(CMP_FLAG);
#define op cmp
	aexecall
#undef op
	asregall
	acheckall(AFLAGS1)
}
static void PUSH(void *src, t_nubit8 len)
{
	t_nubit16 data = d_nubit16(src);
	switch(len) {
	case 16:
		vcpuins.bit = 16;
		vcpu.sp -= 2;
		bugfix(13) vramVarWord(vcpu.ss.selector,vcpu.sp) = data;
		else vramVarWord(vcpu.ss.selector,vcpu.sp) = d_nubit16(src);
		break;
	default:CaseError("PUSH::len");break;}
}
static void POP(void *dest, t_nubit8 len)
{
	switch(len) {
	case 16:
		vcpuins.bit = 16;
		d_nubit16(dest) = vramVarWord(vcpu.ss.selector,vcpu.sp);
		vcpu.sp += 2;
		break;
	default:CaseError("POP::len");break;}
}
static ASMCMP INC(void *dest, t_nubit8 len)
{
	switch(len) {
	case 8:
		async ub1 = d_nubit8(dest);
#define op inc
		aexec_idc8;
#undef op
		vcpuins.bit = 8;
		vcpuins.type = ADD8;
		vcpuins.opr1 = d_nubit8(dest) & 0xff;
		vcpuins.opr2 = 0x01;
		vcpuins.result = (vcpuins.opr1+vcpuins.opr2) & 0xff;
		d_nubit8(dest) = (t_nubit8)vcpuins.result;
		break;
	case 16:
		async uw1 = d_nubit16(dest);
#define op inc
		aexec_idc16;
#undef op
		vcpuins.bit = 16;
		vcpuins.type = ADD16;
		vcpuins.opr1 = d_nubit16(dest) & 0xffff;
		vcpuins.opr2 = 0x0001;
		vcpuins.result = (vcpuins.opr1+vcpuins.opr2)&0xffff;
		d_nubit16(dest) = (t_nubit16)vcpuins.result;
		break;
	default:CaseError("INC::len");break;}
	SetFlags(INC_FLAG);
	asregall
	acheckall(AFLAGS1)
}
static ASMCMP DEC(void *dest, t_nubit8 len)
{
	switch(len) {
	case 8:
		async ub1 = d_nubit8(dest);
#define op dec
		aexec_idc8;
#undef op
		vcpuins.bit = 8;
		vcpuins.type = SUB8;
		vcpuins.opr1 = d_nubit8(dest) & 0xff;
		vcpuins.opr2 = 0x01;
		vcpuins.result = (vcpuins.opr1-vcpuins.opr2)&0xff;
		d_nubit8(dest) = (t_nubit8)vcpuins.result;
		break;
	case 16:
		async uw1 = d_nubit16(dest);
#define op dec
		aexec_idc16;
#undef op
		vcpuins.bit = 16;
		vcpuins.type = SUB16;
		vcpuins.opr1 = d_nubit16(dest) & 0xffff;
		vcpuins.opr2 = 0x0001;
		vcpuins.result = (vcpuins.opr1-vcpuins.opr2)&0xffff;
		d_nubit16(dest) = (t_nubit16)vcpuins.result;
		break;
	default:CaseError("DEC::len");break;}
	SetFlags(DEC_FLAG);
	asregall
	acheckall(AFLAGS1)
}
static void JCC(void *src, t_bool flagj,t_nubit8 len)
{
	switch(len) {
	case 8:
		vcpuins.bit = 8;
		if(flagj)
			bugfix(5) vcpu.eip += d_nsbit8(src);
			else vcpu.eip += d_nubit8(src);
		break;
	default:CaseError("JCC::len");break;}
}
static ASMCMP TEST(void *dest, void *src, t_nubit8 len)
{
	asyncall
	switch(len) {
	case 8:
		vcpuins.bit = 8;
		//vcpuins.type = TEST8;
		vcpuins.opr1 = d_nubit8(dest) & 0xff;
		vcpuins.opr2 = d_nubit8(src) & 0xff;
		vcpuins.result = (vcpuins.opr1&vcpuins.opr2)&0xff;
		break;
	case 16:
		vcpuins.bit = 16;
		//vcpuins.type = TEST16;
		vcpuins.opr1 = d_nubit16(dest) & 0xffff;
		vcpuins.opr2 = d_nubit16(src) & 0xffff;
		vcpuins.result = (vcpuins.opr1&vcpuins.opr2)&0xffff;
		break;
	default:CaseError("TEST::len");break;}
	_ClrOF;
	_ClrCF;
	_ClrAF;
	SetFlags(TEST_FLAG);
#define op test
	aexecall
#undef op
	acheckall(AFLAGS1)
}
static void XCHG(void *dest, void *src, t_nubit8 len)
{
	switch(len) {
	case 8:
		vcpuins.bit = 8;
		vcpuins.opr1 = d_nubit8(dest) & 0xff;
		vcpuins.opr2 = d_nubit8(src) & 0xff;
		d_nubit8(dest) = (t_nubit8)vcpuins.opr2;
		d_nubit8(src) = (t_nubit8)vcpuins.opr1;
		break;
	case 16:
		vcpuins.bit = 16;
		vcpuins.opr1 = d_nubit16(dest) & 0xffff;
		vcpuins.opr2 = d_nubit16(src) & 0xffff;
		d_nubit16(dest) = (t_nubit16)vcpuins.opr2;
		d_nubit16(src) = (t_nubit16)vcpuins.opr1;
		break;
	default:CaseError("XCHG::len");break;}
}
static void MOV(void *dest, void *src, t_nubit8 len)
{
	switch(len) {
	case 8:
		vcpuins.bit = 8;
		d_nubit8(dest) = d_nubit8(src);
		break;
	case 16:
		vcpuins.bit = 16;
		d_nubit16(dest) = d_nubit16(src);
		break;
	default:CaseError("MOV::len");break;}
}
static ASMCMP ROL(void *dest, void *src, t_nubit8 len)
{
	t_nubit8 count,tempcount;
	t_bool tempCF;
	if(src) count = d_nubit8(src);
	else count = 1;
	switch(len) {
	case 8:
		async {
			ub1 = d_nubit8(dest);
			ub2 = count;
		}
		tempcount = (count & 0x1f) % 8;
		vcpuins.bit = 8;
		while(tempcount) {
			tempCF = GetMSB(d_nubit8(dest), 8);
			d_nubit8(dest) = (d_nubit8(dest)<<1)+(t_nubit8)tempCF;
			tempcount--;
		}
		MakeBit(vcpu.eflags,VCPU_EFLAGS_CF,GetLSB(d_nubit8(dest), 8));
		if(count == 1) MakeBit(vcpu.eflags,VCPU_EFLAGS_OF,GetMSB(d_nubit8(dest), 8)^_GetCF);
#define op rol
		aexec_srd8;
#undef op
		break;
	case 16:
		async {
			uw1 = d_nubit16(dest);
			ub2 = count;
		}
		tempcount = (count & 0x1f) % 16;
		vcpuins.bit = 16;
		while(tempcount) {
			tempCF = GetMSB(d_nubit16(dest), 16);
			d_nubit16(dest) = (d_nubit16(dest)<<1)+(t_nubit16)tempCF;
			tempcount--;
		}
		MakeBit(vcpu.eflags,VCPU_EFLAGS_CF,GetLSB(d_nubit16(dest), 16));
		if(count == 1) MakeBit(vcpu.eflags,VCPU_EFLAGS_OF,GetMSB(d_nubit16(dest), 16)^_GetCF);
#define op rol
		aexec_srd16;
#undef op
		break;
	default:CaseError("ROL::len");break;}
	asregall
	acheckall((count == 1) ? AFLAGS1 : AFLAGS2)
}
static ASMCMP ROR(void *dest, void *src, t_nubit8 len)
{
	t_nubit8 count,tempcount;
	t_bool tempCF;
	if(src) count = d_nubit8(src);
	else count = 1;
	switch(len) {
	case 8:
		async {
			ub1 = d_nubit8(dest);
			ub2 = count;
		}
		tempcount = (count & 0x1f) % 8;
		vcpuins.bit = 8;
		while(tempcount) {
			tempCF = GetLSB(d_nubit8(dest), 8);
			d_nubit8(dest) >>= 1;
			if(tempCF) d_nubit8(dest) |= 0x80;
			tempcount--;
		}
		MakeBit(vcpu.eflags,VCPU_EFLAGS_CF,GetMSB(d_nubit8(dest), 8));
		if(count == 1) MakeBit(vcpu.eflags,VCPU_EFLAGS_OF,GetMSB(d_nubit8(dest), 8)^(!!(d_nubit8(dest)&0x40)));
#define op ror
		aexec_srd8;
#undef op
		break;
	case 16:
		async {
			uw1 = d_nubit16(dest);
			ub2 = count;
		}
		tempcount = (count & 0x1f) % 16;
		vcpuins.bit = 16;
		while(tempcount) {
			tempCF = GetLSB(d_nubit16(dest), 16);
			d_nubit16(dest) >>= 1;
			if(tempCF) d_nubit16(dest) |= 0x8000;
			tempcount--;
		}
		MakeBit(vcpu.eflags,VCPU_EFLAGS_CF,GetMSB(d_nubit16(dest), 16));
		if(count == 1) MakeBit(vcpu.eflags,VCPU_EFLAGS_OF,GetMSB(d_nubit16(dest), 16)^(!!(d_nubit16(dest)&0x4000)));
#define op ror
		aexec_srd16;
#undef op
		break;
	default:CaseError("ROR::len");break;}
	asregall
	acheckall((count == 1) ? AFLAGS1 : AFLAGS2)
}
static ASMCMP RCL(void *dest, void *src, t_nubit8 len)
{
	t_nubit8 count,tempcount;
	t_bool tempCF;
	if(src) count = d_nubit8(src);
	else count = 1;
	switch(len) {
	case 8:
		async {
			ub1 = d_nubit8(dest);
			ub2 = count;
		}
		tempcount = (count & 0x1f) % 9;
		vcpuins.bit = 8;
		while(tempcount) {
			tempCF = GetMSB(d_nubit8(dest), 8);
			d_nubit8(dest) = (d_nubit8(dest)<<1)+_GetCF;
			MakeBit(vcpu.eflags,VCPU_EFLAGS_CF,tempCF);
			tempcount--;
		}
		if(count == 1) MakeBit(vcpu.eflags,VCPU_EFLAGS_OF,GetMSB(d_nubit8(dest), 8)^_GetCF);
#define op rcl
		aexec_srd8;
#undef op
		break;
	case 16:
		async {
			uw1 = d_nubit16(dest);
			ub2 = count;
		}
		tempcount = (count & 0x1f) % 17;
		vcpuins.bit = 16;
		while(tempcount) {
			tempCF = GetMSB(d_nubit16(dest), 16);
			d_nubit16(dest) = (d_nubit16(dest)<<1)+_GetCF;
			MakeBit(vcpu.eflags,VCPU_EFLAGS_CF,tempCF);
			tempcount--;
		}
		if(count == 1) MakeBit(vcpu.eflags,VCPU_EFLAGS_OF,GetMSB(d_nubit16(dest), 16)^_GetCF);
#define op rcl
		aexec_srd16;
#undef op
		break;
	default:CaseError("RCL::len");break;}
	asregall
	acheckall((count == 1) ? AFLAGS1 : AFLAGS2)
}
static ASMCMP RCR(void *dest, void *src, t_nubit8 len)
{
	t_nubit8 count,tempcount;
	t_bool tempCF;
	if(src) count = d_nubit8(src);
	else count = 1;
	switch(len) {
	case 8:
		async {
			ub1 = d_nubit8(dest);
			ub2 = count;
		}
		tempcount = (count & 0x1f) % 9;
		vcpuins.bit = 8;
		if(count == 1) MakeBit(vcpu.eflags,VCPU_EFLAGS_OF,GetMSB(d_nubit8(dest), 8)^_GetCF);
		while(tempcount) {
			tempCF = GetLSB(d_nubit8(dest), 8);
			d_nubit8(dest) >>= 1;
			if(_GetCF) d_nubit8(dest) |= 0x80;
			MakeBit(vcpu.eflags,VCPU_EFLAGS_CF,tempCF);
			tempcount--;
		}
#define op rcr
		aexec_srd8;
#undef op
		break;
	case 16:
		async {
			uw1 = d_nubit16(dest);
			ub2 = count;
		}
		tempcount = (count & 0x1f) % 17;
		vcpuins.bit = 16;
		if(count == 1) MakeBit(vcpu.eflags,VCPU_EFLAGS_OF,GetMSB(d_nubit16(dest), 16)^_GetCF);
		while(tempcount) {
			tempCF = GetLSB(d_nubit16(dest), 16);
			d_nubit16(dest) >>= 1;
			if(_GetCF) d_nubit16(dest) |= 0x8000;
			MakeBit(vcpu.eflags,VCPU_EFLAGS_CF,tempCF);
			tempcount--;
		}
#define op rcr
		aexec_srd16;
#undef op
		break;
	default:CaseError("RCR::len");break;}
	asregall
	acheckall((count == 1) ? AFLAGS1 : AFLAGS2)
}
static ASMCMP SHL(void *dest, void *src, t_nubit8 len)
{
	t_nubit8 count,tempcount;
	if(src) count = d_nubit8(src);
	else count = 1;
	switch(len) {
	case 8:
		async {
			ub1 = d_nubit8(dest);
			ub2 = count;
		}
		vcpuins.bit = 8;
		tempcount = count&0x1f;
		while(tempcount) {
			MakeBit(vcpu.eflags,VCPU_EFLAGS_CF,GetMSB(d_nubit8(dest), 8));
			d_nubit8(dest) <<= 1;
			tempcount--;
		}
		if(count == 1) MakeBit(vcpu.eflags,VCPU_EFLAGS_OF,GetMSB(d_nubit8(dest), 8)^_GetCF);
		bugfix(8) {
			if(count != 0) {
				vcpuins.result = d_nubit8(dest);
				SetFlags(SHL_FLAG);
			}
		} else {
			if(count != 1 && count != 0) {
				vcpuins.result = d_nubit8(dest);
				SetFlags(SHL_FLAG);
			}
		}
#define op shl
		aexec_srd8;
#undef op
		break;
	case 16:
		async {
			uw1 = d_nubit16(dest);
			ub2 = count;
		}
		vcpuins.bit = 16;
		tempcount = count&0x1f;
		while(tempcount) {
			MakeBit(vcpu.eflags,VCPU_EFLAGS_CF,GetMSB(d_nubit16(dest), 16));
			d_nubit16(dest) <<= 1;
			tempcount--;
		}
		if(count == 1) MakeBit(vcpu.eflags,VCPU_EFLAGS_OF,GetMSB(d_nubit16(dest), 16)^_GetCF);
		bugfix(8) {
			if(count != 0) {
				vcpuins.result = d_nubit16(dest);
				SetFlags(SHL_FLAG);
			}
		} else {
			if(count != 1 && count != 0) {
				vcpuins.result = d_nubit16(dest);
				SetFlags(SHL_FLAG);
			}
		}
#define op shl
		aexec_srd16;
#undef op
		break;
	default:CaseError("SHL::len");break;}
	if (count) _ClrAF;
	asregall
	acheckall((count == 1) ? AFLAGS1 : AFLAGS2)
}
static ASMCMP SHR(void *dest, void *src, t_nubit8 len)
{
	t_nubit8 count,tempcount,tempdest8;
	t_nubit16 tempdest16;
	if(src) count = d_nubit8(src);
	else count = 1;
	switch(len) {
	case 8:
		async {
			ub1 = d_nubit8(dest);
			ub2 = count;
		}
		vcpuins.bit = 8;
		tempcount = count&0x1f;
		tempdest8 = d_nubit8(dest);
		while(tempcount) {
			MakeBit(vcpu.eflags,VCPU_EFLAGS_CF,GetLSB(d_nubit8(dest), 8));
			d_nubit8(dest) >>= 1;
			tempcount--;
		}
		if(count == 1) MakeBit(vcpu.eflags,VCPU_EFLAGS_OF,!!(tempdest8&0x80));
		bugfix(8) {
			if(count != 0) {
				vcpuins.result = d_nubit8(dest);
				SetFlags(SHR_FLAG);
			}
		} else {
			if(count != 1 && count != 0) {
				vcpuins.result = d_nubit8(dest);
				SetFlags(SHR_FLAG);
			}
		}
#define op shr
		aexec_srd8;
#undef op
		break;
	case 16:
		async {
			uw1 = d_nubit16(dest);
			ub2 = count;
		}
		vcpuins.bit = 16;
		tempcount = count&0x1f;
		tempdest16 = d_nubit16(dest);
		while(tempcount) {
			MakeBit(vcpu.eflags,VCPU_EFLAGS_CF,GetLSB(d_nubit16(dest), 16));
			d_nubit16(dest) >>= 1;
			tempcount--;
		}
		if(count == 1) MakeBit(vcpu.eflags,VCPU_EFLAGS_OF,!!(tempdest16&0x8000));
		bugfix(8) {
			if(count != 0) {
				vcpuins.result = d_nubit16(dest);
				SetFlags(SHR_FLAG);
			}
		} else {
			if(count != 1 && count != 0) {
				vcpuins.result = d_nubit16(dest);
				SetFlags(SHR_FLAG);
			}
		}
#define op shr
		aexec_srd16;
#undef op
		break;
	default:CaseError("SHR::len");break;}
	if (count) _ClrAF;
	asregall
	acheckall((count == 1) ? AFLAGS1 : AFLAGS2)
}
static ASMCMP SAL(void *dest, void *src, t_nubit8 len)
{
	t_nubit8 count,tempcount;
	if(src) count = d_nubit8(src);
	else count = 1;
	switch(len) {
	case 8:
		async {
			ub1 = d_nubit8(dest);
			ub2 = count;
		}
		vcpuins.bit = 8;
		tempcount = count&0x1f;
		while(tempcount) {
			MakeBit(vcpu.eflags,VCPU_EFLAGS_CF,GetMSB(d_nubit8(dest), 8));
			d_nubit8(dest) <<= 1;
			tempcount--;
		}
		if(count == 1) MakeBit(vcpu.eflags,VCPU_EFLAGS_OF,GetMSB(d_nubit8(dest), 8)^_GetCF);
		bugfix(8) {
			if(count != 0) {
				vcpuins.result = d_nubit8(dest);
				SetFlags(SAL_FLAG);
			}
		} else {
			if(count != 1 && count != 0) {
				vcpuins.result = d_nubit8(dest);
				SetFlags(SAL_FLAG);
			}
		}
#define op sal
		aexec_srd8;
#undef op
		break;
	case 16:
		async {
			uw1 = d_nubit16(dest);
			ub2 = count;
		}
		vcpuins.bit = 16;
		tempcount = count&0x1f;
		while(tempcount) {
			MakeBit(vcpu.eflags,VCPU_EFLAGS_CF,GetMSB(d_nubit16(dest), 16));
			d_nubit16(dest) <<= 1;
			tempcount--;
		}
		if(count == 1) MakeBit(vcpu.eflags,VCPU_EFLAGS_OF,GetMSB(d_nubit16(dest), 16)^_GetCF);
		bugfix(8) {
			if(count != 0) {
				vcpuins.result = d_nubit16(dest);
				SetFlags(SAL_FLAG);
			}
		} else {
			if(count != 1 && count != 0) {
				vcpuins.result = d_nubit16(dest);
				SetFlags(SAL_FLAG);
			}
		}
#define op sal
		aexec_srd16;
#undef op
		break;
	default:CaseError("SAL::len");break;}
	if (count) _ClrAF;
	asregall
	acheckall((count == 1) ? AFLAGS1 : AFLAGS2)
}
static ASMCMP SAR(void *dest, void *src, t_nubit8 len)
{
	t_nubit8 count,tempcount,tempdest8;
	t_nubit16 tempdest16;
	if(src) count = d_nubit8(src);
	else count = 1;
	switch(len) {
	case 8:
		async {
			ub1 = d_nubit8(dest);
			ub2 = count;
		}
		vcpuins.bit = 8;
		tempcount = count&0x1f;
		tempdest8 = d_nubit8(dest);
		while(tempcount) {
			MakeBit(vcpu.eflags,VCPU_EFLAGS_CF,GetLSB(d_nubit8(dest), 8));
			d_nsbit8(dest) >>= 1;
			//d_nubit8(dest) |= tempdest8&0x80;
			tempcount--;
		}
		if(count == 1) MakeBit(vcpu.eflags,VCPU_EFLAGS_OF,0);
		bugfix(8) {
			if(count != 0) {
				vcpuins.result = d_nsbit8(dest);
				SetFlags(SAR_FLAG);
			}
		} else {
			if(count != 1 && count != 0) {
				vcpuins.result = d_nsbit8(dest);
				SetFlags(SAR_FLAG);
			}
		}
#define op sar
		aexec_srd8;
#undef op
		break;
	case 16:
		async {
			uw1 = d_nubit16(dest);
			ub2 = count;
		}
		vcpuins.bit = 16;
		tempcount = count&0x1f;
		tempdest16 = d_nubit16(dest);
		while(tempcount) {
			MakeBit(vcpu.eflags,VCPU_EFLAGS_CF,GetLSB(d_nubit16(dest), 16));
			d_nsbit16(dest) >>= 1;
			//d_nubit16(dest) |= tempdest16&0x8000;
			tempcount--;
		}
		if(count == 1) MakeBit(vcpu.eflags,VCPU_EFLAGS_OF,0);
		bugfix(8) {
			if(count != 0) {
				vcpuins.result = d_nsbit16(dest);
				SetFlags(SAR_FLAG);
			}
		} else {
			if(count != 1 && count != 0) {
				vcpuins.result = d_nsbit16(dest);
				SetFlags(SAR_FLAG);
			}
		}
#define op sar
		aexec_srd16;
#undef op
		break;
	default:CaseError("SAR::len");break;}
	if (count) _ClrAF;
	asregall
	acheckall((count == 1) ? AFLAGS1 : AFLAGS2)
}
static void STRDIR(t_nubit8 len, t_bool flagsi, t_bool flagdi)
{
	bugfix(10) {
		/* add parameters flagsi, flagdi */
	} else ;
	switch(len) {
	case 8:
		vcpuins.bit = 8;
		if(_GetDF) {
			if (flagdi) vcpu.di--;
			if (flagsi) vcpu.si--;
		} else {
			if (flagdi) vcpu.di++;
			if (flagsi) vcpu.si++;
		}
		break;
	case 16:
		vcpuins.bit = 16;
		if(_GetDF) {
			if (flagdi) vcpu.di -= 2;
			if (flagsi) vcpu.si -= 2;
		} else {
			if (flagdi) vcpu.di += 2;
			if (flagsi) vcpu.si += 2;
		}
		break;
	default:CaseError("STRDIR::len");break;}
}
static void MOVS(t_nubit8 len)
{
	switch(len) {
	case 8:
		vcpuins.bit = 8;
		vramVarByte(vcpu.es.selector,vcpu.di) = vramVarByte(vcpuins.overds.selector,vcpu.si);
		STRDIR(8,1,1);
		// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  MOVSB\n");
		break;
	case 16:
		vcpuins.bit = 16;
		vramVarWord(vcpu.es.selector,vcpu.di) = vramVarWord(vcpuins.overds.selector,vcpu.si);
		STRDIR(16,1,1);
		// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  MOVSW\n");
		break;
	default:CaseError("MOVS::len");break;}
	//qdcgaCheckVideoRam(vramGetRealAddress(vcpu.es.selector, vcpu.di));
}
static void CMPS(t_nubit8 len)
{
	switch(len) {
	case 8:
		vcpuins.bit = 8;
		vcpuins.type = CMP8;
		vcpuins.opr1 = vramVarByte(vcpuins.overds.selector,vcpu.si);
		vcpuins.opr2 = vramVarByte(vcpu.es.selector,vcpu.di);
		vcpuins.result = (vcpuins.opr1-vcpuins.opr2)&0xff;
		STRDIR(8,1,1);
		SetFlags(CMP_FLAG);
		// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  CMPSB\n");
		break;
	case 16:
		vcpuins.bit = 16;
		vcpuins.type = CMP16;
		vcpuins.opr1 = vramVarWord(vcpuins.overds.selector,vcpu.si);
		vcpuins.opr2 = vramVarWord(vcpu.es.selector,vcpu.di);
		vcpuins.result = (vcpuins.opr1-vcpuins.opr2)&0xffff;
		STRDIR(16,1,1);
		SetFlags(CMP_FLAG);
		// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  CMPSW\n");
		break;
	default:CaseError("CMPS::len");break;}
}
static void STOS(t_nubit8 len)
{
	switch(len) {
	case 8:
		vcpuins.bit = 8;
		vramVarByte(vcpu.es.selector,vcpu.di) = vcpu.al;
		STRDIR(8,0,1);
		/*if (eCPU.di+t<0xc0000 && eCPU.di+t>=0xa0000)
		WriteVideoRam(eCPU.di+t-0xa0000);*/
		// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  STOSB\n");
		break;
	case 16:
		vcpuins.bit = 16;
		vramVarWord(vcpu.es.selector,vcpu.di) = vcpu.ax;
		STRDIR(16,0,1);
		/*if (eCPU.di+((t2=eCPU.es,t2<<4))<0xc0000 && eCPU.di+((t2=eCPU.es,t2<<4))>=0xa0000)
		{
			for (i=0;i<tmpOpdSize;i++)
			{
				WriteVideoRam(eCPU.di+((t2=eCPU.es,t2<<4))-0xa0000+i);
			}
		}*/
		// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  STOSW\n");
		break;
	default:CaseError("STOS::len");break;}
}
static void LODS(t_nubit8 len)
{
	switch(len) {
	case 8:
		vcpuins.bit = 8;
		vcpu.al = vramVarByte(vcpuins.overds.selector,vcpu.si);
		STRDIR(8,1,0);
		// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  LODSB\n");
		break;
	case 16:
		vcpuins.bit = 16;
		vcpu.ax = vramVarWord(vcpuins.overds.selector,vcpu.si);
		STRDIR(16,1,0);
		// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  LODSW\n");
		break;
	default:CaseError("LODS::len");break;}
}
static void SCAS(t_nubit8 len)
{
	switch(len) {
	case 8:
		vcpuins.bit = 8;
		vcpuins.type = CMP8;
		vcpuins.opr1 = vcpu.al;
		vcpuins.opr2 = vramVarByte(vcpu.es.selector,vcpu.di);
		vcpuins.result = (vcpuins.opr1-vcpuins.opr2)&0xff;
		STRDIR(8,0,1);
		SetFlags(CMP_FLAG);
		break;
	case 16:
		vcpuins.bit = 16;
		vcpuins.type = CMP16;
		vcpuins.opr1 = vcpu.ax;
		vcpuins.opr2 = vramVarWord(vcpu.es.selector,vcpu.di);
		vcpuins.result = (vcpuins.opr1-vcpuins.opr2)&0xffff;
		STRDIR(16,0,1);
		SetFlags(CMP_FLAG);
		break;
	default:CaseError("SCAS::len");break;}
}
static ASMCMP NOT(void *dest, t_nubit8 len)
{
	switch(len) {
	case 8:
		async ub1 = d_nubit8(dest);
		vcpuins.bit = 8;
		d_nubit8(dest) = ~d_nubit8(dest);
#define op not
		aexec_nxx8;
#undef op
		break;
	case 16:
		async uw1 = d_nubit16(dest);
		vcpuins.bit = 16;
		d_nubit16(dest) = ~d_nubit16(dest);
#define op not
		aexec_nxx16;
#undef op
		break;
	default:CaseError("NOT::len");break;}
	asregall
	acheckall(AFLAGS1)
}
static ASMCMP NEG(void *dest, t_nubit8 len)
{
	t_nubitcc zero = 0;
	switch(len) {
	case 8:	
		async ub1 = d_nubit8(dest);
		vcpuins.bit = 8;
		SUB((void *)&zero,(void *)dest,8);
		d_nubit8(dest) = (t_nubit8)zero;
#define op neg
		aexec_nxx8;
#undef op
		break;
	case 16:
		async uw1 = d_nubit16(dest);
		vcpuins.bit = 16;
		SUB((void *)&zero,(void *)dest,16);
		d_nubit16(dest) = (t_nubit16)zero;
#define op neg
		aexec_nxx16;
#undef op
		break;
	default:CaseError("NEG::len");break;}
	asregall
	acheckall(AFLAGS1)
}
static ASMCMP MUL(void *src, t_nubit8 len)
{
	t_nubit32 tempresult;
	switch(len) {
	case 8:
		async ub2 = d_nubit8(src);
		vcpuins.bit = 8;
		vcpu.ax = vcpu.al * d_nubit8(src);
		MakeBit(vcpu.eflags,VCPU_EFLAGS_OF,!!vcpu.ah);
		MakeBit(vcpu.eflags,VCPU_EFLAGS_CF,!!vcpu.ah);
#define op mul
		aexec_mdx8;
#undef op
		break;
	case 16:
		async uw2 = d_nubit16(src);
		vcpuins.bit = 16;
		tempresult = vcpu.ax * d_nubit16(src);
		vcpu.dx = (tempresult>>16)&0xffff;
		vcpu.ax = tempresult&0xffff;
		MakeBit(vcpu.eflags,VCPU_EFLAGS_OF,!!vcpu.dx);
		MakeBit(vcpu.eflags,VCPU_EFLAGS_CF,!!vcpu.dx);
#define op mul
		aexec_mdx16;
#undef op
		break;
	default:CaseError("MUL::len");break;}
	acheck(VCPU_EFLAGS_OF | VCPU_EFLAGS_CF) vapiPrintIns(vcpu.cs.selector,vcpu.eip,"+MUL");
}
static ASMCMP IMUL(void *src, t_nubit8 len)
{
	t_nsbit32 tempresult;
	switch(len) {
	case 8:
		async ub2 = d_nubit8(src);
		vcpuins.bit = 8;
		vcpu.ax = (t_nsbit8)vcpu.al * d_nsbit8(src);
		if(vcpu.ax == vcpu.al) {
			_ClrOF;
			_ClrCF;
		} else {
			_SetOF;
			_SetCF;
		}
#define op imul
		aexec_mdx8;
#undef op
		acheck(VCPU_EFLAGS_OF | VCPU_EFLAGS_CF) vapiPrintIns(vcpu.cs.selector,vcpu.eip,"+IMUL");
		break;
	case 16:
		async uw2 = d_nubit16(src);
		vcpuins.bit = 16;
		tempresult = (t_nsbit16)vcpu.ax * d_nsbit16(src);
		vcpu.dx = (t_nubit16)((tempresult>>16)&0xffff);
		vcpu.ax = (t_nubit16)(tempresult&0xffff);
		if(tempresult == (t_nsbit32)vcpu.ax) {
			_ClrOF;
			_ClrCF;
		} else {
			_SetOF;
			_SetCF;
		}
#define op imul
		aexec_mdx16;
#undef op
		acheck(VCPU_EFLAGS_OF | VCPU_EFLAGS_CF) vapiPrintIns(vcpu.cs.selector,vcpu.eip,"+IMUL");
		break;
	default:CaseError("IMUL::len");break;}
}
static ASMCMP DIV(void *src, t_nubit8 len)
{
	t_nubit16 tempAX = vcpu.ax;
	t_nubit32 tempDXAX = (((t_nubit32)vcpu.dx)<<16)+vcpu.ax;
	switch(len) {
	case 8:
		async ub2 = d_nubit8(src);
		vcpuins.bit = 8;
		if(d_nubit8(src) == 0) {
			INT(0x00);
		} else {
			vcpu.al = (t_nubit8)(tempAX / d_nubit8(src));
			vcpu.ah = (t_nubit8)(tempAX % d_nubit8(src));
		}
#define op div
		aexec_mdx8;
#undef op
		acheck(VCPU_EFLAGS_DF | VCPU_EFLAGS_TF) vapiPrintIns(vcpu.cs.selector,vcpu.eip,"+DIV");
		break;
	case 16:
		async uw2 = d_nubit16(src);
		vcpuins.bit = 16;
		if(d_nubit16(src) == 0) {
			INT(0x00);
		} else {
			vcpu.ax = (t_nubit16)(tempDXAX / d_nubit16(src));
			vcpu.dx = (t_nubit16)(tempDXAX % d_nubit16(src));
		}
#define op div
		aexec_mdx16;
#undef op
		acheck(VCPU_EFLAGS_DF | VCPU_EFLAGS_TF) vapiPrintIns(vcpu.cs.selector,vcpu.eip,"+DIV");
		break;
	default:CaseError("DIV::len");break;}
}
static void IDIV(void *src, t_nubit8 len)
{
	t_nsbit16 tempAX = vcpu.ax;
	t_nsbit32 tempDXAX = (((t_nubit32)vcpu.dx)<<16)+vcpu.ax;
	switch(len) {
	case 8:
		async ub2 = d_nubit8(src);
		vcpuins.bit = 8;
		if(d_nubit8(src) == 0) {
			INT(0x00);
		} else {
			vcpu.al = (t_nubit8)(tempAX / d_nsbit8(src));
			vcpu.ah = (t_nubit8)(tempAX % d_nsbit8(src));
		}
#define op idiv
		aexec_mdx8;
#undef op
		acheck(VCPU_EFLAGS_DF | VCPU_EFLAGS_TF) vapiPrintIns(vcpu.cs.selector,vcpu.eip,"+IDIV");
		break;
	case 16:
		async uw2 = d_nubit16(src);
		vcpuins.bit = 16;
		if(d_nubit16(src) == 0) {
			INT(0x00);
		} else {
			vcpu.ax = (t_nubit16)(tempDXAX / d_nsbit16(src));
			vcpu.dx = (t_nubit16)(tempDXAX % d_nsbit16(src));
		}
#define op idiv
		aexec_mdx16;
#undef op
		acheck(VCPU_EFLAGS_DF | VCPU_EFLAGS_TF) vapiPrintIns(vcpu.cs.selector,vcpu.eip,"+IDIV");
		break;
	default:CaseError("IDIV::len");break;}
}
static void INT(t_nubit8 intid)
{
	PUSH((void *)&vcpu.eflags,16);
	_ClrIF;
	_ClrTF;
	PUSH((void *)&vcpu.cs.selector,16);
	PUSH((void *)&vcpu.eip,16);
	vcpu.eip = vramVarWord(0x0000,intid*4+0);
	vcpu.cs.selector = vramVarWord(0x0000,intid*4+2);
}

void OpError()
{
	vapiPrint("The NXVM CPU has encountered an illegal instruction.\n");
	vapiPrint("CS:%04X IP:%04X OP:%02X %02X %02X %02X\n",
		vcpu.cs.selector, vcpu.eip, vramVarByte(vcpu.cs.selector,vcpu.eip+0),
		vramVarByte(vcpu.cs.selector,vcpu.eip+1), vramVarByte(vcpu.cs.selector,vcpu.eip+2),
		vramVarByte(vcpu.cs.selector,vcpu.eip+3), vramVarByte(vcpu.cs.selector,vcpu.eip+4));
	vapiCallBackMachineStop();
}
void ADD_RM8_R8()
{
	vcpu.eip++;
	GetModRegRM(8,8);
	ADD((void *)vcpuins.rm,(void *)vcpuins.r,8);
}
void ADD_RM16_R16()
{
	vcpu.eip++;
	GetModRegRM(16,16);
	ADD((void *)vcpuins.rm,(void *)vcpuins.r,16);
}
void ADD_R8_RM8()
{
	vcpu.eip++;
	GetModRegRM(8,8);
	ADD((void *)vcpuins.r,(void *)vcpuins.rm,8);
}
void ADD_R16_RM16()
{
	vcpu.eip++;
	GetModRegRM(16,16);
	ADD((void *)vcpuins.r,(void *)vcpuins.rm,16);
}
void ADD_AL_I8()
{
	vcpu.eip++;
	GetImm(8);
	ADD((void *)&vcpu.al,(void *)vcpuins.imm,8);
}
void ADD_AX_I16()
{
	vcpu.eip++;
	GetImm(16);
	ADD((void *)&vcpu.ax,(void *)vcpuins.imm,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  ADD_AX_I16\n");
}
void PUSH_ES()
{
	vcpu.eip++;
	PUSH((void *)&vcpu.es.selector,16);
}
void POP_ES()
{
	vcpu.eip++;
	POP((void *)&vcpu.es.selector,16);
}
void OR_RM8_R8()
{
	vcpu.eip++;
	GetModRegRM(8,8);
	OR((void *)vcpuins.rm,(void *)vcpuins.r,8);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  OR_RM8_R8\n");
}
void OR_RM16_R16()
{
	vcpu.eip++;
	GetModRegRM(16,16);
	OR((void *)vcpuins.rm,(void *)vcpuins.r,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  OR_RM16_R16\n");
}
void OR_R8_RM8()
{
	vcpu.eip++;
	GetModRegRM(8,8);
	OR((void *)vcpuins.r,(void *)vcpuins.rm,8);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  OR_R8_RM8\n");
}
void OR_R16_RM16()
{
	vcpu.eip++;
	GetModRegRM(16,16);
	OR((void *)vcpuins.r,(void *)vcpuins.rm,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  OR_R16_RM16\n");
}
void OR_AL_I8()
{
	vcpu.eip++;
	GetImm(8);
	OR((void *)&vcpu.al,(void *)vcpuins.imm,8);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  OR_AL_I8\n");
}
void OR_AX_I16()
{
	vcpu.eip++;
	GetImm(16);
	OR((void *)&vcpu.ax,(void *)vcpuins.imm,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  OR_AX_I16\n");
}
void PUSH_CS()
{
	vcpu.eip++;
	PUSH((void *)&vcpu.cs.selector,16);
}
void POP_CS()
{
	vcpu.eip++;
	POP((void *)&vcpu.cs.selector,16);
}
void INS_0F()
{
	OpError();
}
void ADC_RM8_R8()
{
	vcpu.eip++;
	GetModRegRM(8,8);
	ADC((void *)vcpuins.rm,(void *)vcpuins.r,8);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  ADC_RM8_R8\n");
}
void ADC_RM16_R16()
{
	vcpu.eip++;
	GetModRegRM(16,16);
	ADC((void *)vcpuins.rm,(void *)vcpuins.r,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  ADC_RM16_R16\n");
}
void ADC_R8_RM8()
{
	vcpu.eip++;
	GetModRegRM(8,8);
	ADC((void *)vcpuins.r,(void *)vcpuins.rm,8);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  ADC_R8_RM8\n");
}
void ADC_R16_RM16()
{
	vcpu.eip++;
	GetModRegRM(16,16);
	ADC((void *)vcpuins.r,(void *)vcpuins.rm,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  ADC_R16_RM16\n");
}
static void ADC_AL_I8()
{
	// 0x14 0xib
	vcpu.eip++;
	GetImm(8);
	ADC((void *)&vcpu.al,(void *)vcpuins.imm,8);
}
static void ADC_AX_I16()
{
	// 0x15 0xiw/0xid
	vcpu.eip++;
	GetImm(GetOperandSize);
	ADC((void *)&vcpu.ax, (void *)vcpuins.imm, GetOperandSize);
}
void PUSH_SS()
{
	vcpu.eip++;
	PUSH((void *)&vcpu.ss.selector,16);
}
void POP_SS()
{
	vcpu.eip++;
	POP((void *)&vcpu.ss.selector,16);
}
void SBB_RM8_R8()
{
	vcpu.eip++;
	GetModRegRM(8,8);
	SBB((void *)vcpuins.rm,(void *)vcpuins.r,8);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  SBB_RM8_R8\n");
}
void SBB_RM16_R16()
{
	vcpu.eip++;
	GetModRegRM(16,16);
	SBB((void *)vcpuins.rm,(void *)vcpuins.r,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  SBB_RM16_R16\n");
}
void SBB_R8_RM8()
{
	vcpu.eip++;
	GetModRegRM(8,8);
	SBB((void *)vcpuins.r,(void *)vcpuins.rm,8);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  SBB_R8_RM8\n");
}
void SBB_R16_RM16()
{
	vcpu.eip++;
	GetModRegRM(16,16);
	SBB((void *)vcpuins.r,(void *)vcpuins.rm,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  SBB_R16_RM16\n");
}
void SBB_AL_I8()
{
	vcpu.eip++;
	GetImm(8);
	SBB((void *)&vcpu.al,(void *)vcpuins.imm,8);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  SBB_AL_I8\n");
}
void SBB_AX_I16()
{
	vcpu.eip++;
	GetImm(16);
	SBB((void *)&vcpu.ax,(void *)vcpuins.imm,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  SBB_AX_I16\n");
}
void PUSH_DS()
{
	vcpu.eip++;
	PUSH((void *)&vcpu.ds.selector,16);
}
void POP_DS()
{
	vcpu.eip++;
	POP((void *)&vcpu.ds.selector,16);
}
void AND_RM8_R8()
{
	vcpu.eip++;
	GetModRegRM(8,8);
	AND((void *)vcpuins.rm,(void *)vcpuins.r,8);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  AND_RM8_R8\n");
}
void AND_RM16_R16()
{
	vcpu.eip++;
	GetModRegRM(16,16);
	AND((void *)vcpuins.rm,(void *)vcpuins.r,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  AND_RM16_R16\n");
}
void AND_R8_RM8()
{
	vcpu.eip++;
	GetModRegRM(8,8);
	AND((void *)vcpuins.r,(void *)vcpuins.rm,8);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  AND_R8_RM8\n");
}
void AND_R16_RM16()
{
	vcpu.eip++;
	GetModRegRM(16,16);
	AND((void *)vcpuins.r,(void *)vcpuins.rm,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  AND_R16_RM16\n");
}
void AND_AL_I8()
{
	vcpu.eip++;
	GetImm(8);
	AND((void *)&vcpu.al,(void *)vcpuins.imm,8);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  AND_AL_I8\n");
}
void AND_AX_I16()
{
	vcpu.eip++;
	GetImm(16);
	AND((void *)&vcpu.ax,(void *)vcpuins.imm,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  AND_AX_I16\n");
}
void ES()
{
	vcpu.eip++;
	vcpuins.overds = vcpu.es;
	vcpuins.overss = vcpu.es;
}
ASMCMP DAA()
{
	t_nubit8 oldAL = vcpu.al;
	t_nubit8 newAL = vcpu.al + 0x06;
	vcpu.eip++;
	async;
	if(((vcpu.al & 0x0f) > 0x09) || _GetAF) {
		vcpu.al = newAL;
		MakeBit(vcpu.eflags,VCPU_EFLAGS_CF,_GetCF || ((newAL < oldAL) || (newAL < 0x06)));
		bugfix(19) _SetAF;
		else ;
	} else _ClrAF;
	if(((vcpu.al & 0xf0) > 0x90) || _GetCF) {
		vcpu.al += 0x60;
		_SetCF;
	} else _ClrCF;
	bugfix(18) {
		vcpuins.bit = 8;
		vcpuins.result = (t_nubitcc)vcpu.al;
		SetFlags(DAA_FLAG);
	} else ;
#define op daa
	aexec_cax8;
#undef op
	acheck(AFLAGS1) vapiPrintIns(vcpu.cs.selector,vcpu.eip-1,"DAA");
}
void SUB_RM8_R8()
{
	vcpu.eip++;
	GetModRegRM(8,8);
	SUB((void *)vcpuins.rm,(void *)vcpuins.r,8);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  SUB_RM8_R8\n");
}
void SUB_RM16_R16()
{
	vcpu.eip++;
	GetModRegRM(16,16);
	SUB((void *)vcpuins.rm,(void *)vcpuins.r,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  SUB_RM16_R16\n");
}
void SUB_R8_RM8()
{
	vcpu.eip++;
	GetModRegRM(8,8);
	SUB((void *)vcpuins.r,(void *)vcpuins.rm,8);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  SUB_R8_RM8\n");
}
void SUB_R16_RM16()
{
	vcpu.eip++;
	GetModRegRM(16,16);
	SUB((void *)vcpuins.r,(void *)vcpuins.rm,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  SUB_R16_RM16\n");
}
void SUB_AL_I8()
{
	vcpu.eip++;
	GetImm(8);
	SUB((void *)&vcpu.al,(void *)vcpuins.imm,8);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  SUB_AL_I8\n");
}
void SUB_AX_I16()
{
	vcpu.eip++;
	GetImm(16);
	SUB((void *)&vcpu.ax,(void *)vcpuins.imm,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  SUB_AX_I16\n");
}
void CS()
{
	vcpu.eip++;
	vcpuins.overds = vcpu.cs;
	vcpuins.overss = vcpu.cs;
}
ASMCMP DAS()
{
	t_nubit8 oldAL = vcpu.al;
	vcpu.eip++;
	async;
	if(((vcpu.al & 0x0f) > 0x09) || _GetAF) {
		vcpu.al -= 0x06;
		MakeBit(vcpu.eflags,VCPU_EFLAGS_CF,_GetCF || (oldAL < 0x06));
		_SetAF;
	} else _ClrAF;
	if((vcpu.al > 0x9f) || _GetCF) {
		vcpu.al -= 0x60;
		_SetCF;
	} else _ClrCF;
	bugfix(18) {
		vcpuins.bit = 8;
		vcpuins.result = (t_nubitcc)vcpu.al;
		SetFlags(DAS_FLAG);
	} else ;
#define op das
	aexec_cax8;
#undef op
	acheck(AFLAGS1) vapiPrintIns(vcpu.cs.selector,vcpu.eip-1,"DAS");
}
void XOR_RM8_R8()
{
	vcpu.eip++;
	GetModRegRM(8,8);
	XOR((void *)vcpuins.rm,(void *)vcpuins.r,8);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  XOR_RM8_R8\n");
}
void XOR_RM16_R16()
{
	vcpu.eip++;
	GetModRegRM(16,16);
	XOR((void *)vcpuins.rm,(void *)vcpuins.r,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  XOR_RM16_R16\n");
}
void XOR_R8_RM8()
{
	vcpu.eip++;
	GetModRegRM(8,8);
	XOR((void *)vcpuins.r,(void *)vcpuins.rm,8);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  XOR_R8_RM8\n");
}
void XOR_R16_RM16()
{
	vcpu.eip++;
	GetModRegRM(16,16);
	XOR((void *)vcpuins.r,(void *)vcpuins.rm,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  XOR_R16_RM16\n");
}
void XOR_AL_I8()
{
	vcpu.eip++;
	GetImm(8);
	XOR((void *)&vcpu.al,(void *)vcpuins.imm,8);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  XOR_AL_I8\n");
}
void XOR_AX_I16()
{
	vcpu.eip++;
	GetImm(16);
	XOR((void *)&vcpu.ax,(void *)vcpuins.imm,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  XOR_AX_I16\n");
}
void SS()
{
	vcpu.eip++;
	vcpuins.overds = vcpu.ss;
	vcpuins.overss = vcpu.ss;
}
static void AAA()
{
	// 0x37
	vcpu.eip++;
	if(((vcpu.al & 0x0f) > 0x09) || _GetAF) {
		vcpu.al += 0x06;
		vcpu.al &= 0x0f;
		vcpu.ah += 0x01;
		_SetAF;
		_SetCF;
	} else {
		_ClrAF;
		_ClrCF;
	}
}
void CMP_RM8_R8()
{
	vcpu.eip++;
	GetModRegRM(8,8);
	CMP((void *)vcpuins.rm,(void *)vcpuins.r,8);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  CMP_RM8_R8\n");
}
void CMP_RM16_R16()
{
	vcpu.eip++;
	GetModRegRM(16,16);
	CMP((void *)vcpuins.rm,(void *)vcpuins.r,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  CMP_RM16_R16\n");
}
void CMP_R8_RM8()
{
	vcpu.eip++;
	GetModRegRM(8,8);
	CMP((void *)vcpuins.r,(void *)vcpuins.rm,8);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  CMP_R8_RM8\n");
}
void CMP_R16_RM16()
{
	vcpu.eip++;
	GetModRegRM(16,16);
	CMP((void *)vcpuins.r,(void *)vcpuins.rm,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  CMP_R16_RM16\n");
}
void CMP_AL_I8()
{
	vcpu.eip++;
	GetImm(8);
	CMP((void *)&vcpu.al,(void *)vcpuins.imm,8);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  CMP_AL_I8\n");
}
void CMP_AX_I16()
{
	vcpu.eip++;
	GetImm(16);
	CMP((void *)&vcpu.ax,(void *)vcpuins.imm,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  CMP_AX_I16\n");
}
void DS()
{
	vcpu.eip++;
	vcpuins.overds = vcpu.ds;
	vcpuins.overss = vcpu.ds;
}
static void AAS()
{
	// 0x3f
	vcpu.eip++;
	if(((vcpu.al & 0x0f) > 0x09) || _GetAF) {
		vcpu.al -= 0x06;
		vcpu.al &= 0x0f;
		vcpu.ah -= 0x01;
		_SetAF;
		_SetCF;
	} else {
		_ClrCF;
		_ClrAF;
	}
}
void INC_AX()
{
	vcpu.eip++;
	INC((void *)&vcpu.ax,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  INC_AX\n");
}
void INC_CX()
{
	vcpu.eip++;
	INC((void *)&vcpu.cx,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  INC_CX\n");
}
void INC_DX()
{
	vcpu.eip++;
	INC((void *)&vcpu.dx,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  INC_DX\n");
}
void INC_BX()
{
	vcpu.eip++;
	INC((void *)&vcpu.bx,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  INC_BX\n");
}
void INC_SP()
{
	vcpu.eip++;
	INC((void *)&vcpu.sp,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  INC_SP\n");
}
void INC_BP()
{
	vcpu.eip++;
	INC((void *)&vcpu.bp,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  INC_BP\n");
}
void INC_SI()
{
	vcpu.eip++;
	INC((void *)&vcpu.si,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  INC_SI\n");
}
void INC_DI()
{
	vcpu.eip++;
	INC((void *)&vcpu.di,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  INC_DI\n");
}
void DEC_AX()
{
	vcpu.eip++;
	DEC((void *)&vcpu.ax,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  DEC_AX\n");
}
void DEC_CX()
{
	vcpu.eip++;
	DEC((void *)&vcpu.cx,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  DEC_CX\n");
}
void DEC_DX()
{
	vcpu.eip++;
	DEC((void *)&vcpu.dx,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  DEC_DX\n");
}
void DEC_BX()
{
	vcpu.eip++;
	DEC((void *)&vcpu.bx,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  DEC_BX\n");
}
void DEC_SP()
{
	vcpu.eip++;
	DEC((void *)&vcpu.sp,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  DEC_SP\n");
}
void DEC_BP()
{
	vcpu.eip++;
	DEC((void *)&vcpu.bp,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  DEC_BP\n");
}
void DEC_SI()
{
	vcpu.eip++;
	DEC((void *)&vcpu.si,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  DEC_SI\n");
}
void DEC_DI()
{
	vcpu.eip++;
	DEC((void *)&vcpu.di,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  DEC_DI\n");
}
void PUSH_AX()
{
	vcpu.eip++;
	PUSH((void *)&vcpu.ax,16);
}
void PUSH_CX()
{
	vcpu.eip++;
	PUSH((void *)&vcpu.cx,16);
}
void PUSH_DX()
{
	vcpu.eip++;
	PUSH((void *)&vcpu.dx,16);
}
void PUSH_BX()
{
	vcpu.eip++;
	PUSH((void *)&vcpu.bx,16);
}
void PUSH_SP()
{
	vcpu.eip++;
	PUSH((void *)&vcpu.sp,16);
}
void PUSH_BP()
{
	vcpu.eip++;
	PUSH((void *)&vcpu.bp,16);
}
void PUSH_SI()
{
	vcpu.eip++;
	PUSH((void *)&vcpu.si,16);
}
void PUSH_DI()
{
	vcpu.eip++;
	PUSH((void *)&vcpu.di,16);
}
void POP_AX()
{
	vcpu.eip++;
	POP((void *)&vcpu.ax,16);
}
void POP_CX()
{
	vcpu.eip++;
	POP((void *)&vcpu.cx,16);
}
void POP_DX()
{
	vcpu.eip++;
	POP((void *)&vcpu.dx,16);
}
void POP_BX()
{
	vcpu.eip++;
	POP((void *)&vcpu.bx,16);
}
void POP_SP()
{
	vcpu.eip++;
	POP((void *)&vcpu.sp,16);
}
void POP_BP()
{
	vcpu.eip++;
	POP((void *)&vcpu.bp,16);
}
void POP_SI()
{
	vcpu.eip++;
	POP((void *)&vcpu.si,16);
}
void POP_DI()
{
	vcpu.eip++;
	POP((void *)&vcpu.di,16);
}
void FS()
{
	vcpu.eip++;
	vcpuins.overds = vcpu.fs;
	vcpuins.overss = vcpu.fs;
}
void GS()
{
	vcpu.eip++;
	vcpuins.overds = vcpu.gs;
	vcpuins.overss = vcpu.gs;
}
void OprSize()
{
	vcpu.eip++;
	vcpuins.prefix_oprsize = 0x01;
}
void AddrSize()
{
	vcpu.eip++;
	vcpuins.prefix_addrsize = 0x01;
}
void JO()
{
	vcpu.eip++;
	GetImm(8);
	JCC((void *)vcpuins.imm, _GetOF, 8);
}
void JNO()
{
	vcpu.eip++;
	GetImm(8);
	JCC((void *)vcpuins.imm, !_GetOF, 8);
}
void JC()
{
	vcpu.eip++;
	GetImm(8);
	JCC((void *)vcpuins.imm, _GetCF, 8);
}
void JNC()
{
	vcpu.eip++;
	GetImm(8);
	JCC((void *)vcpuins.imm, !_GetCF, 8);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  JNC\n");
}
void JZ()
{
	vcpu.eip++;
	GetImm(8);
	JCC((void *)vcpuins.imm, _GetZF, 8);
}
void JNZ()
{
	vcpu.eip++;
	GetImm(8);
	JCC((void *)vcpuins.imm, !_GetZF, 8);
}
void JBE()
{
	vcpu.eip++;
	GetImm(8);
	JCC((void *)vcpuins.imm, (_GetCF ||
		_GetZF), 8);
}
void JA()
{
	vcpu.eip++;
	GetImm(8);
	JCC((void *)vcpuins.imm, (!_GetCF &&
		!_GetZF), 8);
}
void JS()
{
	vcpu.eip++;
	GetImm(8);
	JCC((void *)vcpuins.imm, _GetSF, 8);
}
void JNS()
{
	vcpu.eip++;
	GetImm(8);
	JCC((void *)vcpuins.imm, !_GetSF, 8);
}
void JP()
{
	vcpu.eip++;
	GetImm(8);
	JCC((void *)vcpuins.imm, _GetPF, 8);
}
void JNP()
{
	vcpu.eip++;
	GetImm(8);
	JCC((void *)vcpuins.imm, !_GetPF, 8);
}
void JL()
{
	vcpu.eip++;
	GetImm(8);
	JCC((void *)vcpuins.imm, (_GetSF !=
		_GetOF), 8);
}
void JNL()
{
	vcpu.eip++;
	GetImm(8);
	JCC((void *)vcpuins.imm, (_GetSF ==
		_GetOF), 8);
}
void JLE()
{
	vcpu.eip++;
	GetImm(8);
	JCC((void *)vcpuins.imm, (_GetZF ||
		(_GetSF !=
		_GetOF)), 8);
}
void JG()
{
	vcpu.eip++;
	GetImm(8);
	JCC((void *)vcpuins.imm, (!_GetZF &&
		(_GetSF ==
		_GetOF)), 8);
}
void INS_80()
{
	vcpu.eip++;
	GetModRegRM(0,8);
	GetImm(8);
	switch(vcpuins.r) {
	case 0:	ADD((void *)vcpuins.rm,(void *)vcpuins.imm,8);break;
	case 1:	OR ((void *)vcpuins.rm,(void *)vcpuins.imm,8);break;
	case 2:	ADC((void *)vcpuins.rm,(void *)vcpuins.imm,8);break;
	case 3:	SBB((void *)vcpuins.rm,(void *)vcpuins.imm,8);break;
	case 4:	AND((void *)vcpuins.rm,(void *)vcpuins.imm,8);break;
	case 5:	SUB((void *)vcpuins.rm,(void *)vcpuins.imm,8);break;
	case 6:	XOR((void *)vcpuins.rm,(void *)vcpuins.imm,8);break;
	case 7:	CMP((void *)vcpuins.rm,(void *)vcpuins.imm,8);break;
	default:CaseError("INS_80::vcpuins.r");break;}
}
void INS_81()
{
	vcpu.eip++;
	GetModRegRM(0,16);
	GetImm(16);
	switch(vcpuins.r) {
	case 0:	ADD((void *)vcpuins.rm,(void *)vcpuins.imm,16);break;
	case 1:	OR ((void *)vcpuins.rm,(void *)vcpuins.imm,16);break;
	case 2:	ADC((void *)vcpuins.rm,(void *)vcpuins.imm,16);break;
	case 3:	SBB((void *)vcpuins.rm,(void *)vcpuins.imm,16);break;
	case 4:	AND((void *)vcpuins.rm,(void *)vcpuins.imm,16);break;
	case 5:	SUB((void *)vcpuins.rm,(void *)vcpuins.imm,16);break;
	case 6:	XOR((void *)vcpuins.rm,(void *)vcpuins.imm,16);break;
	case 7:	CMP((void *)vcpuins.rm,(void *)vcpuins.imm,16);
	break;
	default:CaseError("INS_81::vcpuins.r");break;}
}
void INS_82()
{
	INS_80();
}
void INS_83()
{
	vcpu.eip++;
	GetModRegRM(0,16);
	GetImm(8);
	switch(vcpuins.r) {
	case 0:	ADD((void *)vcpuins.rm,(void *)vcpuins.imm,12);break;
	case 1:	OR ((void *)vcpuins.rm,(void *)vcpuins.imm,12);break;
	case 2:	ADC((void *)vcpuins.rm,(void *)vcpuins.imm,12);break;
	case 3:	SBB((void *)vcpuins.rm,(void *)vcpuins.imm,12);break;
	case 4:	AND((void *)vcpuins.rm,(void *)vcpuins.imm,12);break;
	case 5:	SUB((void *)vcpuins.rm,(void *)vcpuins.imm,12);break;
	case 6:	XOR((void *)vcpuins.rm,(void *)vcpuins.imm,12);break;
	case 7:	CMP((void *)vcpuins.rm,(void *)vcpuins.imm,12);break;
	default:CaseError("INS_83::vcpuins.r");break;}
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  INS_83\n");
}
void TEST_RM8_R8()
{
	vcpu.eip++;
	GetModRegRM(8,8);
	TEST((void *)vcpuins.rm,(void *)vcpuins.r,8);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  TEST_RM8_R8\n");
}
void TEST_RM16_R16()
{
	vcpu.eip++;
	GetModRegRM(16,16);
	TEST((void *)vcpuins.rm,(void *)vcpuins.r,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  TEST_RM16_R16\n");
}
void XCHG_R8_RM8()
{
	vcpu.eip++;
	GetModRegRM(8,8);
	XCHG((void *)vcpuins.r,(void *)vcpuins.rm,8);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  XCHG_R8_RM8\n");
}
void XCHG_R16_RM16()
{
	vcpu.eip++;
	GetModRegRM(16,16);
	XCHG((void *)vcpuins.r,(void *)vcpuins.rm,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  XCHG_R16_RM16\n");
}
void MOV_RM8_R8()
{
	vcpu.eip++;
	GetModRegRM(8,8);
	MOV((void *)vcpuins.rm,(void *)vcpuins.r,8);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  MOV_RM8_R8\n");
}
void MOV_RM16_R16()
{
	vcpu.eip++;
	GetModRegRM(16,16);
	MOV((void *)vcpuins.rm,(void *)vcpuins.r,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  MOV_RM16_R16\n");
}
void MOV_R8_RM8()
{
	vcpu.eip++;
	GetModRegRM(8,8);
	MOV((void *)vcpuins.r,(void *)vcpuins.rm,8);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  MOV_R8_RM8\n");
}
void MOV_R16_RM16()
{
	vcpu.eip++;
	GetModRegRM(16,16);
	MOV((void *)vcpuins.r,(void *)vcpuins.rm,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  MOV_R16_RM16\n");
}
void MOV_RM16_SEG()
{
	vcpu.eip++;
	GetModRegRM(4,16);
	MOV((void *)vcpuins.rm,(void *)vcpuins.r,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  MOV_RM16_SEG\n");
}
void LEA_R16_M16()
{
	vcpu.eip++;
	GetModRegRMEA();
	d_nubit16(vcpuins.r) = vcpuins.rm & 0xffff;
}
void MOV_SEG_RM16()
{
	vcpu.eip++;
	GetModRegRM(4,16);
	MOV((void *)vcpuins.r,(void *)vcpuins.rm,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  MOV_SEG_RM16\n");
}
void POP_RM16()
{
	vcpu.eip++;
	GetModRegRM(0,16);
	switch(vcpuins.r) {
	case 0:
		bugfix(14) POP((void *)vcpuins.rm,16);
		else POP((void *)vcpuins.rm,16);
		break;
	default:CaseError("POP_RM16::vcpuins.r");break;}
}
void NOP()
{
	vcpu.eip++;
}
void XCHG_CX_AX()
{
	vcpu.eip++;
	XCHG((void *)&vcpu.cx,(void *)&vcpu.ax,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  XCHG_CX_AX\n");
}
void XCHG_DX_AX()
{
	vcpu.eip++;
	XCHG((void *)&vcpu.dx,(void *)&vcpu.ax,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  XCHG_DX_AX\n");
}
void XCHG_BX_AX()
{
	vcpu.eip++;
	XCHG((void *)&vcpu.bx,(void *)&vcpu.ax,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  XCHG_BX_AX\n");
}
void XCHG_SP_AX()
{
	vcpu.eip++;
	XCHG((void *)&vcpu.sp,(void *)&vcpu.ax,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  XCHG_SP_AX\n");
}
void XCHG_BP_AX()
{
	vcpu.eip++;
	XCHG((void *)&vcpu.bp,(void *)&vcpu.ax,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  XCHG_BP_AX\n");
}
void XCHG_SI_AX()
{
	vcpu.eip++;
	XCHG((void *)&vcpu.si,(void *)&vcpu.ax,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  XCHG_SI_AX\n");
}
void XCHG_DI_AX()
{
	vcpu.eip++;
	XCHG((void *)&vcpu.di,(void *)&vcpu.ax,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  XCHG_DI_AX\n");
}
ASMCMP CBW()
{
	vcpu.eip++;
	async;
	vcpu.ax = (t_nsbit8)vcpu.al;
#define op cbw
	aexec_cax16;
#undef op
	acheck(AFLAGS1) vapiPrintIns(vcpu.cs.selector,vcpu.eip-1,"CBW");
}
ASMCMP CWD()
{
	vcpu.eip++;
	async;
	if (vcpu.ax & 0x8000) vcpu.dx = 0xffff;
	else vcpu.dx = 0x0000;
#define op cwd
	aexec_cax16;
#undef op
	acheck(AFLAGS1) vapiPrintIns(vcpu.cs.selector,vcpu.eip-1,"CWD");
}
void CALL_PTR16_16()
{
	t_nubit16 newcs,newip;
	async;
	vcpu.eip++;
	GetImm(16);
	newip = d_nubit16(vcpuins.imm);
	GetImm(16);
	newcs = d_nubit16(vcpuins.imm);
	PUSH((void *)&vcpu.cs.selector,16);
	PUSH((void *)&vcpu.eip,16);
	vcpu.eip = newip;
	vcpu.cs.selector = newcs;
}
NOTIMP WAIT()
{
	vcpu.eip++;
	/* not implemented */
}
void PUSHF()
{
	vcpu.eip++;
	PUSH((void *)&vcpu.eflags,16);
}
void POPF()
{
	vcpu.eip++;
	POP((void *)&vcpu.eflags,16);
}
void SAHF()
{
	vcpu.eip++;
	d_nubit8(&vcpu.eflags) = vcpu.ah;
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  SAHF\n");
}
void LAHF()
{
	vcpu.eip++;
//	vapiPrint("1:LAHF:%4X\n",vcpu.eflags);
	vcpu.ah = d_nubit8(&vcpu.eflags);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  LAHF\n");
}
void MOV_AL_M8()
{
	vcpu.eip++;
	GetMem();
	MOV((void *)&vcpu.al,(void *)vcpuins.rm,8);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  MOV_AL_M8\n");
}
void MOV_AX_M16()
{
	vcpu.eip++;
	GetMem();
	MOV((void *)&vcpu.ax,(void *)vcpuins.rm,16);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  MOV_AX_M16\n");
}
void MOV_M8_AL()
{
	vcpu.eip++;
	GetMem();
	MOV((void *)vcpuins.rm,(void *)&vcpu.al,8);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  MOV_M8_AL\n");
}
void MOV_M16_AX()
{
	vcpu.eip++;
	GetMem();
	MOV((void *)vcpuins.rm,(void *)&vcpu.ax,16);
}
void MOVSB()
{
	vcpu.eip++;
	if(vcpuins.prefix_rep == PREFIX_REP_NONE) MOVS(8);
	else {
		if (vcpu.cx) {
			MOVS(8);
			vcpu.cx--;
		}
		if (vcpu.cx) vcpuins.flaginsloop = 0x01;
	}
}
void MOVSW()
{
	vcpu.eip++;
	if(vcpuins.prefix_rep == PREFIX_REP_NONE) MOVS(16);
	else {
		if (vcpu.cx) {
			MOVS(16);
			vcpu.cx--;
		}
		if (vcpu.cx) vcpuins.flaginsloop = 0x01;
	}
}
void CMPSB()
{
	vcpu.eip++;
	if(vcpuins.prefix_rep == PREFIX_REP_NONE) CMPS(8);
	else {
		if (vcpu.cx) {
			CMPS(8);
			vcpu.cx--;
		}
		if (vcpu.cx && !(vcpuins.prefix_rep == PREFIX_REP_REPZ && !_GetZF) && !(vcpuins.prefix_rep == PREFIX_REP_REPZNZ && _GetZF))
			 vcpuins.flaginsloop = 0x01;
	}
}
void CMPSW()
{
	vcpu.eip++;
	if(vcpuins.prefix_rep == PREFIX_REP_NONE) CMPS(16);
	else {
		if (vcpu.cx) {
			CMPS(16);
			vcpu.cx--;
		}
		if (vcpu.cx && !(vcpuins.prefix_rep == PREFIX_REP_REPZ && !_GetZF) && !(vcpuins.prefix_rep == PREFIX_REP_REPZNZ && _GetZF))
			 vcpuins.flaginsloop = 0x01;
	}
}
void TEST_AL_I8()
{
	vcpu.eip++;
	GetImm(8);
	TEST((void *)&vcpu.al,(void *)vcpuins.imm,8);
}
void TEST_AX_I16()
{
	vcpu.eip++;
	GetImm(16);
	TEST((void *)&vcpu.ax,(void *)vcpuins.imm,16);
}
void STOSB()
{
	vcpu.eip++;
	if(vcpuins.prefix_rep == PREFIX_REP_NONE) STOS(8);
	else {
		if (vcpu.cx) {
			STOS(8);
			vcpu.cx--;
		}
		if (vcpu.cx) vcpuins.flaginsloop = 0x01;
	}
}
void STOSW()
{
	vcpu.eip++;
	if(vcpuins.prefix_rep == PREFIX_REP_NONE) STOS(16);
	else {
		if (vcpu.cx) {
			STOS(16);
			vcpu.cx--;
		}
		if (vcpu.cx) vcpuins.flaginsloop = 0x01;
	}
}
void LODSB()
{
	vcpu.eip++;
	if(vcpuins.prefix_rep == PREFIX_REP_NONE) LODS(8);
	else {
		if (vcpu.cx) {
			LODS(8);
			vcpu.cx--;
		}
		if (vcpu.cx) vcpuins.flaginsloop = 0x01;
	}
}
void LODSW()
{
	vcpu.eip++;
	if(vcpuins.prefix_rep == PREFIX_REP_NONE) LODS(16);
	else {
		if (vcpu.cx) {
			LODS(16);
			vcpu.cx--;
		}
		if (vcpu.cx) vcpuins.flaginsloop = 0x01;
	}
}
void SCASB()
{
	vcpu.eip++;
	if(vcpuins.prefix_rep == PREFIX_REP_NONE) SCAS(8);
	else {
		if (vcpu.cx) {
			SCAS(8);
			vcpu.cx--;
		}
		if (vcpu.cx && !(vcpuins.prefix_rep == PREFIX_REP_REPZ && !_GetZF) && !(vcpuins.prefix_rep == PREFIX_REP_REPZNZ && _GetZF))
			 vcpuins.flaginsloop = 0x01;
	}
}
void SCASW()
{
	vcpu.eip++;
	if(vcpuins.prefix_rep == PREFIX_REP_NONE) SCAS(16);
	else {
		if (vcpu.cx) {
			SCAS(16);
			vcpu.cx--;
		}
		if (vcpu.cx && !(vcpuins.prefix_rep == PREFIX_REP_REPZ && !_GetZF) && !(vcpuins.prefix_rep == PREFIX_REP_REPZNZ && _GetZF))
			 vcpuins.flaginsloop = 0x01;
	}
}
void MOV_AL_I8()
{
	vcpu.eip++;
	GetImm(8);
	MOV((void *)&vcpu.al,(void *)vcpuins.imm,8);
}
void MOV_CL_I8()
{
	vcpu.eip++;
	GetImm(8);
	MOV((void *)&vcpu.cl,(void *)vcpuins.imm,8);
}
void MOV_DL_I8()
{
	vcpu.eip++;
	GetImm(8);
	MOV((void *)&vcpu.dl,(void *)vcpuins.imm,8);
}
void MOV_BL_I8()
{
	vcpu.eip++;
	GetImm(8);
	MOV((void *)&vcpu.bl,(void *)vcpuins.imm,8);
}
void MOV_AH_I8()
{
	vcpu.eip++;
	GetImm(8);
	MOV((void *)&vcpu.ah,(void *)vcpuins.imm,8);
}
void MOV_CH_I8()
{
	vcpu.eip++;
	GetImm(8);
	MOV((void *)&vcpu.ch,(void *)vcpuins.imm,8);
}
void MOV_DH_I8()
{
	vcpu.eip++;
	GetImm(8);
	MOV((void *)&vcpu.dh,(void *)vcpuins.imm,8);
}
void MOV_BH_I8()
{
	vcpu.eip++;
	GetImm(8);
	MOV((void *)&vcpu.bh,(void *)vcpuins.imm,8);
}
void MOV_AX_I16()
{
	vcpu.eip++;
	GetImm(16);
	MOV((void *)&vcpu.ax,(void *)vcpuins.imm,16);
}
void MOV_CX_I16()
{
	vcpu.eip++;
	GetImm(16);
	MOV((void *)&vcpu.cx,(void *)vcpuins.imm,16);
}
void MOV_DX_I16()
{
	vcpu.eip++;
	GetImm(16);
	MOV((void *)&vcpu.dx,(void *)vcpuins.imm,16);
}
void MOV_BX_I16()
{
	vcpu.eip++;
	GetImm(16);
	MOV((void *)&vcpu.bx,(void *)vcpuins.imm,16);
}
void MOV_SP_I16()
{
	vcpu.eip++;
	GetImm(16);
	MOV((void *)&vcpu.sp,(void *)vcpuins.imm,16);
}
void MOV_BP_I16()
{
	vcpu.eip++;
	GetImm(16);
	MOV((void *)&vcpu.bp,(void *)vcpuins.imm,16);
}
void MOV_SI_I16()
{
	vcpu.eip++;
	GetImm(16);
	MOV((void *)&vcpu.si,(void *)vcpuins.imm,16);
}
void MOV_DI_I16()
{
	vcpu.eip++;
	GetImm(16);
	MOV((void *)&vcpu.di,(void *)vcpuins.imm,16);
}
void INS_C0()
{
	vcpu.eip++;
	GetModRegRM(0,8);
	GetImm(8);
	switch(vcpuins.r) {
	case 0:	ROL((void *)vcpuins.rm,(void *)vcpuins.imm,8);break;
	case 1:	ROR((void *)vcpuins.rm,(void *)vcpuins.imm,8);break;
	case 2:	RCL((void *)vcpuins.rm,(void *)vcpuins.imm,8);break;
	case 3:	RCR((void *)vcpuins.rm,(void *)vcpuins.imm,8);break;
	case 4:	SHL((void *)vcpuins.rm,(void *)vcpuins.imm,8);break;
	case 5:	SHR((void *)vcpuins.rm,(void *)vcpuins.imm,8);break;
	case 6:	SAL((void *)vcpuins.rm,(void *)vcpuins.imm,8);break;
	case 7:	SAR((void *)vcpuins.rm,(void *)vcpuins.imm,8);break;
	default:CaseError("INS_C0::vcpuins.r");break;}
}
void INS_C1()
{
	vcpu.eip++;
	GetModRegRM(0,16);
	GetImm(8);
	switch(vcpuins.r) {
	case 0:	ROL((void *)vcpuins.rm,(void *)vcpuins.imm,16);break;
	case 1:	ROR((void *)vcpuins.rm,(void *)vcpuins.imm,16);break;
	case 2:	RCL((void *)vcpuins.rm,(void *)vcpuins.imm,16);break;
	case 3:	RCR((void *)vcpuins.rm,(void *)vcpuins.imm,16);break;
	case 4:	SHL((void *)vcpuins.rm,(void *)vcpuins.imm,16);break;
	case 5:	SHR((void *)vcpuins.rm,(void *)vcpuins.imm,16);break;
	case 6:	SAL((void *)vcpuins.rm,(void *)vcpuins.imm,16);break;
	case 7:	SAR((void *)vcpuins.rm,(void *)vcpuins.imm,16);break;
	default:CaseError("INS_C1::vcpuins.r");break;}
}
void RET_I16()
{
	t_nubit16 addsp;
	vcpu.eip++;
	bugfix(15) {
		GetImm(16);
		addsp = d_nubit16(vcpuins.imm);
	} else {
		GetImm(8);
		addsp = d_nubit8(vcpuins.imm);
	}
	POP((void *)&vcpu.eip,16);
	vcpu.sp += addsp;
}
void RET()
{
	vcpu.eip++;
	POP((void *)&vcpu.eip,16);
}
void LES_R16_M16()
{
	vcpu.eip++;
	GetModRegRM(16,16);
	MOV((void *)vcpuins.r,(void *)vcpuins.rm,16);
	MOV((void *)&vcpu.es.selector,(void *)(vcpuins.rm+2),16);
}
void LDS_R16_M16()
{
	vcpu.eip++;
	GetModRegRM(16,16);
	MOV((void *)vcpuins.r,(void *)vcpuins.rm,16);
	MOV((void *)&vcpu.ds.selector,(void *)(vcpuins.rm+2),16);
}
void MOV_M8_I8()
{
	vcpu.eip++;
	GetModRegRM(8,8);
	GetImm(8);
	MOV((void *)vcpuins.rm,(void *)vcpuins.imm,8);
}
void MOV_M16_I16()
{
	vcpu.eip++;
	GetModRegRM(16,16);
	GetImm(16);
	MOV((void *)vcpuins.rm,(void *)vcpuins.imm,16);
}
void RETF_I16()
{
	t_nubit16 addsp;
	vcpu.eip++;
	GetImm(16);
	addsp = d_nubit16(vcpuins.imm);
	POP((void *)&vcpu.eip,16);
	POP((void *)&vcpu.cs.selector,16);
	vcpu.sp += addsp;
}
void RETF()
{
	POP((void *)&vcpu.eip,16);
	POP((void *)&vcpu.cs.selector,16);
}
void INT3()
{
	async;
	vcpu.eip++;
	INT(0x03);
}
void INT_I8()
{
	async;
	vcpu.eip++;
	GetImm(8);
	INT(d_nubit8(vcpuins.imm));
}
void INTO()
{
	async;
	vcpu.eip++;
	if(_GetOF) INT(0x04);
}
void IRET()
{
	vcpu.eip++;
	POP((void *)&vcpu.eip,16);
	POP((void *)&vcpu.cs.selector,16);
	POP((void *)&vcpu.eflags,16);
}
void INS_D0()
{
	vcpu.eip++;
	GetModRegRM(0,8);
	switch(vcpuins.r) {
	case 0:	ROL((void *)vcpuins.rm,NULL,8);break;
	case 1:	ROR((void *)vcpuins.rm,NULL,8);break;
	case 2:	RCL((void *)vcpuins.rm,NULL,8);break;
	case 3:	RCR((void *)vcpuins.rm,NULL,8);break;
	case 4:	SHL((void *)vcpuins.rm,NULL,8);break;
	case 5:	SHR((void *)vcpuins.rm,NULL,8);break;
	case 6:	SAL((void *)vcpuins.rm,NULL,8);break;
	case 7:	SAR((void *)vcpuins.rm,NULL,8);break;
	default:CaseError("INS_D0::vcpuins.r");break;}
}
void INS_D1()
{
	vcpu.eip++;
	GetModRegRM(0,16);
	switch(vcpuins.r) {
	case 0:	ROL((void *)vcpuins.rm,NULL,16);break;
	case 1:	ROR((void *)vcpuins.rm,NULL,16);break;
	case 2:	RCL((void *)vcpuins.rm,NULL,16);break;
	case 3:	RCR((void *)vcpuins.rm,NULL,16);break;
	case 4:	SHL((void *)vcpuins.rm,NULL,16);break;
	case 5:	SHR((void *)vcpuins.rm,NULL,16);break;
	case 6:	SAL((void *)vcpuins.rm,NULL,16);break;
	case 7:	SAR((void *)vcpuins.rm,NULL,16);break;
	default:CaseError("INS_D1::vcpuins.r");break;}
}
void INS_D2()
{
	vcpu.eip++;
	GetModRegRM(0,8);
	switch(vcpuins.r) {
	case 0:	ROL((void *)vcpuins.rm,(void *)&vcpu.cl,8);break;
	case 1:	ROR((void *)vcpuins.rm,(void *)&vcpu.cl,8);break;
	case 2:	RCL((void *)vcpuins.rm,(void *)&vcpu.cl,8);break;
	case 3:	RCR((void *)vcpuins.rm,(void *)&vcpu.cl,8);break;
	case 4:	SHL((void *)vcpuins.rm,(void *)&vcpu.cl,8);break;
	case 5:	SHR((void *)vcpuins.rm,(void *)&vcpu.cl,8);break;
	case 6:	SAL((void *)vcpuins.rm,(void *)&vcpu.cl,8);break;
	case 7:	SAR((void *)vcpuins.rm,(void *)&vcpu.cl,8);break;
	default:CaseError("INS_D2::vcpuins.r");break;}
}
void INS_D3()
{
	vcpu.eip++;
	GetModRegRM(0,16);
	switch(vcpuins.r) {
	case 0:	ROL((void *)vcpuins.rm,(void *)&vcpu.cl,16);break;
	case 1:	ROR((void *)vcpuins.rm,(void *)&vcpu.cl,16);break;
	case 2:	RCL((void *)vcpuins.rm,(void *)&vcpu.cl,16);break;
	case 3:	RCR((void *)vcpuins.rm,(void *)&vcpu.cl,16);break;
	case 4:	SHL((void *)vcpuins.rm,(void *)&vcpu.cl,16);break;
	case 5:	SHR((void *)vcpuins.rm,(void *)&vcpu.cl,16);break;
	case 6:	SAL((void *)vcpuins.rm,(void *)&vcpu.cl,16);break;
	case 7:	SAR((void *)vcpuins.rm,(void *)&vcpu.cl,16);break;
	default:CaseError("INS_D3::vcpuins.r");break;}
}
static void AAM()
{
	// 0xd4 0x0a
	t_nubit8 base;
	vcpu.eip++;
	GetImm(8);
	base = d_nubit8(vcpuins.imm);
	if (base != 0x0a) {
		/* TODO: EXCEPTION */
		/* 06: invalid instruction */
		return;
	}
	vcpu.ah = vcpu.al / base;
	vcpu.al = vcpu.al % base;
	vcpuins.bit = 0x08;
	vcpuins.result = vcpu.al & 0xff;
	SetFlags(AAM_FLAG);
}
static void AAD()
{
	// 0xd5 0x0a
	t_nubit8 base;
	vcpu.eip++;
	GetImm(8);
	base = d_nubit8(vcpuins.imm);
	if (base != 0x0a) {
		/* TODO: EXCEPTION */
		/* 06: invalid instruction */
		return;
	}
	vcpu.al = (vcpu.al + (vcpu.ah * base)) & 0xff;
	vcpu.ah = 0x00;
	vcpuins.bit = 0x08;
	vcpuins.result = vcpu.al & 0xff;
	SetFlags(AAD_FLAG);
}
void XLAT()
{
	vcpu.eip++;
	vcpu.al = vramVarByte(vcpuins.overds.selector,vcpu.bx+vcpu.al);
}
/*
void INS_D9()
{// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  INS_D9\n");
}
void INS_DB()
{// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  INS_DB\n");
}
*/
void LOOPNZ()
{
	t_nsbit8 rel8;
	vcpu.eip++;
	GetImm(8);
	bugfix(12) rel8 = d_nsbit8(vcpuins.imm);
	else rel8 = d_nubit8(vcpuins.imm);
	vcpu.cx--;
	if(vcpu.cx && !_GetZF) vcpu.eip += rel8;
}
void LOOPZ()
{
	t_nsbit8 rel8;
	vcpu.eip++;
	GetImm(8);
	bugfix(12) rel8 = d_nsbit8(vcpuins.imm);
	else rel8 = d_nubit8(vcpuins.imm);
	vcpu.cx--;
	if(vcpu.cx && _GetZF) vcpu.eip += rel8;
}
void LOOP()
{
	t_nsbit8 rel8;
	vcpu.eip++;
	GetImm(8);
	bugfix(12) rel8 = d_nsbit8(vcpuins.imm);
	else rel8 = d_nubit8(vcpuins.imm);
	vcpu.cx--;
	if(vcpu.cx) vcpu.eip += rel8;
}
void JCXZ_REL8()
{
	vcpu.eip++;
	GetImm(8);
	JCC((void*)vcpuins.imm,!vcpu.cx,8);
}
void IN_AL_I8()
{
	vcpu.eip++;
	GetImm(8);
//	vapiPrint("IN: %02X\n",d_nubit8(vcpuins.imm));
	ExecFun(vport.in[d_nubit8(vcpuins.imm)]);
	vcpu.al = vport.iobyte;
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  IN_AL_I8\n");
}
void IN_AX_I8()
{
	vcpu.eip++;
	GetImm(8);
//	vapiPrint("IN: %02X\n",d_nubit8(vcpuins.imm));
	ExecFun(vport.in[d_nubit8(vcpuins.imm)]);
	vcpu.ax = vport.ioword;
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  IN_AX_I8\n");
}
void OUT_I8_AL()
{
	vcpu.eip++;
	GetImm(8);
//	vapiPrint("OUT: %02X\n",d_nubit8(vcpuins.imm));
	vport.iobyte = vcpu.al;
	ExecFun(vport.out[d_nubit8(vcpuins.imm)]);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  OUT_I8_AL\n");
}
void OUT_I8_AX()
{
	vcpu.eip++;
	GetImm(8);
//	vapiPrint("OUT: %02X\n",d_nubit8(vcpuins.imm));
	vport.ioword = vcpu.ax;
	ExecFun(vport.out[d_nubit8(vcpuins.imm)]);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  OUT_I8_AX\n");
}
void CALL_REL16()
{
	t_nsbit16 rel16;
	async;
	vcpu.eip++;
	GetImm(16);
	rel16 = d_nsbit16(vcpuins.imm);
	PUSH((void *)&vcpu.eip,16);
	bugfix(12) vcpu.eip += rel16;
	else vcpu.eip += d_nubit16(vcpuins.imm);
}
void JMP_REL16()
{
	t_nsbit16 rel16;
	vcpu.eip++;
	GetImm(16);
	rel16 = d_nsbit16(vcpuins.imm);
	bugfix(2) vcpu.eip += rel16;
	else vcpu.eip += d_nubit16(vcpuins.imm);
}
void JMP_PTR16_16()
{
	t_nubit16 newip,newcs;
	vcpu.eip++;
	GetImm(16);
	newip = d_nubit16(vcpuins.imm);
	GetImm(16);
	newcs = d_nubit16(vcpuins.imm);
	vcpu.eip = newip;
	vcpu.cs.selector = newcs;
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  JMP_PTR16_16\n");
}
void JMP_REL8()
{
	t_nsbit8 rel8;
	vcpu.eip++;
	GetImm(8);
	rel8 = d_nsbit8(vcpuins.imm);
	bugfix(9) vcpu.eip += rel8;
	else vcpu.eip += d_nubit8(vcpuins.imm);
}
void IN_AL_DX()
{
	vcpu.eip++;
//	vapiPrint("IN: %04X\n",vcpu.dx);
	ExecFun(vport.in[vcpu.dx]);
	vcpu.al = vport.iobyte;
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  IN_AL_DX\n");
}
void IN_AX_DX()
{
	vcpu.eip++;
//	vapiPrint("IN: %04X\n",vcpu.dx);
	ExecFun(vport.in[vcpu.dx]);
	vcpu.ax = vport.ioword;
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  IN_AX_DX\n");
}
void OUT_DX_AL()
{
	vcpu.eip++;
	vport.iobyte = vcpu.al;
//	vapiPrint("OUT: %04X\n",vcpu.dx);
	ExecFun(vport.out[vcpu.dx]);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  OUT_DX_AL\n");
}
void OUT_DX_AX()
{
	vcpu.eip++;
//	vapiPrint("OUT: %04X\n",vcpu.dx);
	vport.ioword = vcpu.ax;
	ExecFun(vport.out[vcpu.dx]);
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  OUT_DX_AX\n");
}
NOTIMP LOCK()
{
	vcpu.eip++;
	/* Not Implemented */
}
void REPNZ()
{
	// CMPS,SCAS
	vcpu.eip++;
	vcpuins.prefix_rep = PREFIX_REP_REPZNZ;
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  REPNZ\n");
}
void REP()
{	// MOVS,LODS,STOS,CMPS,SCAS
	vcpu.eip++;
	vcpuins.prefix_rep = PREFIX_REP_REPZ;
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  REP\n");
}
NOTIMP HLT()
{
	vcpu.eip++;
	/* Not Implemented */
}
ASMCMP CMC()
{
	vcpu.eip++;
	async;
	vcpu.eflags ^= VCPU_EFLAGS_CF;
#define op cmc
	aexec_csf
#undef op
	acheck(AFLAGS1) vapiPrintIns(vcpu.cs.selector,vcpu.eip-1,"CMC");
}
void INS_F6()
{
	vcpu.eip++;
	GetModRegRM(0,8);
	switch(vcpuins.r) {
	case 0:	GetImm(8);
			TEST((void *)vcpuins.rm,(void *)vcpuins.imm,8);
			break;
	case 2:	NOT ((void *)vcpuins.rm,8);	break;
	case 3:	NEG ((void *)vcpuins.rm,8);	break;
	case 4:	MUL ((void *)vcpuins.rm,8);	break;
	case 5:	IMUL((void *)vcpuins.rm,8);	break;
	case 6:	DIV ((void *)vcpuins.rm,8);	break;
	case 7:	IDIV((void *)vcpuins.rm,8);	break;
	default:CaseError("INS_F6::vcpuins.r");break;}
}
void INS_F7()
{
	vcpu.eip++;
	GetModRegRM(0,16);
	switch(vcpuins.r) {
	case 0:	GetImm(16);
			TEST((void *)vcpuins.rm,(void *)vcpuins.imm,16);
			break;
	case 2:	NOT ((void *)vcpuins.rm,16);	break;
	case 3:	NEG ((void *)vcpuins.rm,16);	break;
	case 4:	MUL ((void *)vcpuins.rm,16);	break;
	case 5:	IMUL((void *)vcpuins.rm,16);	break;
	case 6:	DIV ((void *)vcpuins.rm,16);	break;
	case 7:	IDIV((void *)vcpuins.rm,16);	break;
	default:CaseError("INS_F7::vcpuins.r");break;}
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  INS_F7\n");
}
ASMCMP CLC()
{
	vcpu.eip++;
	async;
	_ClrCF;
#define op clc
	aexec_csf
#undef op
	acheck(AFLAGS1) vapiPrintIns(vcpu.cs.selector,vcpu.eip-1,"CLC");
}
ASMCMP STC()
{
	vcpu.eip++;
	async;
	_SetCF;
#define op stc
	aexec_csf
#undef op
	acheck(AFLAGS1) vapiPrintIns(vcpu.cs.selector,vcpu.eip-1,"STC");
}
void CLI()
{
	vcpu.eip++;
	_ClrIF;
}
void STI()
{
	vcpu.eip++;
	_SetIF;
}
ASMCMP CLD()
{
	vcpu.eip++;
	async;
	_ClrDF;
#define op cld
	aexec_csf
#undef op
	acheck(AFLAGS1) vapiPrintIns(vcpu.cs.selector,vcpu.eip-1,"CLD");
}
ASMCMP STD()
{
	vcpu.eip++;
	async;
	_SetDF;
#define op STD
	aexec_csf
#undef op
	acheck(AFLAGS1) vapiPrintIns(vcpu.cs.selector,vcpu.eip-1,"STD");
}
void INS_FE()
{
	vcpu.eip++;
	GetModRegRM(0,8);
	switch(vcpuins.r) {
	case 0:	INC((void *)vcpuins.rm,8);	break;
	case 1:	DEC((void *)vcpuins.rm,8);	break;
	default:CaseError("INS_FE::vcpuins.r");break;}
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  INS_FE\n");
}
void INS_FF()
{
	async;
	vcpu.eip++;
	GetModRegRM(0,16);
	switch(vcpuins.r) {
	case 0:	INC((void *)vcpuins.rm,16);	break;
	case 1:	DEC((void *)vcpuins.rm,16);	break;
	case 2:	/* CALL_RM16 */
		PUSH((void *)&vcpu.eip,16);
		vcpu.eip = d_nubit16(vcpuins.rm);
		break;
	case 3:	/* CALL_M16_16 */
		PUSH((void *)&vcpu.cs.selector,16);
		PUSH((void *)&vcpu.eip,16);
		vcpu.eip = d_nubit16(vcpuins.rm);
		bugfix(11) vcpu.cs.selector = d_nubit16(vcpuins.rm+2);
		else vcpu.cs.selector = d_nubit16(vcpuins.rm+1);
		break;
	case 4:	/* JMP_RM16 */
		vcpu.eip = d_nubit16(vcpuins.rm);
		break;
	case 5:	/* JMP_M16_16 */
		vcpu.eip = d_nubit16(vcpuins.rm);
		bugfix(11) vcpu.cs.selector = d_nubit16(vcpuins.rm+2);
		else vcpu.cs.selector = d_nubit16(vcpuins.rm+1);
		break;
	case 6:	/* PUSH_RM16 */
		PUSH((void *)vcpuins.rm,16);
		break;
	default:CaseError("INS_FF::vcpuins.r");break;}
	// _vapiPrintAddr(vcpu.cs.selector,vcpu.eip);vapiPrint("  INS_FF\n");
}

static t_bool IsPrefix(t_nubit8 opcode)
{
	switch(opcode) {
	case 0xf0: case 0xf2: case 0xf3:
	case 0x2e: case 0x36: case 0x3e: case 0x26:
	case 0x64: case 0x65: case 0x66: case 0x67:
				return 0x01;break;
	default:	return 0x00;break;
	}
}
static void ClrPrefix()
{
	vcpuins.overds = vcpu.ds;
	vcpuins.overss = vcpu.ss;
	vcpuins.prefix_rep = PREFIX_REP_NONE;
	vcpuins.prefix_lock = 0x00;
	vcpuins.prefix_oprsize = 0x00;
	vcpuins.prefix_addrsize = 0x00;
}
static void ExecIns()
{
	t_nubit8 opcode;

	bugfix(17) {
		/* Note: in this case, if we use two prefixes, the second prefix
		   will be discarded incorrectly */
		ClrPrefix();
		vcpuins.oldcs   = vcpu.cs;
		vcpuins.oldeip  = vcpu.eip;
		vcpuins.flaginsloop = 0x00;
		vcpuins.except = 0x00;
		vcpuins.excode = 0x00;
		do {
			opcode = vramVarByte(vcpu.cs.selector, vcpu.eip);
			ExecFun(vcpuins.table[opcode]);
		} while (IsPrefix(opcode));
		if (vcpuins.flaginsloop) {
			vcpu.cs = vcpuins.oldcs;
			vcpu.eip  = vcpuins.oldeip;
		}
	} else {
		bugfix(16) {
			/* Note: in this bug, if an interrupt generated between
			   prefix and operation, the prefix may not be deployed */
			if (IsPrefix(opcode)) ExecFun(vcpuins.table[opcode]);
			opcode = vramVarByte(vcpu.cs.selector, vcpu.eip);
			ExecFun(vcpuins.table[opcode]);
			ClrPrefix();
		} else {
			ExecFun(vcpuins.table[opcode]);
			if (!IsPrefix(opcode)) ClrPrefix();
		}
	}
}
static void ExecInt()
{
	/* hardware interrupt handeler */
	if(vcpu.flagnmi) INT(0x02);
	vcpu.flagnmi = 0x00;

	if(_GetIF && vpicScanINTR())
		INT(vpicGetINTR());

	if(_GetTF) INT(0x01);
}

void QDX()
{
	vcpu.eip++;
	GetImm(8);
	switch (d_nubit8(vcpuins.imm)) {
	case 0x00:
	case 0xff:
		vapiPrint("\nNXVM CPU STOP at CS:%04X IP:%04X INS:QDX IMM:%02X\n",
			vcpu.cs.selector,vcpu.eip,d_nubit8(vcpuins.imm));
		vapiPrint("This happens because of the special instruction.\n");
		vapiCallBackMachineStop();
		break;
	case 0x01:
	case 0xfe:
		vapiPrint("\nNXVM CPU RESET at CS:%04X IP:%04X INS:QDX IMM:%02X\n",
			vcpu.cs.selector,vcpu.eip,d_nubit8(vcpuins.imm));
		vapiPrint("This happens because of the special instruction.\n");
		vapiCallBackMachineReset();
		break;
	case 0x02:
		vapiPrint("\nNXVM CPU PRINT16 at CS:%04X IP:%04X INS:QDX IMM:%02X\n",
			vcpu.cs.selector,vcpu.eip,d_nubit8(vcpuins.imm));
		vapiCallBackDebugPrintRegs(16);
		break;
	case 0x03:
		vapiPrint("\nNXVM CPU PRINT32 at CS:%04X IP:%04X INS:QDX IMM:%02X\n",
			vcpu.cs.selector,vcpu.eip,d_nubit8(vcpuins.imm));
		vapiCallBackDebugPrintRegs(32);
		break;
	default:
		qdbiosExecInt(d_nubit8(vcpuins.imm));
		MakeBit(vramVarWord(_ss,_sp + 4), VCPU_EFLAGS_ZF, _GetZF);
		MakeBit(vramVarWord(_ss,_sp + 4), VCPU_EFLAGS_CF, _GetCF);
		break;
	}
}

void vcpuinsInit()
{
	memset(&vcpuins, 0x00, sizeof(t_cpuins));
	vcpuins.table[0x00] = (t_faddrcc)ADD_RM8_R8;
	vcpuins.table[0x01] = (t_faddrcc)ADD_RM16_R16;
	vcpuins.table[0x02] = (t_faddrcc)ADD_R8_RM8;
	vcpuins.table[0x03] = (t_faddrcc)ADD_R16_RM16;
	vcpuins.table[0x04] = (t_faddrcc)ADD_AL_I8;
	vcpuins.table[0x05] = (t_faddrcc)ADD_AX_I16;
	vcpuins.table[0x06] = (t_faddrcc)PUSH_ES;
	vcpuins.table[0x07] = (t_faddrcc)POP_ES;
	vcpuins.table[0x08] = (t_faddrcc)OR_RM8_R8;
	vcpuins.table[0x09] = (t_faddrcc)OR_RM16_R16;
	vcpuins.table[0x0a] = (t_faddrcc)OR_R8_RM8;
	vcpuins.table[0x0b] = (t_faddrcc)OR_R16_RM16;
	vcpuins.table[0x0c] = (t_faddrcc)OR_AL_I8;
	vcpuins.table[0x0d] = (t_faddrcc)OR_AX_I16;
	vcpuins.table[0x0e] = (t_faddrcc)PUSH_CS;
	vcpuins.table[0x0f] = (t_faddrcc)POP_CS;
	//vcpuins.table[0x0f] = (t_faddrcc)INS_0F;
	vcpuins.table[0x10] = (t_faddrcc)ADC_RM8_R8;
	vcpuins.table[0x11] = (t_faddrcc)ADC_RM16_R16;
	vcpuins.table[0x12] = (t_faddrcc)ADC_R8_RM8;
	vcpuins.table[0x13] = (t_faddrcc)ADC_R16_RM16;
	vcpuins.table[0x14] = (t_faddrcc)ADC_AL_I8;
	vcpuins.table[0x15] = (t_faddrcc)ADC_AX_I16;
	vcpuins.table[0x16] = (t_faddrcc)PUSH_SS;
	vcpuins.table[0x17] = (t_faddrcc)POP_SS;
	vcpuins.table[0x18] = (t_faddrcc)SBB_RM8_R8;
	vcpuins.table[0x19] = (t_faddrcc)SBB_RM16_R16;
	vcpuins.table[0x1a] = (t_faddrcc)SBB_R8_RM8;
	vcpuins.table[0x1b] = (t_faddrcc)SBB_R16_RM16;
	vcpuins.table[0x1c] = (t_faddrcc)SBB_AL_I8;
	vcpuins.table[0x1d] = (t_faddrcc)SBB_AX_I16;
	vcpuins.table[0x1e] = (t_faddrcc)PUSH_DS;
	vcpuins.table[0x1f] = (t_faddrcc)POP_DS;
	vcpuins.table[0x20] = (t_faddrcc)AND_RM8_R8;
	vcpuins.table[0x21] = (t_faddrcc)AND_RM16_R16;
	vcpuins.table[0x22] = (t_faddrcc)AND_R8_RM8;
	vcpuins.table[0x23] = (t_faddrcc)AND_R16_RM16;
	vcpuins.table[0x24] = (t_faddrcc)AND_AL_I8;
	vcpuins.table[0x25] = (t_faddrcc)AND_AX_I16;
	vcpuins.table[0x26] = (t_faddrcc)ES;
	vcpuins.table[0x27] = (t_faddrcc)DAA;
	vcpuins.table[0x28] = (t_faddrcc)SUB_RM8_R8;
	vcpuins.table[0x29] = (t_faddrcc)SUB_RM16_R16;
	vcpuins.table[0x2a] = (t_faddrcc)SUB_R8_RM8;
	vcpuins.table[0x2b] = (t_faddrcc)SUB_R16_RM16;
	vcpuins.table[0x2c] = (t_faddrcc)SUB_AL_I8;
	vcpuins.table[0x2d] = (t_faddrcc)SUB_AX_I16;
	vcpuins.table[0x2e] = (t_faddrcc)CS;
	vcpuins.table[0x2f] = (t_faddrcc)DAS;
	vcpuins.table[0x30] = (t_faddrcc)XOR_RM8_R8;
	vcpuins.table[0x31] = (t_faddrcc)XOR_RM16_R16;
	vcpuins.table[0x32] = (t_faddrcc)XOR_R8_RM8;
	vcpuins.table[0x33] = (t_faddrcc)XOR_R16_RM16;
	vcpuins.table[0x34] = (t_faddrcc)XOR_AL_I8;
	vcpuins.table[0x35] = (t_faddrcc)XOR_AX_I16;
	vcpuins.table[0x36] = (t_faddrcc)SS;
	vcpuins.table[0x37] = (t_faddrcc)AAA;
	vcpuins.table[0x38] = (t_faddrcc)CMP_RM8_R8;
	vcpuins.table[0x39] = (t_faddrcc)CMP_RM16_R16;
	vcpuins.table[0x3a] = (t_faddrcc)CMP_R8_RM8;
	vcpuins.table[0x3b] = (t_faddrcc)CMP_R16_RM16;
	vcpuins.table[0x3c] = (t_faddrcc)CMP_AL_I8;
	vcpuins.table[0x3d] = (t_faddrcc)CMP_AX_I16;
	vcpuins.table[0x3e] = (t_faddrcc)DS;
	vcpuins.table[0x3f] = (t_faddrcc)AAS;
	vcpuins.table[0x40] = (t_faddrcc)INC_AX;
	vcpuins.table[0x41] = (t_faddrcc)INC_CX;
	vcpuins.table[0x42] = (t_faddrcc)INC_DX;
	vcpuins.table[0x43] = (t_faddrcc)INC_BX;
	vcpuins.table[0x44] = (t_faddrcc)INC_SP;
	vcpuins.table[0x45] = (t_faddrcc)INC_BP;
	vcpuins.table[0x46] = (t_faddrcc)INC_SI;
	vcpuins.table[0x47] = (t_faddrcc)INC_DI;
	vcpuins.table[0x48] = (t_faddrcc)DEC_AX;
	vcpuins.table[0x49] = (t_faddrcc)DEC_CX;
	vcpuins.table[0x4a] = (t_faddrcc)DEC_DX;
	vcpuins.table[0x4b] = (t_faddrcc)DEC_BX;
	vcpuins.table[0x4c] = (t_faddrcc)DEC_SP;
	vcpuins.table[0x4d] = (t_faddrcc)DEC_BP;
	vcpuins.table[0x4e] = (t_faddrcc)DEC_SI;
	vcpuins.table[0x4f] = (t_faddrcc)DEC_DI;
	vcpuins.table[0x50] = (t_faddrcc)PUSH_AX;
	vcpuins.table[0x51] = (t_faddrcc)PUSH_CX;
	vcpuins.table[0x52] = (t_faddrcc)PUSH_DX;
	vcpuins.table[0x53] = (t_faddrcc)PUSH_BX;
	vcpuins.table[0x54] = (t_faddrcc)PUSH_SP;
	vcpuins.table[0x55] = (t_faddrcc)PUSH_BP;
	vcpuins.table[0x56] = (t_faddrcc)PUSH_SI;
	vcpuins.table[0x57] = (t_faddrcc)PUSH_DI;
	vcpuins.table[0x58] = (t_faddrcc)POP_AX;
	vcpuins.table[0x59] = (t_faddrcc)POP_CX;
	vcpuins.table[0x5a] = (t_faddrcc)POP_DX;
	vcpuins.table[0x5b] = (t_faddrcc)POP_BX;
	vcpuins.table[0x5c] = (t_faddrcc)POP_SP;
	vcpuins.table[0x5d] = (t_faddrcc)POP_BP;
	vcpuins.table[0x5e] = (t_faddrcc)POP_SI;
	vcpuins.table[0x5f] = (t_faddrcc)POP_DI;
	vcpuins.table[0x60] = (t_faddrcc)OpError;
	vcpuins.table[0x61] = (t_faddrcc)OpError;
	vcpuins.table[0x62] = (t_faddrcc)OpError;
	vcpuins.table[0x63] = (t_faddrcc)OpError;
	vcpuins.table[0x64] = (t_faddrcc)FS;
	vcpuins.table[0x65] = (t_faddrcc)GS;
	vcpuins.table[0x66] = (t_faddrcc)OprSize;
	vcpuins.table[0x67] = (t_faddrcc)AddrSize;
	vcpuins.table[0x68] = (t_faddrcc)OpError;
	//vcpuins.table[0x68] = (t_faddrcc)PUSH_I16;
	vcpuins.table[0x69] = (t_faddrcc)OpError;
	vcpuins.table[0x6a] = (t_faddrcc)OpError;
	vcpuins.table[0x6b] = (t_faddrcc)OpError;
	vcpuins.table[0x6c] = (t_faddrcc)OpError;
	vcpuins.table[0x6d] = (t_faddrcc)OpError;
	vcpuins.table[0x6e] = (t_faddrcc)OpError;
	vcpuins.table[0x6f] = (t_faddrcc)OpError;
	vcpuins.table[0x70] = (t_faddrcc)JO;
	vcpuins.table[0x71] = (t_faddrcc)JNO;
	vcpuins.table[0x72] = (t_faddrcc)JC;
	vcpuins.table[0x73] = (t_faddrcc)JNC;
	vcpuins.table[0x74] = (t_faddrcc)JZ;
	vcpuins.table[0x75] = (t_faddrcc)JNZ;
	vcpuins.table[0x76] = (t_faddrcc)JBE;
	vcpuins.table[0x77] = (t_faddrcc)JA;
	vcpuins.table[0x78] = (t_faddrcc)JS;
	vcpuins.table[0x79] = (t_faddrcc)JNS;
	vcpuins.table[0x7a] = (t_faddrcc)JP;
	vcpuins.table[0x7b] = (t_faddrcc)JNP;
	vcpuins.table[0x7c] = (t_faddrcc)JL;
	vcpuins.table[0x7d] = (t_faddrcc)JNL;
	vcpuins.table[0x7e] = (t_faddrcc)JLE;
	vcpuins.table[0x7f] = (t_faddrcc)JG;
	vcpuins.table[0x80] = (t_faddrcc)INS_80;
	vcpuins.table[0x81] = (t_faddrcc)INS_81;
	vcpuins.table[0x82] = (t_faddrcc)INS_82;
	vcpuins.table[0x83] = (t_faddrcc)INS_83;
	vcpuins.table[0x84] = (t_faddrcc)TEST_RM8_R8;
	vcpuins.table[0x85] = (t_faddrcc)TEST_RM16_R16;
	vcpuins.table[0x86] = (t_faddrcc)XCHG_R8_RM8;
	vcpuins.table[0x87] = (t_faddrcc)XCHG_R16_RM16;
	vcpuins.table[0x88] = (t_faddrcc)MOV_RM8_R8;
	vcpuins.table[0x89] = (t_faddrcc)MOV_RM16_R16;
	vcpuins.table[0x8a] = (t_faddrcc)MOV_R8_RM8;
	vcpuins.table[0x8b] = (t_faddrcc)MOV_R16_RM16;
	vcpuins.table[0x8c] = (t_faddrcc)MOV_RM16_SEG;
	vcpuins.table[0x8d] = (t_faddrcc)LEA_R16_M16;
	vcpuins.table[0x8e] = (t_faddrcc)MOV_SEG_RM16;
	vcpuins.table[0x8f] = (t_faddrcc)POP_RM16;
	vcpuins.table[0x90] = (t_faddrcc)NOP;
	vcpuins.table[0x91] = (t_faddrcc)XCHG_CX_AX;
	vcpuins.table[0x92] = (t_faddrcc)XCHG_DX_AX;
	vcpuins.table[0x93] = (t_faddrcc)XCHG_BX_AX;
	vcpuins.table[0x94] = (t_faddrcc)XCHG_SP_AX;
	vcpuins.table[0x95] = (t_faddrcc)XCHG_BP_AX;
	vcpuins.table[0x96] = (t_faddrcc)XCHG_SI_AX;
	vcpuins.table[0x97] = (t_faddrcc)XCHG_DI_AX;
	vcpuins.table[0x98] = (t_faddrcc)CBW;
	vcpuins.table[0x99] = (t_faddrcc)CWD;
	vcpuins.table[0x9a] = (t_faddrcc)CALL_PTR16_16;
	vcpuins.table[0x9b] = (t_faddrcc)WAIT;
	vcpuins.table[0x9c] = (t_faddrcc)PUSHF;
	vcpuins.table[0x9d] = (t_faddrcc)POPF;
	vcpuins.table[0x9e] = (t_faddrcc)SAHF;
	vcpuins.table[0x9f] = (t_faddrcc)LAHF;
	vcpuins.table[0xa0] = (t_faddrcc)MOV_AL_M8;
	vcpuins.table[0xa1] = (t_faddrcc)MOV_AX_M16;
	vcpuins.table[0xa2] = (t_faddrcc)MOV_M8_AL;
	vcpuins.table[0xa3] = (t_faddrcc)MOV_M16_AX;
	vcpuins.table[0xa4] = (t_faddrcc)MOVSB;
	vcpuins.table[0xa5] = (t_faddrcc)MOVSW;
	vcpuins.table[0xa6] = (t_faddrcc)CMPSB;
	vcpuins.table[0xa7] = (t_faddrcc)CMPSW;
	vcpuins.table[0xa8] = (t_faddrcc)TEST_AL_I8;
	vcpuins.table[0xa9] = (t_faddrcc)TEST_AX_I16;
	vcpuins.table[0xaa] = (t_faddrcc)STOSB;
	vcpuins.table[0xab] = (t_faddrcc)STOSW;
	vcpuins.table[0xac] = (t_faddrcc)LODSB;
	vcpuins.table[0xad] = (t_faddrcc)LODSW;
	vcpuins.table[0xae] = (t_faddrcc)SCASB;
	vcpuins.table[0xaf] = (t_faddrcc)SCASW;
	vcpuins.table[0xb0] = (t_faddrcc)MOV_AL_I8;
	vcpuins.table[0xb1] = (t_faddrcc)MOV_CL_I8;
	vcpuins.table[0xb2] = (t_faddrcc)MOV_DL_I8;
	vcpuins.table[0xb3] = (t_faddrcc)MOV_BL_I8;
	vcpuins.table[0xb4] = (t_faddrcc)MOV_AH_I8;
	vcpuins.table[0xb5] = (t_faddrcc)MOV_CH_I8;
	vcpuins.table[0xb6] = (t_faddrcc)MOV_DH_I8;
	vcpuins.table[0xb7] = (t_faddrcc)MOV_BH_I8;
	vcpuins.table[0xb8] = (t_faddrcc)MOV_AX_I16;
	vcpuins.table[0xb9] = (t_faddrcc)MOV_CX_I16;
	vcpuins.table[0xba] = (t_faddrcc)MOV_DX_I16;
	vcpuins.table[0xbb] = (t_faddrcc)MOV_BX_I16;
	vcpuins.table[0xbc] = (t_faddrcc)MOV_SP_I16;
	vcpuins.table[0xbd] = (t_faddrcc)MOV_BP_I16;
	vcpuins.table[0xbe] = (t_faddrcc)MOV_SI_I16;
	vcpuins.table[0xbf] = (t_faddrcc)MOV_DI_I16;
	vcpuins.table[0xc0] = (t_faddrcc)OpError;
	vcpuins.table[0xc1] = (t_faddrcc)OpError;
	vcpuins.table[0xc2] = (t_faddrcc)RET_I16;
	vcpuins.table[0xc3] = (t_faddrcc)RET;
	vcpuins.table[0xc4] = (t_faddrcc)LES_R16_M16;
	vcpuins.table[0xc5] = (t_faddrcc)LDS_R16_M16;
	vcpuins.table[0xc6] = (t_faddrcc)MOV_M8_I8;
	vcpuins.table[0xc7] = (t_faddrcc)MOV_M16_I16;
	vcpuins.table[0xc8] = (t_faddrcc)OpError;
	vcpuins.table[0xc9] = (t_faddrcc)OpError;
	vcpuins.table[0xca] = (t_faddrcc)RETF_I16;
	vcpuins.table[0xcb] = (t_faddrcc)RETF;
	vcpuins.table[0xcc] = (t_faddrcc)INT3;
	vcpuins.table[0xcd] = (t_faddrcc)INT_I8;
	vcpuins.table[0xce] = (t_faddrcc)INTO;
	vcpuins.table[0xcf] = (t_faddrcc)IRET;
	vcpuins.table[0xd0] = (t_faddrcc)INS_D0;
	vcpuins.table[0xd1] = (t_faddrcc)INS_D1;
	vcpuins.table[0xd2] = (t_faddrcc)INS_D2;
	vcpuins.table[0xd3] = (t_faddrcc)INS_D3;
	vcpuins.table[0xd4] = (t_faddrcc)AAM;
	vcpuins.table[0xd5] = (t_faddrcc)AAD;
	vcpuins.table[0xd6] = (t_faddrcc)OpError;
	vcpuins.table[0xd7] = (t_faddrcc)XLAT;
	vcpuins.table[0xd8] = (t_faddrcc)OpError;
	vcpuins.table[0xd9] = (t_faddrcc)OpError;
	//vcpuins.table[0xd9] = (t_faddrcc)INS_D9;
	vcpuins.table[0xda] = (t_faddrcc)OpError;
	vcpuins.table[0xdb] = (t_faddrcc)OpError;
	//vcpuins.table[0xdb] = (t_faddrcc)INS_DB;
	vcpuins.table[0xdc] = (t_faddrcc)OpError;
	vcpuins.table[0xdd] = (t_faddrcc)OpError;
	vcpuins.table[0xde] = (t_faddrcc)OpError;
	vcpuins.table[0xdf] = (t_faddrcc)OpError;
	vcpuins.table[0xe0] = (t_faddrcc)LOOPNZ;
	vcpuins.table[0xe1] = (t_faddrcc)LOOPZ;
	vcpuins.table[0xe2] = (t_faddrcc)LOOP;
	vcpuins.table[0xe3] = (t_faddrcc)JCXZ_REL8;
	vcpuins.table[0xe4] = (t_faddrcc)IN_AL_I8;
	vcpuins.table[0xe5] = (t_faddrcc)IN_AX_I8;
	vcpuins.table[0xe6] = (t_faddrcc)OUT_I8_AL;
	vcpuins.table[0xe7] = (t_faddrcc)OUT_I8_AX;
	vcpuins.table[0xe8] = (t_faddrcc)CALL_REL16;
	vcpuins.table[0xe9] = (t_faddrcc)JMP_REL16;
	vcpuins.table[0xea] = (t_faddrcc)JMP_PTR16_16;
	vcpuins.table[0xeb] = (t_faddrcc)JMP_REL8;
	vcpuins.table[0xec] = (t_faddrcc)IN_AL_DX;
	vcpuins.table[0xed] = (t_faddrcc)IN_AX_DX;
	vcpuins.table[0xee] = (t_faddrcc)OUT_DX_AL;
	vcpuins.table[0xef] = (t_faddrcc)OUT_DX_AX;
	vcpuins.table[0xf0] = (t_faddrcc)LOCK;
	vcpuins.table[0xf1] = (t_faddrcc)QDX;
	vcpuins.table[0xf2] = (t_faddrcc)REPNZ;
	vcpuins.table[0xf3] = (t_faddrcc)REP;
	vcpuins.table[0xf4] = (t_faddrcc)HLT;
	vcpuins.table[0xf5] = (t_faddrcc)CMC;
	vcpuins.table[0xf6] = (t_faddrcc)INS_F6;
	vcpuins.table[0xf7] = (t_faddrcc)INS_F7;
	vcpuins.table[0xf8] = (t_faddrcc)CLC;
	vcpuins.table[0xf9] = (t_faddrcc)STC;
	vcpuins.table[0xfa] = (t_faddrcc)CLI;
	vcpuins.table[0xfb] = (t_faddrcc)STI;
	vcpuins.table[0xfc] = (t_faddrcc)CLD;
	vcpuins.table[0xfd] = (t_faddrcc)STD;
	vcpuins.table[0xfe] = (t_faddrcc)INS_FE;
	vcpuins.table[0xff] = (t_faddrcc)INS_FF;
}
void vcpuinsReset()
{
	vcpuins.rm = vcpuins.r = vcpuins.imm = (t_vaddrcc)NULL;
	vcpuins.opr1 = vcpuins.opr2 = vcpuins.result = vcpuins.bit = 0;
	ClrPrefix();
}
void vcpuinsRefresh()
{
	ExecIns();
	ExecInt();
}
void vcpuinsFinal() {}
