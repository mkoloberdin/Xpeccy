#include "i80286.h"
#include <stdio.h>

// TODO: protected mode
// segment:
//	b0,1 = rpl (request privelege level)
//	b2 = table type (0:GDT/1:current LDT)
//	b3..15 = descriptor index in table
// real.addr = descr.base(24 bit)+offset(adr)
// flag:
// [7:P][6,5:DPL][4:1][3:1][2:C][1:R][0:A]	code; R:segment can be readed (not only imm adr)
// [7:P][6,5:DPL][4:1][3:0][2:D][1:W][0:A]	data; W:segment is writeable
// [7:P][6,5:DPL][4:0][3-0:TYPE]		system
// rd/wr with segment P=0 -> interrupt

unsigned char i286_mrd(CPU* cpu, xSegPtr seg, unsigned short adr) {
	cpu->t++;
	return cpu->mrd(seg.base + adr, 0, cpu->data) & 0xff;
}

void i286_mwr(CPU* cpu, xSegPtr seg, unsigned short adr, int val) {
	cpu->t++;
	cpu->mwr(seg.base + adr, val, cpu->data);
}

// iord/wr

unsigned short i286_ird(CPU* cpu, int adr) {
	return cpu->ird(adr, cpu->data) & 0xffff;
}

void i286_iwr(CPU* cpu, int adr, int val) {
	cpu->iwr(adr, val, cpu->data);
}

// imm: byte from ip
unsigned char i286_rd_imm(CPU* cpu) {
	unsigned char res = i286_mrd(cpu, cpu->cs, cpu->pc);
	cpu->pc++;
	return res;
}

// immw:word from ip
unsigned short i286_rd_immw(CPU* cpu) {
	PAIR(w,h,l) rx;
	rx.l = i286_rd_imm(cpu);
	rx.h = i286_rd_imm(cpu);
	return rx.w;
}

// set segment registers

xSegPtr i286_cash_seg(CPU* cpu, unsigned short val) {
	xSegPtr p;
	PAIR(w,h,l)off;
	int adr;
	unsigned short tmp;
	p.idx = val;
	if (cpu->mode == I286_MOD_REAL) {
		p.flag = 0xf2;		// present, dpl=3, segment, data, writeable
		p.base = val << 4;
		p.limit = 0xffff;
	} else {
		adr = (val & 4) ? cpu->ldtr.base : cpu->gdtr.base;
		adr += val & 0xfff8;
		off.l = cpu->mrd(adr++, 0, cpu->data);
		off.h = cpu->mrd(adr++, 0, cpu->data);
		p.limit = off.w;
		off.l = cpu->mrd(adr++, 0, cpu->data);
		off.h = cpu->mrd(adr++, 0, cpu->data);
		tmp = cpu->mrd(adr++, 0, cpu->data);
		p.base = (tmp << 16) | off.w;
		p.flag = cpu->mrd(adr, 0, cpu->data);
	}
	return p;
}

// stack

void i286_push(CPU* cpu, unsigned short w) {
	cpu->sp--;
	i286_mwr(cpu, cpu->ss, cpu->sp, (w >> 8) & 0xff);
	cpu->sp--;
	i286_mwr(cpu, cpu->ss, cpu->sp, w & 0xff);
}

unsigned short i286_pop(CPU* cpu) {
	PAIR(w,h,l) rx;
	rx.l = i286_mrd(cpu, cpu->ss, cpu->sp);
	cpu->sp++;
	rx.h = i286_mrd(cpu, cpu->ss, cpu->sp);
	cpu->sp++;
	return rx.w;
}

// INT n

void i286_interrupt(CPU* cpu, int n) {
	cpu->intrq |= I286_INT;
	cpu->intvec = n;
}

// mod r/m

int i286_get_reg(CPU* cpu, int wrd) {
	int res = -1;
	if (wrd) {
		switch((cpu->mod >> 3) & 7) {
			case 0: res = cpu->al; break;
			case 1: res = cpu->cl; break;
			case 2: res = cpu->dl; break;
			case 3: res = cpu->bl; break;
			case 4: res = cpu->ah; break;
			case 5: res = cpu->ch; break;
			case 6: res = cpu->dh; break;
			case 7: res = cpu->bh; break;
		}
		res &= 0xff;
	} else {
		switch((cpu->mod >> 3) & 7) {
			case 0: res = cpu->ax; break;
			case 1: res = cpu->cx; break;
			case 2: res = cpu->dx; break;
			case 3: res = cpu->bx; break;
			case 4: res = cpu->sp; break;
			case 5: res = cpu->bp; break;
			case 6: res = cpu->si; break;
			case 7: res = cpu->di; break;
		}
		res &= 0xffff;
	}
	return res;
}

void i286_set_reg(CPU* cpu, int val, int wrd) {
	if (wrd) {
		val &= 0xffff;
		switch((cpu->mod >> 3) & 7) {
			case 0: cpu->ax = val; break;
			case 1: cpu->cx = val; break;
			case 2: cpu->dx = val; break;
			case 3: cpu->bx = val; break;
			case 4: cpu->sp = val; break;
			case 5: cpu->bp = val; break;
			case 6: cpu->si = val; break;
			case 7: cpu->di = val; break;
		}
	} else {
		val &= 0xff;
		switch((cpu->mod >> 3) & 7) {
			case 0: cpu->al = val; break;
			case 1: cpu->cl = val; break;
			case 2: cpu->dl = val; break;
			case 3: cpu->bl = val; break;
			case 4: cpu->ah = val; break;
			case 5: cpu->ch = val; break;
			case 6: cpu->dh = val; break;
			case 7: cpu->bh = val; break;
		}
	}
}

// read mod, calculate effective address in cpu->tmpi, read byte/word from EA to cpu->tmpw, set register N to cpu->twrd
// modbyte: [7.6:mod][5.4.3:regN][2.1.0:adr/reg]
void i286_rd_ea(CPU* cpu, int wrd) {
	cpu->tmpw = 0;	// = disp
	cpu->mod = i286_rd_imm(cpu);
	cpu->twrd = i286_get_reg(cpu, wrd);
	if ((cpu->mod & 0xc0) == 0x40) {
		cpu->ltw = i286_rd_imm(cpu);
		cpu->htw = (cpu->ltw & 0x80) ? 0xff : 0x00;
	} else if ((cpu->mod & 0xc0) == 0x80) {
		cpu->tmpw = i286_rd_immw(cpu);
	}
	if ((cpu->mod & 0xc0) == 0xc0) {
		if (wrd) {
			switch(cpu->mod & 7) {
				case 0: cpu->tmpw = cpu->ax; break;
				case 1: cpu->tmpw = cpu->cx; break;
				case 2: cpu->tmpw = cpu->dx; break;
				case 3: cpu->tmpw = cpu->bx; break;
				case 4: cpu->tmpw = cpu->sp; break;
				case 5: cpu->tmpw = cpu->bp; break;
				case 6: cpu->tmpw = cpu->si; break;
				case 7: cpu->tmpw = cpu->di; break;
			}
		} else {
			cpu->htw = 0;
			switch(cpu->mod & 7) {
				case 0: cpu->ltw = cpu->al; break;
				case 1: cpu->ltw = cpu->cl; break;
				case 2: cpu->ltw = cpu->dl; break;
				case 3: cpu->ltw = cpu->bl; break;
				case 4: cpu->ltw = cpu->ah; break;
				case 5: cpu->ltw = cpu->ch; break;
				case 6: cpu->ltw = cpu->dh; break;
				case 7: cpu->ltw = cpu->bh; break;
			}
		}
		cpu->tmpi = -1;		// reg
	} else {
		switch(cpu->mod & 0x07) {
			case 0: cpu->tmpi = cpu->bx + cpu->si + cpu->tmpw;
				if (cpu->seg.idx < 0) cpu->seg = cpu->ds;
				break;
			case 1: cpu->tmpi = cpu->bx + cpu->di + cpu->tmpw;
				if (cpu->seg.idx < 0) cpu->seg = cpu->ds;
				break;
			case 2: cpu->tmpi = cpu->bp + cpu->si + cpu->tmpw;
				if (cpu->seg.idx < 0) cpu->seg = cpu->ss;
				break;
			case 3: cpu->tmpi = cpu->bp + cpu->di + cpu->tmpw;
				if (cpu->seg.idx < 0) cpu->seg = cpu->ss;
				break;
			case 4: cpu->tmpi = cpu->si + cpu->tmpw;
				if (cpu->seg.idx < 0) cpu->seg = cpu->ds;
				break;
			case 5: cpu->tmpi = cpu->di + cpu->tmpw;
				if (cpu->seg.idx < 0) cpu->seg = cpu->ds;	// TODO: or es in some opcodes (not overrideable)
				break;
			case 6:	if (cpu->mod & 0xc0) {
					cpu->tmpi = cpu->bp + cpu->tmpw;
					if (cpu->seg.idx < 0) cpu->seg = cpu->ss;
				} else {
					cpu->tmpi = cpu->tmpw;
					if (cpu->seg.idx < 0) cpu->seg = cpu->ds;
				}
				break;
			case 7: cpu->tmpi = cpu->bx + cpu->tmpw;
				if (cpu->seg.idx < 0) cpu->seg = cpu->ds;
				break;
		}
		cpu->tmpi = 0;		// memory address
		cpu->ea.seg = cpu->seg;
		cpu->ea.adr = cpu->tmpi & 0xffff;
		cpu->ltw = i286_mrd(cpu, cpu->ea.seg, cpu->ea.adr);
		cpu->htw = wrd ? i286_mrd(cpu, cpu->ea.seg, cpu->ea.adr + 1) : 0;
	}
}

// must be called after i286_rd_ea, cpu->ea must be calculated, cpu->mod setted
void i286_wr_ea(CPU* cpu, int val, int wrd) {
	if (cpu->tmpi < 0) {		// this is a reg
		if (wrd) {
			switch(cpu->mod & 7) {
				case 0: cpu->ax = val & 0xffff; break;
				case 1: cpu->cx = val & 0xffff; break;
				case 2: cpu->dx = val & 0xffff; break;
				case 3: cpu->bx = val & 0xffff; break;
				case 4: cpu->sp = val & 0xffff; break;
				case 5: cpu->bp = val & 0xffff; break;
				case 6: cpu->si = val & 0xffff; break;
				case 7: cpu->di = val & 0xffff; break;
			}
		} else {
			switch(cpu->mod & 7) {
				case 0: cpu->al = val & 0xff; break;
				case 1: cpu->cl = val & 0xff; break;
				case 2: cpu->dl = val & 0xff; break;
				case 3: cpu->bl = val & 0xff; break;
				case 4: cpu->ah = val & 0xff; break;
				case 5: cpu->ch = val & 0xff; break;
				case 6: cpu->dh = val & 0xff; break;
				case 7: cpu->bh = val & 0xff; break;
			}
		}
	} else {
		i286_mwr(cpu, cpu->ea.seg, cpu->ea.adr, val & 0xff);
		if (wrd) i286_mwr(cpu, cpu->ea.seg, cpu->ea.adr + 1, (val >> 8) & 0xff);
	}
}

// add/adc

static const int i286_add_FO[8] = {0, 0, 0, 1, 1, 0, 0, 0};

unsigned short i286_add8(CPU* cpu, unsigned char p1, unsigned char p2, int rf) {
	cpu->f &= ~(I286_FS | I286_FZ | I286_FP);
	if (rf) cpu->f &= ~(I286_FO | I286_FC | I286_FA);
	unsigned short res = p1 + p2;
	cpu->tmp = ((p1 & 0x80) >> 7) | ((p2 & 0x80) >> 6) | ((res & 0x80) >> 5);
	if (i286_add_FO[cpu->tmp]) cpu->f |= I286_FO;
	if (res & 0x80) cpu->f |= I286_FS;
	if (!(res & 0xff)) cpu->f |= I286_FZ;
	if ((p1 & 15) + (p2 & 15) > 15) cpu->f |= I286_FA;
	if (parity(res & 0xff)) cpu->f |= I286_FP;
	if (res > 0xff) cpu->f |= I286_FC;
	return res & 0xff;
}

unsigned short i286_add16(CPU* cpu, unsigned short p1, unsigned short p2, int rf) {
	cpu->f &= ~(I286_FS | I286_FZ | I286_FP);
	if (rf) cpu->f &= ~(I286_FO | I286_FC | I286_FA);
	int res = p1 + p2;
	cpu->tmp = ((p1 & 0x8000) >> 15) | ((p2 & 0x8000) >> 14) | ((res & 0x8000) >> 13);
	if (i286_add_FO[cpu->tmp]) cpu->f |= I286_FO;
	if (res & 0x8000) cpu->f |= I286_FS;
	if (!(res & 0xffff)) cpu->f |= I286_FZ;
	if ((p1 & 0xfff) + (p2 & 0xfff) > 0xfff) cpu->f |= I286_FA;
	if (parity(res & 0xffff)) cpu->f |= I286_FP;
	if (res > 0xffff) cpu->f |= I286_FC;
	return res & 0xffff;
}

// 00,mod: add eb,rb	EA.byte += reg.byte
void i286_op00(CPU* cpu) {
	i286_rd_ea(cpu, 0);
	cpu->tmpw = i286_add8(cpu, cpu->ltw, cpu->lwr, 1);
	i286_wr_ea(cpu, cpu->ltw, 0);
}

// 01,mod: add ew,rw	EA.word += reg.word
void i286_op01(CPU* cpu) {
	i286_rd_ea(cpu, 1);
	cpu->tmpw = i286_add16(cpu, cpu->tmpw, cpu->twrd, 1);
	i286_wr_ea(cpu, cpu->tmpw, 1);
}

// 02,mod: add rb,eb	reg.byte += EA.byte
void i286_op02(CPU* cpu) {
	i286_rd_ea(cpu, 0);
	cpu->tmpw = i286_add8(cpu, cpu->ltw, cpu->lwr, 1);
	i286_set_reg(cpu, cpu->ltw, 0);
}

// 03,mod: add rw,ew	reg.word += EA.word
void i286_op03(CPU* cpu) {
	i286_rd_ea(cpu, 1);
	cpu->tmpw = i286_add16(cpu, cpu->tmpw, cpu->twrd, 1);
	i286_set_reg(cpu, cpu->tmpw, 1);
}

// 04,db: add AL,db	AL += db
void i286_op04(CPU* cpu) {
	cpu->lwr = i286_rd_imm(cpu);
	cpu->al = i286_add8(cpu, cpu->al, cpu->lwr, 1);
}

