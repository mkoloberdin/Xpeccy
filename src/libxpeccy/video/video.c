#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>


#include "video.h"

int vidFlag = 0;
// float brdsize = 1.0;

unsigned char inkTab[] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
};

unsigned char papTab[] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
  0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
  0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
  0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
  0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
  0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0d, 0x0d, 0x0d, 0x0d, 0x0d, 0x0d, 0x0d, 0x0d,
  0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
};

// zx screen adr
unsigned short scrAdrs[8192];
unsigned short atrAdrs[8192];

void vidInitAdrs() {
	unsigned short sadr = 0x0000;
	unsigned short aadr = 0x1800;
	int idx = 0;
	int a,b,c,d;
	for (a = 0; a < 4; a++) {	// parts (4th for profi)
		for (b = 0; b < 8; b++) {	// box lines
			for (c = 0; c < 8; c++) {	// pixel lines
				for (d = 0; d < 32; d++) {	// x bytes
					scrAdrs[idx] = sadr + d;
					atrAdrs[idx] = aadr + d;
					idx++;
				}
				sadr += 0x100;
			}
			sadr -= 0x7e0;
			aadr += 0x20;
		}
		sadr += 0x700;
	}
}

Video* vidCreate(Memory* me) {
	Video* vid = (Video*)malloc(sizeof(Video));
	memset(vid,0x00,sizeof(Video));
	vidSetMode(vid, VID_UNKNOWN);
	vid->mem = me;
	vid->lay.full.x = 448;
	vid->lay.full.y = 320;
	vid->lay.bord.x = 136;
	vid->lay.bord.y = 80;
	vid->lay.sync.x = 80;
	vid->lay.sync.y = 32;
	vid->lay.intpos.x = 0;
	vid->lay.intpos.y = 0;
	vid->lay.intSize = 64;
	vid->frmsz = vid->lay.full.x * vid->lay.full.y;
	vid->intMask = 0x01;		// FRAME INT for all
	vid->ula = ulaCreate();
	vid->fps = 50;
	vidUpdateLayout(vid, 0.5);

	vid->nextbrd = 0;
	vid->curscr = 5;
	vid->fcnt = 0;

	vid->nsDraw = 0;
	vid->ray.x = 0;
	vid->ray.y = 0;
	vid->idx = 0;

	vid->ray.ptr = vid->scrimg;
	vid->linptr = vid->scrimg;

	vid->ray.img = vid->scrimg;
	vid->v9938.lay = &vid->lay;
	vid->v9938.ray = &vid->ray;

	return vid;
}

void vidDestroy(Video* vid) {
	ulaDestroy(vid->ula);
	free(vid);
}

void vidUpdateTimings(Video* vid) {
	vid->nsPerFrame = 1e9 / vid->fps;
	vid->nsPerLine = vid->nsPerFrame / vid->lay.full.y;
	vid->nsPerDot = vid->nsPerLine / vid->lay.full.x;
}

void vidUpdateLayout(Video* vid, float brdsize) {
	if (brdsize < 0.0) brdsize = 0.0;
	if (brdsize > 1.0) brdsize = 1.0;
	vid->lcut.x = (int)floor(vid->lay.sync.x + ((vid->lay.bord.x - vid->lay.sync.x) * (1.0 - brdsize))) & 0xfffc;
	vid->lcut.y = (int)floor(vid->lay.sync.y + ((vid->lay.bord.y - vid->lay.sync.y) * (1.0 - brdsize))) & 0xfffc;
	vid->rcut.x = (int)floor(vid->lay.full.x - ((1.0 - brdsize) * (vid->lay.full.x - vid->lay.bord.x - 256))) & 0xfffc;
	vid->rcut.y = (int)floor(vid->lay.full.y - ((1.0 - brdsize) * (vid->lay.full.y - vid->lay.bord.y - 192))) & 0xfffc;
	vid->vsze.x = vid->rcut.x - vid->lcut.x;
	vid->vsze.y = vid->rcut.y - vid->lcut.y;
	vid->vBytes = vid->vsze.x * vid->vsze.x * 6;	// real size of image buffer (3 bytes/dot x2:x1)
	vidUpdateTimings(vid);
}

