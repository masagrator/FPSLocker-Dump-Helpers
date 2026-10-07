// Include the most common headers from the C standard library
#include <stdio.h>
#include <stdlib.h>

// Include the main libnx system header, for Switch development
#include <switch.h>
#include "dmntcht.h"
extern "C" {
#include "armadillo.h"
#include "strext.h"
}

const uint64_t Tid = 0x0100C49025D3E000;
DmntCheatProcessMetadata cheatMetadata = {0};
u64 mappings_count = 0;
MemoryInfo* memoryInfoBuffers = 0;

bool isServiceRunning(const char *serviceName) {	
	Handle handle;	
	SmServiceName service_name = smEncodeName(serviceName);	
	if (R_FAILED(smRegisterService(&handle, service_name, false, 1))) 
		return true;
	else {
		svcCloseHandle(handle);	
		smUnregisterService(service_name);
		return false;
	}
}

size_t checkAvailableHeap() {
	size_t startSize = 200 * 1024 * 1024;
	void* allocation = malloc(startSize);
	while (allocation) {
		free(allocation);
		startSize += 1024 * 1024;
		allocation = malloc(startSize);
	}
	return startSize - (1024 * 1024);
}

size_t searchDataX(uint32_t* data, size_t data_instruction_count, uint32_t* instruction, size_t instruction_count) {
	for (size_t i = 0; i < data_instruction_count; i++) {
		bool error = false;
		for (size_t x = 0; x < instruction_count; x++) {
			if (i+x >= data_instruction_count || data[i+x] != instruction[x]) {
				error = true;
				break;
			}
		}
		if (error == false) return i;
	}
	return UINT64_MAX;
}

size_t searchDataMasked(uint32_t* data, size_t data_instruction_count, const uint32_t* instruction, const uint32_t* mask, size_t instruction_count) {
	for (size_t i = 0; i + instruction_count <= data_instruction_count; i++) {
		bool error = false;
		for (size_t x = 0; x < instruction_count; x++) {
			if ((data[i+x] & mask[x]) != instruction[x]) {
				error = true;
				break;
			}
		}
		if (error == false) return i;
	}
	return UINT64_MAX;
}

// Decodes ADRP + LDR pair and returns absolute address (0 on error)
uint64_t decodeAdrpLdr(uint32_t* buffer, size_t adrp_itr, size_t ldr_itr, const char* name) {
	ad_insn *insn = NULL;
	uint64_t base = cheatMetadata.main_nso_extents.base;
	ArmadilloDisassemble(buffer[adrp_itr], (adrp_itr * 4) + base, &insn);
	if (insn -> instr_id != AD_INSTR_ADRP) {
		printf("%s: ADRP error!\n", name);
		ArmadilloDone(&insn);
		return 0;
	}
	uint64_t address = insn -> operands[1].op_imm.bits;
	ArmadilloDone(&insn);
	ArmadilloDisassemble(buffer[ldr_itr], (ldr_itr * 4) + base, &insn);
	if (insn -> instr_id != AD_INSTR_LDR) {
		printf("%s: LDR error!\n", name);
		ArmadilloDone(&insn);
		return 0;
	}
	address += insn -> operands[2].op_imm.bits;
	ArmadilloDone(&insn);
	return address;
}

