/*
 * Copyright (C) 1999-2010  Terence M. Welsh
 *
 * This file is part of Solar Winds.
 *
 * Solar Winds is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * Solar Winds is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

// Solar Winds screen saver

#ifdef WIN32
#include <windows.h>
#include <rsWin32Saver/rsWin32Saver.h>
#include <rsWin32Saver/rsWin32SaverSettings.h>
#include <regstr.h>
#include <commctrl.h>
#include "resource.h"
#include "solarWindsSettings.h"
#include "../common/saverRegistry.h"
#endif
#ifdef RS_XSCREENSAVER
#include <rsXScreenSaver/rsXScreenSaver.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <rsText/rsText.h>
#include <rsMath/rsMath.h>
#include <math.h>
#include <time.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include "solarWindsState.h"


#define NUMCONSTS 9
#define PIx2 6.28318530718f
#define DEG2RAD 0.0174532925f


class wind;


// Global variables
#ifdef WIN32
// The one global the framework mandates: rsWin32Saver.h declares it extern and
// reads it back. Everything else this module owns is in solarWindsState::State.
LPCTSTR registryPath = ("Software\\Really Slick\\Solar Winds");
#endif

solarWindsState::State& solarWindsState::state(){
	static State s;
	return s;
}

using solarWindsState::state;
using solarWindsState::kLightSize;
#ifdef RS_XSCREENSAVER
enum{
	DEFAULTS1,
	DEFAULTS2,
	DEFAULTS3,
	DEFAULTS4,
	DEFAULTS5,
	DEFAULTS6
};
#endif


class wind{
public:
	float **emitters;
	float **particles;
	int **linelist;
	int *lastparticle;
	int whichparticle;
	float c[NUMCONSTS];
	float ct[NUMCONSTS];
	float cv[NUMCONSTS];
	int numEmitters;
	int numParticles;
	int hasLineList;

	wind();
	~wind();
	void update();
};

wind::wind(){
	const auto& s = state();
	int i;

	emitters = new float*[s.dEmitters];
	for(i=0; i<s.dEmitters; i++){
		emitters[i] = new float[3];
		emitters[i][0] = rsRandf(60.0f) - 30.0f;
		emitters[i][1] = rsRandf(60.0f) - 30.0f;
		emitters[i][2] = rsRandf(30.0f) - 15.0f;
	}

	particles = new float*[s.dParticles];
	for(i=0; i<s.dParticles; i++){
		particles[i] = new float[6];  // 3 for pos, 3 for color
		particles[i][2] = 100.0f;  // start particles behind viewer
	}

	whichparticle = 0;

	if(s.dGeometry == 2){  // allocate memory for lines
		linelist = new int*[s.dParticles];
		for(i=0; i<s.dParticles; i++){
			linelist[i] = new int[2];
			linelist[i][0] = -1;
			linelist[i][1] = -1;
		}
		lastparticle = new int[s.dEmitters];
		for(i=0; i<s.dEmitters; i++)
			lastparticle[i] = i;
	}

	for(i=0; i<NUMCONSTS; i++){
		ct[i] = rsRandf(PIx2);
		cv[i] = rsRandf(0.00005f * float(s.dWindspeed) * float(s.dWindspeed))
			+ 0.00001f * float(s.dWindspeed) * float(s.dWindspeed);
	}

	numEmitters = s.dEmitters;
	numParticles = s.dParticles;
	hasLineList = (s.dGeometry == 2);
}

wind::~wind(){
	int i;

	for(i=0; i<numEmitters; i++)
		delete[] emitters[i];
	delete[] emitters;

	for(i=0; i<numParticles; i++)
		delete[] particles[i];
	delete[] particles;

	if(hasLineList){
		for(i=0; i<numParticles; i++)
			delete[] linelist[i];
		delete[] linelist;
		delete[] lastparticle;
	}
}

void wind::update(){
	auto& s = state();
	int i;
	float x, y, z;
	float temp;
	static float evel = float(s.dEmitterspeed) * 0.01f;
	static float pvel = float(s.dParticlespeed) * 0.01f;
	static float pointsize = 0.04f * float(s.dSize);
	static float linesize = 0.005f * float(s.dSize);

	// update constants
	for(i=0; i<NUMCONSTS; i++){
		ct[i] += cv[i];
		if(ct[i] > PIx2)
			ct[i] -= PIx2;
		c[i] = cosf(ct[i]);
	}
	
	// calculate emissions
	for(i=0; i<s.dEmitters; i++){
		emitters[i][2] += evel;  // emitter moves toward viewer
		if(emitters[i][2] > 15.0f){  // reset emitter
			emitters[i][0] = rsRandf(60.0f) - 30.0f;
			emitters[i][1] = rsRandf(60.0f) - 30.0f;
			emitters[i][2] = -15.0f;
		}
		particles[whichparticle][0] = emitters[i][0];
		particles[whichparticle][1] = emitters[i][1];
		particles[whichparticle][2] = emitters[i][2];
		if(s.dGeometry == 2){  // link particles to form lines
			if(linelist[whichparticle][0] >= 0)
				linelist[linelist[whichparticle][0]][1] = -1;
			linelist[whichparticle][0] = -1;
			if(emitters[i][2] == -15.0f)
				linelist[whichparticle][1] = -1;
			else
				linelist[whichparticle][1] = lastparticle[i];
			linelist[lastparticle[i]][0] = whichparticle;
			lastparticle[i] = whichparticle;
		}
		whichparticle++;
		if(whichparticle >= s.dParticles)
			whichparticle = 0;
	}

	// calculate particle positions and colors
	// first modify constants that affect colors
	c[6] *= 9.0f / float(s.dParticlespeed);
	c[7] *= 9.0f / float(s.dParticlespeed);
	c[8] *= 9.0f / float(s.dParticlespeed);
	// then update each particle
	for(i=0; i<s.dParticles; i++){
		// store old positions
		x = particles[i][0];
		y = particles[i][1];
		z = particles[i][2];
		// make new positions
		particles[i][0] = x + (c[0] * y + c[1] * z) * pvel;
		particles[i][1] = y + (c[2] * z + c[3] * x) * pvel;
		particles[i][2] = z + (c[4] * x + c[5] * y) * pvel;
		// calculate colors
		particles[i][3] = float(fabs((particles[i][0] - x) * c[6]));
		particles[i][4] = float(fabs((particles[i][1] - y) * c[7]));
		particles[i][5] = float(fabs((particles[i][2] - z) * c[8]));
		// clamp colors
		if(particles[i][3] > 1.0f)
			particles[i][3] = 1.0f;
		if(particles[i][4] > 1.0f)
			particles[i][4] = 1.0f;
		if(particles[i][5] > 1.0f)
			particles[i][5] = 1.0f;
	}

	// draw particles
	switch(s.dGeometry){
	case 0:  // lights
		for(i=0; i<s.dParticles; i++){
			glColor3fv(&particles[i][3]);
			glPushMatrix();
				glTranslatef(particles[i][0], particles[i][1], particles[i][2]);
				glCallList(1);
			glPopMatrix();
		}
		break;
	case 1:  // points
		for(i=0; i<s.dParticles; i++){
			temp = particles[i][2] + 40.0f;
			if(temp < 0.01f)
				temp = 0.01f;
			glPointSize(pointsize * temp);
			glBegin(GL_POINTS);
				glColor3fv(&particles[i][3]);
				glVertex3fv(particles[i]);
			glEnd();
		}
		break;
	case 2:  // lines
		for(i=0; i<s.dParticles; i++){
			temp = particles[i][2] + 40.0f;
			if(temp < 0.01f)
				temp = 0.01f;
			if(linelist[i][1] >= 0){
				glLineWidth(linesize * temp);
				glBegin(GL_LINES);
					glColor3fv(&particles[i][3]);
					if(linelist[i][0] == -1)
						glColor3f(0.0f, 0.0f, 0.0f);
					glVertex3fv(particles[i]);
					glColor3fv(&particles[linelist[i][1]][3]);
					if(linelist[linelist[i][1]][1] == -1)
						glColor3f(0.0f, 0.0f, 0.0f);
					glVertex3fv(particles[linelist[i][1]]);
				glEnd();
			}
		}
	}
}


void draw(){
	auto& s = state();
	int i;

	if(!s.dBlur)
		glClear(GL_COLOR_BUFFER_BIT);
	else{
		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
			glLoadIdentity();
			glOrtho(0.0, 1.0, 0.0, 1.0, 1.0, -1.0);
			glMatrixMode(GL_MODELVIEW);
			glLoadIdentity();
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				glColor4f(0.0f, 0.0f, 0.0f, 0.5f - (float(s.dBlur) * 0.0049f));
				glBegin(GL_TRIANGLE_STRIP);
					glVertex3f(0.0f, 0.0f, 0.0f);
					glVertex3f(1.0f, 0.0f, 0.0f);
					glVertex3f(0.0f, 1.0f, 0.0f);
					glVertex3f(1.0f, 1.0f, 0.0f);
				glEnd();
				if(s.dGeometry == 0)
					glBlendFunc(GL_ONE, GL_ONE);
				else
					glBlendFunc(GL_SRC_ALPHA, GL_ONE);  // Necessary for point and line smoothing (I don't know why)
						// Maybe it's just my video card...
		glMatrixMode(GL_PROJECTION);
		glPopMatrix();
	}

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glTranslatef(0.0, 0.0, -15.0);

	// You should need to draw twice if using blur, once to each buffer.
	// But wglSwapLayerBuffers appears to copy the back to the
	// front instead of just switching the pointers to them.  It turns
	// out that both NVidia and 3dfx prefer to use PFD_SWAP_COPY instead
	// of PFD_SWAP_EXCHANGE in the PIXELFORMATDESCRIPTOR.  I don't know why...
	// So this may not work right on other platforms or all video cards.

	// Update surfaces
	for(i=0; i<s.dWinds; i++)
		s.winds[i].update();

	// print text
	static float totalTime = 0.0f;
	totalTime += s.frameTime;
	static std::string str;
	static int frames = 0;
	++frames;
	if(frames == 20){
		str = "FPS = " + to_string(20.0f / totalTime);
		totalTime = 0.0f;
		frames = 0;
	}
	if(kStatistics){
		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glLoadIdentity();
		glOrtho(0.0f, 50.0f * s.aspectRatio, 0.0f, 50.0f, -1.0f, 1.0f);

		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glLoadIdentity();
		glTranslatef(1.0f, 48.0f, 0.0f);

		glColor3f(1.0f, 0.6f, 0.0f);
		s.textwriter->draw(str);

		glPopMatrix();
		glMatrixMode(GL_PROJECTION);
		glPopMatrix();
	}

#ifdef WIN32
	wglSwapLayerBuffers(s.hdc, WGL_SWAP_MAIN_PLANE);
#endif
#ifdef RS_XSCREENSAVER
	glXSwapBuffers(xdisplay, xwindow);
#endif
}


void idleProc(){
	auto& s = state();
	// update time
	static rsTimer timer;
	s.frameTime = float(timer.tick());

#ifdef RS_XSCREENSAVER
	const bool shouldDraw = (s.readyToDraw && !isSuspended && !checkingPassword);
#else
	const bool shouldDraw = (s.readyToDraw && !isSuspended);
#endif
	if(shouldDraw)
		draw();
}


void setDefaults(int which){
	auto& s = state();
	switch(which){
	case DEFAULTS1:  // Regular
		s.dWinds = 1;
		s.dEmitters = 30;
		s.dParticles = 2000;
		s.dGeometry = 0;
		s.dSize = 50;
		s.dWindspeed = 20;
		s.dEmitterspeed = 15;
		s.dParticlespeed = 10;
		s.dBlur = 40;
		dFrameRateLimit = 60;
		break;
	case DEFAULTS2:  // Cosmic Strings
		s.dWinds = 1;
		s.dEmitters = 50;
		s.dParticles = 3000;
		s.dGeometry = 2;
		s.dSize = 20;
		s.dWindspeed = 10;
		s.dEmitterspeed = 10;
		s.dParticlespeed = 10;
		s.dBlur = 10;
		dFrameRateLimit = 60;
		break;
	case DEFAULTS3:  // Cold Pricklies
		s.dWinds = 1;
		s.dEmitters = 300;
		s.dParticles = 3000;
		s.dGeometry = 2;
		s.dSize = 5;
		s.dWindspeed = 20;
		s.dEmitterspeed = 100;
		s.dParticlespeed = 15;
		s.dBlur = 70;
		dFrameRateLimit = 60;
		break;
	case DEFAULTS4:  // Space Fur
		s.dWinds = 2;
		s.dEmitters = 400;
		s.dParticles = 1600;
		s.dGeometry = 2;
		s.dSize = 15;
		s.dWindspeed = 20;
		s.dEmitterspeed = 15;
		s.dParticlespeed = 10;
		s.dBlur = 0;
		dFrameRateLimit = 60;
		break;
	case DEFAULTS5:  // Jiggly
		s.dWinds = 1;
		s.dEmitters = 40;
		s.dParticles = 1200;
		s.dGeometry = 1;
		s.dSize = 20;
		s.dWindspeed = 100;
		s.dEmitterspeed = 20;
		s.dParticlespeed = 4;
		s.dBlur = 50;
		dFrameRateLimit = 60;
		break;
	case DEFAULTS6:  // Undertow
		s.dWinds = 1;
		s.dEmitters = 400;
		s.dParticles = 1200;
		s.dGeometry = 0;
		s.dSize = 40;
		s.dWindspeed = 20;
		s.dEmitterspeed = 1;
		s.dParticlespeed = 100;
		s.dBlur = 50;
		dFrameRateLimit = 60;
	}
}


#ifdef RS_XSCREENSAVER
void handleCommandLine(int argc, char* argv[]){
	auto& s = state();
	setDefaults(DEFAULTS1);
	getArgumentsValue(argc, argv, std::string("-s.winds"), s.dWinds, 1, 10);
	getArgumentsValue(argc, argv, std::string("-emitters"), s.dEmitters, 1, 1000);
	getArgumentsValue(argc, argv, std::string("-perticles"), s.dParticles, 1, 10000);
	getArgumentsValue(argc, argv, std::string("-geometry"), s.dGeometry, 0, 2);
	getArgumentsValue(argc, argv, std::string("-size"), s.dSize, 1, 100);
	getArgumentsValue(argc, argv, std::string("-windspeed"), s.dWindspeed, 1, 100);
	getArgumentsValue(argc, argv, std::string("-emitterspeed"), s.dEmitterspeed, 1, 100);
	getArgumentsValue(argc, argv, std::string("-particlespeed"), s.dParticlespeed, 1, 100);
	getArgumentsValue(argc, argv, std::string("-blur"), s.dBlur, 1, 100);
}
#endif


void reshape(int width, int height){
	auto& s = state();
	glViewport(0, 0, width, height);
	s.aspectRatio = float(width) / float(height);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(90.0, s.aspectRatio, 1.0, 10000.0);
}


#ifdef WIN32
void initSaver(HWND hwnd){
	auto& s = state();
	RECT rect;

	// Window initialization
	s.hdc = GetDC(hwnd);
	setBestPixelFormat(s.hdc);
	s.hglrc = wglCreateContext(s.hdc);
	GetClientRect(hwnd, &rect);
	wglMakeCurrent(s.hdc, s.hglrc);

	reshape(rect.right, rect.bottom);
#endif
#ifdef RS_XSCREENSAVER
void initSaver(){
	auto& s = state();
#endif
	int i, j;
	float x, y, temp;

	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	if(!s.dGeometry)
		glBlendFunc(GL_ONE, GL_ONE);
	else
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);  // Necessary for point and line smoothing (I don't know why)
	glEnable(GL_BLEND);

	if(!s.dGeometry){  // Init lights
		for(i=0; i<kLightSize; i++){
			for(j=0; j<kLightSize; j++){
				x = float(i - kLightSize / 2) / float(kLightSize / 2);
				y = float(j - kLightSize / 2) / float(kLightSize / 2);
				temp = 1.0f - float(sqrt((x * x) + (y * y)));
				if(temp > 1.0f)
					temp = 1.0f;
				if(temp < 0.0f)
					temp = 0.0f;
				s.lightTexture[i][j] = (unsigned char)(255.0f * temp);
			}
		}
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, 1);
		glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexImage2D(GL_TEXTURE_2D, 0, 1, kLightSize, kLightSize, 0,
			GL_LUMINANCE, GL_UNSIGNED_BYTE, s.lightTexture);
		temp = 0.02f * float(s.dSize);
		glNewList(1, GL_COMPILE);
			glBindTexture(GL_TEXTURE_2D, 1);
			glBegin(GL_TRIANGLE_STRIP);
				glTexCoord2f(0.0f, 0.0f);
				glVertex3f(-temp, -temp, 0.0f);
				glTexCoord2f(1.0f, 0.0f);
				glVertex3f(temp, -temp, 0.0f);
				glTexCoord2f(0.0f, 1.0f);
				glVertex3f(-temp, temp, 0.0f);
				glTexCoord2f(1.0f, 1.0f);
				glVertex3f(temp, temp, 0.0f);
			glEnd();
		glEndList();
	}

	if(s.dGeometry == 1){  // init point smoothing
		glEnable(GL_POINT_SMOOTH);
		glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);
	}

	if(s.dGeometry == 2){  // init line smoothing
		glEnable(GL_LINE_SMOOTH);
		glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
	}
	// Initialize surfaces
	s.winds = new wind[s.dWinds];

	// Initialize text
	s.textwriter = new rsText;

	s.readyToDraw = 1;
}


#ifdef RS_XSCREENSAVER
void cleanUp(){
	auto& s = state();
	// Free memory
	delete[] s.winds;
}
#endif


#ifdef WIN32
void cleanUp(HWND hwnd){
	auto& s = state();
	// Free memory
	delete[] s.winds;

	// Kill device context
	ReleaseDC(hwnd, s.hdc);
	wglMakeCurrent(NULL, NULL);
	wglDeleteContext(s.hglrc);
}


// Initialize all user-defined stuff
void readRegistry(){
	auto& s = state();
	LONG result;
	HKEY skey;
	DWORD val;

	setDefaults(DEFAULTS1);

	result = RegOpenKeyEx(HKEY_CURRENT_USER, registryPath, 0, KEY_READ, &skey);
	if(result != ERROR_SUCCESS)
		return;


	if(rssaver::readRegistryDWORD(skey, "Winds", val))
		s.dWinds = solarWindsSettings::clampToRange(val, solarWindsSettings::kWinds);
	if(rssaver::readRegistryDWORD(skey, "Emitters", val))
		s.dEmitters = solarWindsSettings::clampToRange(val, solarWindsSettings::kEmitters);
	if(rssaver::readRegistryDWORD(skey, "Particles", val))
		s.dParticles = solarWindsSettings::clampToRange(val, solarWindsSettings::kParticles);
	if(rssaver::readRegistryDWORD(skey, "Geometry", val))
		s.dGeometry = solarWindsSettings::clampToRange(val, solarWindsSettings::kGeometry);
	if(rssaver::readRegistryDWORD(skey, "Size", val))
		s.dSize = solarWindsSettings::clampToRange(val, solarWindsSettings::kSize);
	if(rssaver::readRegistryDWORD(skey, "Windspeed", val))
		s.dWindspeed = solarWindsSettings::clampToRange(val, solarWindsSettings::kWindspeed);
	if(rssaver::readRegistryDWORD(skey, "Emitterspeed", val))
		s.dEmitterspeed = solarWindsSettings::clampToRange(val, solarWindsSettings::kEmitterspeed);
	if(rssaver::readRegistryDWORD(skey, "Particlespeed", val))
		s.dParticlespeed = solarWindsSettings::clampToRange(val, solarWindsSettings::kParticlespeed);
	if(rssaver::readRegistryDWORD(skey, "Blur", val))
		s.dBlur = solarWindsSettings::clampToRange(val, solarWindsSettings::kBlur);
	if(rssaver::readRegistryDWORD(skey, "FrameRateLimit", val))
		dFrameRateLimit = rsWin32Saver::clampFrameRateLimit(val);

	RegCloseKey(skey);
}


// Save all user-defined stuff
void writeRegistry(){
	auto& s = state();
    LONG result;
	HKEY skey;
	DWORD val, disp;

	result = RegCreateKeyEx(HKEY_CURRENT_USER, registryPath, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &skey, &disp);
	if(result != ERROR_SUCCESS)
		return;

	val = s.dWinds;
	RegSetValueEx(skey, "Winds", 0, REG_DWORD, (CONST BYTE*)&val, sizeof(val));
	val = s.dEmitters;
	RegSetValueEx(skey, "Emitters", 0, REG_DWORD, (CONST BYTE*)&val, sizeof(val));
	val = s.dParticles;
	RegSetValueEx(skey, "Particles", 0, REG_DWORD, (CONST BYTE*)&val, sizeof(val));
	val = s.dGeometry;
	RegSetValueEx(skey, "Geometry", 0, REG_DWORD, (CONST BYTE*)&val, sizeof(val));
	val = s.dSize;
	RegSetValueEx(skey, "Size", 0, REG_DWORD, (CONST BYTE*)&val, sizeof(val));
	val = s.dWindspeed;
	RegSetValueEx(skey, "Windspeed", 0, REG_DWORD, (CONST BYTE*)&val, sizeof(val));
	val = s.dEmitterspeed;
	RegSetValueEx(skey, "Emitterspeed", 0, REG_DWORD, (CONST BYTE*)&val, sizeof(val));
	val = s.dParticlespeed;
	RegSetValueEx(skey, "Particlespeed", 0, REG_DWORD, (CONST BYTE*)&val, sizeof(val));
	val = s.dBlur;
	RegSetValueEx(skey, "Blur", 0, REG_DWORD, (CONST BYTE*)&val, sizeof(val));
	val = dFrameRateLimit;
	RegSetValueEx(skey, "FrameRateLimit", 0, REG_DWORD, (CONST BYTE*)&val, sizeof(val));

	RegCloseKey(skey);
}


INT_PTR CALLBACK aboutProc(HWND hdlg, UINT msg, WPARAM wpm, LPARAM lpm){
	switch(msg){
	case WM_CTLCOLORSTATIC:
		if((HWND(lpm) == GetDlgItem(hdlg, WEBPAGE)) || (HWND(lpm) == GetDlgItem(hdlg, MAIL))){
			SetTextColor(HDC(wpm), RGB(0,0,255));
			SetBkColor(HDC(wpm), COLORREF(GetSysColor(COLOR_3DFACE)));
			return (INT_PTR)GetSysColorBrush(COLOR_3DFACE);
		}
		break;
    case WM_COMMAND:
		switch(LOWORD(wpm)){
		case IDOK:
		case IDCANCEL:
			EndDialog(hdlg, LOWORD(wpm));
			break;
		case WEBPAGE:
			ShellExecute(NULL, "open", "http://www.reallyslick.com", NULL, NULL, SW_SHOWNORMAL);
		}
	}
	return FALSE;
}


void initControls(HWND hdlg){
	auto& s = state();
	char cval[16];

	SendDlgItemMessage(hdlg, WINDS, UDM_SETRANGE, 0, LPARAM(MAKELONG(DWORD(10), DWORD(1))));
	SendDlgItemMessage(hdlg, WINDS, UDM_SETPOS, 0, LPARAM(s.dWinds));

	SendDlgItemMessage(hdlg, EMITTERS, UDM_SETRANGE, 0, LPARAM(MAKELONG(DWORD(1000), DWORD(1))));
	SendDlgItemMessage(hdlg, EMITTERS, UDM_SETPOS, 0, LPARAM(s.dEmitters));

	SendDlgItemMessage(hdlg, PARTICLES, UDM_SETRANGE, 0, LPARAM(MAKELONG(DWORD(10000), DWORD(1))));
	SendDlgItemMessage(hdlg, PARTICLES, UDM_SETPOS, 0, LPARAM(s.dParticles));

	SendDlgItemMessage(hdlg, GEOMETRY, CB_DELETESTRING, WPARAM(2), 0);
	SendDlgItemMessage(hdlg, GEOMETRY, CB_DELETESTRING, WPARAM(1), 0);
	SendDlgItemMessage(hdlg, GEOMETRY, CB_DELETESTRING, WPARAM(0), 0);
	SendDlgItemMessage(hdlg, GEOMETRY, CB_ADDSTRING, 0, LPARAM("Lights"));
	SendDlgItemMessage(hdlg, GEOMETRY, CB_ADDSTRING, 0, LPARAM("Points"));
	SendDlgItemMessage(hdlg, GEOMETRY, CB_ADDSTRING, 0, LPARAM("Lines"));
	SendDlgItemMessage(hdlg, GEOMETRY, CB_SETCURSEL, WPARAM(s.dGeometry), 0);

	SendDlgItemMessage(hdlg, SIZE, TBM_SETRANGE, 0, LPARAM(MAKELONG(DWORD(1), DWORD(100))));
	SendDlgItemMessage(hdlg, SIZE, TBM_SETPOS, 1, LPARAM(s.dSize));
	SendDlgItemMessage(hdlg, SIZE, TBM_SETLINESIZE, 0, LPARAM(1));
	SendDlgItemMessage(hdlg, SIZE, TBM_SETPAGESIZE, 0, LPARAM(10));
	sprintf_s(cval, "%d", s.dSize);
	SendDlgItemMessage(hdlg, SIZETEXT, WM_SETTEXT, 0, LPARAM(cval));

	SendDlgItemMessage(hdlg, WINDSPEED, TBM_SETRANGE, 0, LPARAM(MAKELONG(DWORD(1), DWORD(100))));
	SendDlgItemMessage(hdlg, WINDSPEED, TBM_SETPOS, 1, LPARAM(s.dWindspeed));
	SendDlgItemMessage(hdlg, WINDSPEED, TBM_SETLINESIZE, 0, LPARAM(1));
	SendDlgItemMessage(hdlg, WINDSPEED, TBM_SETPAGESIZE, 0, LPARAM(10));
	sprintf_s(cval, "%d", s.dWindspeed);
	SendDlgItemMessage(hdlg, WINDSPEEDTEXT, WM_SETTEXT, 0, LPARAM(cval));

	SendDlgItemMessage(hdlg, EMITTERSPEED, TBM_SETRANGE, 0, LPARAM(MAKELONG(DWORD(1), DWORD(100))));
	SendDlgItemMessage(hdlg, EMITTERSPEED, TBM_SETPOS, 1, LPARAM(s.dEmitterspeed));
	SendDlgItemMessage(hdlg, EMITTERSPEED, TBM_SETLINESIZE, 0, LPARAM(1));
	SendDlgItemMessage(hdlg, EMITTERSPEED, TBM_SETPAGESIZE, 0, LPARAM(10));
	sprintf_s(cval, "%d", s.dEmitterspeed);
	SendDlgItemMessage(hdlg, EMITTERSPEEDTEXT, WM_SETTEXT, 0, LPARAM(cval));

	SendDlgItemMessage(hdlg, PARTICLESPEED, TBM_SETRANGE, 0, LPARAM(MAKELONG(DWORD(1), DWORD(100))));
	SendDlgItemMessage(hdlg, PARTICLESPEED, TBM_SETPOS, 1, LPARAM(s.dParticlespeed));
	SendDlgItemMessage(hdlg, PARTICLESPEED, TBM_SETLINESIZE, 0, LPARAM(1));
	SendDlgItemMessage(hdlg, PARTICLESPEED, TBM_SETPAGESIZE, 0, LPARAM(10));
	sprintf_s(cval, "%d", s.dParticlespeed);
	SendDlgItemMessage(hdlg, PARTICLESPEEDTEXT, WM_SETTEXT, 0, LPARAM(cval));

	SendDlgItemMessage(hdlg, BLUR, TBM_SETRANGE, 0, LPARAM(MAKELONG(DWORD(0), DWORD(100))));
	SendDlgItemMessage(hdlg, BLUR, TBM_SETPOS, 1, LPARAM(s.dBlur));
	SendDlgItemMessage(hdlg, BLUR, TBM_SETLINESIZE, 0, LPARAM(1));
	SendDlgItemMessage(hdlg, BLUR, TBM_SETPAGESIZE, 0, LPARAM(10));
	sprintf_s(cval, "%d", s.dBlur);
	SendDlgItemMessage(hdlg, BLURTEXT, WM_SETTEXT, 0, LPARAM(cval));

	initFrameRateLimitSlider(hdlg, FRAMERATELIMIT, FRAMERATELIMITTEXT);
}


INT_PTR CALLBACK screenSaverConfigureDialog(HWND hdlg, UINT msg,
										 WPARAM wpm, LPARAM lpm){
	auto& s = state();
	int ival;
	char cval[16];

    switch(msg){
    case WM_INITDIALOG:
        InitCommonControls();
        readRegistry();
        initControls(hdlg);
        return TRUE;
    case WM_COMMAND:
        switch(LOWORD(wpm)){
        case IDOK:
            s.dWinds = SendDlgItemMessage(hdlg, WINDS, UDM_GETPOS, 0, 0);
			s.dEmitters = SendDlgItemMessage(hdlg, EMITTERS, UDM_GETPOS, 0, 0);
			s.dParticles = SendDlgItemMessage(hdlg, PARTICLES, UDM_GETPOS, 0, 0);
			s.dGeometry = SendDlgItemMessage(hdlg, GEOMETRY, CB_GETCURSEL, 0, 0);
			s.dSize = SendDlgItemMessage(hdlg, SIZE, TBM_GETPOS, 0, 0);
			s.dWindspeed = SendDlgItemMessage(hdlg, WINDSPEED, TBM_GETPOS, 0, 0);
			s.dEmitterspeed = SendDlgItemMessage(hdlg, EMITTERSPEED, TBM_GETPOS, 0, 0);
			s.dParticlespeed = SendDlgItemMessage(hdlg, PARTICLESPEED, TBM_GETPOS, 0, 0);
			s.dBlur = SendDlgItemMessage(hdlg, BLUR, TBM_GETPOS, 0, 0);
			dFrameRateLimit = SendDlgItemMessage(hdlg, FRAMERATELIMIT, TBM_GETPOS, 0, 0);
			writeRegistry();
            // Fall through
        case IDCANCEL:
            EndDialog(hdlg, LOWORD(wpm));
            break;
		case DEFAULTS1:
			setDefaults(DEFAULTS1);
			initControls(hdlg);
			break;
		case DEFAULTS2:
			setDefaults(DEFAULTS2);
			initControls(hdlg);
			break;
		case DEFAULTS3:
			setDefaults(DEFAULTS3);
			initControls(hdlg);
			break;
		case DEFAULTS4:
			setDefaults(DEFAULTS4);
			initControls(hdlg);
			break;
		case DEFAULTS5:
			setDefaults(DEFAULTS5);
			initControls(hdlg);
			break;
		case DEFAULTS6:
			setDefaults(DEFAULTS6);
			initControls(hdlg);
			break;
		case ABOUT:
			DialogBox(mainInstance, MAKEINTRESOURCE(DLG_ABOUT), hdlg, aboutProc);
		}
        return TRUE;
	case WM_HSCROLL:
		if(HWND(lpm) == GetDlgItem(hdlg, SIZE)){
			ival = SendDlgItemMessage(hdlg, SIZE, TBM_GETPOS, 0, 0);
			sprintf_s(cval, "%d", ival);
			SendDlgItemMessage(hdlg, SIZETEXT, WM_SETTEXT, 0, LPARAM(cval));
		}
		if(HWND(lpm) == GetDlgItem(hdlg, WINDSPEED)){
			ival = SendDlgItemMessage(hdlg, WINDSPEED, TBM_GETPOS, 0, 0);
			sprintf_s(cval, "%d", ival);
			SendDlgItemMessage(hdlg, WINDSPEEDTEXT, WM_SETTEXT, 0, LPARAM(cval));
		}
		if(HWND(lpm) == GetDlgItem(hdlg, EMITTERSPEED)){
			ival = SendDlgItemMessage(hdlg, EMITTERSPEED, TBM_GETPOS, 0, 0);
			sprintf_s(cval, "%d", ival);
			SendDlgItemMessage(hdlg, EMITTERSPEEDTEXT, WM_SETTEXT, 0, LPARAM(cval));
		}
		if(HWND(lpm) == GetDlgItem(hdlg, PARTICLESPEED)){
			ival = SendDlgItemMessage(hdlg, PARTICLESPEED, TBM_GETPOS, 0, 0);
			sprintf_s(cval, "%d", ival);
			SendDlgItemMessage(hdlg, PARTICLESPEEDTEXT, WM_SETTEXT, 0, LPARAM(cval));
		}
		if(HWND(lpm) == GetDlgItem(hdlg, BLUR)){
			ival = SendDlgItemMessage(hdlg, BLUR, TBM_GETPOS, 0, 0);
			sprintf_s(cval, "%d", ival);
			SendDlgItemMessage(hdlg, BLURTEXT, WM_SETTEXT, 0, LPARAM(cval));
		}
		if(HWND(lpm) == GetDlgItem(hdlg, FRAMERATELIMIT))
			updateFrameRateLimitSlider(hdlg, FRAMERATELIMIT, FRAMERATELIMITTEXT);
		return TRUE;
    }
    return FALSE;
}


LONG screenSaverProc(HWND hwnd, UINT msg, WPARAM wpm, LPARAM lpm){
	auto& s = state();
	static unsigned long threadID;

	switch(msg){
	case WM_CREATE:
		readRegistry();
		initSaver(hwnd);
		s.readyToDraw = 1;
		break;
	case WM_DESTROY:
		s.readyToDraw = 0;
		cleanUp(hwnd);
		break;
	}
	return defScreenSaverProc(hwnd, msg, wpm, lpm);
}
#endif