int xscr = 0;
int yscr = 0;
int adr = 0;
unsigned char col = 0;
unsigned char ink = 0;
unsigned char pap = 0;
unsigned char scrbyte = 0;
unsigned char atrbyte = 0;
unsigned char nxtbyte = 0;

void vidDarkTail(Video* vid) {
	xscr = vid->ray.x;
	yscr = vid->ray.y;
	unsigned char* ptr = vid->ray.ptr;
	do {
		if ((yscr >= vid->lcut.y) && (yscr < vid->rcut.y) && (xscr >= vid->lcut.x) && (xscr < vid->rcut.x)) {
			*(ptr++) >>= 1;
			*(ptr++) >>= 1;
			*(ptr++) >>= 1;
			*(ptr++) >>= 1;
			*(ptr++) >>= 1;
			*(ptr++) >>= 1;
		}
		if (++xscr >= vid->lay.full.x) {
			xscr = 0;
			if (++yscr >= vid->lay.full.y)
				ptr = NULL;
		}
	} while (ptr);
	vid->tail = 1;
}

void vidGetScreen(Video* vid, unsigned char* dst, int bank, int shift, int watr) {
	unsigned char* pixs = vid->mem->ramData + MADR(bank, shift & 0x2000);
	unsigned char* atrs = pixs + 0x1800;
	unsigned char sbyte, abyte, aink, apap;
	int prt, lin, row, xpos, bitn, cidx;
	int sadr, aadr;
	for (prt = 0; prt < 3; prt++) {
		for (lin = 0; lin < 8; lin++) {
			for (row = 0; row < 8; row++) {
				for (xpos = 0; xpos < 32; xpos++) {
					sadr = (prt << 11) | (lin << 5) | (row << 8) | xpos;
					aadr = (prt << 8) | (lin << 5) | xpos;
					sbyte = *(pixs + sadr);
					abyte = watr ? *(atrs + aadr) : 0x47;
					aink = inkTab[abyte & 0x7f];
					apap = papTab[abyte & 0x7f];
					for (bitn = 0; bitn < 8; bitn++) {
						cidx = (sbyte & (128 >> bitn)) ? aink : apap;
						*(dst++) = vid->pal[cidx].r;
						*(dst++) = vid->pal[cidx].g;
						*(dst++) = vid->pal[cidx].b;
					}
				}
			}
		}
	}
}

/*
waits for 128K, +2
	--wwwwww wwwwww-- : 16-dots cycle, start on border 8 dots before screen
waits for +2a, +3
	ww--wwww wwwwwwww : same
unsigned char waitsTab_A[16] = {5,5,4,4,3,3,2,2,1,1,0,0,0,0,6,6};	// 48K
unsigned char waitsTab_B[16] = {1,1,0,0,7,7,6,6,5,5,4,4,3,3,2,2};	// +2A,+3
*/

int contTabA[] = {12,12,10,10,8,8,6,6,4,4,2,2,0,0,0,0};		// 48K 128K +2 (bank 1,3,5,7)
int contTabB[] = {2,1,0,0,14,13,12,11,10,9,8,7,6,5,4,3};	// +2A +3 (bank 4,5,6,7)

void vidWait(Video* vid) {
	if (vid->ray.y < vid->lay.bord.y) return;		// above screen
	if (vid->ray.y > (vid->lay.bord.y + 191)) return;	// below screen
	xscr = vid->ray.x - vid->lay.bord.x; // + 2;
	if (xscr < 0) return;
	if (xscr > 253) return;
	vidSync(vid, contTabA[xscr & 0x0f] * vid->nsPerDot);
}

void vidSetFont(Video* vid, char* src) {
	memcpy(vid->font,src,0x800);
}

