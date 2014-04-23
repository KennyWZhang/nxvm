/* This file is a part of NXVM project. */

/* Central Processing Unit: Intel 8086e (80386) */

#ifndef NXVM_VCPU_H
#define NXVM_VCPU_H

#ifdef __cplusplus
/*extern "C" {*/
#endif

#include "vglobal.h"

#ifndef ECPUACT
typedef struct {
	t_nubit16 selector;
	/* invisible portion/descriptor part */
		t_nubit32 base;
		t_nubit32 limit;
		t_nubit8  dpl; /* if segment is cs, this is cpl */
		t_bool    g;
		t_bool    d_b;
		t_bool    p;
		t_bool    s;
		/* type attribute part */
		/* for system segments */
			t_nubit8 type; /* type id for system segments */
		/* for application segments */
			t_bool c_d; /* code segment or data segment */
			t_bool c_e; /* conforming or expand-down */
			t_bool r_w; /* readable or writable */
			t_bool a;   /* accessed */
} t_cpu_segreg;
typedef struct {
	union {
		union {
			struct {t_nubit8 al,ah;};
			t_nubit16 ax;
		};
		t_nubit32 eax;
	};
	union {
		union {
			struct {t_nubit8 bl,bh;};
			t_nubit16 bx;
		};
		t_nubit32 ebx;
	};
	union {
		union {
			struct {t_nubit8 cl,ch;};
			t_nubit16 cx;
		};
		t_nubit32 ecx;
	};
	union {
		union {
			struct {t_nubit8 dl,dh;};
			t_nubit16 dx;
		};
		t_nubit32 edx;
	};
	union {
		t_nubit16 sp;
		t_nubit32 esp;
	};
	union {
		t_nubit16 bp;
		t_nubit32 ebp;
	};
	union {
		t_nubit16 si;
		t_nubit32 esi;
	};
	union {
		t_nubit16 di;
		t_nubit32 edi;
	};
	union {
		t_nubit16 ip;
		t_nubit32 eip;
	};
	union {
		t_nubit16 flags;
		t_nubit32 eflags;
	};
	t_cpu_segreg cs, ss, ds, es, fs, gs, ldtr, tr;
	t_nubit48 gdtr, idtr;
	t_nubit32 ldtrcr, trcr;
	t_nubit32 cr0, cr1, cr2, cr3;
	t_nubit32 dr0, dr1, dr2, dr3, dr4, dr5, dr6, dr7;
	t_nubit32 tr6, tr7;
	t_bool flagnmi;
} t_cpu;

#define _eax    (vcpu.eax)
#define _ebx    (vcpu.ebx)
#define _ecx    (vcpu.ecx)
#define _edx    (vcpu.edx)
#define _esp    (vcpu.esp)
#define _ebp    (vcpu.ebp)
#define _esi    (vcpu.esi)
#define _edi    (vcpu.edi)
#define _eip    (vcpu.eip)
#define _ax     (vcpu.ax)
#define _bx     (vcpu.bx)
#define _cx     (vcpu.cx)
#define _dx     (vcpu.dx)
#define _ah     (vcpu.ah)
#define _bh     (vcpu.bh)
#define _ch     (vcpu.ch)
#define _dh     (vcpu.dh)
#define _al     (vcpu.al)
#define _bl     (vcpu.bl)
#define _cl     (vcpu.cl)
#define _dl     (vcpu.dl)
#define _sp     (vcpu.sp)
#define _bp     (vcpu.bp)
#define _si     (vcpu.si)
#define _di     (vcpu.di)
#define _ds     (vcpu.ds.selector)
#define _cs     (vcpu.cs.selector)
#define _ss     (vcpu.ss.selector)
#define _ds     (vcpu.ds.selector)
#define _es     (vcpu.es.selector)
#define _fs     (vcpu.fs.selector)
#define _gs     (vcpu.gs.selector)
#define _eflags (vcpu.eflags)
#define _gdtr   (vcpu.gdtr)
#define _idtr   (vcpu.idtr)
#define _ldtr   (vcpu.ldtr)
#define _tr     (vcpu.tr)