// 05,dw: add AX,dw	AX += dw
void i286_op05(CPU* cpu) {
	cpu->twrd = i286_rd_immw(cpu);
	cpu->ax = i286_add16(cpu, cpu->ax, cpu->twrd, 1);
}

// 06: push es
void i286_op06(CPU* cpu) {
	i286_push(cpu, cpu->es.idx);
}

// 07: pop es
void i286_op07(CPU* cpu) {
	cpu->tmpw = i286_pop(cpu);
	cpu->es = i286_cash_seg(cpu, cpu->tmpw);
}

// or

unsigned char i286_or8(CPU* cpu, unsigned char p1, unsigned char p2) {
	unsigned char res =  p1 | p2;
	cpu->f &= ~(I286_FO | I286_FS | I286_FZ | I286_FP | I286_FC);
	if (res & 0x80) cpu->f |= I286_FS;
	if (!res) cpu->f |= I286_FZ;
	if (parity(res)) cpu->f |= I286_FP;
	return res;
}

unsigned short i286_or16(CPU* cpu, unsigned short p1, unsigned short p2) {
	unsigned short res =  p1 | p2;
	cpu->f &= ~(I286_FO | I286_FS | I286_FZ | I286_FP | I286_FC);
	if (res & 0x8000) cpu->f |= I286_FS;
	if (!res) cpu->f |= I286_FZ;
	if (parity(res)) cpu->f |= I286_FP;
	return res;
}

// 08,mod: or eb,rb
void i286_op08(CPU* cpu) {
	i286_rd_ea(cpu, 0);
	cpu->ltw = i286_or8(cpu, cpu->ltw, cpu->lwr);
	i286_wr_ea(cpu, cpu->ltw, 0);
}

// 09,mod: or ew,rw
void i286_op09(CPU* cpu) {
	i286_rd_ea(cpu, 1);
	cpu->tmpw = i286_or16(cpu, cpu->tmpw, cpu->twrd);
	i286_wr_ea(cpu, cpu->ltw, 1);
}

// 0a,mod: or rb,eb
void i286_op0A(CPU* cpu) {
	i286_rd_ea(cpu, 0);
	cpu->ltw = i286_or8(cpu, cpu->ltw, cpu->lwr);
	i286_set_reg(cpu, cpu->ltw, 0);
}

// 0b,mod: or rw,ew
void i286_op0B(CPU* cpu) {
	i286_rd_ea(cpu, 1);
	cpu->tmpw = i286_or16(cpu, cpu->tmpw, cpu->twrd);
	i286_set_reg(cpu, cpu->ltw, 1);
}

// 0c,db: or al,db
void i286_op0C(CPU* cpu) {
	cpu->tmp = i286_rd_imm(cpu);
	cpu->al = i286_or8(cpu, cpu->al, cpu->tmp);
}

// 0d,dw: or ax,dw
void i286_op0D(CPU* cpu) {
	cpu->twrd = i286_rd_immw(cpu);
	cpu->ax = i286_or16(cpu, cpu->ax, cpu->twrd);
}

// 0e: push cs
void i286_op0E(CPU* cpu) {
	i286_push(cpu, cpu->cs.idx);
}

// 0f: prefix
extern opCode i286_0f_tab[256];

void i286_op0F(CPU* cpu) {
	cpu->opTab = i286_0f_tab;
}

// 10,mod: adc eb,rb
void i286_op10(CPU* cpu) {
	i286_rd_ea(cpu, 0);
	cpu->tmpb = cpu->f & I286_FC;
	cpu->tmpw = i286_add8(cpu, cpu->ltw, cpu->lwr, 1);
	if (cpu->tmpb)
		cpu->tmpw = i286_add8(cpu, cpu->ltw, 1, 0);	// add 1 and not reset flags
	i286_wr_ea(cpu, cpu->tmpw, 0);
}

// 11,mod: adc ew,rw
void i286_op11(CPU* cpu) {
	i286_rd_ea(cpu, 1);
	cpu->tmpb = cpu->f & I286_FC;
	cpu->tmpw = i286_add16(cpu, cpu->tmpw, cpu->twrd, 1);
	if (cpu->tmpb)
		cpu->tmpw = i286_add16(cpu, cpu->tmpw, 1, 0);
	i286_wr_ea(cpu, cpu->tmpw, 0);
}

// 12,mod: adc rb,eb
void i286_op12(CPU* cpu) {
	i286_rd_ea(cpu, 0);
	cpu->tmpb = cpu->f & I286_FC;
	cpu->tmpw = i286_add8(cpu, cpu->ltw, cpu->lwr, 1);
	if (cpu->tmpb)
		cpu->tmpw = i286_add8(cpu, cpu->ltw, 1, 0);	// add 1 and not reset flags
	i286_set_reg(cpu, cpu->tmpw, 0);
}

// 13,mod: adc rw,ew
void i286_op13(CPU* cpu) {
	i286_rd_ea(cpu, 1);
	cpu->tmpb = cpu->f & I286_FC;
	cpu->tmpw = i286_add16(cpu, cpu->tmpw, cpu->twrd, 1);
	if (cpu->tmpb)
		cpu->tmpw = i286_add16(cpu, cpu->tmpw, 1, 0);
	i286_set_reg(cpu, cpu->tmpw, 0);
}

// 14,db: adc al,db
void i286_op14(CPU* cpu) {
	cpu->lwr = i286_rd_imm(cpu);
	cpu->tmpb = cpu->f & I286_FC;
	cpu->al = i286_add8(cpu, cpu->al, cpu->lwr, 1);
	if (cpu->tmpb) cpu->al = i286_add8(cpu, cpu->al, 1, 0);
}

// 15,dw: adc ax,dw
void i286_op15(CPU* cpu) {
	cpu->twrd = i286_rd_immw(cpu);
	cpu->tmpb = cpu->f & I286_FC;
	cpu->ax = i286_add16(cpu, cpu->ax, cpu->twrd, 1);
	if (cpu->tmpb) cpu->ax = i286_add16(cpu, cpu->ax, 1, 0);
}

// 16: push ss
void i286_op16(CPU* cpu) {
	i286_push(cpu, cpu->ss.idx);
}

// 17: pop ss
void i286_op17(CPU* cpu) {
	cpu->tmpw = i286_pop(cpu);
	cpu->ss = i286_cash_seg(cpu, cpu->tmpw);
}

// sub/sbc

static const int i286_sub_FO[8] = {0, 1, 0, 0, 0, 0, 1, 0};

unsigned char i286_sub8(CPU* cpu, unsigned char p1, unsigned char p2, int rf) {
	cpu->f &= ~(I286_FS | I286_FZ | I286_FP);
	if (rf) cpu->f &= ~(I286_FO | I286_FC | I286_FA);
	unsigned short res = p1 - p2;
	cpu->tmp = ((p1 & 0x80) >> 7) | ((p2 & 0x80) >> 6) | ((res & 0x80) >> 5);
	if (i286_sub_FO[cpu->tmp & 7]) cpu->f |= I286_FO;
	if (res & 0x80) cpu->f |= I286_FS;
	if (!(res & 0xff)) cpu->f |= I286_FZ;
	if (parity(res & 0xff)) cpu->f |= I286_FP;
	if (res & 0xff00) cpu->f |= I286_FC;
	if ((p1 & 0x0f) < (p2 & 0x0f)) cpu->f |= I286_FA;
	return res & 0xff;
}

unsigned short i286_sub16(CPU* cpu, unsigned short p1, unsigned short p2, int rf) {
	cpu->f &= ~(I286_FS | I286_FZ | I286_FP);
	if (rf) cpu->f &= ~(I286_FO | I286_FC | I286_FA);
	int res = p1 - p2;
	cpu->tmp = ((p1 & 0x8000) >> 15) | ((p2 & 0x8000) >> 14) | ((res & 0x8000) >> 13);
	if (i286_sub_FO[cpu->tmp & 7]) cpu->f |= I286_FO;
	if (res & 0x8000) cpu->f |= I286_FS;
	if (!(res & 0xffff)) cpu->f |= I286_FZ;
	if (parity(res & 0xffff)) cpu->f |= I286_FP;
	if (p2 > p1) cpu->f |= I286_FC;
	if ((p1 & 0x0fff) < (p2 & 0x0fff)) cpu->f |= I286_FA;
	return res & 0xffff;
}

// 18,mod: sbb eb,rb	NOTE: sbc
void i286_op18(CPU* cpu) {
	i286_rd_ea(cpu, 0);
	cpu->tmpb = cpu->f & I286_FC;
	cpu->ltw = i286_sub8(cpu, cpu->ltw, cpu->lwr, 1);
	if (cpu->tmpb) cpu->ltw = i286_sub8(cpu, cpu->ltw, 1, 0);
	i286_wr_ea(cpu, cpu->ltw, 0);
}

// 19,mod: sbb ew,rw
void i286_op19(CPU* cpu) {
	i286_rd_ea(cpu, 1);
	cpu->tmpb = cpu->f & I286_FC;
	cpu->tmpw = i286_sub16(cpu, cpu->tmpw, cpu->twrd, 1);
	if (cpu->tmpb) cpu->tmpw = i286_sub16(cpu, cpu->tmpw, 1, 0);
	i286_wr_ea(cpu, cpu->tmpw, 1);
}

// 1a,mod: sbb rb,eb
void i286_op1A(CPU* cpu) {
	i286_rd_ea(cpu, 0);
	cpu->tmpb = cpu->f & I286_FC;
	cpu->ltw = i286_sub8(cpu, cpu->lwr, cpu->ltw, 1);
	if (cpu->tmpb) cpu->ltw = i286_sub8(cpu, cpu->ltw, 1, 0);
	i286_set_reg(cpu, cpu->ltw, 0);
}

// 1b,mod: sbb rw,ew
void i286_op1B(CPU* cpu) {
	i286_rd_ea(cpu, 1);
	cpu->tmpb = cpu->f & I286_FC;
	cpu->tmpw = i286_sub16(cpu, cpu->twrd, cpu->tmpw, 1);
	if (cpu->tmpb) cpu->tmpw = i286_sub16(cpu, cpu->tmpw, 1, 0);
	i286_set_reg(cpu, cpu->tmpw, 1);
}

// 1c,db: sbb al,db
void i286_op1C(CPU* cpu) {
	cpu->lwr = i286_rd_imm(cpu);
	cpu->tmpb = cpu->f & I286_FC;
	cpu->al = i286_sub8(cpu, cpu->al, cpu->lwr, 1);
	if (cpu->tmpb) cpu->al = i286_sub8(cpu, cpu->al, 1, 0);
}

// 1d,dw: sbb ax,dw
void i286_op1D(CPU* cpu) {
	cpu->twrd = i286_rd_immw(cpu);
	cpu->tmpb = cpu->f & I286_FC;
	cpu->ax = i286_sub16(cpu, cpu->ax, cpu->twrd, 1);
	if (cpu->tmpb) cpu->ax = i286_sub16(cpu, cpu->ax, 1, 0);
}

// 1e: push ds
void i286_op1E(CPU* cpu) {
	i286_push(cpu, cpu->ds.idx);
}

// 1f: pop ds
void i286_op1F(CPU* cpu) {
	cpu->tmpw = i286_pop(cpu);
	cpu->ds = i286_cash_seg(cpu, cpu->tmpw);
}

// and

unsigned char i286_and8(CPU* cpu, unsigned char p1, unsigned char p2) {
	cpu->f &= ~(I286_FO | I286_FS | I286_FP | I286_FZ | I286_FC);
	p1 &= p2;
	if (p1 & 0x80) cpu->f |= I286_FS;
	if (!p1) cpu->f |= I286_FZ;
	if (parity(p1)) cpu->f |= I286_FP;
	return p1;
}

unsigned short i286_and16(CPU* cpu, unsigned short p1, unsigned short p2) {
	cpu->f &= ~(I286_FO | I286_FS | I286_FP | I286_FZ | I286_FC);
	p1 &= p2;
	if (p1 & 0x8000) cpu->f |= I286_FS;
	if (!p1) cpu->f |= I286_FZ;
	if (parity(p1)) cpu->f |= I286_FP;
	return p1;
}

// 20,mod: and eb,rb
void i286_op20(CPU* cpu) {
	i286_rd_ea(cpu, 0);
	cpu->ltw = i286_and8(cpu, cpu->ltw, cpu->lwr);
	i286_wr_ea(cpu, cpu->ltw, 0);
}

// 21,mod: and ew,rw
void i286_op21(CPU* cpu) {
	i286_rd_ea(cpu, 1);
	cpu->tmpw = i286_and16(cpu, cpu->tmpw, cpu->twrd);
	i286_wr_ea(cpu, cpu->tmpw, 1);
}

// 22,mod: and rb,eb
void i286_op22(CPU* cpu) {
	i286_rd_ea(cpu, 0);
	cpu->ltw = i286_and8(cpu, cpu->ltw, cpu->lwr);
	i286_set_reg(cpu, cpu->ltw, 0);
}

// 23,mod: and rw,ew
void i286_op23(CPU* cpu) {
	i286_rd_ea(cpu, 1);
	cpu->tmpw = i286_and16(cpu, cpu->tmpw, cpu->twrd);
	i286_set_reg(cpu, cpu->tmpw, 1);
}

// 24,db: and al,db
void i286_op24(CPU* cpu) {
	cpu->ltw = i286_rd_imm(cpu);
	cpu->al = i286_and8(cpu, cpu->al, cpu->ltw);
}

// 25,dw: and ax,dw
void i286_op25(CPU* cpu) {
	cpu->twrd = i286_rd_immw(cpu);
	cpu->ax = i286_and16(cpu, cpu->ax, cpu->tmpw);
}

// 26: ES segment override prefix
void i286_op26(CPU* cpu) {
	cpu->seg = cpu->es;
}

// 27: daa
void i286_op27(CPU* cpu) {
	if (((cpu->al & 0x0f) > 9) || (cpu->f & I286_FA)) {
		cpu->al += 6;
		cpu->f |= I286_FA;
	} else {
		cpu->f &= ~I286_FA;
	}
	if ((cpu->al > 0x9f) || (cpu->f & I286_FC)) {
		cpu->al += 0x60;
		cpu->f |= I286_FC;
	} else {
		cpu->f &= ~I286_FC;
	}
	cpu->f &= ~(I286_FS | I286_FZ | I286_FP);
	if (cpu->al & 0x80) cpu->f |= I286_FS;
	if (!cpu->al) cpu->f |= I286_FZ;
	if (parity(cpu->al)) cpu->f |= I286_FP;
}