void searchInRAM() {
	// FPS lock: fmov v1.2d, #30.0 ; ADRP at REF-0x4, LDR at REF+0x8
	const uint32_t search_fps[] = {0x6F01F7C1};
	// Dynamic Resolution renderer global; ADRP at REF-0x8, LDR at REF-0x4
	// 08 0d 40 f9 e8 07 00 f9 09 05 40 f9 89 01 00 b4 08 09 40 f9 29 05 40 f9 bf 39 03 d5
	const uint32_t search_dr[] = {0xF9400D08, 0xF90007E8, 0xF9400509, 0xB4000189, 0xF9400908, 0xF9400529, 0xD50339BF};
	// DRS regulator offset in renderer: ldr x23, [x20, #imm] (imm12 * 8) ; ldr x8, [x23, #0x40]
	const uint32_t search_dr_reg[] = {0xF9400297, 0xF94022E8};
	const uint32_t search_dr_reg_mask[] = {0xFFC003FF, 0xFFFFFFFF};
	// DRS regulator init (ldr s8, [x0, #0xF8] ; ldr x0, [x21, #0x20]) - main_offset for MASTER_WRITE = REF
	// 08 f8 40 bd a0 12 40 f9
	const uint32_t search_dr_init[] = {0xBD40F808, 0xF94012A0};
	// Same place when FPSLocker patch is already applied (ldr s8, [x0, #0xF8] ; ldr s9, [x0, #0x144])
	const uint32_t search_dr_init_patched[] = {0xBD40F808, 0xBD414409};

	size_t count = cheatMetadata.main_nso_extents.size / 4;
	uint32_t* buffer_c = new uint32_t[count];
	dmntchtReadCheatProcessMemory(cheatMetadata.main_nso_extents.base, (void*)buffer_c, cheatMetadata.main_nso_extents.size);

	nsInitialize();
	size_t appControlDataSize = 0;
	s32 appContentMetaStatusSize = 0;
	NsApplicationControlData appControlData;
	NsApplicationContentMetaStatus appContentMetaStatus[2];
	if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource::NsApplicationControlSource_Storage, Tid, &appControlData, sizeof(NsApplicationControlData), &appControlDataSize))) {
		printf("Game version: " CONSOLE_YELLOW "%s" CONSOLE_RESET, appControlData.nacp.display_version);
		if (R_SUCCEEDED(nsListApplicationContentMetaStatus(Tid, 0, appContentMetaStatus, 2, &appContentMetaStatusSize))) {
			u32 index = 0;
			if (appContentMetaStatus[1].meta_type == NcmContentMetaType_Patch) index = 1;
			printf("/" CONSOLE_YELLOW "v%d" CONSOLE_RESET, appContentMetaStatus[index].version / 65536);
		}
		printf("\n");
	}
	nsExit();
	printf("BID: " CONSOLE_YELLOW "%016lX\n" CONSOLE_RESET, __builtin_bswap64(*(uint64_t*)&cheatMetadata.main_nso_build_id[0]));

	size_t itr = searchDataX(buffer_c, count, (uint32_t*)search_fps, sizeof(search_fps) / 4);
	if (itr != UINT64_MAX && itr >= 1) {
		uint64_t address = decodeAdrpLdr(buffer_c, itr - 1, itr + 2, "FPS lock");
		if (address)
			printf("Offset storing FPS lock: " CONSOLE_YELLOW "0x%lX\n" CONSOLE_RESET, address - cheatMetadata.main_nso_extents.base);
	}
	else printf("FPS lock instruction was not found in executable!\n");

	itr = searchDataX(buffer_c, count, (uint32_t*)search_dr, sizeof(search_dr) / 4);
	if (itr != UINT64_MAX && itr >= 2) {
		uint64_t address = decodeAdrpLdr(buffer_c, itr - 2, itr - 1, "DR");
		if (address) {
			uint32_t reg_offset = 0x14C8;
			size_t itr2 = searchDataMasked(buffer_c, count, search_dr_reg, search_dr_reg_mask, sizeof(search_dr_reg) / 4);
			if (itr2 != UINT64_MAX)
				reg_offset = ((buffer_c[itr2] >> 10) & 0xFFF) * 8;
			else printf("DR regulator offset not found, using default 0x14C8\n");
			printf("Offset storing DR renderer: " CONSOLE_YELLOW "0x%lX\n" CONSOLE_RESET, address - cheatMetadata.main_nso_extents.base);
			printf("DR min target: " CONSOLE_YELLOW "[MAIN, 0x%lX, 0x%X, 0xC]\n" CONSOLE_RESET, address - cheatMetadata.main_nso_extents.base, reg_offset);
			printf("DR max target: " CONSOLE_YELLOW "[MAIN, 0x%lX, 0x%X, 0x10]\n" CONSOLE_RESET, address - cheatMetadata.main_nso_extents.base, reg_offset);
		}
	}
	else printf("DR instructions were not found in executable!\n");

	itr = searchDataX(buffer_c, count, (uint32_t*)search_dr_init, sizeof(search_dr_init) / 4);
	if (itr != UINT64_MAX)
		printf("DR init patch main_offset: " CONSOLE_YELLOW "0x%lX\n" CONSOLE_RESET, itr * 4);
	else {
		itr = searchDataX(buffer_c, count, (uint32_t*)search_dr_init_patched, sizeof(search_dr_init_patched) / 4);
		if (itr != UINT64_MAX)
			printf("DR init patch main_offset: " CONSOLE_YELLOW "0x%lX" CONSOLE_RESET " (already patched)\n", itr * 4);
		else printf("DR init instructions were not found in executable!\n");
	}

	delete[] buffer_c;
}