void vidSetFps(Video* vid, int fps) {
	if (fps < 25) fps = 25;
	else if (fps > 100) fps = 100;
	vid->fps = fps;
	vidUpdateTimings(vid);
}

/*
inline void vidSingleDot(Video* vid, unsigned char idx) {
	*(vid->ray.ptr++) = vid->pal[idx].r;
	*(vid->ray.ptr++) = vid->pal[idx].g;
	*(vid->ray.ptr++) = vid->pal[idx].b;
}

inline void vidPutDot(&video* vid, unsigned char idx) {
	vidSingleDot(vid, idx);
	vidSingleDot(vid, idx);
}
*/

// video drawing

void vidDrawBorder(Video* vid) {
	vidPutDot(&vid->ray, vid->pal, vid->brdcol);
}

// common 256 x 192
void vidDrawNormal(Video* vid) {
	yscr = vid->ray.y - vid->lay.bord.y;
	if ((yscr < 0) || (yscr > 191)) {
		col = vid->brdcol;
		if (vid->ula->active) col |= 8;
		vid->atrbyte = 0xff;
	} else {
		xscr = vid->ray.x - vid->lay.bord.x;
		if ((xscr & 7) == 4) {
			nxtbyte = vid->mem->ramData[MADR(vid->curscr, scrAdrs[vid->idx])];
			//adr = ((yscr & 0xc0) << 5) | ((yscr & 7) << 8) | ((yscr & 0x38) << 2) | (((xscr + 4) & 0xf8) >> 3);
			//nxtbyte = vid->mem->ram[vid->curscr ? 7 :5].data[adr];
		}
		if ((vid->ray.x < vid->lay.bord.x) || (vid->ray.x > vid->lay.bord.x + 255)) {
			col = vid->brdcol;
			if (vid->ula->active) col |= 8;
			vid->atrbyte = 0xff;
		} else {
			if ((xscr & 7) == 0) {
				scrbyte = nxtbyte;
				vid->atrbyte = vid->mem->ramData[MADR(vid->curscr, atrAdrs[vid->idx])];
				if (vid->idx < 0x1b00) vid->idx++;
				//adr = 0x1800 | ((yscr & 0xc0) << 2) | ((yscr & 0x38) << 2) | (((xscr + 4) & 0xf8) >> 3);
				//vid->atrbyte = vid->mem->ram[vid->curscr ? 7 :5].data[adr];
				if (vid->ula->active) {
					ink = ((vid->atrbyte & 0xc0) >> 2) | (vid->atrbyte & 7);
					pap = ((vid->atrbyte & 0xc0) >> 2) | ((vid->atrbyte & 0x38) >> 3) | 8;
				} else {
					if ((vid->atrbyte & 0x80) && vid->flash) scrbyte ^= 0xff;
					ink = inkTab[vid->atrbyte & 0x7f];
					pap = papTab[vid->atrbyte & 0x7f];
				}
			}
			col = (scrbyte & 0x80) ? ink : pap;
			scrbyte <<= 1;
		}
	}
	vidPutDot(&vid->ray, vid->pal, col);
}

// alco 16col
void vidDrawAlco(Video* vid) {
	yscr = vid->ray.y - vid->lay.bord.y;
	if ((yscr < 0) || (yscr > 191)) {
		col = vid->brdcol;
	} else {
		xscr = vid->ray.x - vid->lay.bord.x;
		if ((xscr < 0) || (xscr > 255)) {
			col = vid->brdcol;
		} else {
			adr = ((yscr & 0xc0) << 5) | ((yscr & 7) << 8) | ((yscr & 0x38) << 2) | ((xscr & 0xf8) >> 3);
			switch (xscr & 7) {
				case 0:
					scrbyte = vid->mem->ramData[MADR(vid->curscr ^ 1, adr)];
					col = inkTab[scrbyte & 0x7f];
					break;
				case 2:
					scrbyte = vid->mem->ramData[MADR(vid->curscr, adr)];
					col = inkTab[scrbyte & 0x7f];
					break;
				case 4:
					scrbyte = vid->mem->ramData[MADR(vid->curscr ^ 1, adr + 0x2000)];
					col = inkTab[scrbyte & 0x7f];
					break;
				case 6:
					scrbyte = vid->mem->ramData[MADR(vid->curscr, adr + 0x2000)];
					col = inkTab[scrbyte & 0x7f];
					break;
				default:
					col = ((scrbyte & 0x38)>>3) | ((scrbyte & 0x80)>>4);
					break;

			}
		}
	}
	vidPutDot(&vid->ray, vid->pal, col);
}

