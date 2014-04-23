/* This file is a part of NXVM project. */

// CPU Instruction Set: Intel 8086

#ifndef NXVM_VCPUINS_H
#define NXVM_VCPUINS_H

#ifdef __cplusplus
/*extern "C" {*/
#endif

#include "vglobal.h"

typedef enum {
	ADD8,ADD16,
	//OR8,OR16,
	ADC8,ADC16,
	SBB8,SBB16,
	/*AND8,AND16,*/
	SUB8,SUB16,
	/*XOR8,XOR16,*/
	CMP8,CMP16
	/*TEST8,TEST16*/
} t_cpuins_type;
typedef enum {
	PREFIX_REP_NONE,
	PREFIX_REP_REPZ,
	PREFIX_REP_REPZNZ
} t_cpuins_prefix_rep;
typedef t_bool t_cpuins_prefix;
typedef struct {
	t_nubit32 oldeip;
	t_bool    flaginsloop;
	t_faddrcc table[0x100];
	t_vaddrcc rm, r, imm;
	t_nubitcc opr1, opr2, result, bit;
	t_cpuins_type type;
	t_cpu_segreg oldcs, overds, overss;
	t_cpuins_prefix_rep prefix_rep;
	t_cpuins_prefix     prefix_lock;
	t_cpuins_prefix     prefix_oprsize;
	t_cpuins_prefix     prefix_addrsize;
	t_nubit32 except, excode;
} t_cpuins;

#define VCPUINS_EXCEPT_GP 0x00000001 /* general protection */
#define VCPUINS_EXCEPT_NP 0x00000002 /* segment not present */
#define VCPUINS_EXCEPT_PF 0x00000004 /* page fault */

extern t_cpuins vcpuins;

void vcpuinsInit();
void vcpuinsReset();
void vcpuinsRefresh();
void vcpuinsFinal();

#ifdef __cplusplus
/*}_EOCD_*/
#endif

#endif