// 28,mod: sub eb,rb
void i286_op28(CPU* cpu) {
	i286_rd_ea(cpu, 0);
	cpu->ltw = i286_sub8(cpu, cpu->ltw, cpu->lwr, 1);
	i286_wr_ea(cpu, cpu->ltw, 0);
}

// 29,mod: sub ew,rw
void i286_op29(CPU* cpu) {
	i286_rd_ea(cpu, 1);
	cpu->tmpw = i286_sub16(cpu, cpu->tmpw, cpu->twrd, 1);
	i286_wr_ea(cpu, cpu->tmpw, 1);
}

// 2a,mod: sub rb,eb
void i286_op2A(CPU* cpu) {
	i286_rd_ea(cpu, 0);
	cpu->ltw = i286_sub8(cpu, cpu->lwr, cpu->ltw, 1);
	i286_set_reg(cpu, cpu->ltw, 0);
}

// 2b,mod: sub rw,ew
void i286_op2B(CPU* cpu) {
	i286_rd_ea(cpu, 1);
	cpu->tmpw = i286_sub16(cpu, cpu->twrd, cpu->tmpw, 1);
	i286_set_reg(cpu, cpu->tmpw, 1);
}

// 2c,db: sub al,db
void i286_op2C(CPU* cpu) {
	cpu->ltw = i286_rd_imm(cpu);
	cpu->al =  i286_sub8(cpu, cpu->al, cpu->ltw, 1);
}

// 2d,dw: sub ax,dw
void i286_op2D(CPU* cpu) {
	cpu->tmpw = i286_rd_immw(cpu);
	cpu->ax =  i286_sub16(cpu, cpu->ax, cpu->tmpw, 1);
}

// 2e: CS segment override prefix
void i286_op2E(CPU* cpu) {
	cpu->seg = cpu->cs;
}

// 2f: das
void i286_op2F(CPU* cpu) {
	if (((cpu->al & 15) > 9) || (cpu->f & I286_FA)) {
		cpu->al -= 9;
		cpu->f |= I286_FA;
	} else {
		cpu->f &= ~I286_FA;
	}
	if ((cpu->al > 0x9f) || (cpu->f & I286_FC)) {
		cpu->al -= 0x60;
		cpu->f |= I286_FC;
	} else {
		cpu->f &= ~I286_FC;
	}
	cpu->f &= ~(I286_FS | I286_FZ | I286_FP);
	if (cpu->al & 0x80) cpu->f |= I286_FS;
	if (!cpu->al) cpu->f |= I286_FZ;
	if (parity(cpu->al)) cpu->f |= I286_FP;
}

// xor

unsigned char i286_xor8(CPU* cpu, unsigned char p1, unsigned char p2) {
	p1 ^= p2;
	cpu->f &= ~(I286_FO | I286_FS | I286_FZ | I286_FP | I286_FC);
	if (p1 & 0x80) cpu->f |= I286_FS;
	if (!p1) cpu->f |= I286_FZ;
	if (parity(p1)) cpu->f |= I286_FP;
	return p1;
}

unsigned short i286_xor16(CPU* cpu, unsigned short p1, unsigned short p2) {
	p1 ^= p2;
	cpu->f &= ~(I286_FO | I286_FS | I286_FZ | I286_FP | I286_FC);
	if (p1 & 0x8000) cpu->f |= I286_FS;
	if (!p1) cpu->f |= I286_FZ;
	if (parity(p1)) cpu->f |= I286_FP;
	return p1;
}

// 30,mod: xor eb,rb
void i286_op30(CPU* cpu) {
	i286_rd_ea(cpu, 0);
	cpu->ltw = i286_xor8(cpu, cpu->ltw, cpu->lwr);
	i286_wr_ea(cpu, cpu->ltw, 0);
}

// 31,mod: xor ew,rw
void i286_op31(CPU* cpu) {
	i286_rd_ea(cpu, 1);
	cpu->tmpw = i286_xor16(cpu, cpu->tmpw, cpu->twrd);
	i286_wr_ea(cpu, cpu->tmpw, 1);
}

// 32,mod: xor rb,eb
void i286_op32(CPU* cpu) {
	i286_rd_ea(cpu, 0);
	cpu->ltw = i286_xor8(cpu, cpu->ltw, cpu->lwr);
	i286_set_reg(cpu, cpu->ltw, 0);
}

// 33,mod: xor rw,ew
void i286_op33(CPU* cpu) {
	i286_rd_ea(cpu, 1);
	cpu->tmpw = i286_xor16(cpu, cpu->tmpw, cpu->twrd);
	i286_set_reg(cpu, cpu->tmpw, 1);
}

// 34,db: xor al,db
void i286_op34(CPU* cpu) {
	cpu->ltw = i286_rd_imm(cpu);
	cpu->al = i286_xor8(cpu, cpu->al, cpu->ltw);
}

// 35,dw: xor ax,dw
void i286_op35(CPU* cpu) {
	cpu->tmpw = i286_rd_immw(cpu);
	cpu->ax = i286_xor16(cpu, cpu->ax, cpu->tmpw);
}

// 36: SS segment override prefix
void i286_op36(CPU* cpu) {
	cpu->seg = cpu->ss;
}

// 37: aaa
void i286_op37(CPU* cpu) {
	if (((cpu->al & 0x0f) > 0x09) || (cpu->f & I286_FA)) {
		cpu->al += 6;
		cpu->ah++;
		cpu->f |= (I286_FA | I286_FC);
	} else {
		cpu->f &= ~(I286_FA | I286_FC);
	}
}

// 38,mod: cmp eb,rb
void i286_op38(CPU* cpu) {
	i286_rd_ea(cpu, 0);
	cpu->tmp = i286_sub8(cpu, cpu->ltw, cpu->lwr, 1);
}

// 39,mod: cmp ew,rw
void i286_op39(CPU* cpu) {
	i286_rd_ea(cpu, 1);
	cpu->tmpw = i286_sub16(cpu, cpu->tmpw, cpu->twrd, 1);
}

// 3a,mod: cmp rb,eb
void i286_op3A(CPU* cpu) {
	i286_rd_ea(cpu, 0);
	cpu->tmp = i286_sub8(cpu, cpu->lwr, cpu->ltw, 1);
}

// 3b,mod: cmp rw,ew
void i286_op3B(CPU* cpu) {
	i286_rd_ea(cpu, 1);
	cpu->tmpw = i286_sub16(cpu, cpu->twrd, cpu->tmpw, 1);
}

// 3c,db: cmp al,db
void i286_op3C(CPU* cpu) {
	cpu->ltw = i286_rd_imm(cpu);
	cpu->ltw = i286_sub8(cpu, cpu->al, cpu->ltw, 1);
}

// 3d,dw: cmp ax,dw
void i286_op3D(CPU* cpu) {
	cpu->tmpw = i286_rd_immw(cpu);
	cpu->tmpw = i286_sub16(cpu, cpu->ax, cpu->tmpw, 1);
}

// 3e: DS segment override prefix
void i286_op3E(CPU* cpu) {
	cpu->seg = cpu->ds;
}

// 3f: aas
void i286_op3F(CPU* cpu) {
	if (((cpu->al & 15) > 9) | (cpu->f & I286_FA)) {
		cpu->al -= 6;
		cpu->ah--;
		cpu->f |= (I286_FA | I286_FC);
	} else {
		cpu->f &= ~(I286_FA | I286_FC);
	}
	cpu->al &= 15;
}

// inc

unsigned char i286_inc8(CPU* cpu, unsigned char r) {
	r++;
	cpu->f &= ~(I286_FO | I286_FS | I286_FZ | I286_FA | I286_FP);
	if (r == 0x80) cpu->f |= I286_FO;
	if (r & 0x80) cpu->f |= I286_FS;
	if (!r) cpu->f |= I286_FZ;
	if ((r & 15) == 0) cpu->f |= I286_FA;	// ? 0fff
	if (parity(r)) cpu->f |= I286_FP;
	return r;
}

unsigned short i286_inc16(CPU* cpu, unsigned short r) {
	r++;
	cpu->f &= ~(I286_FO | I286_FS | I286_FZ | I286_FA | I286_FP);
	if (r == 0x8000) cpu->f |= I286_FO;
	if (r & 0x8000) cpu->f |= I286_FS;
	if (!r) cpu->f |= I286_FZ;
	if ((r & 15) == 0) cpu->f |= I286_FA;	// ? 0fff
	if (parity(r)) cpu->f |= I286_FP;
	return r;
}

// 40: inc ax
void i286_op40(CPU* cpu) {
	cpu->ax = i286_inc16(cpu, cpu->ax);
}

// 41: inc cx
void i286_op41(CPU* cpu) {
	cpu->cx = i286_inc16(cpu, cpu->cx);
}

// 42: inc dx
void i286_op42(CPU* cpu) {
	cpu->dx = i286_inc16(cpu, cpu->dx);
}

// 43: inc bx
void i286_op43(CPU* cpu) {
	cpu->bx = i286_inc16(cpu, cpu->bx);
}

// 44: inc sp
void i286_op44(CPU* cpu) {
	cpu->sp = i286_inc16(cpu, cpu->sp);
}

// 45: inc bp
void i286_op45(CPU* cpu) {
	cpu->bp = i286_inc16(cpu, cpu->bp);
}

// 46: inc si
void i286_op46(CPU* cpu) {
	cpu->si = i286_inc16(cpu, cpu->si);
}

// 47: inc di
void i286_op47(CPU* cpu) {
	cpu->di = i286_inc16(cpu, cpu->di);
}

// dec

unsigned char i286_dec8(CPU* cpu, unsigned char r) {
	r--;
	cpu->f &= ~(I286_FO | I286_FS | I286_FZ | I286_FA | I286_FP);
	if (r == 0x7f) cpu->f |= I286_FO;
	if (r & 0x80) cpu->f |= I286_FS;
	if (!r) cpu->f |= I286_FZ;
	if ((r & 15) == 15) cpu->f |= I286_FA;
	if (parity(r)) cpu->f |= I286_FP;
	return r;
}

unsigned short i286_dec16(CPU* cpu, unsigned short r) {
	r--;
	cpu->f &= ~(I286_FO | I286_FS | I286_FZ | I286_FA | I286_FP);
	if (r == 0x7fff) cpu->f |= I286_FO;
	if (r & 0x8000) cpu->f |= I286_FS;
	if (!r) cpu->f |= I286_FZ;
	if ((r & 15) == 15) cpu->f |= I286_FA;
	if (parity(r)) cpu->f |= I286_FP;
	return r;
}

// 48: dec ax
void i286_op48(CPU* cpu) {
	cpu->ax = i286_dec16(cpu, cpu->ax);
}

// 49: dec cx
void i286_op49(CPU* cpu) {
	cpu->cx = i286_dec16(cpu, cpu->cx);
}

// 4a: dec dx
void i286_op4A(CPU* cpu) {
	cpu->dx = i286_dec16(cpu, cpu->dx);
}

// 4b: dec bx
void i286_op4B(CPU* cpu) {
	cpu->bx = i286_dec16(cpu, cpu->bx);
}

// 4c: dec sp
void i286_op4C(CPU* cpu) {
	cpu->sp = i286_dec16(cpu, cpu->sp);
}

// 4d: dec bp
void i286_op4D(CPU* cpu) {
	cpu->bp = i286_dec16(cpu, cpu->bp);
}

// 4e: dec si
void i286_op4E(CPU* cpu) {
	cpu->si = i286_dec16(cpu, cpu->si);
}

// 4f: dec di
void i286_op4F(CPU* cpu) {
	cpu->di = i286_dec16(cpu, cpu->di);
}

// 50: push ax
void i286_op50(CPU* cpu) {
	i286_push(cpu, cpu->ax);
}

// 51: push cx
void i286_op51(CPU* cpu) {
	i286_push(cpu, cpu->cx);
}

// 52: push dx
void i286_op52(CPU* cpu) {
	i286_push(cpu, cpu->dx);
}

// 53: push bx
void i286_op53(CPU* cpu) {
	i286_push(cpu, cpu->bx);
}

// 54: push sp
void i286_op54(CPU* cpu) {
	i286_push(cpu, cpu->sp);
}

// 55: push bp
void i286_op55(CPU* cpu) {
	i286_push(cpu, cpu->bp);
}

// 56: push si
void i286_op56(CPU* cpu) {
	i286_push(cpu, cpu->si);
}

// 57: push di
void i286_op57(CPU* cpu) {
	i286_push(cpu, cpu->di);
}

// 58: pop ax
void i286_op58(CPU* cpu) {
	cpu->ax = i286_pop(cpu);
}

// 59: pop cx
void i286_op59(CPU* cpu) {
	cpu->cx = i286_pop(cpu);
}

// 5a: pop dx
void i286_op5A(CPU* cpu) {
	cpu->dx = i286_pop(cpu);
}

// 5b: pop bx
void i286_op5B(CPU* cpu) {
	cpu->bx = i286_pop(cpu);
}

// 5c: pop sp
void i286_op5C(CPU* cpu) {
	cpu->sp = i286_pop(cpu);
}

// 5d: pop bp
void i286_op5D(CPU* cpu) {
	cpu->bp = i286_pop(cpu);
}

// 5e: pop si
void i286_op5E(CPU* cpu) {
	cpu->si = i286_pop(cpu);
}

// 5f: pop di
void i286_op5F(CPU* cpu) {
	cpu->di = i286_pop(cpu);
}

// 60: pusha	(push ax,cx,dx,bx,orig.sp,bp,si,di)
void i286_op60(CPU* cpu) {
	cpu->tmpw = cpu->sp;
	i286_push(cpu, cpu->ax);
	i286_push(cpu, cpu->cx);
	i286_push(cpu, cpu->dx);
	i286_push(cpu, cpu->bx);
	i286_push(cpu, cpu->tmpw);
	i286_push(cpu, cpu->bp);
	i286_push(cpu, cpu->si);
	i286_push(cpu, cpu->di);
}

// 61: popa	(pop di,si,bp,(ignore sp),bx,dx,cx,ax)
void i286_op61(CPU* cpu) {
	cpu->di = i286_pop(cpu);
	cpu->si = i286_pop(cpu);
	cpu->bp = i286_pop(cpu);
	cpu->tmpw = i286_pop(cpu);
	cpu->bx = i286_pop(cpu);
	cpu->dx = i286_pop(cpu);
	cpu->cx = i286_pop(cpu);
	cpu->ax = i286_pop(cpu);
}