// hardware multicolor
void vidDrawHwmc(Video* vid) {
	yscr = vid->ray.y - vid->lay.bord.y;
	if ((yscr < 0) || (yscr > 191)) {
		col = vid->brdcol;
	} else {
		xscr = vid->ray.x - vid->lay.bord.x;
		if ((xscr & 7) == 4) {
			adr = ((yscr & 0xc0) << 5) | ((yscr & 7) << 8) | ((yscr & 0x38) << 2) | (((xscr + 4) & 0xf8) >> 3);
			nxtbyte = vid->mem->ramData[MADR(vid->curscr, adr)];
		}
		if ((xscr < 0) || (xscr > 255)) {
			col = vid->brdcol;
		} else {
			if ((xscr & 7) == 0) {
				scrbyte = nxtbyte;
				adr = ((yscr & 0xc0) << 5) | ((yscr & 7) << 8) | ((yscr & 0x38) << 2) | ((xscr & 0xf8) >> 3);
				vid->atrbyte = vid->mem->ramData[MADR(vid->curscr, adr)];
				if ((vid->atrbyte & 0x80) && vid->flash) scrbyte ^= 0xff;
				ink = inkTab[vid->atrbyte & 0x7f];
				pap = papTab[vid->atrbyte & 0x7f];
			}
			col = (scrbyte & 0x80) ? ink : pap;
			scrbyte <<= 1;
		}
	}
	vidPutDot(&vid->ray, vid->pal, col);
}

// atm ega
void vidDrawATMega(Video* vid) {
	yscr = vid->ray.y - 76;
	xscr = vid->ray.x - 96;
	if ((yscr < 0) || (yscr > 199) || (xscr < 0) || (xscr > 319)) {
		col = vid->brdcol;
	} else {
		adr = (yscr * 40) + (xscr >> 3);
		switch (xscr & 7) {
			case 0:
				scrbyte = vid->mem->ramData[MADR(vid->curscr ^ 4, adr)];
				col = inkTab[scrbyte & 0x7f];
				break;
			case 2:
				scrbyte = vid->mem->ramData[MADR(vid->curscr, adr)];
				col = inkTab[scrbyte & 0x7f];
				break;
			case 4:
				scrbyte = vid->mem->ramData[MADR(vid->curscr ^ 4, adr + 0x2000)];
				col = inkTab[scrbyte & 0x7f];
				break;
			case 6:
				scrbyte = vid->mem->ramData[MADR(vid->curscr, adr + 0x2000)];
				col = inkTab[scrbyte & 0x7f];
				break;
			default:
				col = ((scrbyte & 0x38)>>3) | ((scrbyte & 0x80)>>4);
				break;
		}
	}
	vidPutDot(&vid->ray, vid->pal, col);
}

// atm text

void vidDrawByteDD(Video* vid) {		// draw byte $scrbyte with colors $ink,$pap at double-density mode
	for (int i = 0x80; i > 0; i >>= 1) {
		vidSingleDot(&vid->ray, vid->pal, (scrbyte & i) ? ink : pap);
	}
}

void vidATMDoubleDot(Video* vid,unsigned char colr) {
	ink = inkTab[colr & 0x7f];
	pap = papTab[colr & 0x3f] | ((colr & 0x80) >> 4);
	vidDrawByteDD(vid);
}

