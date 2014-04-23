/* This file is a part of NXVM project. */

// Not completely simulated.

#include "memory.h"
#include "time.h"

#include "system/vapi.h"

#include "vcpu.h"
#include "vpic.h"
#include "vcpuins.h"

#include "vpit.h"

t_pit vpit;
static clock_t t1,t2;
//static int i;

void vpitIntTick()
{
	t_float64 dt;
	t2 = clock();
	dt = (t_float64)((t2-t1)*1e3/((t_float64)CLOCKS_PER_SEC));
	//i++;
	if(dt >= VPIT_TICK) {
		//vapiPrint("%lf,%d\n",dt,i);
		//i = 0;
		vpicSetIRQ(0x00);
		//vapiPrint("%lf\n",dt);
		t1 = t2;
	}
}

void IO_Read_0040() {}
void IO_Read_0041() {}
void IO_Read_0042() {}
void IO_Read_0043() {}
void IO_Write_0040() {}
void IO_Write_0041() {}
void IO_Write_0042() {}
void IO_Write_0043()
{
	switch(vcpu.al>>0x06) {
	case 0x00:	vpit.mode0 = vcpu.al;break;
	case 0x01:	vpit.mode1 = vcpu.al;break;
	case 0x02:	vpit.mode2 = vcpu.al;break;
	case 0x03:	vpit.rcw = vcpu.al;break;
	default:break;}
}

void PITInit()
{
	t1 = clock();
	memset(&vpit,0,sizeof(t_pit));
	vcpuinsInPort[0x0040] = (t_faddrcc)IO_Read_0040;
	vcpuinsInPort[0x0041] = (t_faddrcc)IO_Read_0041;
	vcpuinsInPort[0x0042] = (t_faddrcc)IO_Read_0042;
	vcpuinsInPort[0x0043] = (t_faddrcc)IO_Read_0043;
	vcpuinsOutPort[0x0040] = (t_faddrcc)IO_Write_0040;
	vcpuinsOutPort[0x0041] = (t_faddrcc)IO_Write_0041;
	vcpuinsOutPort[0x0042] = (t_faddrcc)IO_Write_0042;
	vcpuinsOutPort[0x0043] = (t_faddrcc)IO_Write_0043;
}
void PITTerm() {}