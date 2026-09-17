/************************************************************************************

Filename	:	VrInputRight.c 
Content		:	Handles common controller input functionality
Created		:	September 2019
Authors		:	Simon Brown, Lubos Vonasek

*************************************************************************************/

#include "VrInput.h"
#include "mathlib.h"
#include "VrClientInfo.h"
#include "VrMath.h"
#include "VrRenderer.h"

void Sys_AddMouseMoveEvent(int dx, int dy);
void Sys_AddMouseButtonEvent(int button, bool pressed);
void Sys_AddKeyEvent(int key, bool pressed);

void Android_ButtonChange(int key, int state);
int Android_GetButton(int key);

void handleTrackedControllerButton_AsButton(uint32_t buttonsNew, uint32_t buttonsOld, bool mouse, uint32_t button, int key)
{
    if ((buttonsNew & button) != (buttonsOld & button))
    {
        if (mouse)
        {
            Sys_AddMouseButtonEvent(key, (buttonsNew & button) != 0);
        }
        else
        {
            Android_ButtonChange(key, ((buttonsNew & button) != 0) ? 1 : 0);
        }
    }
}

void handleTrackedControllerButton_AsKey(uint32_t buttonsNew, uint32_t buttonsOld, uint32_t button, int key)
{
    if ((buttonsNew & button) != (buttonsOld & button))
    {
        Sys_AddKeyEvent(key, (buttonsNew & button) != 0);
    }
}

void
handleTrackedControllerButton_AsToggleButton(uint32_t buttonsNew, uint32_t buttonsOld, uint32_t button, int key)
{
    if ((buttonsNew & button) != (buttonsOld & button))
    {
        Android_ButtonChange(key, Android_GetButton(key) ? 0 : 1);
    }
}

void rotateAboutOrigin(float x, float y, float rotation, vec2_t out)
{
    out[0] = cosf(DEG2RAD(-rotation)) * x  +  sinf(DEG2RAD(-rotation)) * y;
    out[1] = cosf(DEG2RAD(-rotation)) * y  -  sinf(DEG2RAD(-rotation)) * x;
}

float length(float x, float y)
{
    return sqrtf(powf(x, 2.0f) + powf(y, 2.0f));
}

#define NLF_DEADZONE 0.1
#define NLF_POWER 2.2

float nonLinearFilter(float in)
{
    float val = 0.0f;
    if (in > NLF_DEADZONE)
    {
        val = in;
        val -= NLF_DEADZONE;
        val /= (1.0f - NLF_DEADZONE);
        val = powf(val, NLF_POWER);
    }
    else if (in < -NLF_DEADZONE)
    {
        val = in;
        val += NLF_DEADZONE;
        val /= (1.0f - NLF_DEADZONE);
        val = -powf(fabsf(val), NLF_POWER);
    }

    return val;
}

bool between(float min, float val, float max)
{
    return (min < val) && (val < max);
}

#ifndef max
#define max( x, y ) ( ( ( x ) > ( y ) ) ? ( x ) : ( y ) )
#define min( x, y ) ( ( ( x ) < ( y ) ) ? ( x ) : ( y ) )
#endif


float clamp(float _min, float _val, float _max)
{
    return max(min(_val, _max), _min);
}

void Doom3Quest_GetScreenRes(int *width, int *height);

void controlMouse(bool showingMenu) {
    static int cursorX = 0;
    static int cursorY = 0;
    static bool waitForLevelController = true;
    static int waitFrames = 0;
    static bool previousShowingMenu = true;
    static float yaw;

    //Has menu been toggled?
    bool toggledMenuOn = !previousShowingMenu && showingMenu;
    previousShowingMenu = showingMenu;

    static int width = 0;
    static int height = 0;
    if ((width == 0) || (height == 0)) {
        Doom3Quest_GetScreenRes(&width, &height);
    }

    if (toggledMenuOn || waitForLevelController)
    {
        cursorX = (float)((-(pVRClientInfo->weaponangles_temp[YAW]-yaw) * width) / 70.0F);
        cursorY = (float)((pVRClientInfo->weaponangles_temp[PITCH] * height) / 70.0F);
        yaw = pVRClientInfo->weaponangles_temp[YAW];

        Sys_AddMouseMoveEvent(-10000, -10000);
        Sys_AddMouseMoveEvent((width / 2.0F), (height / 2.0F));

        if (toggledMenuOn) {
            waitFrames = 0;
        }
        waitForLevelController = true;
    }

    if (!showingMenu)
    {
        return;
    }

    //Should we carry on waiting for the controller to be pointing forwards before we start sending mouse input?
    if (waitForLevelController) {
        // PCVR: this gate is why the pointer felt dead when the menu opened.
        //
        // It sends no mouse input at all until the controller's PITCH is within
        // five degrees of 30 - a specific downward tilt. Until that happens the
        // cursor is re-centred every frame, so it sits frozen in the middle of
        // the menu and the only way to wake it is to wave the controller until
        //
        // Widened to twenty degrees, and released after 60 frames regardless,
        // so it can never hold the pointer hostage. The point of the gate is to
        // avoid flinging the cursor on the first frame, and that is handled
        // properly below instead: the tracking origin is re-taken at the moment
        // it opens, which makes the first delta zero by construction wherever
        // the controller happens to be pointing.
        waitFrames++;

        if (between(-20, (pVRClientInfo->weaponangles_temp[PITCH]-30), 20) ||
            waitFrames > 60) {
            waitForLevelController = false;

            yaw = pVRClientInfo->weaponangles_temp[YAW];
            cursorX = 0;
            cursorY = (int)((pVRClientInfo->weaponangles_temp[PITCH] * height) / 70.0F);
        }
        return;
    }

    int newCursorX = (float)((-(pVRClientInfo->weaponangles_temp[YAW]-yaw) * width) / 70.0F);
    int newCursorY = (float)((pVRClientInfo->weaponangles_temp[PITCH] * height) / 70.0F);

    Sys_AddMouseMoveEvent(newCursorX - cursorX, newCursorY - cursorY);

    cursorY = newCursorY;
    cursorX = newCursorX;
}