// 62,mod: bound rw,md		@eff.addr (md): 2words = min,max. check if min<=rw<=max, INT5 if not
void i286_op62(CPU* cpu) {
	i286_rd_ea(cpu, 1);	// twrd=rw, tmpw=min
	if (cpu->tmpi < 0) {	// interrupts. TODO: fix for protected mode
		i286_interrupt(cpu, 6);		// bad mod
	} else if ((signed short)cpu->twrd < (signed short)cpu->tmpw) {	// not in bounds: INT5
		i286_interrupt(cpu, 5);
	} else {
		cpu->ltw = i286_mrd(cpu, cpu->ea.seg, cpu->ea.adr + 2);
		cpu->htw = i286_mrd(cpu, cpu->ea.seg, cpu->ea.adr + 3);
		if ((signed short)cpu->twrd > (signed short)cpu->tmpw) {
			i286_interrupt(cpu, 5);
		}
	}
}

// 63,mod: arpl ew,rw		adjust RPL of EW not less than RPL of RW
void i286_op63(CPU* cpu) {
	i286_interrupt(cpu, 6);	// real mode
	// TODO: protected mode
}

// 64: repeat next cmps/scas cx times or cf=1
void i286_op64(CPU* cpu) {}

// 65: repeat next cmps/scas cx times or cf=0
void i286_op65(CPU* cpu) {}

// 66: operand size override prefix
void i286_op66(CPU* cpu) {}

// 67: address size override prefix
void i286_op67(CPU* cpu) {}

// 68: push wrd
void i286_op68(CPU* cpu) {
	cpu->tmpw = i286_rd_immw(cpu);
	i286_push(cpu, cpu->tmpw);
}

// mul
int i286_smul(CPU* cpu, signed short p1, signed short p2) {
	int res = p1 * p2;
	cpu->f &= ~(I286_FO | I286_FC);
	if ((p1 & 0x7fff) * (p2 & 0x7fff) > 0xffff)
		cpu->f |= I286_FC;
	cpu->tmp = ((p1 & 0x8000) >> 15) | ((p2 & 0x8000) >> 14) | ((res & 0x8000) >> 13);
	if (i286_add_FO[cpu->tmp & 7])
		cpu->f |= I286_FO;
	return res;
}

// 69,mod,dw: imul rw,ea,dw: rw = ea.w * wrd
void i286_op69(CPU* cpu) {
	i286_rd_ea(cpu, 1);
	cpu->twrd = i286_rd_immw(cpu);
	cpu->tmpi = i286_smul(cpu, cpu->tmpw, cpu->twrd);
	i286_set_reg(cpu, cpu->tmpi & 0xffff, 1);
}

// 6a,db: push byte (sign extended to word)
void i286_op6A(CPU* cpu) {
	cpu->ltw = i286_rd_imm(cpu);
	cpu->htw = (cpu->ltw & 0x80) ? 0xff : 0x00;
	i286_push(cpu, cpu->tmpw);
}

// 6b,mod,db: imul rw,ea,db: rw = ea.w * db
void i286_op6B(CPU* cpu) {
	i286_rd_ea(cpu, 1);
	cpu->lwr = i286_rd_imm(cpu);
	cpu->hwr = (cpu->lwr & 0x80) ? 0xff : 0x00;
	cpu->tmpi = i286_smul(cpu, cpu->tmpw, cpu->twrd);
	i286_set_reg(cpu, cpu->tmpi & 0xffff, 1);
}

// 6c: ins dx,db: wr (DS:DI),(in DX) byte
void i286_op6C(CPU* cpu) {
	cpu->tmp = i286_ird(cpu, cpu->dx);
	i286_mwr(cpu, cpu->es, cpu->di, cpu->tmp);
	cpu->di += (cpu->f & I286_FD) ? -1 : 1;
	if (cpu->rep == I286_REPZ) {
		cpu->cx--;
		if (cpu->cx)
			cpu->pc = cpu->oldpc;
	}
}

// 6d: ins dx,dw: same with word
void i286_op6D(CPU* cpu) {
	if (cpu->di == 0xffff) {
		i286_interrupt(cpu, 13);
	} else {
		cpu->ltw = i286_ird(cpu, cpu->dx);
		cpu->htw = i286_ird(cpu, cpu->dx);
		i286_mwr(cpu, cpu->es, cpu->di, cpu->ltw);
		i286_mwr(cpu, cpu->es, cpu->di + 1, cpu->htw);
		cpu->di += (cpu->f & I286_FD) ? -2 : 2;
		if (cpu->rep == I286_REPZ) {
			cpu->cx--;
			if (cpu->cx)
				cpu->pc = cpu->oldpc;
		}
	}
}

// 6e: outs dx,byte: out (dx),[ds:si]
void i286_op6E(CPU* cpu) {
	cpu->tmp = i286_mrd(cpu, cpu->ds, cpu->si);
	i286_iwr(cpu, cpu->dx, cpu->tmp);
	cpu->si += (cpu->f & I286_FD) ? -1 : 1;
	if (cpu->rep == I286_REPZ) {
		cpu->cx--;
		if (cpu->cx)
			cpu->pc = cpu->oldpc;
	}
}

// 6f: outs dx,wrd
void i286_op6F(CPU* cpu) {
	if (cpu->si == 0xffff) {
		i286_interrupt(cpu, 13);
	} else {
		cpu->ltw = i286_mrd(cpu, cpu->ds, cpu->si);
		cpu->htw = i286_mrd(cpu, cpu->ds, cpu->si + 1);
		i286_iwr(cpu, cpu->dx, cpu->ltw);
		i286_iwr(cpu, cpu->dx, cpu->htw);
		cpu->si += (cpu->f & I286_FD) ? -2 : 2;
		if (cpu->rep == I286_REPZ) {
			cpu->cx--;
			if (cpu->cx)
				cpu->pc = cpu->oldpc;
		}
	}
}

// cond jump
void i286_jr(CPU* cpu, int cnd) {
	cpu->ltw = i286_rd_imm(cpu);
	cpu->htw = (cpu->ltw & 0x80) ? 0xff : 0x00;
	if (cnd) {
		cpu->pc += cpu->tmpw;
		cpu->t += 4;
	}
}

// 70: jo cb
void i286_op70(CPU* cpu) {i286_jr(cpu, cpu->f & I286_FO);}
// 71: jno cb
void i286_op71(CPU* cpu) {i286_jr(cpu, !(cpu->f & I286_FO));}
// 72: jc cb (aka jb,jnae)
void i286_op72(CPU* cpu) {i286_jr(cpu, cpu->f & I286_FC);}
// 73: jnc cb (aka jnb,jae)
void i286_op73(CPU* cpu) {i286_jr(cpu, !(cpu->f & I286_FC));}
// 74: jz cb (aka je)
void i286_op74(CPU* cpu) {i286_jr(cpu, cpu->f & I286_FZ);}
// 75: jnz cb (aka jne)
void i286_op75(CPU* cpu) {i286_jr(cpu, !(cpu->f & I286_FZ));}
// 76: jba cb (aka jna): CF=1 && Z=1
void i286_op76(CPU* cpu) {i286_jr(cpu, (cpu->f & I286_FC) && (cpu->f & I286_FZ));}
// 77: jnba cb (aka ja): CF=0 && Z=0
void i286_op77(CPU* cpu) {i286_jr(cpu, !((cpu->f & I286_FC) || (cpu->f & I286_FZ)));}
// 78: js cb
void i286_op78(CPU* cpu) {i286_jr(cpu, cpu->f & I286_FS);}
// 79: jns cb
void i286_op79(CPU* cpu) {i286_jr(cpu, !(cpu->f & I286_FS));}
// 7a: jp cb (aka jpe)
void i286_op7A(CPU* cpu) {i286_jr(cpu, cpu->f & I286_FP);}
// 7b: jnp cb (aka jpo)
void i286_op7B(CPU* cpu) {i286_jr(cpu, !(cpu->f & I286_FP));}
// 7c: jl cb (aka jngl) FS!=FO
void i286_op7C(CPU* cpu) {i286_jr(cpu, !(cpu->f & I286_FS) != !(cpu->f & I286_FO));}
// 7d: jnl cb (aka jgl) FS==FO
void i286_op7D(CPU* cpu) {i286_jr(cpu, !(cpu->f & I286_FS) == !(cpu->f & I286_FO));}
// 7e: jle cb (aka jng) (FZ=1)||(FS!=FO)
void i286_op7E(CPU* cpu) {i286_jr(cpu, (cpu->f & I286_FZ) || (!(cpu->f & I286_FS) != !(cpu->f & I286_FO)));}
// 7f: jnle cb (aka jg) (FZ=0)&&(FS=FO)
void i286_op7F(CPU* cpu) {i286_jr(cpu, !(cpu->f & I286_FZ) && (!(cpu->f & I286_FS) == !(cpu->f & I286_FO)));}

// 80: ALU eb,byte
void i286_op80(CPU* cpu) {
	i286_rd_ea(cpu, 0);
	cpu->tmpb = i286_rd_imm(cpu);
	switch((cpu->mod >> 3) & 7) {
		case 0: cpu->tmpb = i286_add8(cpu, cpu->ltw, cpu->tmpb, 1); break;		// add
		case 1: cpu->tmpb = i286_or8(cpu, cpu->ltw, cpu->tmpb); break;			// or
		case 2: cpu->tmpb = i286_add8(cpu, cpu->ltw, cpu->tmpb, 1);			// adc
			if (cpu->f & I286_FC) cpu->tmpb = i286_add8(cpu, cpu->tmpb, 1, 0);
			break;
		case 3: cpu->tmpb = i286_sub8(cpu, cpu->ltw, cpu->tmpb, 1);			// sbb
			if (cpu->f & I286_FC) cpu->tmpb = i286_sub8(cpu, cpu->tmpb, 1, 0);
			break;
		case 4: cpu->tmpb = i286_and8(cpu, cpu->ltw, cpu->tmpb); break;			// and
		case 5:										// sub
		case 7: cpu->tmpb = i286_sub8(cpu, cpu->ltw, cpu->tmpb, 1); break;		// cmp
		case 6: cpu->tmpb = i286_xor8(cpu, cpu->ltw, cpu->tmpb); break;			// xor
	}
	if ((cpu->mod & 0x38) != 0x38)		// CMP drop result of SUB
		i286_wr_ea(cpu, cpu->tmpb, 0);
}

// 81: ALU ew,word
void i286_op81(CPU* cpu) {
	i286_rd_ea(cpu, 1);
	cpu->lwr = i286_rd_imm(cpu);
	if (cpu->com == 0x81) {			// 81: word
		cpu->hwr = i286_rd_imm(cpu);
	} else {				// 83: byte extended to word
		cpu->hwr = (cpu->lwr & 0x80) ? 0xff : 0x00;
	}
	switch((cpu->mod >> 3) & 7) {
		case 0: cpu->twrd = i286_add16(cpu, cpu->tmpw, cpu->twrd, 1); break;
		case 1: cpu->twrd = i286_or16(cpu, cpu->tmpw, cpu->twrd); break;
		case 2: cpu->twrd = i286_add16(cpu, cpu->tmpw, cpu->twrd, 1);
			if (cpu->f & I286_FC) cpu->twrd = i286_add16(cpu, cpu->twrd, 1, 0);
			break;
		case 3: cpu->twrd = i286_sub16(cpu, cpu->tmpw, cpu->twrd, 1);
			if (cpu->f & I286_FC) cpu->twrd = i286_sub16(cpu, cpu->twrd, 1, 0);
			break;
		case 4: cpu->twrd = i286_and16(cpu, cpu->tmpw, cpu->twrd); break;
		case 5:
		case 7: cpu->twrd = i286_sub16(cpu, cpu->tmpw, cpu->twrd, 1); break;
		case 6: cpu->twrd = i286_xor16(cpu, cpu->tmpw, cpu->twrd); break;
	}
	if ((cpu->mod & 0x38) != 0x38)
		i286_wr_ea(cpu, cpu->twrd, 1);
}

// 82: ALU eb,byte (==80)
void i286_op82(CPU* cpu) {
	i286_op80(cpu);
}

// 83: ALU ew,signed.byte
void i286_op83(CPU* cpu) {
	i286_op81(cpu);
}

// 84,mod: test eb,rb = and w/o storing result
void i286_op84(CPU* cpu) {
	i286_rd_ea(cpu, 0);
	cpu->tmpb = i286_and8(cpu, cpu->ltw, cpu->lwr);
}

// 85,mod: test ew,rw
void i286_op85(CPU* cpu) {
	i286_rd_ea(cpu, 1);
	cpu->tmpw = i286_and16(cpu, cpu->tmpw, cpu->twrd);
}

// 86,mod: xchg eb,rb = swap values
void i286_op86(CPU* cpu) {
	i286_rd_ea(cpu, 0);	// tmpw=ea, twrd=reg
	i286_wr_ea(cpu, cpu->lwr, 0);
	i286_set_reg(cpu, cpu->ltw, 0);
}

// 87,mod: xchg ew,rw
void i286_op87(CPU* cpu) {
	i286_rd_ea(cpu, 1);
	i286_wr_ea(cpu, cpu->twrd, 1);
	i286_set_reg(cpu, cpu->tmpw, 1);
}

// 88,mod: mov eb,rb
void i286_op88(CPU* cpu) {
	i286_rd_ea(cpu, 0);
	i286_wr_ea(cpu, cpu->ltw, 0);
}

// 89,mod: mov ew,rw
void i286_op89(CPU* cpu) {
	i286_rd_ea(cpu, 1);
	i286_wr_ea(cpu, cpu->twrd, 1);
}

// 8a,mod: mov rb,eb
void i286_op8A(CPU* cpu) {
	i286_rd_ea(cpu, 0);
	i286_set_reg(cpu, cpu->ltw, 0);
}

// 8b,mod: mov rw,ew
void i286_op8B(CPU* cpu) {
	i286_rd_ea(cpu, 1);
	i286_set_reg(cpu, cpu->tmpw, 1);
}

// 8c,mod: mov ew,[es,cs,ss,ds]	TODO: ignore N.bit2?
void i286_op8C(CPU* cpu) {
	i286_rd_ea(cpu, 1);
	switch((cpu->mod & 0x18) >> 3) {
		case 0: cpu->twrd = cpu->es.idx; break;
		case 1: cpu->twrd = cpu->cs.idx; break;
		case 2: cpu->twrd = cpu->ss.idx; break;
		case 3: cpu->twrd = cpu->ds.idx; break;
	}
	i286_wr_ea(cpu, cpu->twrd, 1);
}

// 8d: lea rw,ea	rw = ea.offset
void i286_op8D(CPU* cpu) {
	i286_rd_ea(cpu, 0);
	if (cpu->tmpi < 0) {	// 2nd operand is register
		i286_interrupt(cpu, 6);
	} else {
		i286_set_reg(cpu, cpu->ea.adr, 1);
	}
}