void vidDrawATMtext(Video* vid) {
	yscr = vid->ray.y - 76;
	xscr = vid->ray.x - 96;
	if ((yscr < 0) || (yscr > 199) || (xscr < 0) || (xscr > 319)) {
		vidPutDot(&vid->ray, vid->pal, vid->brdcol);
	} else {
		adr = 0x1c0 + ((yscr & 0xf8) << 3) + (xscr >> 3);
		if ((xscr & 3) == 0) {
			if ((xscr & 7) == 0) {
				scrbyte = vid->mem->ramData[MADR(vid->curscr, adr)];
				col = vid->mem->ramData[MADR(vid->curscr ^ 4, adr + 0x2000)];
			} else {
				scrbyte = vid->mem->ramData[MADR(vid->curscr, adr + 0x2000)];
				col = vid->mem->ramData[MADR(vid->curscr ^ 4, adr + 1)];
			}
			scrbyte = vid->font[(scrbyte << 3) | (yscr & 7)];
			vidATMDoubleDot(vid,col);
		}
	}
}

// atm hardware multicolor
void vidDrawATMhwmc(Video* vid) {
	yscr = vid->ray.y - 76;
	xscr = vid->ray.x - 96;
	if ((yscr < 0) || (yscr > 199) || (xscr < 0) || (xscr > 319)) {
		vidPutDot(&vid->ray, vid->pal, vid->brdcol);
	} else {
		xscr = vid->ray.x - 96;
		yscr = vid->ray.y - 76;
		adr = (yscr * 40) + (xscr >> 3);
		if ((xscr & 3) == 0) {
			if ((xscr & 7) == 0) {
				scrbyte = vid->mem->ramData[MADR(vid->curscr, adr)];
				col = vid->mem->ramData[MADR(vid->curscr ^ 4, adr)];
			} else {
				scrbyte = vid->mem->ramData[MADR(vid->curscr, adr + 0x2000)];
				col = vid->mem->ramData[MADR(vid->curscr ^ 4, adr + 0x2000)];
			}
			vidATMDoubleDot(vid,col);
		}
		//vid->ray.ptr++;
		//if (vidFlag & VF_DOUBLE) vid->ray.ptr++;
	}
}


// profi 512x240

void vidProfiScr(Video* vid) {
	yscr = vid->ray.y - vid->lay.bord.y + 24;
	if ((yscr < 0) || (yscr > 239)) {
		vidPutDot(&vid->ray, vid->pal, vid->brdcol);
	} else {
		xscr = vid->ray.x - vid->lay.bord.x;
		if ((xscr < 0) || (xscr > 255)) {
			vidPutDot(&vid->ray, vid->pal, vid->brdcol);
		} else {
			if ((xscr & 3) == 0) {
				adr = scrAdrs[vid->idx & 0x1fff] & 0x1fff;
				if (xscr & 4) {
					vid->idx++;
				} else {
					adr |= 0x2000;
				}
				if (vid->curscr == 7) {
					scrbyte = vid->mem->ramData[MADR(6, adr)];
					col = vid->mem->ramData[MADR(0x3a, adr)];		// b0..2 ink, b3..5 pap, b6 inkBR, b7 papBR
				} else {
					scrbyte = vid->mem->ramData[MADR(4, adr)];
					col = vid->mem->ramData[MADR(0x38, adr)];
				}
				ink = inkTab[col & 0x47];
				pap = papTab[(col & 0x3f) | ((col >> 1) & 0x40)];
				vidDrawByteDD(vid);
			}
		}
	}
}

// tsconf

void vidDrawTSLNormal(Video*);
void vidTSline(Video*);
void vidDrawTSL16(Video*);
void vidDrawTSL256(Video*);
void vidDrawTSLText(Video*);
void vidDrawEvoText(Video*);

// v9938

void vidDrawV9938(Video* vid) {
	vid->v9938.draw(&vid->v9938);
}

void vidLineV9938(Video* vid) {
	if (vid->v9938.cbLine)
		vid->v9938.cbLine(&vid->v9938);
}