// Main program entrypoint
int main(int argc, char* argv[])
{
	// This example uses a text console, as a simple way to output text to the screen.
	// If you want to write a software-rendered graphics application,
	//   take a look at the graphics/simplegfx example, which uses the libnx Framebuffer API instead.
	// If on the other hand you want to write an OpenGL based application,
	//   take a look at the graphics/opengl set of examples, which uses EGL instead.
	consoleInit(NULL);

	// Configure our supported input layout: a single player with standard controller styles
	padConfigureInput(1, HidNpadStyleSet_NpadStandard);

	// Initialize the default gamepad (which reads handheld mode inputs as well as the first connected controller)
	PadState pad;
	padInitializeDefault(&pad);

	bool error = false;
	if (!isServiceRunning("dmnt:cht")) {
		printf("DMNT:CHT not detected!\n");
		error = true;
	}
	pmdmntInitialize();
	uint64_t PID = 0;
	if (R_FAILED(pmdmntGetApplicationProcessId(&PID))) {
		printf("Game not initialized.\n");
		error = true;
	}
	pmdmntExit();
	if (error) {
		printf("Press + to exit.");
		while (appletMainLoop()) {   
			// Scan the gamepad. This should be done once for each frame
			padUpdate(&pad);

			// padGetButtonsDown returns the set of buttons that have been
			// newly pressed in this frame compared to the previous one
			u64 kDown = padGetButtonsDown(&pad);

			if (kDown & HidNpadButton_Plus)
				break; // break in order to return to hbmenu

			// Your code goes here

			// Update the console, sending a new frame to the display
			consoleUpdate(NULL);
		}
	}
	else {
		size_t availableHeap = checkAvailableHeap();
		printf("Available Heap: %ld MB\n", (availableHeap / (1024 * 1024)));
		consoleUpdate(NULL);
		dmntchtInitialize();
		bool hasCheatProcess = false;
		dmntchtHasCheatProcess(&hasCheatProcess);
		if (!hasCheatProcess) {
			dmntchtForceOpenCheatProcess();
		}

		Result res = dmntchtGetCheatProcessMetadata(&cheatMetadata);
		if (res)
			printf("dmntchtGetCheatProcessMetadata ret: 0x%x\n", res);
		
		if (cheatMetadata.title_id != Tid) {

		}
		if (!res) {
			res = dmntchtGetCheatProcessMappingCount(&mappings_count);
			if (res)
				printf("dmntchtGetCheatProcessMappingCount ret: 0x%x\n", res);
			else printf("Mapping count: %ld\n", mappings_count);
		}

		memoryInfoBuffers = new MemoryInfo[mappings_count];

		if (!res) {
			res = dmntchtGetCheatProcessMappings(memoryInfoBuffers, mappings_count, 0, &mappings_count);
			if (res)
				printf("dmntchtGetCheatProcessMappings ret: 0x%x\n", res);
		}

		//Test run

		if (!res) {
			printf("\n----------\nPress A for Scan\n");
			printf("Press + to Exit\n\n");
			consoleUpdate(NULL);
			while (appletMainLoop()) {   
				padUpdate(&pad);

				u64 kDown = padGetButtonsDown(&pad);

				if (kDown & HidNpadButton_A)
					break;

				if (kDown & HidNpadButton_Plus) {
					dmntchtExit();
					consoleExit(NULL);
					return 0;
				}

			}
			printf("Searching RAM...\n\n");
			consoleUpdate(NULL);
			appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad);
			searchInRAM();
			printf(CONSOLE_BLUE "\n---------------------------------------------\n\n" CONSOLE_RESET);
			printf(CONSOLE_WHITE "Search is finished!\n");
			consoleUpdate(NULL);
			appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
		}
		
		delete[] memoryInfoBuffers;
		dmntchtExit();
		printf("Press + to exit.");
		while (appletMainLoop()) {   
			// Scan the gamepad. This should be done once for each frame
			padUpdate(&pad);

			// padGetButtonsDown returns the set of buttons that have been
			// newly pressed in this frame compared to the previous one
			u64 kDown = padGetButtonsDown(&pad);

			if (kDown & HidNpadButton_Plus)
				break; // break in order to return to hbmenu

			// Your code goes here

			// Update the console, sending a new frame to the display
			consoleUpdate(NULL);
		}
	}

	// Deinitialize and clean up resources used by the console (important!)
	consoleExit(NULL);
	return 0;
}