// 8e,mod: mov [es,not cs,ss,ds],ew
void i286_op8E(CPU* cpu) {
	i286_rd_ea(cpu, 1);
	switch((cpu->mod & 0x38) >> 3) {
		case 0: cpu->es = i286_cash_seg(cpu, cpu->tmpw); break;
		case 1: break;
		case 2: cpu->ss = i286_cash_seg(cpu, cpu->tmpw); break;
		case 3: cpu->ds = i286_cash_seg(cpu, cpu->tmpw); break;
	}
}

// 8f, mod: pop ew
void i286_op8F(CPU* cpu) {
	i286_rd_ea(cpu, 0);
	cpu->tmpw = i286_pop(cpu);
	if (cpu->tmpi < 0) {
		i286_set_reg(cpu, cpu->tmpw, 1);
	} else {
		i286_wr_ea(cpu, cpu->tmpw, 1);
	}
}

// 90: nop = xchg ax,ax
void i286_op90(CPU* cpu) {}

// 91: xchg ax,cx
void i286_op91(CPU* cpu) {
	cpu->tmpw = cpu->ax;
	cpu->ax = cpu->cx;
	cpu->cx = cpu->tmpw;
}

// 92: xchg ax,dx
void i286_op92(CPU* cpu) {
	cpu->tmpw = cpu->ax;
	cpu->ax = cpu->dx;
	cpu->dx = cpu->tmpw;
}

// 93: xchg ax,bx
void i286_op93(CPU* cpu) {
	cpu->tmpw = cpu->ax;
	cpu->ax = cpu->bx;
	cpu->bx = cpu->tmpw;
}

// 94: xchg ax,sp
void i286_op94(CPU* cpu) {
	cpu->tmpw = cpu->ax;
	cpu->ax = cpu->sp;
	cpu->sp = cpu->tmpw;
}

// 95:xchg ax,bp
void i286_op95(CPU* cpu) {
	cpu->tmpw = cpu->ax;
	cpu->ax = cpu->bp;
	cpu->bp = cpu->tmpw;
}

// 96:xchg ax,si
void i286_op96(CPU* cpu) {
	cpu->tmpw = cpu->ax;
	cpu->ax = cpu->si;
	cpu->si = cpu->tmpw;
}

// 97:xchg ax,di
void i286_op97(CPU* cpu) {
	cpu->tmpw = cpu->ax;
	cpu->ax = cpu->di;
	cpu->di = cpu->tmpw;
}

// 98:cbw : sign extend AL to AX
void i286_op98(CPU* cpu) {
	cpu->ah = (cpu->al & 0x80) ? 0xff : 0x00;
}

// 99:cwd : sign extend AX to DX:AX
void i286_op99(CPU* cpu) {
	cpu->dx = (cpu->ah & 0x80) ? 0xffff : 0x0000;
}

// 9a: callf cd (cd=SEG:ADR)
void i286_op9A(CPU* cpu) {
	cpu->ea.adr = i286_rd_immw(cpu);	// offset
	cpu->tmpw = i286_rd_immw(cpu);		// segment
	cpu->ea.seg = i286_cash_seg(cpu, cpu->tmpw);
	i286_push(cpu, cpu->pc);
	i286_push(cpu, cpu->cs.idx);
	cpu->cs = cpu->ea.seg;
	cpu->pc = cpu->ea.adr;
	cpu->t = 41;
}

// 9b: wait
void i286_op9B(CPU* cpu) {
	// wait for busy=0
}

// 9c: pushf
void i286_op9C(CPU* cpu) {
	i286_push(cpu, cpu->f);
}

// 9d: popf
void i286_op9D(CPU* cpu) {
	cpu->f = i286_pop(cpu);
}

// 9e: sahf
void i286_op9E(CPU* cpu) {
	cpu->f &= (I286_FS | I286_FZ | I286_FA | I286_FP | I286_FC);
	cpu->f |= (cpu->ah & (I286_FS | I286_FZ | I286_FA | I286_FP | I286_FC));
}

// 9f: lahf
void i286_op9F(CPU* cpu) {
	cpu->ah = (cpu->f & 0xff);
}

// a0,iw: mov al,[iw]
void i286_opA0(CPU* cpu) {
	cpu->tmpw = i286_rd_immw(cpu);
	cpu->al = i286_mrd(cpu, cpu->ds, cpu->tmpw);
}

// a1,iw: mov ax,[iw]
void i286_opA1(CPU* cpu) {
	cpu->tmpw = i286_rd_immw(cpu);
	if (cpu->tmpw == 0xffff) {
		i286_interrupt(cpu, 13);
	} else {
		cpu->al = i286_mrd(cpu, cpu->ds, cpu->tmpw);
		cpu->ah = i286_mrd(cpu, cpu->ds, cpu->tmpw + 1);
	}
}

// a2,xb: mov [iw],al
void i286_opA2(CPU* cpu) {
	cpu->tmpw = i286_rd_immw(cpu);
	i286_mwr(cpu, cpu->ds, cpu->tmpw, cpu->al);
}

// a3,xw: mov [iw],ax
void i286_opA3(CPU* cpu) {
	cpu->tmpw = i286_rd_immw(cpu);
	if (cpu->tmpw == 0xffff) {
		i286_interrupt(cpu, 13);
	} else {
		i286_mwr(cpu, cpu->ds, cpu->tmpw, cpu->al);
		i286_mwr(cpu, cpu->ds, cpu->tmpw + 1, cpu->ah);
	}
}

// a4: movsb: [ds:si]->[es:di], si,di ++ or --
void i286_opA4(CPU* cpu) {
	cpu->tmp = i286_mrd(cpu, cpu->ds, cpu->si);
	i286_mwr(cpu, cpu->es, cpu->di, cpu->tmp);
	if (cpu->f & I286_FD) {
		cpu->si--;
		cpu->di--;
	} else {
		cpu->si++;
		cpu->di++;
	}
	if (cpu->rep == I286_REPZ) {
		cpu->cx--;
		if (cpu->cx)
			cpu->pc = cpu->oldpc;
	}
}

// a5: movsw (movsb for word)
void i286_opA5(CPU* cpu) {
	if ((cpu->si == 0xffff) || (cpu->di == 0xffff)) {
		i286_interrupt(cpu, 13);
	} else {
		cpu->ltw = i286_mrd(cpu, cpu->ds, cpu->si);
		cpu->htw = i286_mrd(cpu, cpu->ds, cpu->si + 1);
		i286_mwr(cpu, cpu->es, cpu->di, cpu->ltw);
		i286_mwr(cpu, cpu->es, cpu->di + 1, cpu->htw);
		if (cpu->f & I286_FD) {
			cpu->si -= 2;
			cpu->di -= 2;
		} else {
			cpu->si += 2;
			cpu->di += 2;
		}
		if (cpu->rep == I286_REPZ) {
			cpu->cx--;
			if (cpu->cx)
				cpu->pc = cpu->oldpc;
		}
	}
}

int i286_check_rep(CPU* cpu) {
	int res = 0;
	switch(cpu->rep) {
		case I286_REPNZ: res = !(cpu->f & I286_FZ); break;
		case I286_REPZ: res = (cpu->f & I286_FZ); break;
	}
	return res;
}

// a6: cmpsb: cmp [ds:si]-[es:di], adv si,di
void i286_opA6(CPU* cpu) {
	cpu->ltw = i286_mrd(cpu, cpu->ds, cpu->si);
	cpu->lwr = i286_mrd(cpu, cpu->es, cpu->di);
	cpu->htw = i286_sub8(cpu, cpu->ltw, cpu->lwr, 1);
	if (cpu->f & I286_FD) {
		cpu->si--;
		cpu->di--;
	} else {
		cpu->si++;
		cpu->di++;
	}
	if (i286_check_rep(cpu))
		cpu->pc = cpu->oldpc;
}

// a7: cmpsw
void i286_opA7(CPU* cpu) {
	if ((cpu->si == 0xffff) || (cpu->di == 0xffff)) {
		i286_interrupt(cpu, 13);
	} else {
		cpu->ltw = i286_mrd(cpu, cpu->ds, cpu->si);
		cpu->htw = i286_mrd(cpu, cpu->ds, cpu->si + 1);
		cpu->lwr = i286_mrd(cpu, cpu->es, cpu->di);
		cpu->hwr = i286_mrd(cpu, cpu->es, cpu->di + 1);
		cpu->tmpw = i286_sub16(cpu, cpu->tmpw, cpu->twrd, 1);
		if (cpu->f & I286_FD) {
			cpu->si -= 2;
			cpu->di -= 2;
		} else {
			cpu->si += 2;
			cpu->di += 2;
		}
		if (i286_check_rep(cpu))
			cpu->pc = cpu->oldpc;
	}
}

// a8,byte: test al,byte
void i286_opA8(CPU* cpu) {
	cpu->ltw = i286_rd_imm(cpu);
	cpu->lwr = i286_and8(cpu, cpu->al, cpu->ltw);
}

// a9,wrd: test ax,wrd
void i286_opA9(CPU* cpu) {
	cpu->tmpw = i286_rd_immw(cpu);
	cpu->twrd = i286_and16(cpu, cpu->ax, cpu->tmpw);
}

// aa: stob  al->[ds:di], adv di
void i286_opAA(CPU* cpu) {
	i286_mwr(cpu, cpu->ds, cpu->di, cpu->al);
	cpu->di += (cpu->f & I286_FD) ? -1 : 1;
	if (cpu->rep == I286_REPZ) {
		cpu->cx--;
		if (cpu->cx)
			cpu->pc = cpu->oldpc;
	}
}

// ab: stow: ax->[ds:di], adv di
void i286_opAB(CPU* cpu) {
	if (cpu->di == 0xffff) {
		i286_interrupt(cpu, 13);
	} else {
		i286_mwr(cpu, cpu->ds, cpu->di, cpu->al);
		i286_mwr(cpu, cpu->ds, cpu->di + 1, cpu->ah);
	}
	cpu->di += (cpu->f & I286_FD) ? -2 : 2;
	if (cpu->rep == I286_REPZ) {
		cpu->cx--;
		if (cpu->cx)
			cpu->pc = cpu->oldpc;
	}
}

// ac: ldosb: [ds:si]->al, adv si
void i286_opAC(CPU* cpu) {
	cpu->al = i286_mrd(cpu, cpu->ds, cpu->si);
	cpu->si += (cpu->f & I286_FD) ? -1 : 1;
}

// ad: ldosw [ds:si]->ax, adv si
void i286_opAD(CPU* cpu) {
	if (cpu->si == 0xffff) {
		i286_interrupt(cpu, 13);
	} else {
		cpu->al = i286_mrd(cpu, cpu->ds, cpu->si);
		cpu->ah = i286_mrd(cpu, cpu->ds, cpu->si + 1);
		cpu->si += (cpu->f & I286_FD) ? -2 : 2;
	}
}

// ae: scasb	cmp al,[es:di]
void i286_opAE(CPU* cpu) {
	cpu->ltw = i286_mrd(cpu, cpu->es, cpu->di);
	cpu->lwr = i286_sub8(cpu, cpu->al, cpu->ltw, 1);
	cpu->di += (cpu->f & I286_FD) ? -1 : 1;
	if (i286_check_rep(cpu))
		cpu->pc = cpu->oldpc;
}

// af: scasw	cmp ax,[es:di]
void i286_opAF(CPU* cpu) {
	if (cpu->di == 0xffff) {
		i286_interrupt(cpu, 13);
	} else {
		cpu->ltw = i286_mrd(cpu, cpu->es, cpu->di);
		cpu->htw = i286_mrd(cpu, cpu->es, cpu->di + 1);
		cpu->twrd = i286_sub16(cpu, cpu->ax, cpu->tmpw, 1);
		cpu->di += (cpu->f & I286_FD) ? -2 : 2;
		if (i286_check_rep(cpu))
			cpu->pc = cpu->oldpc;
	}
}

// b0..b7,ib: mov rb,ib
void i286_opB0(CPU* cpu) {cpu->al = i286_rd_imm(cpu);}
void i286_opB1(CPU* cpu) {cpu->cl = i286_rd_imm(cpu);}
void i286_opB2(CPU* cpu) {cpu->dl = i286_rd_imm(cpu);}
void i286_opB3(CPU* cpu) {cpu->bl = i286_rd_imm(cpu);}
void i286_opB4(CPU* cpu) {cpu->ah = i286_rd_imm(cpu);}
void i286_opB5(CPU* cpu) {cpu->ch = i286_rd_imm(cpu);}
void i286_opB6(CPU* cpu) {cpu->dh = i286_rd_imm(cpu);}
void i286_opB7(CPU* cpu) {cpu->bh = i286_rd_imm(cpu);}

// b8..bf,iw: mov rw,iw
void i286_opB8(CPU* cpu) {cpu->ax = i286_rd_immw(cpu);}
void i286_opB9(CPU* cpu) {cpu->cx = i286_rd_immw(cpu);}
void i286_opBA(CPU* cpu) {cpu->dx = i286_rd_immw(cpu);}
void i286_opBB(CPU* cpu) {cpu->bx = i286_rd_immw(cpu);}
void i286_opBC(CPU* cpu) {cpu->sp = i286_rd_immw(cpu);}
void i286_opBD(CPU* cpu) {cpu->bp = i286_rd_immw(cpu);}
void i286_opBE(CPU* cpu) {cpu->si = i286_rd_immw(cpu);}
void i286_opBF(CPU* cpu) {cpu->di = i286_rd_immw(cpu);}

// rotate/shift

// rol: FC<-b7...b0<-b7
unsigned char i286_rol8(CPU* cpu, unsigned char p) {
	cpu->f &= ~(I286_FC | I286_FO);
	p = (p << 1) | ((p & 0x80) ? 1 : 0);
	if (p & 1) cpu->f |= I286_FC;
	if (!(cpu->f & I286_FC) != !(p & 0x80)) cpu->f |= I286_FO;
	return p;
}

unsigned short i286_rol16(CPU* cpu, unsigned short p) {
	cpu->f &= ~(I286_FC | I286_FO);
	p = (p << 1) | ((p & 0x8000) ? 1 : 0);
	if (p & 1) cpu->f |= I286_FC;
	if (!(cpu->f & I286_FC) != !(p & 0x8000)) cpu->f |= I286_FO;
	return p;
}

// ror: b0->b7...b0->CF
unsigned char i286_ror8(CPU* cpu, unsigned char p) {
	cpu->f &= ~(I286_FC | I286_FO);
	p = (p >> 1) | ((p & 1) ? 0x80 : 0);
	if (p & 0x80) cpu->f |= I286_FC;
	if (!(p & 0x80) != !(p & 0x40)) cpu->f |= I286_FO;
	return p;
}