#define VCPU_EFLAGS_CF 0x00000001
#define VCPU_EFLAGS_PF 0x00000004
#define VCPU_EFLAGS_AF 0x00000010
#define VCPU_EFLAGS_ZF 0x00000040
#define VCPU_EFLAGS_SF 0x00000080
#define VCPU_EFLAGS_TF 0x00000100
#define VCPU_EFLAGS_IF 0x00000200
#define VCPU_EFLAGS_DF 0x00000400
#define VCPU_EFLAGS_OF 0x00000800
#define VCPU_EFLAGS_IOPLL 0x00001000
#define VCPU_EFLAGS_IOPLH 0x00002000
#define VCPU_EFLAGS_IOPL  0x00003000
#define VCPU_EFLAGS_NT    0x00004000
#define VCPU_EFLAGS_RF    0x00010000
#define VCPU_EFLAGS_VM    0x00020000
/*#define VCPU_EFLAGS_AC    0x00040000
#define VCPU_EFLAGS_VIF   0x00080000
#define VCPU_EFLAGS_VIP   0x00100000
#define VCPU_EFLAGS_ID    0x00200000*/
#define _GetCF    (GetBit(_eflags, VCPU_EFLAGS_CF))
#define _GetPF    (GetBit(_eflags, VCPU_EFLAGS_PF))
#define _GetAF    (GetBit(_eflags, VCPU_EFLAGS_AF))
#define _GetZF    (GetBit(_eflags, VCPU_EFLAGS_ZF))
#define _GetSF    (GetBit(_eflags, VCPU_EFLAGS_SF))
#define _GetTF    (GetBit(_eflags, VCPU_EFLAGS_TF))
#define _GetIF    (GetBit(_eflags, VCPU_EFLAGS_IF))
#define _GetDF    (GetBit(_eflags, VCPU_EFLAGS_DF))
#define _GetOF    (GetBit(_eflags, VCPU_EFLAGS_OF))
#define _GetIOPLL (GetBit(_eflags, VCPU_EFLAGS_IOPLL))
#define _GetIOPLH (GetBit(_eflags, VCPU_EFLAGS_IOPLH))
#define _GetIOPL  ((_eflags & VCPU_EFLAGS_IOPL) >> 12)
#define _GetNT    (GetBit(_eflags, VCPU_EFLAGS_NT))
#define _GetRF    (GetBit(_eflags, VCPU_EFLAGS_RF))
#define _GetVM    (GetBit(_eflags, VCPU_EFLAGS_VM))
/*#define _GetAC    (GetBit(_eflags, VCPU_EFLAGS_AC))
#define _GetVIF   (GetBit(_eflags, VCPU_EFLAGS_VIF))
#define _GetVIP   (GetBit(_eflags, VCPU_EFLAGS_VIP))
#define _GetID    (GetBit(_eflags, VCPU_EFLAGS_ID))*/
#define _SetCF    (SetBit(_eflags, VCPU_EFLAGS_CF))
#define _SetPF    (SetBit(_eflags, VCPU_EFLAGS_PF))
#define _SetAF    (SetBit(_eflags, VCPU_EFLAGS_AF))
#define _SetZF    (SetBit(_eflags, VCPU_EFLAGS_ZF))
#define _SetSF    (SetBit(_eflags, VCPU_EFLAGS_SF))
#define _SetTF    (SetBit(_eflags, VCPU_EFLAGS_TF))
#define _SetIF    (SetBit(_eflags, VCPU_EFLAGS_IF))
#define _SetDF    (SetBit(_eflags, VCPU_EFLAGS_DF))
#define _SetOF    (SetBit(_eflags, VCPU_EFLAGS_OF))
#define _SetIOPLL (SetBit(_eflags, VCPU_EFLAGS_IOPLL))
#define _SetIOPLH (SetBit(_eflags, VCPU_EFLAGS_IOPLH))
#define _SetIOPL  (SetBit(_eflags, VCPU_EFLAGS_IOPL)
#define _SetNT    (SetBit(_eflags, VCPU_EFLAGS_NT))
#define _SetRF    (SetBit(_eflags, VCPU_EFLAGS_RF))
#define _SetVM    (SetBit(_eflags, VCPU_EFLAGS_VM))
/*#define _SetAC    (SetBit(_eflags, VCPU_EFLAGS_AC))
#define _SetVIF   (SetBit(_eflags, VCPU_EFLAGS_VIF))
#define _SetVIP   (SetBit(_eflags, VCPU_EFLAGS_VIP))
#define _SetID    (SetBit(_eflags, VCPU_EFLAGS_ID))*/
#define _ClrCF    (ClrBit(_eflags, VCPU_EFLAGS_CF))
#define _ClrPF    (ClrBit(_eflags, VCPU_EFLAGS_PF))
#define _ClrAF    (ClrBit(_eflags, VCPU_EFLAGS_AF))
#define _ClrZF    (ClrBit(_eflags, VCPU_EFLAGS_ZF))
#define _ClrSF    (ClrBit(_eflags, VCPU_EFLAGS_SF))
#define _ClrTF    (ClrBit(_eflags, VCPU_EFLAGS_TF))
#define _ClrIF    (ClrBit(_eflags, VCPU_EFLAGS_IF))
#define _ClrDF    (ClrBit(_eflags, VCPU_EFLAGS_DF))
#define _ClrOF    (ClrBit(_eflags, VCPU_EFLAGS_OF))
#define _ClrIOPLL (ClrBit(_eflags, VCPU_EFLAGS_IOPLL))
#define _ClrIOPLH (ClrBit(_eflags, VCPU_EFLAGS_IOPLH))
#define _ClrIOPL  (ClrBit(_eflags, VCPU_EFLAGS_IOPL)
#define _ClrNT    (ClrBit(_eflags, VCPU_EFLAGS_NT))
#define _ClrRF    (ClrBit(_eflags, VCPU_EFLAGS_RF))
#define _ClrVM    (ClrBit(_eflags, VCPU_EFLAGS_VM))
/*#define _ClrAC    (ClrBit(_eflags, VCPU_EFLAGS_AC))
#define _ClrVIF   (ClrBit(_eflags, VCPU_EFLAGS_VIF))
#define _ClrVIP   (ClrBit(_eflags, VCPU_EFLAGS_VIP))
#define _ClrID    (ClrBit(_eflags, VCPU_EFLAGS_ID))*/

