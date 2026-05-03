#include "sysmodules/acta.hpp"
#include "sheet.h"
#include "sheet_t3x.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.hpp"
#include "states/LumaValidation.hpp"
#include "states/MainUI.hpp"
#include "states/TEST.hpp"

MainStruct mainStruct = MainStruct();

void SFX(const char* path) {
    if (ndspChnIsPlaying(1)) return;

    FILE* f = fopen(path, "rb");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    u32 size = ftell(f);
    u32 dataSize = size - 44;
    fseek(f, 44, SEEK_SET);

    static u8* buffer = nullptr;
    static ndspWaveBuf waveBuf;

    if (buffer) {
        linearFree(buffer);
        buffer = nullptr;
    }

    buffer = (u8*)linearAlloc(dataSize);
    if (!buffer) { fclose(f); return; }
    fread(buffer, 1, dataSize, f);
    fclose(f);

    memset(&waveBuf, 0, sizeof(ndspWaveBuf));
    waveBuf.data_vaddr = buffer;
    waveBuf.nsamples = dataSize / 2;
    waveBuf.looping = false;

    DSP_FlushDataCache(buffer, dataSize);

    ndspChnSetRate(1, 16000.0f);
    ndspChnSetFormat(1, NDSP_FORMAT_MONO_PCM16);
    ndspChnWaveBufAdd(1, &waveBuf);
}

static void sceneInit(void)
{
	C2D_SpriteSheet spriteSheet = C2D_SpriteSheetLoadFromMem(sheet_t3x, sheet_t3x_size);
	C2D_SpriteFromSheet(&mainStruct.top, spriteSheet, sheet_top_idx);
    C2D_SpriteFromSheet(&mainStruct.bottom, spriteSheet, sheet_bottom_idx);
	C2D_SpriteFromSheet(&mainStruct.go_back, spriteSheet, sheet_go_back_idx);
	C2D_SpriteFromSheet(&mainStruct.header, spriteSheet, sheet_header_idx);
	C2D_SpriteFromSheet(&mainStruct.nintendo_unloaded_deselected, spriteSheet, sheet_nintendo_unloaded_deselected_idx);
	C2D_SpriteFromSheet(&mainStruct.nintendo_unloaded_selected, spriteSheet, sheet_nintendo_unloaded_selected_idx);
	C2D_SpriteFromSheet(&mainStruct.nintendo_loaded_selected, spriteSheet, sheet_nintendo_loaded_selected_idx);
	C2D_SpriteFromSheet(&mainStruct.nintendo_loaded_deselected, spriteSheet, sheet_nintendo_loaded_deselected_idx);
	C2D_SpriteFromSheet(&mainStruct.brewtendo_unloaded_deselected, spriteSheet, sheet_brewtendo_unloaded_deselected_idx);
	C2D_SpriteFromSheet(&mainStruct.brewtendo_unloaded_selected, spriteSheet, sheet_brewtendo_unloaded_selected_idx);
	C2D_SpriteFromSheet(&mainStruct.brewtendo_loaded_selected, spriteSheet, sheet_brewtendo_loaded_selected_idx);
	C2D_SpriteFromSheet(&mainStruct.brewtendo_loaded_deselected, spriteSheet, sheet_brewtendo_loaded_deselected_idx);
    
    C2D_SpriteSetCenter(&mainStruct.top, 0.5f, 0.5f);
    C2D_SpriteSetPos(&mainStruct.top, 200, 120);
    C2D_SpriteSetPos(&mainStruct.go_back, 0, 214);
    C2D_SpriteSetCenter(&mainStruct.header, 0.5f, 0.0f);
    C2D_SpriteSetPos(&mainStruct.header, 160, 0);
    C2D_SpriteSetPos(&mainStruct.brewtendo_loaded_selected, 49, 59);
    C2D_SpriteSetPos(&mainStruct.brewtendo_unloaded_selected, 49, 59);
    C2D_SpriteSetPos(&mainStruct.brewtendo_unloaded_deselected, 49, 59);
    C2D_SpriteSetPos(&mainStruct.brewtendo_loaded_deselected, 49, 59);
    C2D_SpriteSetPos(&mainStruct.nintendo_loaded_selected, 165, 59);
    C2D_SpriteSetPos(&mainStruct.nintendo_unloaded_selected, 165, 59);
    C2D_SpriteSetPos(&mainStruct.nintendo_unloaded_deselected, 165, 59);
    C2D_SpriteSetPos(&mainStruct.nintendo_loaded_deselected, 165, 59);
    C2D_SpriteSetCenter(&mainStruct.bottom, 0.5f, 0.5f);
    C2D_SpriteSetPos(&mainStruct.bottom, 160, 120);
    
	textBuf = C2D_TextBufNew(4096); // initialize the text buffer with a max glyph count of 4096
}

int main()
{
	// Initialize the libs
    romfsInit();
	nsInit();
	ndmuInit();
	frdInit(false);
	actInit(false);
    ndspInit();
	gfxInitDefault();
	
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
	C2D_Prepare();

	// This version or higher is required creating/swapping friend accounts
	FRD_SetClientSdkVersion(0x70000c8);

	// Create screen
	C3D_RenderTarget* top_screen = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	C3D_RenderTarget* bottom_screen = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

	// Initialize the scene
	sceneInit();

	// set button selected and current account to nasc environment
	u8 nascEnvironment, nfsType, nfsNo;
	FRD_GetServerTypes(&nascEnvironment, &nfsType, &nfsNo);
	
	mainStruct.buttonSelected = static_cast<NascEnvironment>(nascEnvironment);
	mainStruct.currentAccount = mainStruct.buttonSelected;

	// NULL-terminate string
	mainStruct.errorString[0] = 0;

	// Main loop
	while (aptMainLoop()) {
		bool exit = false;
		
		// get any input, and if applicable the location where the screen was touched
		hidScanInput();
		touchPosition touch;
		hidTouchRead(&touch);
		u32 kDown = hidKeysDown();
		u32 kHeld = hidKeysHeld();
		
		// Render the scene
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		
        C2D_TargetClear(top_screen, C2D_Color32(241, 235, 136, 0xFF));
        C2D_TargetClear(bottom_screen, C2D_Color32(241, 235, 136, 0xFF));
		
		// TODO: change firstRunOfState into stateRunIndex, incrementing every time the state is run
		if (mainStruct.lastState != mainStruct.state) mainStruct.firstRunOfState = true;
		
		if (mainStruct.state == 0) {
			exit = LumaValidation::checkIfLumaOptionsEnabled(&mainStruct, top_screen, bottom_screen, kDown, kHeld, touch);
		} else {
            
            exit = MainUI::drawUI(&mainStruct, top_screen, bottom_screen, kDown, kHeld, touch);
            
            //if (mainStruct.welcome == 0) {
                //exit = TEST::drawUI(&mainStruct, top_screen, bottom_screen, kDown, kHeld, touch);
            //} else {
                //exit = MainUI::drawUI(&mainStruct, top_screen, bottom_screen, kDown, kHeld, touch);
            //}
		}
		
		mainStruct.lastState = mainStruct.state;
		mainStruct.firstRunOfState = false;
		
		C3D_FrameEnd(0);
		
		if (exit) break;
	}
    
    SFX("romfs:/sfx/HOME_OPEN.wav");
    
	// Deinitialize the libs
	C2D_Fini();
	C3D_Fini();
    ndspExit();
	gfxExit();
	actExit();
	frdExit();
	ndmuExit();

	if (mainStruct.needsReboot) {
		NS_RebootSystem();
	}

	nsExit();
    romfsExit();

	return 0;
}