unsigned short i286_ror16(CPU* cpu, unsigned short p) {
	cpu->f &= ~(I286_FC | I286_FO);
	p = (p >> 1) | ((p & 1) ? 0x8000 : 0);
	if (p & 0x8000) cpu->f |= I286_FC;
	if (!(p & 0x8000) != !(p & 0x4000)) cpu->f |= I286_FO;
	return p;
}

// rcl: CF<-b7..b0<-CF
unsigned char i286_rcl8(CPU* cpu, unsigned char p) {
	cpu->tmp = (cpu->f & I286_FC);
	cpu->f &= ~(I286_FC | I286_FO);
	if (p & 0x80) cpu->f |= I286_FC;
	p = (p << 1) | (cpu->tmp ? 1 : 0);
	if (!(cpu->f & I286_FC) != !(p & 0x80)) cpu->f |= I286_FO;
	return p;
}

unsigned short i286_rcl16(CPU* cpu, unsigned short p) {
	cpu->tmp = (cpu->f & I286_FC);
	cpu->f &= ~(I286_FC | I286_FO);
	if (p & 0x8000) cpu->f |= I286_FC;
	p = (p << 1) | (cpu->tmp ? 1 : 0);
	if (!(cpu->f & I286_FC) != !(p & 0x8000)) cpu->f |= I286_FO;
	return p;
}

// rcr: CF->b7..b0->CF
unsigned char i286_rcr8(CPU* cpu, unsigned char p) {
	cpu->tmp = (cpu->f & I286_FC);
	cpu->f &= (I286_FC | I286_FO);
	if (p & 1) cpu->f |= I286_FC;
	p = (p >> 1) | (cpu->tmp ? 0x80 : 0);
	if (!(p & 0x80) != !(p & 0x40)) cpu->f |= I286_FO;
	return p;
}

unsigned short i286_rcr16(CPU* cpu, unsigned short p) {
	cpu->tmp = (cpu->f & I286_FC);
	cpu->f &= (I286_FC | I286_FO);
	if (p & 1) cpu->f |= I286_FC;
	p = (p >> 1) | (cpu->tmp ? 0x8000 : 0);
	if (!(p & 0x8000) != !(p & 0x4000)) cpu->f |= I286_FO;
	return p;
}

// sal: CF<-b7..b0<-0
unsigned char i286_sal8(CPU* cpu, unsigned char p) {
	cpu->f &= ~(I286_FC | I286_FO);
	if (p & 0x80) cpu->f |= I286_FC;
	p <<= 1;
	if (!(cpu->f & I286_FC) != !(p & 0x80)) cpu->f |= I286_FO;
	return p;
}

unsigned short i286_sal16(CPU* cpu, unsigned short p) {
	cpu->f &= ~(I286_FC | I286_FO);
	if (p & 0x8000) cpu->f |= I286_FC;
	p <<= 1;
	if (!(cpu->f & I286_FC) != !(p & 0x8000)) cpu->f |= I286_FO;
	return p;
}

// shr 0->b7..b0->CF
unsigned char i286_shr8(CPU* cpu, unsigned char p) {
	cpu->f &= ~(I286_FC | I286_FO);
	if (p & 1) cpu->f |= I286_FC;
	if (p & 0x80) cpu->f |= I286_FO;
	p >>= 1;
	return p;
}

unsigned short i286_shr16(CPU* cpu, unsigned short p) {
	cpu->f &= ~(I286_FC | I286_FO);
	if (p & 1) cpu->f |= I286_FC;
	if (p & 0x8000) cpu->f |= I286_FO;
	p >>= 1;
	return p;
}

// sar b7->b7..b0->CF
unsigned char i286_sar8(CPU* cpu, unsigned char p) {
	cpu->f &= ~(I286_FC | I286_FO);
	if (p & 1) cpu->f |= I286_FC;
	p = (p >> 1) | (p & 0x80);
	return p;
}

unsigned short i286_sar16(CPU* cpu, unsigned short p) {
	cpu->f &= ~(I286_FC | I286_FO);
	if (p & 1) cpu->f |= I286_FC;
	p = (p >> 1) | (p & 0x8000);
	return p;
}

typedef unsigned char(*cb286rot8)(CPU*, unsigned char);
typedef unsigned short(*cb286rot16)(CPU*, unsigned short);

static cb286rot8 i286_rot8_tab[8] = {
	i286_rol8, i286_ror8, i286_rcl8, i286_rcr8,
	i286_sal8, i286_shr8, 0, i286_sar8
};

static cb286rot16 i286_rot16_tab[8] = {
	i286_rol16, i286_ror16, i286_rcl16, i286_rcr16,
	i286_sal16, i286_shr16, 0, i286_sar16
};

void i286_rotsh8(CPU* cpu) {
	// i286_rd_ea already called, cpu->tmpb is number of repeats
	// cpu->ltw = ea.byte, result should be back in cpu->ltw. flags is setted
	cpu->tmpb &= 0x1f;		// only 5 bits is counted
	cpu->t += cpu->tmpb;		// 1T for each iteration
	cb286rot8 foo = i286_rot8_tab[(cpu->mod >> 3) & 7];
	if (foo) {
		while (cpu->tmpb) {
			cpu->ltw = foo(cpu, cpu->ltw);
			cpu->tmpb--;
		}
	}
}

void i286_rotsh16(CPU* cpu) {
	cpu->tmpb &= 0x1f;
	cpu->t += cpu->tmpb;
	cb286rot16 foo = i286_rot16_tab[(cpu->mod >> 3) & 7];
	if (foo) {
		while (cpu->tmpb) {
			cpu->tmpw = foo(cpu, cpu->tmpw);
			cpu->tmpb--;
		}
	}
}

// c0,mod,db: rotate/shift ea byte db times
void i286_opC0(CPU* cpu) {
	i286_rd_ea(cpu, 0);
	cpu->tmpb = i286_rd_imm(cpu);
	i286_rotsh8(cpu);
	i286_wr_ea(cpu, cpu->ltw, 0);
}

// c1,mod,db: rotate/shift ea word db times
void i286_opC1(CPU* cpu) {
	i286_rd_ea(cpu, 1);
	cpu->tmpb = i286_rd_imm(cpu);
	i286_rotsh16(cpu);
	i286_wr_ea(cpu, cpu->tmpw, 1);
}

// c2,iw: ret iw	pop pc, pop iw bytes
void i286_opC2(CPU* cpu) {
	cpu->tmpw = i286_rd_immw(cpu);
	cpu->pc = i286_pop(cpu);
	cpu->sp += cpu->tmpw;
}

// c3: ret
void i286_opC3(CPU* cpu) {
	cpu->pc = i286_pop(cpu);
}

// c4,mod: les rw,ed
void i286_opC4(CPU* cpu) {
	i286_rd_ea(cpu, 1);	// tmpw = offset (to register)
	cpu->lwr = i286_mrd(cpu, cpu->ea.seg, cpu->ea.adr + 2);		// twrd = segment (es)
	cpu->hwr = i286_mrd(cpu, cpu->ea.seg, cpu->ea.adr + 3);
	cpu->es = i286_cash_seg(cpu, cpu->twrd);
	i286_set_reg(cpu, cpu->tmpw, 1);
}

// c5,mod: lds rw,ed (same c4 with ds)
void i286_opC5(CPU* cpu) {
	i286_rd_ea(cpu, 1);
	cpu->lwr = i286_mrd(cpu, cpu->ea.seg, cpu->ea.adr + 2);
	cpu->hwr = i286_mrd(cpu, cpu->ea.seg, cpu->ea.adr + 3);
	cpu->ds = i286_cash_seg(cpu, cpu->twrd);
	i286_set_reg(cpu, cpu->tmpw, 1);
}

// c6,mod,ib: mov ea,ib
void i286_opC6(CPU* cpu) {
	i286_rd_ea(cpu, 0);
	cpu->lwr = i286_rd_imm(cpu);
	i286_wr_ea(cpu, cpu->lwr, 0);
}

// c7,mod,iw: mov ea,iw
void i286_opC7(CPU* cpu) {
	i286_rd_ea(cpu, 1);
	cpu->twrd = i286_rd_immw(cpu);
	i286_wr_ea(cpu, cpu->twrd, 1);
}

// c8,iw,ib: enter iw,ib
void i286_opC8(CPU* cpu) {
	cpu->t = 11;
	cpu->tmpw = i286_rd_immw(cpu);
	cpu->tmpb = i286_rd_imm(cpu) & 0x1f;
	i286_push(cpu, cpu->bp);
	cpu->ea.adr = cpu->sp;
	if (cpu->tmpb > 0) {
		while(--cpu->tmpb) {
			cpu->bp -= 2;
			cpu->lwr = i286_mrd(cpu, cpu->ds, cpu->bp);		// +1T
			cpu->hwr = i286_mrd(cpu, cpu->ds, cpu->bp + 1);		// +1T
			i286_push(cpu, cpu->twrd);				// +2T
		}
		i286_push(cpu, cpu->ea.adr);					// +2T (1?)
	}
	cpu->bp = cpu->ea.adr;
	cpu->sp -= cpu->tmpw;
}

// c9: leave
void i286_opC9(CPU* cpu) {
	cpu->sp = cpu->bp;
	cpu->bp = i286_pop(cpu);
}

// ca,iw: retf iw	pop adr,seg,iw bytes	15/25/55T
void i286_opCA(CPU* cpu) {
	cpu->tmpw = i286_rd_immw(cpu);
	cpu->pc = i286_pop(cpu);
	cpu->tmpw = i286_pop(cpu);
	cpu->cs = i286_cash_seg(cpu, cpu->tmpw);
	cpu->sp += cpu->tmpw;
	cpu->t += cpu->tmpw;
}

// cb: retf	pop adr,seg
void i286_opCB(CPU* cpu) {
	cpu->pc = i286_pop(cpu);
	cpu->tmpw = i286_pop(cpu);
	cpu->cs = i286_cash_seg(cpu, cpu->tmpw);
}

// cc: int 3
void i286_opCC(CPU* cpu) {
	i286_interrupt(cpu, 3);
}

// cd,ib: int ib
void i286_opCD(CPU* cpu) {
	cpu->tmpb = i286_rd_imm(cpu);
	i286_interrupt(cpu, cpu->tmpb);
}

// ce: into	int 4 if FO=1
void i286_opCE(CPU* cpu) {
	if (cpu->f & I286_FO)
		i286_interrupt(cpu, 4);
}

// cf: iret	pop pc,cs,flag
void i286_opCF(CPU* cpu) {
	cpu->pc = i286_pop(cpu);
	cpu->tmpw = i286_pop(cpu);
	cpu->cs = i286_cash_seg(cpu, cpu->tmpw);
	cpu->f = i286_pop(cpu);
	cpu->inten &= ~I286_BLK_NMI;		// nmi on
}

// d0,mod: rot/shift ea.byte 1 time
void i286_opD0(CPU* cpu) {
	i286_rd_ea(cpu, 0);
	cpu->tmpb = 1;
	i286_rotsh8(cpu);
	i286_wr_ea(cpu, cpu->ltw, 0);
}

// d1,mod: rot/shift ea.word 1 time
void i286_opD1(CPU* cpu) {
	i286_rd_ea(cpu, 1);
	cpu->tmpb = 1;
	i286_rotsh16(cpu);
	i286_wr_ea(cpu, cpu->tmpw, 1);
}

// d2,mod: rot/shift ea.byte CL times
void i286_opD2(CPU* cpu) {
	i286_rd_ea(cpu, 0);
	cpu->tmpb = cpu->cl;
	i286_rotsh8(cpu);
	i286_wr_ea(cpu, cpu->ltw, 0);
}

// d3,mod: rot/shift ea.word CL times
void i286_opD3(CPU* cpu) {
	i286_rd_ea(cpu, 1);
	cpu->tmpb = cpu->cl;
	i286_rotsh16(cpu);
	i286_wr_ea(cpu, cpu->tmpw, 1);
}

// d4 0a: aam
void i286_opD4(CPU* cpu) {
	cpu->tmpb = i286_rd_imm(cpu);
	if (cpu->tmpb == 0x0a) {
		cpu->ah = cpu->al / 10;
		cpu->al = cpu->al % 10;
		cpu->f &= ~(I286_FS | I286_FZ | I286_FP);
		if (cpu->ah & 0x80) cpu->f |= I286_FS;
		if (!cpu->ax) cpu->f |= I286_FZ;
		if (parity(cpu->ax)) cpu->f |= I286_FP;
	}
}

// d5 0a: aad
void i286_opD5(CPU* cpu) {
	cpu->tmpb = i286_rd_imm(cpu);
	if (cpu->tmpb == 0x0a) {
		cpu->al = cpu->ah * 10 + cpu->al;
		cpu->ah = 0;
		cpu->f &= ~(I286_FS | I286_FZ | I286_FP);
		if (cpu->al & 0x80) cpu->f |= I286_FS;
		if (!cpu->al) cpu->f |= I286_FZ;
		if (parity(cpu->al)) cpu->f |= I286_FP;
	}
}

// d6:
void i286_opD6(CPU* cpu) {
	i286_interrupt(cpu, 6);
}

// d7: xlatb	al = [ds:bx+al]
void i286_opD7(CPU* cpu) {
	cpu->tmpw = cpu->bx + cpu->al;
	cpu->al = i286_mrd(cpu, cpu->ds, cpu->tmpw);
}

// d8: for 80287
void i286_opD8(CPU* cpu) {}

// d9: 80287
void i286_opD9(CPU* cpu) {}

// da: 80287
void i286_opDA(CPU* cpu) {}

// db: 80287
void i286_opDB(CPU* cpu) {}

// dc: 80287
void i286_opDC(CPU* cpu) {}

// dd: 80287
void i286_opDD(CPU* cpu) {}

// de: 80287
void i286_opDE(CPU* cpu) {}

// df: 80287
void i286_opDF(CPU* cpu) {}

// e0,cb: loopnz cb:	cx--,jump short if (cx!=0)&&(fz=0)
void i286_opE0(CPU* cpu) {
	cpu->cx--;
	i286_jr(cpu, cpu->cx && !(cpu->f & I286_FZ));
}

// e1,cb: loopz cb
void i286_opE1(CPU* cpu) {
	cpu->cx--;
	i286_jr(cpu, cpu->cx && (cpu->f & I286_FZ));
}

