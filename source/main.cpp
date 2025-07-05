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

uint64_t Tid = 0x01005AE0209A8000;
DmntCheatProcessMetadata cheatMetadata = {0};
u64 mappings_count = 0;
MemoryInfo* memoryInfoBuffers = 0;
uint8_t utf_encoding = 0;
struct ue4Results {
	const char* iterator;
	bool isFloat = false;
	int default_value_int;
	float default_value_float;
	uint32_t offset;
	uint32_t add;
};

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
			if (i+x >= data_instruction_count) break;
			if (data[i+x] != instruction[x]) {
				error = true;
				break;
			}
		}
		if (error == false) return i;
	}
	return UINT64_MAX;
}

void printGameInfo() {
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
	printf("BID: " CONSOLE_YELLOW "%lX\n" CONSOLE_RESET, __builtin_bswap64(*(uint64_t*)&cheatMetadata.main_nso_build_id[0]));
}

void searchInRAM() {
	char search[] = "\x00\x00\x70\x42\x3C\x00\x00\x00\x00\x00\x70\x42\x02\x00\x00\x00";
	char searchAlt[] = "\x00\x00\xF0\x41\x1E\x00\x00\x00\x00\x00\xF0\x41\x04\x00\x00\x00";
	static_assert((sizeof(search)-1) % 4 == 0);
	uint32_t* buffer_c = new uint32_t[cheatMetadata.main_nso_extents.size / 4];
	dmntchtReadCheatProcessMemory(cheatMetadata.main_nso_extents.base, (void*)buffer_c, cheatMetadata.main_nso_extents.size);
	size_t itr = searchDataX(buffer_c, cheatMetadata.main_nso_extents.size / 4, (uint32_t*)&search[0], (sizeof(search)-1) / 4);
	if (itr != UINT64_MAX) {
		printf("Offset storing FPS values: \n" CONSOLE_YELLOW "0x%lX\n" CONSOLE_RESET, itr * 4);
	}
	else {
		itr = searchDataX(buffer_c, cheatMetadata.main_nso_extents.size / 4, (uint32_t*)&searchAlt[0], (sizeof(searchAlt)-1) / 4);
		if (itr != UINT64_MAX) {
			printf("Offset storing FPS values: \n" CONSOLE_YELLOW "0x%lX\n" CONSOLE_RESET, itr * 4);
		}
		else printf("FPS Lock was not found in executable!\n");
	}
	printf("\n---\n\n");
	char search2[] = "\x21\xD8\x21\x7E\x41\x18\x21\x1E\x00\x20\x21\x1E";
	itr = searchDataX(buffer_c, cheatMetadata.main_nso_extents.size / 4, (uint32_t*)&search2[0], (sizeof(search2)-1) / 4);
	if (itr != UINT64_MAX) {
		ad_insn *insn = NULL;
		uint64_t distance = (itr * 4);
		ArmadilloDisassemble(buffer_c[itr], distance, &insn);
		if (insn -> instr_id != AD_INSTR_UCVTF) {
			printf("UCVTF error!\n");
			ArmadilloDone(&insn);
			delete[] buffer_c;
			return;
		}
		ArmadilloDone(&insn);
		distance += 4;
		ArmadilloDisassemble(buffer_c[itr+1], distance, &insn);
		if (insn -> instr_id != AD_INSTR_FDIV) {
			printf("FDIV error!\n");
			ArmadilloDone(&insn);
			delete[] buffer_c;
			return;
		}
		ArmadilloDone(&insn);
		printf("Offset storing FPS lock: \n" CONSOLE_YELLOW "0x%lX\n" CONSOLE_RESET, itr * 4);
		printf("Replace instructions to direct it at offset provided before via \"ADRP X1 ...\", \"LDR S1, [X1...\".\n");
	}
	else printf("FPS Lock was not found in executable!\n");
	printf("\n---\n\n");
	char search3[] = "\x09\x49\xA8\x52\x21\x01\x27\x1E\x00\x20\x21\x1E";
	itr = searchDataX(buffer_c, cheatMetadata.main_nso_extents.size / 4, (uint32_t*)&search3[0], (sizeof(search3)-1) / 4);
	if (itr != UINT64_MAX) {
		ad_insn *insn = NULL;
		uint64_t distance = (itr * 4);
		ArmadilloDisassemble(buffer_c[itr], distance, &insn);
		if (insn -> instr_id != AD_INSTR_MOV) {
			printf("MOV error!\n");
			ArmadilloDone(&insn);
			delete[] buffer_c;
			return;
		}
		ArmadilloDone(&insn);
		distance += 4;
		ArmadilloDisassemble(buffer_c[itr+1], distance, &insn);
		if (insn -> instr_id != AD_INSTR_FMOV) {
			printf("FMOV error!\n");
			ArmadilloDone(&insn);
			delete[] buffer_c;
			return;
		}
		ArmadilloDone(&insn);
		distance += 12;
		ArmadilloDisassemble(buffer_c[itr+4], distance, &insn);
		if (insn -> instr_id != AD_INSTR_MOV) {
			printf("MOV error!\n");
			ArmadilloDone(&insn);
			delete[] buffer_c;
			return;
		}
		ArmadilloDone(&insn);
		distance += 4;
		ArmadilloDisassemble(buffer_c[itr+5], distance, &insn);
		if (insn -> instr_id != AD_INSTR_FMOV) {
			printf("FMOV error!\n");
			ArmadilloDone(&insn);
			delete[] buffer_c;
			return;
		}
		ArmadilloDone(&insn);
		printf("Offset storing DR Target 1: \n" CONSOLE_YELLOW "0x%lX\n" CONSOLE_RESET, itr * 4);
		printf("Offset storing DR Target 2: \n" CONSOLE_YELLOW "0x%lX\n" CONSOLE_RESET, (itr+4) * 4);
		printf("Store custom DR Target at: \n" CONSOLE_YELLOW "0x%lX\n" CONSOLE_RESET, cheatMetadata.main_nso_extents.size - 0x100);
		printf("Replace instructions to direct it at offsets provided before via \"ADRP X9 ...\", \"LDR S1, [X9...\".\n");
	}
	else printf("FPS Lock was not found in executable!\n");
	printf("\n---\n\n");
	printGameInfo();
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
		pmdmntExit();
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
		
		if (!res) {
			if (cheatMetadata.title_id != Tid) {
				printf("Game has different Titleid than expected!\nTool may not work properly.\n");
			}
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