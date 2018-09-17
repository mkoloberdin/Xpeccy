#include <stdio.h>

#include "sound.h"
#include "xcore.h"

#include <iostream>
#include <QMutex>

#include <SDL.h>

// new
static unsigned char sbuf[0x2000];
static unsigned long posf = 0;		// fill pos
static unsigned long posp = 0;		// play pos

static int smpCount = 0;
OutSys *sndOutput = NULL;
static int sndChunks = 882;
static int sndBufSize = 1764;
int nsPerSample = 23143;

static sndPair sndLev;

OutSys* findOutSys(const char*);

// output

#include "hardware.h"

// return 1 when buffer is full
int sndSync(Computer* comp) {
	if (!(conf.emu.fast || conf.emu.pause)) {
		tapSync(comp->tape,comp->tapCount);
		comp->tapCount = 0;
		tsSync(comp->ts,nsPerSample);
		gsFlush(comp->gs);
		saaFlush(comp->saa);
		sndLev = comp->hw->vol(comp, &conf.snd.vol);

		sndLev.left = sndLev.left * conf.snd.vol.master / 100;
		sndLev.right = sndLev.right * conf.snd.vol.master / 100;

		if (sndLev.left > 0x7fff) sndLev.left = 0x7fff;
		if (sndLev.right > 0x7fff) sndLev.right = 0x7fff;
	}
	sbuf[posf & 0x1fff] = sndLev.left & 0xff;
	posf++;
	sbuf[posf & 0x1fff] = (sndLev.left >> 8) & 0xff;
	posf++;
	sbuf[posf & 0x1fff] = sndLev.right & 0xff;
	posf++;
	sbuf[posf & 0x1fff] = (sndLev.right >> 8) & 0xff;
	posf++;
	smpCount++;
	if (smpCount < sndChunks) return 0;
	conf.snd.fill = 0;
	smpCount = 0;
	return 1;
}

void sndCalibrate(Computer* comp) {
	sndChunks = conf.snd.rate / 50;			// samples / frame
	sndBufSize = conf.snd.chans * sndChunks;	// buffer size
	nsPerSample = 1e9 / conf.snd.rate;		// ns / sample
}

std::string sndGetOutputName() {
	std::string res = "NULL";
	if (sndOutput != NULL) {
		res = sndOutput->name;
	}
	return res;
}

void setOutput(const char* name) {
	if (sndOutput != NULL) {
		sndOutput->close();
	}
	sndOutput = findOutSys(name);
	if (sndOutput == NULL) {
		printf("Can't find sound system '%s'. Reset to NULL\n",name);
		setOutput("NULL");
	} else if (!sndOutput->open()) {
		printf("Can't open sound system '%s'. Reset to NULL\n",name);
		setOutput("NULL");
	}
	// sndCalibrate();
}

int sndOpen() {
	sndChunks = conf.snd.rate / 50;
	if (sndOutput == NULL) return 0;
	if (!sndOutput->open()) {
		setOutput("NULL");
	}
	posp = 0;
	posf = 0;
	return 1;
}

void sndPlay() {
	if (sndOutput) {
		sndOutput->play();
	}
	// playPos = (bufA.pos - sndBufSize) & 0xffff;
}

void sndClose() {
	if (sndOutput != NULL)
		sndOutput->close();
}

std::string sndGetName() {
	std::string res = "NULL";
	if (sndOutput != NULL) {
		res = sndOutput->name;
	}
	return res;
}

//------------------------
// Sound output
//------------------------

/*
void fillBuffer(int len) {
	int pos = 0;
	while (pos < len) {
		bufB.data[pos++] = 0x80 + bufA.data[playPos++];
		playPos &= 0x1fff;
	}
}
*/

// NULL

int null_open() {
	return 1;
}

void null_play() {}
void null_close() {}

// SDL

void sdlPlayAudio(void*, Uint8* stream, int len) {
	if (posf - posp < len) {
		while (len > 0) {
			*(stream++) = sndLev.left & 0xff;;
			*(stream++) = (sndLev.left >> 8) & 0xff;
			*(stream++) = sndLev.right & 0xff;
			*(stream++) = (sndLev.right >> 8) & 0xff;
			len -= 4;
		}
	} else {
		while(len > 0) {
			*(stream++) = sbuf[posp & 0x1fff];
			posp++;
			len--;
		}
	}
}

int sdlopen() {
//	printf("Open SDL audio device...");
	int res;
	SDL_AudioSpec asp;
	SDL_AudioSpec dsp;
	asp.freq = conf.snd.rate;
	asp.format = AUDIO_U16LSB;
	asp.channels = conf.snd.chans;
	asp.samples = 512;
	asp.callback = &sdlPlayAudio;
	asp.userdata = NULL;
	if (SDL_OpenAudio(&asp, &dsp) != 0) {
		printf("SDL audio device opening...failed\n");
		res = 0;
	} else {
		printf("SDL audio device opening...success: %i %i (%i / %i)\n",dsp.freq, dsp.samples,dsp.format,AUDIO_U16LSB);
		SDL_PauseAudio(0);
		res = 1;
	}
	return res;
}

void sdlplay() {
}

void sdlclose() {
	SDL_PauseAudio(1);
	SDL_CloseAudio();
}

// init

OutSys sndTab[] = {
	{xOutputNone,"NULL",&null_open,&null_play,&null_close},
#if defined(HAVESDL1) || defined(HAVESDL2)
	{xOutputSDL,"SDL",&sdlopen,&sdlplay,&sdlclose},
#endif
	{0,NULL,NULL,NULL,NULL}
};

OutSys* findOutSys(const char* name) {
	OutSys* res = NULL;
	int idx = 0;
	while (sndTab[idx].name != NULL) {
		if (strcmp(sndTab[idx].name,name) == 0) {
			res = &sndTab[idx];
			break;
		}
		idx++;
	}
	return res;
}

void sndInit() {
	conf.snd.rate = 44100;
	conf.snd.chans = 2;
	conf.snd.enabled = 1;
	sndOutput = NULL;
	conf.snd.vol.beep = 100;
	conf.snd.vol.tape = 100;
	conf.snd.vol.ay = 100;
	conf.snd.vol.gs = 100;
	initNoise();							// ay/ym
}