// e2,cb: loop cb	check only cx
void i286_opE2(CPU* cpu) {
	cpu->cx--;
	i286_jr(cpu, cpu->cx);
}

// e3: jcxz cb
void i286_opE3(CPU* cpu) {
	i286_jr(cpu, !cpu->cx);
}

// e4,ib: in al,ib	al = in(ib)
void i286_opE4(CPU* cpu) {
	cpu->tmpb = i286_rd_imm(cpu);
	cpu->al = i286_ird(cpu, cpu->tmpb) & 0xff;
}

// e5,ib: in ax,ib	ax = in(ib)[x2]
void i286_opE5(CPU* cpu) {
	cpu->tmpb = i286_rd_imm(cpu);
	cpu->ax = i286_ird(cpu, cpu->tmpb);
}

// e6,ib: out ib,al	out(ib),al
void i286_opE6(CPU* cpu) {
	cpu->tmpb = i286_rd_imm(cpu);
	i286_iwr(cpu, cpu->tmpb, cpu->al);
}

// e7,ib: out ib,ax	out(ib),ax
void i286_opE7(CPU* cpu) {
	cpu->tmpb = i286_rd_imm(cpu);
	i286_iwr(cpu, cpu->tmpb, cpu->ax);
}

// e8,cw: call cw	(relative call)
void i286_opE8(CPU* cpu) {
	cpu->tmpw = i286_rd_immw(cpu);
	i286_push(cpu, cpu->pc);
	cpu->pc += cpu->tmpw;
}

// e9,cw: jmp cw	(relative jump)
void i286_opE9(CPU* cpu) {
	cpu->tmpw = i286_rd_immw(cpu);
	cpu->pc += cpu->tmpw;
}

// ea,ow,sw: jmp ow:sw	(far jump)
void i286_opEA(CPU* cpu) {
	cpu->tmpw = i286_rd_immw(cpu);
	cpu->twrd = i286_rd_immw(cpu);
	cpu->pc = cpu->tmpw;
	cpu->cs = i286_cash_seg(cpu, cpu->twrd);
}

// eb,cb: jump cb	(short jump)
void i286_opEB(CPU* cpu) {
	cpu->ltw = i286_rd_imm(cpu);
	cpu->htw = (cpu->ltw & 0x80) ? 0xff : 0x00;
	cpu->pc += cpu->tmpw;
}

// ec: in al,dx
void i286_opEC(CPU* cpu) {
	cpu->al = i286_ird(cpu, cpu->dx) & 0xff;
}

// ed: in ax,dx
void i286_opED(CPU* cpu) {
	cpu->ax = i286_ird(cpu, cpu->dx);
}

// ee: out dx,al
void i286_opEE(CPU* cpu) {
	i286_iwr(cpu, cpu->dx, cpu->al);
}

// ef: out dx,ax
void i286_opEF(CPU* cpu) {
	i286_iwr(cpu, cpu->dx, cpu->ax);
}

// f0: lock prefix (for multi-CPU)
void i286_opF0(CPU* cpu) {
	cpu->lock = 1;
}

// f1: undef, doesn't cause interrupt
void i286_opF1(CPU* cpu) {}

// f2: REPNZ prefix for scas/cmps: repeat until Z=1
void i286_opF2(CPU* cpu) {
	cpu->rep = I286_REPNZ;
}

// f3: REPZ prefix for scas/cmps: repeat until Z=0
// f3: REP prefix for ins/movs/outs/stos: cx--,repeat if cx!=0
void i286_opF3(CPU* cpu) {
	cpu->rep = I286_REPZ;
}

// f4: hlt	halt until interrupt
void i286_opF4(CPU* cpu) {
	if (!(cpu->intrq & cpu->inten))
		cpu->pc = cpu->oldpc;
}

// f5:cmc
void i286_opF5(CPU* cpu) {
	cpu->f ^= I286_FC;
}

// f6,mod:
void i286_opF60(CPU* cpu) {		// test eb,ib
	cpu->tmpb = i286_rd_imm(cpu);
	cpu->tmpb = i286_and8(cpu, cpu->ltw, cpu->tmpb);
}

void i286_opF62(CPU* cpu) {		// not eb
	i286_wr_ea(cpu, cpu->ltw ^ 0xff, 0);
}

void i286_opF63(CPU* cpu) {		// neg eb
	cpu->ltw = i286_sub8(cpu, 0, cpu->ltw, 1);
	i286_wr_ea(cpu, cpu->ltw, 0);
}

void i286_opF64(CPU* cpu) {		// mul eb
	cpu->ax = cpu->ltw * cpu->al;
	cpu->f &= ~(I286_FO | I286_FC);
	if (cpu->ah) cpu->f |= (I286_FC | I286_FO);
}

void i286_opF65(CPU* cpu) {		// imul eb
	cpu->ax = (signed char)cpu->ltw * (signed char)cpu->al;
	cpu->f &= ~(I286_FO | I286_FC);
	if (cpu->ah != ((cpu->al & 0x80) ? 0xff : 0x00)) cpu->f |= (I286_FO | I286_FC);
}

void i286_opF66(CPU* cpu) {		// div eb
	if (cpu->ltw == 0) {				// div by zero
		i286_interrupt(cpu, 0);
	} else {
		// TODO: int0 if quo>0xff
		cpu->tmpw = cpu->ax / cpu->ltw;
		cpu->twrd = cpu->ax % cpu->ltw;
		cpu->al = cpu->ltw;
		cpu->ah = cpu->lwr;
	}
}

void i286_opF67(CPU* cpu) {		// idiv eb
	if (cpu->ltw == 0) {
		i286_interrupt(cpu, 0);
	} else {
		// TODO: int0 if quo>0xff
		cpu->tmpw = (signed short)cpu->ax / (signed char)cpu->ltw;
		cpu->twrd = (signed short)cpu->ax % (signed char)cpu->ltw;
		cpu->al = cpu->ltw;
		cpu->ah = cpu->lwr;
	}
}

cbcpu i286_F6_tab[8] = {
	i286_opF60, i286_opF60, i286_opF62, i286_opF63,
	i286_opF64, i286_opF65, i286_opF66, i286_opF67
};

void i286_opF6(CPU* cpu) {
	i286_rd_ea(cpu, 0);
	i286_F6_tab[(cpu->mod >> 3) & 7](cpu);
}

// f7,mod:

void i286_opF70(CPU* cpu) {		// test ew,iw
	cpu->twrd = i286_rd_immw(cpu);
	cpu->twrd = i286_and16(cpu, cpu->tmpw, cpu->twrd);
}

void i286_opF72(CPU* cpu) {		// not ew
	i286_wr_ea(cpu, cpu->tmpw ^ 0xffff, 1);
}

void i286_opF73(CPU* cpu) {		// neg ew
	cpu->twrd = i286_sub16(cpu, 0, cpu->tmpw, 1);
	i286_wr_ea(cpu, cpu->twrd, 1);
}

void i286_opF74(CPU* cpu) {		// mul ew
	cpu->tmpi = cpu->tmpw * cpu->ax;
	cpu->ax = cpu->tmpi & 0xffff;
	cpu->dx = (cpu->tmpi >> 16) & 0xffff;
	cpu->f &= ~(I286_FO | I286_FC);
	if (cpu->dx) cpu->f |= (I286_FC | I286_FO);
}

void i286_opF75(CPU* cpu) {		// imul ew
	cpu->tmpi = (signed short)cpu->tmpw * (signed short)cpu->ax;
	cpu->ax = cpu->tmpi & 0xffff;
	cpu->dx = (cpu->tmpi >> 16) & 0xffff;
	cpu->f &= ~(I286_FO | I286_FC);
	if (cpu->dx != ((cpu->ah & 0x80) ? 0xff : 0x00)) cpu->f |= (I286_FO | I286_FC);
}

void i286_opF76(CPU* cpu) {		// div ew
	if (cpu->tmpw == 0) {				// div by zero
		i286_interrupt(cpu, 0);
	} else {
		// TODO: int0 if quo>0xffff
		cpu->tmpi = (cpu->dx << 16) | cpu->ax;
		cpu->ax = cpu->tmpi / cpu->tmpw;
		cpu->dx = cpu->tmpi % cpu->tmpw;
	}
}

void i286_opF77(CPU* cpu) {		// idiv ew
	if (cpu->tmpw == 0) {
		i286_interrupt(cpu, 0);
	} else {
		// TODO: int0 if quo>0xffff
		cpu->ax = (signed int)cpu->tmpi / (signed short)cpu->tmpw;
		cpu->dx = (signed int)cpu->tmpi % (signed short)cpu->tmpw;
	}
}

cbcpu i286_F7_tab[8] = {
	i286_opF70, i286_opF70, i286_opF72, i286_opF73,
	i286_opF74, i286_opF75, i286_opF76, i286_opF77
};

void i286_opF7(CPU* cpu) {
	i286_rd_ea(cpu, 1);
	i286_F7_tab[(cpu->mod >> 3) & 7](cpu);
}

// f8: clc
void i286_opF8(CPU* cpu) {
	cpu->f &= ~I286_FC;
}

// f9: stc
void i286_opF9(CPU* cpu) {
	cpu->f |= I286_FC;
}

// fa: cli
void i286_opFA(CPU* cpu) {
	cpu->f &= ~I286_FI;
}

// fb: sti
void i286_opFB(CPU* cpu) {
	cpu->f |= I286_FI;
}

// fc: cld
void i286_opFC(CPU* cpu) {
	cpu->f &= ~I286_FD;
}

// fd: std
void i286_opFD(CPU* cpu) {
	cpu->f |= I286_FD;
}

// fe: inc/dec ea.byte
void i286_opFE(CPU* cpu) {
	i286_rd_ea(cpu, 0);
	switch((cpu->mod >> 3) & 7) {
		case 0: cpu->ltw = i286_inc8(cpu, cpu->ltw);
			i286_wr_ea(cpu, cpu->ltw, 0);
			break;
		case 1: cpu->ltw = i286_dec8(cpu, cpu->ltw);
			i286_wr_ea(cpu, cpu->ltw, 0);
			break;
	}
}

// ff: extend ops ea.word
void i286_opFF(CPU* cpu) {
	i286_rd_ea(cpu, 1);
	switch((cpu->mod >> 3) & 7) {
		case 0: cpu->tmpw = i286_inc16(cpu, cpu->tmpw);
			i286_wr_ea(cpu, cpu->tmpw, 1);
			break;	// inc ew
		case 1: cpu->tmpw = i286_dec16(cpu, cpu->tmpw);
			i286_wr_ea(cpu, cpu->tmpw, 1);
			break;	// dec ew
		case 2:	i286_push(cpu, cpu->pc);
			cpu->pc = cpu->tmpw;
			break; // call ew
		case 3:	cpu->lwr = i286_mrd(cpu, cpu->ea.seg, cpu->ea.adr + 2);
			cpu->hwr = i286_mrd(cpu, cpu->ea.seg, cpu->ea.adr + 3);
			i286_push(cpu, cpu->pc);
			i286_push(cpu, cpu->cs.idx);
			cpu->pc = cpu->tmpw;
			cpu->cs = i286_cash_seg(cpu, cpu->twrd);
			break; // callf ed
		case 4: cpu->pc = cpu->tmpw;
			break; // jmp ew
		case 5:	cpu->lwr = i286_mrd(cpu, cpu->ea.seg, cpu->ea.adr + 2);
			cpu->hwr = i286_mrd(cpu, cpu->ea.seg, cpu->ea.adr + 3);
			cpu->pc = cpu->tmpw;
			cpu->cs = i286_cash_seg(cpu, cpu->twrd);
			break; // jmpf ed
		case 6: i286_push(cpu, cpu->tmpw);
			break; // push ew
		case 7:
			break;	// ???
	}
}

// com.b0 = word operation
// :e - effective address/register
// :r - register (n)
// :1 - imm.byte
// :2 - imm.word
// :3 - byte signed offset
// :n - word signed offset
// :4 - far address
// :s - segment register (n)