#define _MakePageFaultErrorCode(p,wr,us) ((p) | ((wr) << 1) | ((us) << 2))

#define VCPU_LINEAR_DIR    0xffc00000
#define VCPU_LINEAR_PAGE   0x003ff000
#define VCPU_LINEAR_OFFSET 0x00000fff
#define _GetLinear_DIR(linear)    (((linear) & VCPU_LINEAR_DIR)    >> 22)
#define _GetLinear_PAGE(linear)   (((linear) & VCPU_LINEAR_PAGE)   >> 12)
#define _GetLinear_OFFSET(linear) (((linear) & VCPU_LINEAR_OFFSET) >>  0)

#define VCPU_PGENTRY_BASE  0xfffff000
#define VCPU_PGENTRY_D     0x00000040
#define VCPU_PGENTRY_A     0x00000020
#define VCPU_PGENTRY_US    0x00000004
#define VCPU_PGENTRY_RW    0x00000002
#define VCPU_PGENTRY_P     0x00000001
#define _GetPageEntry_P(pgentry)    (GetBit((pgentry), VCPU_PGENTRY_P))
#define _GetPageEntry_RW(pgentry)   (GetBit((pgentry), VCPU_PGENTRY_RW))
#define _GetPageEntry_US(pgentry)   (GetBit((pgentry), VCPU_PGENTRY_US))
#define _GetPageEntry_A(pgentry)    (GetBit((pgentry), VCPU_PGENTRY_A))
#define _GetPageEntry_D(pgentry)    (GetBit((pgentry), VCPU_PGENTRY_D))
#define _GetPageEntry_BASE(pgentry) ((pgentry) & VCPU_PGENTRY_BASE)
#define _SetPageEntry_A(pgentry)    (SetBit((pgentry), VCPU_PGENTRY_A))
#define _SetPageEntry_D(pgentry)    (SetBit((pgentry), VCPU_PGENTRY_D))

