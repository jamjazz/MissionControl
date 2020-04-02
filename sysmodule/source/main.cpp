#include <switch.h>
#include "bthid.hpp"

#include "logger.hpp"

// Adjust size as needed.
#define INNER_HEAP_SIZE 0x80000

#ifdef __cplusplus
extern "C" {
#endif

// Sysmodules should not use applet*.
u32 __nx_applet_type = AppletType_None;


size_t nx_inner_heap_size = INNER_HEAP_SIZE;
char   nx_inner_heap[INNER_HEAP_SIZE];

void __libnx_initheap(void) {
	void*  addr = nx_inner_heap;
	size_t size = nx_inner_heap_size;

	// Newlib
	extern char* fake_heap_start;
	extern char* fake_heap_end;

	fake_heap_start = (char*)addr;
	fake_heap_end   = (char*)addr + size;
}

// Init/exit services, update as needed.
void __attribute__((weak)) __appInit(void) {
    Result rc;

    // Initialize default services.
    rc = smInitialize();
    if (R_FAILED(rc))
        fatalThrow(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));

    // Ensure system firmware version has been set before we start doing IPC that relies on it
    if (hosversionGet() == 0) {
        rc = setsysInitialize();
        if (R_SUCCEEDED(rc)) {
            SetSysFirmwareVersion fw;
            rc = setsysGetFirmwareVersion(&fw);
            if (R_SUCCEEDED(rc))
                hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
            setsysExit();
        }
    }

    rc = fsInitialize();
    if (R_FAILED(rc))
        fatalThrow(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));

    fsdevMountSdmc();

    rc = pscmInitialize();
    if R_FAILED(rc)
        fatalThrow(rc);

    rc = btdrvInitialize();
    if (R_FAILED(rc))
        fatalThrow(rc);

    rc = hiddbgInitialize();
    if R_FAILED(rc)
        fatalThrow(rc);
}

void __attribute__((weak)) userAppExit(void);

void __attribute__((weak)) __appExit(void) {
    hiddbgExit();
    btdrvExit();
    pscmExit();

    fsdevUnmountAll();
    fsExit();
    smExit();
}

#ifdef __cplusplus
}
#endif


int main(int argc, char* argv[]) {
    Result rc;
    PscPmModule pmModule = {};
    PscPmState  pmState;
    uint32_t    pmFlags;

    /* Init power management */
    PscPmModuleId pmModuleId = static_cast<PscPmModuleId>(0xbd);
    const uint16_t deps[] = { PscPmModuleId_Bluetooth }; //PscPmModuleId_Bluetooth, PscPmModuleId_Btm, PscPmModuleId_Hid ??
    rc = pscmGetPmModule(&pmModule, pmModuleId, deps, sizeof(deps) / sizeof(u16), true);
    if (R_FAILED(rc))
        fatalThrow(rc);

    mc::bthid::Initialise();
    //mc::log::Write("HID initialised");

    while (!mc::bthid::exitFlag) {

        /* Check power management events */
        if R_SUCCEEDED(eventWait(&pmModule.event, U64_MAX)) {
            
            rc = pscPmModuleGetRequest(&pmModule, &pmState, &pmFlags);
            if (R_SUCCEEDED(rc)) {
                switch(pmState) {
                    case PscPmState_Awake:
                        //mc::log::Write("pmState: PscPmState_Awake");
                        break;
                    case PscPmState_ReadyAwaken:
                        //mc::log::Write("pmState: PscPmState_ReadyAwaken");
                        break;
                    case PscPmState_ReadySleep:
                        //mc::log::Write("pmState: PscPmState_ReadySleep");
                        mc::bthid::PrepareForSleep();
                        break;
                    case PscPmState_ReadyShutdown:
                    case PscPmState_ReadyAwakenCritical:              
                    case PscPmState_ReadySleepCritical:
                    default:
                        break;
                }

                rc = pscPmModuleAcknowledge(&pmModule, pmState);
            }
        }
    }

    mc::bthid::Cleanup();

    /* Cleanup power management */
    eventClose(&pmModule.event);
    pscPmModuleFinalize(&pmModule);
    pscPmModuleClose(&pmModule);
    
    return 0;
}
