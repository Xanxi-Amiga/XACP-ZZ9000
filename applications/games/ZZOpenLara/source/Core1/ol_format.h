/* SPDX-License-Identifier: GPL-3.0-only */
/* ZZ9000 / XACP port contributions: Copyright (C) 2026 Xanxi.
 * GPLv3 section 7(b) additional attribution: see ATTRIBUTION.txt.
 * These terms apply only to original port contributions in this file;
 * embedded or separately licensed third-party material retains its own license.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "utils.h"
enum InputKey { ikNone,
// keyboard
    ikLeft, ikRight, ikUp, ikDown, ikSpace, ikTab, ikEnter, ikEscape, ikShift, ikCtrl, ikAlt,
    ik0, ik1, ik2, ik3, ik4, ik5, ik6, ik7, ik8, ik9,
    ikA, ikB, ikC, ikD, ikE, ikF, ikG, ikH, ikI, ikJ, ikK, ikL, ikM,
    ikN, ikO, ikP, ikQ, ikR, ikS, ikT, ikU, ikV, ikW, ikX, ikY, ikZ,
    ikN0, ikN1, ikN2, ikN3, ikN4, ikN5, ikN6, ikN7, ikN8, ikN9, ikNAdd, ikNSub, ikNMul, ikNDiv, ikNDot, 
    ikF1, ikF2, ikF3, ikF4, ikF5, ikF6, ikF7, ikF8, ikF9, ikF10, ikF11, ikF12,
    ikMinus, ikPlus, ikLSB, ikRSB, ikSlash, ikBSlash, ikComma, ikDot, ikTilda, ikColon, ikApos, ikPrev, ikNext, ikHome, ikEnd, ikDel, ikIns, ikBack,
// mouse
    ikMouseL, ikMouseR, ikMouseM,
// touch
    ikTouchA, ikTouchB, ikTouchC, ikTouchD, ikTouchE, ikTouchF,

    ikMAX 
};

enum JoyKey {
// gamepad
    jkNone, jkA, jkB, jkX, jkY, jkLB, jkRB, jkSelect, jkStart, jkL, jkR, jkLT, jkRT, jkLeft, jkRight, jkUp, jkDown, jkMAX
};

enum ControlKey {
    cLeft, cRight, cUp, cDown, cJump, cWalk, cAction, cWeapon, cLook, cDuck, cDash, cRoll, cInventory, cStart, cMAX
};


namespace Core { struct {struct {int language;} audio;} settings; }
namespace Sound {Stream *openCDAudioWAD(const char*,int){return NULL;}}
#include "format.h"