#define VCPU_SELETOR_RPL 0x0003 /* requestor's privilege level */
#define VCPU_SELETOR_TI  0x0004 /* table indicator */
#define VCPU_SELETOR_IDX 0xfff8 /* index */
#define _GetSelector_RPL(selector)    (((selector) & VCPU_SELETOR_RPL) >> 0)
#define _GetSelector_TI(selector)     (GetBit((selector), VCPU_SELETOR_TI))
#define _GetSelector_INDEX(selector)  (((selector) & VCPU_SELETOR_IDX) >> 3)
#define _GetSelector_OFFSET(selector) (((selector) & VCPU_SELETOR_IDX) >> 0)
#define _IsSelectorNull(selector)     (!_GetSelector_TI(selector) && !_GetSelector_INDEX(selector))
#define _GetCPL  (_GetSelector_RPL(vcpu.cs.selector))

#define VCPU_SEGDESC_BASE_0  0x000000ffffff0000
#define VCPU_SEGDESC_BASE_1  0xff00000000000000
#define VCPU_SEGDESC_LIMIT_0 0x000000000000ffff
#define VCPU_SEGDESC_LIMIT_1 0x000f000000000000
#define VCPU_SEGDESC_TYPE_A  0x0000010000000000 /* descriptor type: accessed */
#define VCPU_SEGDESC_TYPE_R  0x0000020000000000 /* descriptor type: readable    (code) */
#define VCPU_SEGDESC_TYPE_W  0x0000020000000000 /* descriptor type: writable    (data) */
#define VCPU_SEGDESC_TYPE_C  0x0000040000000000 /* descriptor type: conforming  (code) */
#define VCPU_SEGDESC_TYPE_E  0x0000040000000000 /* descriptor type: expand-down (data) */
#define VCPU_SEGDESC_TYPE_CD 0x0000080000000000 /* descriptor type: executable */
#define VCPU_SEGDESC_TYPE    0x00000f0000000000 /* descriptor type */
#define VCPU_SEGDESC_S       0x0000100000000000 /* descriptor system segment flag (sys=0, app=1) */
#define VCPU_SEGDESC_DPL     0x0000600000000000 /* descriptor previlege level*/
#define VCPU_SEGDESC_P       0x0000800000000000 /* segment present */
#define VCPU_SEGDESC_AVL     0x0010000000000000 /* available for programmers */
#define VCPU_SEGDESC_DB      0x0040000000000000 /* default size / b */
#define VCPU_SEGDESC_G       0x0080000000000000 /* granularity */
#define _GetSegDesc_BASE(descriptor)   \
	((((descriptor) & VCPU_SEGDESC_BASE_0) >> 16) | (((descriptor) & VCPU_SEGDESC_BASE_1) >> 32))
#define _GetSegDesc_LIMIT(descriptor)  \
	((((descriptor) & VCPU_SEGDESC_LIMIT_0) >> 0) | (((descriptor) & VCPU_SEGDESC_LIMIT_1) >> 32))
