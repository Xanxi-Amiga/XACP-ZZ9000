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


// Solar Winds' module state, in one struct reached through a function-local
// static (Task 6 in docs/MAINTENANCE.md, SonarCloud cpp:S5421). The rationale
// lives in that document; src/starfield/starfieldState.h is the worked example.
//
// The source file is solarWinds.cpp, so the header and namespace follow that
// rather than the directory name, the same way solarWindsSettings.h does.

#ifndef SOLARWINDS_STATE_H
#define SOLARWINDS_STATE_H

#ifdef WIN32
#include <Windows.h>
#endif

#include <rsText/rsText.h>

// Defined in solarWinds.cpp, below the state it is reached through. The struct
// only needs to hold a pointer to it.
class wind;

namespace solarWindsState {

constexpr int kLightSize = 64;

struct State {
	State() = default;
	State(const State&) = delete;
	State& operator=(const State&) = delete;

#ifdef WIN32
	HGLRC hglrc = nullptr;
	HDC hdc = nullptr;
#endif
	float aspectRatio = 0.0f;
	float frameTime = 0.0f;
	int readyToDraw = 0;

	// Allocated in initSaver, deleted in cleanUp. Ownership is unchanged by the
	// move into this struct.
	wind* winds = nullptr;

	unsigned char lightTexture[kLightSize][kLightSize] = {};

	// text output
	rsText* textwriter = nullptr;

	// Parameters edited in the dialog box
	int dWinds = 0;
	int dEmitters = 0;
	int dParticles = 0;
	int dGeometry = 0;
	int dSize = 0;
	int dParticlespeed = 0;
	int dEmitterspeed = 0;
	int dWindspeed = 0;
	int dBlur = 0;
};

// Defined in solarWinds.cpp. External linkage on purpose: the test suite reaches
// the saver's state through this rather than declaring externs of its own.
State& state();

}  // namespace solarWindsState

#endif

