#include <format>
#include <3ds.h>
#include <sys/stat.h>
#include "TEST.hpp"
#include "../sysmodules/acta.hpp"
#include "../plgldr.h"

bool TEST::drawUI(MainStruct *mainStruct, C3D_RenderTarget* top_screen, C3D_RenderTarget* bottom_screen, u32 kDown, u32 kHeld, touchPosition touch)
{

    C2D_SceneBegin(top_screen);
    DrawVersionString();
    C2D_DrawSprite(&mainStruct->top);

        if (kDown & KEY_A) {
            mainStruct->buttonWasPressed = true;
        }

        mainStruct->needsReboot = true;

        return true;
    }