#define _GetSegDesc_TYPE(descriptor)    (((descriptor) & VCPU_SEGDESC_TYPE) >> 40)
#define _GetSegDesc_TYPE_A(descriptor)  (GetBit((descriptor), VCPU_SEGDESC_TYPE_A))
#define _GetSegDesc_TYPE_R(descriptor)  (GetBit((descriptor), VCPU_SEGDESC_TYPE_R))
#define _GetSegDesc_TYPE_W(descriptor)  (GetBit((descriptor), VCPU_SEGDESC_TYPE_W))
#define _GetSegDesc_TYPE_C(descriptor)  (GetBit((descriptor), VCPU_SEGDESC_TYPE_C))
#define _GetSegDesc_TYPE_E(descriptor)  (GetBit((descriptor), VCPU_SEGDESC_TYPE_E))
#define _GetSegDesc_TYPE_CD(descriptor) (GetBit((descriptor), VCPU_SEGDESC_TYPE_CD))
#define _GetSegDesc_S(descriptor)       (GetBit((descriptor), VCPU_SEGDESC_S))
#define _GetSegDesc_DPL(descriptor)     (((descriptor) & VCPU_SEGDESC_DPL)    >> 45)
#define _GetSegDesc_P(descriptor)       (GetBit((descriptor), VCPU_SEGDESC_P))
#define _GetSegDesc_AVL(descriptor)     (GetBit((descriptor), VCPU_SEGDESC_AVL))
#define _GetSegDesc_DB(descriptor)      (GetBit((descriptor), VCPU_SEGDESC_DB))
#define _GetSegDesc_G(descriptor)       (GetBit((descriptor), VCPU_SEGDESC_G))
#define _MakeSegDesc(base,limit,type,s,dpl,p,avl,db,g) \
	(((t_nubit64)((base)  & 0xff000000) << 32) | \
	 ((t_nubit64)((g)     & 0x00000001) << 55) | \
	 ((t_nubit64)((db)    & 0x00000001) << 54) | \
	 ((t_nubit64)((avl)   & 0x00000001) << 52) | \
	 ((t_nubit64)((limit) & 0x000f0000) << 32) | \
	 ((t_nubit64)((p)     & 0x00000001) << 47) | \
	 ((t_nubit64)((dpl)   & 0x00000003) << 45) | \
	 ((t_nubit64)((s)     & 0x00000001) << 44) | \
	 ((t_nubit64)((type)  & 0x0000000f) << 40) | \
	 ((t_nubit64)((base)  & 0x00ffffff) << 16) | \
	 ((t_nubit64)((limit) & 0x0000ffff) << 0 ))

#define VCPU_GDTR_LIMIT 0x00000000ffff
#define VCPU_GDTR_BASE  0xffffffff0000
#define _GetGDTR_LIMIT ((vcpu.gdtr & VCPU_GDTR_LIMIT) >>  0)
#define _GetGDTR_BASE  ((vcpu.gdtr & VCPU_GDTR_BASE)  >> 16)

#define _GetLDTR_LIMIT (_GetSegDesc_LIMIT(vcpu.ldtr.descriptor))

#define VCPU_IDTR_LIMIT 0x00000000ffff
#define VCPU_IDTR_BASE  0xffffffff0000

#define VCPU_CR0_PE     0x00000001
#define VCPU_CR0_PG     0x80000000
#define _GetCR0_PE  (GetBit(vcpu.cr0, VCPU_CR0_PE))
#define _GetCR0_PG  (GetBit(vcpu.cr0, VCPU_CR0_PG))

#define VCPU_CR3_BASE   0xfffff000
#define _GetCR3_BASE    (vcpu.cr3 & VCPU_CR3_BASE)

#define _LoadGDTR16(base,limit)  (vcpu.gdtr = ((t_nubit24)(base) << 16) | (t_nubit16)(limit))
#define _LoadGDTR32(base,limit)  (vcpu.gdtr = ((t_nubit32)(base) << 16) | (t_nubit16)(limit))
#define _LoadLDTR(selector)      (vcpu.ldtr = (t_nubit16)(selector))
#define _LoadIDTR16(base,limit)  (vcpu.idtr = ((t_nubit24)(base) << 16) | (t_nubit16)(limit))
#define _LoadIDTR32(base,limit)  (vcpu.idtr = ((t_nubit32)(base) << 16) | (t_nubit16)(limit))
#define _LoadTR
#define _LoadCR0
#define _LoadCR1
#define _LoadCR2
#define _LoadCR3
#define _LoadDR0
#define _LoadDR1
#define _LoadDR2
#define _LoadDR3
#define _LoadDR4
#define _LoadDR5
#define _LoadDR6
#define _LoadDR7
#define _LoadTR6
#define _LoadTR7

extern t_cpu vcpu;
#else
#include "ecpu/ecpu.h"
typedef t_ecpu t_cpu;
#define vcpu ecpu
#endif

void vcpuInit();
void vcpuReset();
void vcpuRefresh();
void vcpuFinal();

#ifdef __cplusplus
/*}_EOCD_*/
#endif

#endif