opCode i80286_tab[256] = {
	{0, 1, i286_op00, 0, "add :e,:r"},
	{OF_WORD, 1, i286_op01, 0, "add :e,:r"},
	{0, 1, i286_op02, 0, "add :r,:e"},
	{OF_WORD, 1, i286_op03, 0, "add :r,:e"},
	{0, 1, i286_op04, 0, "add al,:1"},
	{0, 1, i286_op05, 0, "add ax,:2"},
	{0, 1, i286_op06, 0, "push es"},
	{0, 1, i286_op07, 0, "pop es"},
	{0, 1, i286_op08, 0, "or :e,:r"},
	{OF_WORD, 1, i286_op09, 0, "or :e,:r"},
	{0, 1, i286_op0A, 0, "or :r,:e"},
	{OF_WORD, 1, i286_op0B, 0, "or :r,:e"},
	{0, 1, i286_op0C, 0, "or al,:1"},
	{0, 1, i286_op0D, 0, "or ax,:2"},
	{0, 1, i286_op0E, 0, "push cs"},
	{OF_PREFIX, 1, i286_op0F, 0, "prefix 0F"},
	{0, 1, i286_op10, 0, "adc :e,:r"},
	{OF_WORD, 1, i286_op11, 0, "adc :e,:r"},
	{0, 1, i286_op12, 0, "adc :r,:e"},
	{OF_WORD, 1, i286_op13, 0, "adc :r,:e"},
	{0, 1, i286_op14, 0, "adc al,:1"},
	{0, 1, i286_op15, 0, "adc ax,:2"},
	{0, 1, i286_op16, 0, "push ss"},
	{0, 1, i286_op17, 0, "pop ss"},
	{0, 1, i286_op18, 0, "sbb :e,:r"},
	{OF_WORD, 1, i286_op19, 0, "sbb :e,:r"},
	{0, 1, i286_op1A, 0, "sbb :r,:e"},
	{OF_WORD, 1, i286_op1B, 0, "sbb :r,:e"},
	{0, 1, i286_op1C, 0, "sbb al,:1"},
	{0, 1, i286_op1D, 0, "sbb ax,:2"},
	{0, 1, i286_op1E, 0, "push ds"},
	{0, 1, i286_op1F, 0, "pop ds"},
	{0, 1, i286_op20, 0, "and :e,:r"},
	{OF_WORD, 1, i286_op21, 0, "and :e,:r"},
	{0, 1, i286_op22, 0, "and :r,:e"},
	{OF_WORD, 1, i286_op23, 0, "and :r,:e"},
	{0, 1, i286_op24, 0, "and al,:1"},
	{0, 1, i286_op25, 0, "and ax,:2"},
	{OF_PREFIX, 1, i286_op26, 0, "segment ES"},
	{0, 1, i286_op27, 0, "daa"},
	{0, 1, i286_op28, 0, "sub :e,:r"},
	{OF_WORD, 1, i286_op29, 0, "sub :e,:r"},
	{0, 1, i286_op2A, 0, "sub :r,:e"},
	{OF_WORD, 1, i286_op2B, 0, "sub :r,:e"},
	{0, 1, i286_op2C, 0, "sub al,:1"},
	{0, 1, i286_op2D, 0, "sub ax,:2"},
	{OF_PREFIX, 1, i286_op2E, 0, "segment CS"},
	{0, 1, i286_op2F, 0, "das"},
	{0, 1, i286_op30, 0, "xor :e,:r"},
	{OF_WORD, 1, i286_op31, 0, "xor :e,:r"},
	{0, 1, i286_op32, 0, "xor :r,:e"},
	{OF_WORD, 1, i286_op33, 0, "xor :r,:e"},
	{0, 1, i286_op34, 0, "xor al,:1"},
	{0, 1, i286_op35, 0, "xor ax,:2"},
	{OF_PREFIX, 1, i286_op36, 0, "segment SS"},
	{0, 1, i286_op37, 0, "aaa"},
	{0, 1, i286_op38, 0, "cmp :e,:r"},
	{OF_WORD, 1, i286_op39, 0, "cmp :e,:r"},
	{0, 1, i286_op3A, 0, "cmp :r,:e"},
	{OF_WORD, 1, i286_op3B, 0, "cmp :r,:e"},
	{0, 1, i286_op3C, 0, "cmp al,:1"},
	{0, 1, i286_op3D, 0, "cmp ax,:2"},
	{OF_PREFIX, 1, i286_op3E, 0, "segment DS"},
	{0, 1, i286_op3F, 0, "aas"},
	{0, 1, i286_op40, 0, "inc ax"},
	{0, 1, i286_op41, 0, "inc cx"},
	{0, 1, i286_op42, 0, "inc dx"},
	{0, 1, i286_op43, 0, "inc bx"},
	{0, 1, i286_op44, 0, "inc sp"},
	{0, 1, i286_op45, 0, "inc bp"},
	{0, 1, i286_op46, 0, "inc si"},
	{0, 1, i286_op47, 0, "inc di"},
	{0, 1, i286_op48, 0, "dec ax"},
	{0, 1, i286_op49, 0, "dec cx"},
	{0, 1, i286_op4A, 0, "dec dx"},
	{0, 1, i286_op4B, 0, "dec bx"},
	{0, 1, i286_op4C, 0, "dec sp"},
	{0, 1, i286_op4D, 0, "dec bp"},
	{0, 1, i286_op4E, 0, "dec si"},
	{0, 1, i286_op4F, 0, "dec di"},
	{0, 1, i286_op50, 0, "push ax"},
	{0, 1, i286_op51, 0, "push cx"},
	{0, 1, i286_op52, 0, "push dx"},
	{0, 1, i286_op53, 0, "push bx"},
	{0, 1, i286_op54, 0, "push sp"},
	{0, 1, i286_op55, 0, "push bp"},
	{0, 1, i286_op56, 0, "push si"},
	{0, 1, i286_op57, 0, "push di"},
	{0, 1, i286_op58, 0, "pop ax"},
	{0, 1, i286_op59, 0, "pop cx"},
	{0, 1, i286_op5A, 0, "pop dx"},
	{0, 1, i286_op5B, 0, "pop bx"},
	{0, 1, i286_op5C, 0, "pop sp"},
	{0, 1, i286_op5D, 0, "pop bp"},
	{0, 1, i286_op5E, 0, "pop si"},
	{0, 1, i286_op5F, 0, "pop di"},
	{0, 1, i286_op60, 0, "pusha"},
	{0, 1, i286_op61, 0, "popa"},
	{OF_WORD, 1, i286_op62, 0, "bound :r,:e"},
	{OF_WORD, 1, i286_op63, 0, "arpl :e,:r"},
	{OF_PREFIX, 1, i286_op64, 0, "repnc"},		// repeat following cmps/scas cx times or until cf=1
	{OF_PREFIX, 1, i286_op65, 0, "repc"},		// repeat following cmps/scas cx times or until cf=0
	{OF_PREFIX, 1, i286_op66, 0, ""},
	{OF_PREFIX, 1, i286_op67, 0, ""},
	{0, 1, i286_op68, 0, "push :2"},
	{OF_WORD, 1, i286_op69, 0, "imul :r,:e,:2"},
	{0, 1, i286_op6A, 0, "push :1"},
	{OF_WORD, 1, i286_op6B, 0, "imul :r,:e,:1"},
	{0, 1, i286_op6C, 0, "insb"},
	{0, 1, i286_op6D, 0, "insw"},
	{0, 1, i286_op6E, 0, "outsb"},
	{0, 1, i286_op6F, 0, "outsw"},
	{0, 1, i286_op70, 0, "jo :3"},
	{0, 1, i286_op71, 0, "jno :3"},
	{0, 1, i286_op72, 0, "jnae :3"},
	{0, 1, i286_op73, 0, "jnb :3"},
	{0, 1, i286_op74, 0, "jz :3"},
	{0, 1, i286_op75, 0, "jnz :3"},
	{0, 1, i286_op76, 0, "jbe :3"},
	{0, 1, i286_op77, 0, "jnbe :3"},
	{0, 1, i286_op78, 0, "js :3"},
	{0, 1, i286_op79, 0, "jns :3"},
	{0, 1, i286_op7A, 0, "jpe :3"},
	{0, 1, i286_op7B, 0, "jpo :3"},
	{0, 1, i286_op7C, 0, "jnge :3"},
	{0, 1, i286_op7D, 0, "jnl :3"},
	{0, 1, i286_op7E, 0, "jle :3"},
	{0, 1, i286_op7F, 0, "jg :3"},
	{0, 1, i286_op80, 0, ":A :e,:1"},	// :A = mod:N (add,or,adc,sbb,and,sub,xor,cmp)
	{OF_WORD, 1, i286_op81, 0, ":A word :e,:2"},
	{0, 1, i286_op82, 0, ":A :e,:1"},
	{OF_WORD, 1, i286_op83, 0, ":A word :e,:2"},
	{0, 1, i286_op84, 0, "test :e,:r"},
	{OF_WORD, 1, i286_op85, 0, "test :e,:r"},
	{0, 1, i286_op86, 0, "xchg :e,:r"},
	{OF_WORD, 1, i286_op87, 0, "xchg :e,:r"},
	{0, 1, i286_op88, 0, "mov :e,:r"},
	{OF_WORD, 1, i286_op89, 0, "mov :e,:r"},
	{0, 1, i286_op8A, 0, "mov :r,:e"},
	{OF_WORD, 1, i286_op8B, 0, "mov word :r,:e"},
	{OF_WORD, 1, i286_op8C, 0, "mov :e,:s"},	// :s segment register from mod:N
	{OF_WORD, 1, i286_op8D, 0, "lea :r,:e"},
	{OF_WORD, 1, i286_op8E, 0, "mov :s,:e"},
	{OF_WORD, 1, i286_op8F, 0, "push :e"},
	{0, 1, i286_op90, 0, "nop"},
	{0, 1, i286_op91, 0, "xchg ax,cx"},
	{0, 1, i286_op92, 0, "xchg ax,dx"},
	{0, 1, i286_op93, 0, "xchg ax,bx"},
	{0, 1, i286_op94, 0, "xchg ax,sp"},
	{0, 1, i286_op95, 0, "xchg ax,bp"},
	{0, 1, i286_op96, 0, "xchg ax,si"},
	{0, 1, i286_op97, 0, "xchg ax,di"},
	{0, 1, i286_op98, 0, "cbw"},
	{0, 1, i286_op99, 0, "cwd"},
	{0, 1, i286_op9A, 0, "callf :p"},
	{0, 1, i286_op9B, 0, "wait"},
	{0, 1, i286_op9C, 0, "pushf"},
	{0, 1, i286_op9D, 0, "popf"},
	{0, 1, i286_op9E, 0, "sahf"},
	{0, 1, i286_op9F, 0, "lahf"},
	{0, 1, i286_opA0, 0, "mov al,[:2]"},
	{0, 1, i286_opA1, 0, "mov ax,[:2]"},
	{0, 1, i286_opA2, 0, "mov [:2],al"},
	{0, 1, i286_opA3, 0, "mov [:2],ax"},
	{0, 1, i286_opA4, 0, "movsb"},
	{0, 1, i286_opA5, 0, "movsw"},
	{0, 1, i286_opA6, 0, "cmpsb"},
	{0, 1, i286_opA7, 0, "cmpsw"},
	{0, 1, i286_opA8, 0, "test al,:1"},
	{0, 1, i286_opA9, 0, "test ax,:2"},
	{0, 1, i286_opAA, 0, "stosb"},
	{0, 1, i286_opAB, 0, "stosw"},
	{0, 1, i286_opAC, 0, "lodsb"},
	{0, 1, i286_opAD, 0, "lodsw"},
	{0, 1, i286_opAE, 0, "scasb"},
	{0, 1, i286_opAF, 0, "scasw"},
	{0, 1, i286_opB0, 0, "mov al,:1"},
	{0, 1, i286_opB1, 0, "mov cl,:1"},
	{0, 1, i286_opB2, 0, "mov dl,:1"},
	{0, 1, i286_opB3, 0, "mov bl,:1"},
	{0, 1, i286_opB4, 0, "mov ah,:1"},
	{0, 1, i286_opB5, 0, "mov ch,:1"},
	{0, 1, i286_opB6, 0, "mov dh,:1"},
	{0, 1, i286_opB7, 0, "mov bh,:1"},
	{0, 1, i286_opB8, 0, "mov ax,:2"},
	{0, 1, i286_opB9, 0, "mov cx,:2"},
	{0, 1, i286_opBA, 0, "mov dx,:2"},
	{0, 1, i286_opBB, 0, "mov bx,:2"},
	{0, 1, i286_opBC, 0, "mov sp,:2"},
	{0, 1, i286_opBD, 0, "mov bp,:2"},
	{0, 1, i286_opBE, 0, "mov si,:2"},
	{0, 1, i286_opBF, 0, "mov di,:2"},
	{0, 1, i286_opC0, 0, ":R :e,:1"},	// :R rotate group (rol,ror,rcl,rcr,sal,shr,*rot6,sar)
	{OF_WORD, 1, i286_opC1, 0, ":R word :e,:2"},
	{0, 1, i286_opC2, 0, "ret :2"},
	{0, 1, i286_opC3, 0, "ret"},
	{OF_WORD, 1, i286_opC4, 0, "les :r, :e"},
	{OF_WORD, 1, i286_opC5, 0, "led :r, :e"},
	{0, 1, i286_opC6, 0, "mov :e, :1"},
	{OF_WORD, 1, i286_opC7, 0, "mov :e, :2"},
	{0, 1, i286_opC8, 0, "enter :2 :1"},
	{0, 1, i286_opC9, 0, "leave"},
	{0, 1, i286_opCA, 0, "retf :2"},
	{0, 1, i286_opCB, 0, "retf"},
	{0, 1, i286_opCC, 0, "int 3"},
	{0, 1, i286_opCD, 0, "int :1"},
	{0, 1, i286_opCE, 0, "into"},
	{0, 1, i286_opCF, 0, "iret"},
	{0, 1, i286_opD0, 0, ":R :e,1"},
	{OF_WORD, 1, i286_opD1, 0, ":R word :e,1"},
	{0, 1, i286_opD2, 0, ":R :e,cl"},
	{OF_WORD, 1, i286_opD3, 0, ":R word :e,cl"},
	{0, 1, i286_opD4, 0, "aam"},
	{0, 1, i286_opD5, 0, "aad"},
	{0, 1, i286_opD6, 0, "? salc"},
	{0, 1, i286_opD7, 0, "xlatb"},
	{0, 1, i286_opD8, 0, "* x87"},
	{0, 1, i286_opD9, 0, "* x87"},
	{0, 1, i286_opDA, 0, "* x87"},
	{0, 1, i286_opDB, 0, "* x87"},
	{0, 1, i286_opDC, 0, "* x87"},
	{0, 1, i286_opDD, 0, "* x87"},
	{0, 1, i286_opDE, 0, "* x87"},
	{0, 1, i286_opDF, 0, "* x87"},
	{0, 1, i286_opE0, 0, "loopnz :3"},
	{0, 1, i286_opE1, 0, "loopz :3"},
	{0, 1, i286_opE2, 0, "loop :3"},
	{0, 1, i286_opE3, 0, "jcxz :3"},
	{0, 1, i286_opE4, 0, "in al,:1"},
	{0, 1, i286_opE5, 0, "in ax,:1"},
	{0, 1, i286_opE6, 0, "out :1,al"},
	{0, 1, i286_opE7, 0, "out :1,ax"},
	{0, 1, i286_opE8, 0, "call :n"},		// :n = near, 2byte offset
	{0, 1, i286_opE9, 0, "jmp :n"},			// jmp near
	{0, 1, i286_opEA, 0, "jmpf :p"},		// jmp far
	{0, 1, i286_opEB, 0, "jmp :3"},			// jmp short
	{0, 1, i286_opEC, 0, "in al,dx"},
	{0, 1, i286_opED, 0, "in ax,dx"},
	{0, 1, i286_opEE, 0, "out dx,al"},
	{0, 1, i286_opEF, 0, "out dx,ax"},
	{OF_PREFIX, 1, i286_opF0, 0, "lock"},
	{0, 1, i286_opF1, 0, "undef"},
	{OF_PREFIX, 1, i286_opF2, 0, "repnz"},
	{OF_PREFIX, 1, i286_opF3, 0, "repz/rep"},
	{0, 1, i286_opF4, 0, "hlt"},
	{0, 1, i286_opF5, 0, "cmc"},
	{0, 1, i286_opF6, 0, ":X :e"},		// test,test,not,neg,mul,imul,div,idiv
	{OF_WORD, 1, i286_opF7, 0, ":X word :e"},
	{0, 1, i286_opF8, 0, "clc"},
	{0, 1, i286_opF9, 0, "stc"},
	{0, 1, i286_opFA, 0, "cli"},
	{0, 1, i286_opFB, 0, "sti"},
	{0, 1, i286_opFC, 0, "cld"},
	{0, 1, i286_opFD, 0, "std"},
	{0, 1, i286_opFE, 0, ":E :e"},		// inc,dec,...
	{OF_WORD, 1, i286_opFF, 0, ":E word :e"},	// inc,dec,not,neg,call,callf,jmp,jmpf,push,???
};