void vidFrameV9938(Video* vid) {
	if (vid->v9938.cbFram)
		vid->v9938.cbFram(&vid->v9938);
}


// debug

void vidBreak(Video* vid) {
	printf("vid->mode = 0x%.2X\n",vid->vmode);
	// assert(0);
}

// weiter

typedef struct {
	int id;
	void(*callback)(Video*);
	void(*lineCall)(Video*);
	void(*framCall)(Video*);
} xVideoMode;

xVideoMode vidModeTab[] = {
	{VID_NORMAL, vidDrawNormal, NULL, NULL},
	{VID_ALCO, vidDrawAlco, NULL, NULL},
	{VID_HWMC, vidDrawHwmc, NULL, NULL},
	{VID_ATM_EGA, vidDrawATMega, NULL, NULL},
	{VID_ATM_TEXT, vidDrawATMtext, NULL, NULL},
	{VID_ATM_HWM, vidDrawATMhwmc, NULL, NULL},
	{VID_EVO_TEXT, vidDrawEvoText, NULL, NULL},
	{VID_TSL_NORMAL, vidDrawTSLNormal, vidTSline, NULL},
	{VID_TSL_16, vidDrawTSL16, vidTSline, NULL},
	{VID_TSL_256, vidDrawTSL256, vidTSline, NULL},
	{VID_TSL_TEXT, vidDrawTSLText, vidTSline, NULL},
	{VID_PRF_MC, vidProfiScr, NULL, NULL},
	{VID_V9938, vidDrawV9938, NULL, vidFrameV9938},
	{VID_UNKNOWN, vidDrawBorder, NULL, NULL}
};

void vidSetMode(Video* vid, int mode) {
	vid->vmode = mode;
	if (vid->noScreen) {
		vid->callback = &vidDrawBorder;
	} else {
		int i = 0;
		do {
			if ((vidModeTab[i].id == VID_UNKNOWN) || (vidModeTab[i].id == mode)) {
				vid->callback = vidModeTab[i].callback;
				vid->lineCall = vidModeTab[i].lineCall;
				vid->framCall = vidModeTab[i].framCall;
				break;
			}
			i++;
		} while (1);
	}
}

void vidSync(Video* vid, int ns) {
	vid->nsDraw += ns;
	while (vid->nsDraw >= vid->nsPerDot) {
		if ((vid->ray.y >= vid->lcut.y) && (vid->ray.y < vid->rcut.y)) {
			if ((vid->ray.x >= vid->lcut.x) && (vid->ray.x < vid->rcut.x)) {
				if (vid->ray.x & 8)
					vid->brdcol = vid->nextbrd;
				vid->callback(vid);		// put dot
			}
		}
		if ((vid->intMask & 1) && (vid->ray.y == vid->lay.intpos.y) && (vid->ray.x == vid->lay.intpos.x)) {
			vid->intFRAME = 1;
			vid->v9938.sr[0] |= 0x80;
		}
		if (vid->intFRAME && (vid->ray.x >= vid->lay.intpos.x + vid->lay.intSize))
			vid->intFRAME = 0;
		if (++vid->ray.x >= vid->lay.full.x) {
			vid->ray.x = 0;
			vid->nextrow = 1;
			vid->intFRAME = 0;
			if (vid->lineCall) vid->lineCall(vid);
			if (++vid->ray.y >= vid->lay.full.y) {
				vid->ray.y = 0;
				vid->ray.ptr = vid->ray.img;
				vid->linptr = vid->ray.img;
				vid->fcnt++;
				vid->flash = (vid->fcnt & 0x20) ? 1 : 0;
				vid->tsconf.scrLine = vid->tsconf.yOffset;
				vid->idx = 0;
				vid->newFrame = 1;
				vid->tail = 0;
				if (vid->framCall) vid->framCall(vid);
				if (vid->debug) vidDarkTail(vid);
			}
		}
		vid->nsDraw -= vid->nsPerDot;
	}
}