#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
/*
 * Load MSP430.DLL at runtime via LoadLibrary/GetProcAddress.
 * This avoids needing an import library and sidesteps the 32-bit
 * __stdcall name decoration mismatch (the DLL exports undecorated
 * names like "MSP430_Run", but 32-bit MSVC expects "_MSP430_Run@8").
 * GetProcAddress always uses undecorated names, so it just works.
 */
#include <DLL430_SYMBOL.h>
#undef DLL430_SYMBOL
#define DLL430_SYMBOL  /* suppress __declspec(dllimport) */
#else
#include <unistd.h>
#endif

#include <MSP430.h>
#include <MSP430_EnergyTrace.h>
#include <MSP430_Debug.h>

#ifdef _WIN32
/* Function pointer types */
typedef STATUS_T (WINAPI *pfn_MSP430_Initialize)(const char*, int32_t*);
typedef STATUS_T (WINAPI *pfn_MSP430_Close)(int32_t);
typedef STATUS_T (WINAPI *pfn_MSP430_VCC)(int32_t);
typedef STATUS_T (WINAPI *pfn_MSP430_OpenDevice)(const char*, const char*, int32_t, int32_t, int32_t);
typedef STATUS_T (WINAPI *pfn_MSP430_GetFoundDevice)(uint8_t*, int32_t);
typedef STATUS_T (WINAPI *pfn_MSP430_Run)(int32_t, int32_t);
typedef STATUS_T (WINAPI *pfn_MSP430_EnableEnergyTrace)(const EnergyTraceSetup*, const EnergyTraceCallbacks*, EnergyTraceHandle*);
typedef STATUS_T (WINAPI *pfn_MSP430_DisableEnergyTrace)(const EnergyTraceHandle);
typedef STATUS_T (WINAPI *pfn_MSP430_ResetEnergyTrace)(const EnergyTraceHandle);
typedef STATUS_T (WINAPI *pfn_MSP430_LoadDeviceDb)(const char*);
typedef int32_t  (WINAPI *pfn_MSP430_Error_Number)(void);
typedef const char* (WINAPI *pfn_MSP430_Error_String)(int32_t);

static pfn_MSP430_Initialize        pMSP430_Initialize;
static pfn_MSP430_Close             pMSP430_Close;
static pfn_MSP430_VCC               pMSP430_VCC;
static pfn_MSP430_OpenDevice        pMSP430_OpenDevice;
static pfn_MSP430_GetFoundDevice    pMSP430_GetFoundDevice;
static pfn_MSP430_Run               pMSP430_Run;
static pfn_MSP430_EnableEnergyTrace pMSP430_EnableEnergyTrace;
static pfn_MSP430_DisableEnergyTrace pMSP430_DisableEnergyTrace;
static pfn_MSP430_ResetEnergyTrace  pMSP430_ResetEnergyTrace;
static pfn_MSP430_LoadDeviceDb      pMSP430_LoadDeviceDb;
static pfn_MSP430_Error_Number      pMSP430_Error_Number;
static pfn_MSP430_Error_String      pMSP430_Error_String;

/* Redirect calls to function pointers so the rest of the code is unchanged */
#define MSP430_Initialize        pMSP430_Initialize
#define MSP430_Close             pMSP430_Close
#define MSP430_VCC               pMSP430_VCC
#define MSP430_OpenDevice        pMSP430_OpenDevice
#define MSP430_GetFoundDevice    pMSP430_GetFoundDevice
#define MSP430_Run               pMSP430_Run
#define MSP430_EnableEnergyTrace pMSP430_EnableEnergyTrace
#define MSP430_DisableEnergyTrace pMSP430_DisableEnergyTrace
#define MSP430_ResetEnergyTrace  pMSP430_ResetEnergyTrace
#define MSP430_LoadDeviceDb      pMSP430_LoadDeviceDb
#define MSP430_Error_Number      pMSP430_Error_Number
#define MSP430_Error_String      pMSP430_Error_String

static int LoadMSP430(void) {
	HMODULE dll = LoadLibraryA("MSP430.DLL");
	if (!dll) {
		fprintf(stderr, "Error: Could not load MSP430.DLL (error %lu).\n"
		                "Place MSP430.DLL in the same directory as this exe,\n"
		                "or install TI MSP Debug Stack / Code Composer Studio.\n",
		                (unsigned long)GetLastError());
		return -1;
	}

	#define LOAD(name) do { \
		p##name = (pfn_##name)GetProcAddress(dll, #name); \
		if (!p##name) { \
			fprintf(stderr, "Error: MSP430.DLL missing function: " #name "\n"); \
			FreeLibrary(dll); \
			return -1; \
		} \
	} while(0)

	LOAD(MSP430_Initialize);
	LOAD(MSP430_Close);
	LOAD(MSP430_VCC);
	LOAD(MSP430_OpenDevice);
	LOAD(MSP430_GetFoundDevice);
	LOAD(MSP430_Run);
	LOAD(MSP430_EnableEnergyTrace);
	LOAD(MSP430_DisableEnergyTrace);
	LOAD(MSP430_ResetEnergyTrace);
	LOAD(MSP430_Error_Number);
	LOAD(MSP430_Error_String);

	#undef LOAD

	/* LoadDeviceDb may not exist in older DLL versions */
	pMSP430_LoadDeviceDb = (pfn_MSP430_LoadDeviceDb)GetProcAddress(dll, "MSP430_LoadDeviceDb");

	return 0;
}
#endif /* _WIN32 */

enum { ET_RECORD_SIZE = 18 };

static uint16_t read_le_u16(const uint8_t* p) {
	return (uint16_t)p[0]
	     | ((uint16_t)p[1] << 8);
}

static uint32_t read_le_u32(const uint8_t* p) {
	return (uint32_t)p[0]
	     | ((uint32_t)p[1] << 8)
	     | ((uint32_t)p[2] << 16)
	     | ((uint32_t)p[3] << 24);
}

static uint64_t read_le_u56(const uint8_t* p) {
	return (uint64_t)p[0]
	     | ((uint64_t)p[1] << 8)
	     | ((uint64_t)p[2] << 16)
	     | ((uint64_t)p[3] << 24)
	     | ((uint64_t)p[4] << 32)
	     | ((uint64_t)p[5] << 40)
	     | ((uint64_t)p[6] << 48);
}

void push_cb(void* pContext, const uint8_t* pBuffer, uint32_t nBufferSize) {
	if (nBufferSize % ET_RECORD_SIZE != 0) {
		fprintf(stderr, "Error: Unexpected EnergyTrace record length %u bytes.\n", nBufferSize);
		return;
	}

	uint32_t n = nBufferSize / ET_RECORD_SIZE;
	for (uint32_t i = 0; i < n; i++) {
		const uint8_t* ev = pBuffer + (i * ET_RECORD_SIZE);
		if (ev[0] == ET_EVENT_CURR_VOLT_ENERGY) {
			uint64_t timestamp = read_le_u56(ev + 1);
			uint32_t current = read_le_u32(ev + 8);
			uint32_t voltage = read_le_u16(ev + 12);
			uint32_t energy = read_le_u32(ev + 14);
			printf("%020" PRIu64 ",%010" PRIu32 ",%010" PRIu32 ",%010" PRIu32 "\n",
			       timestamp,
			       current,
			       voltage,
			       energy);
		}
	}
}

void error_cb(void* pContext, const char* pszErrorText) {
	printf("error %s\n", pszErrorText);
}

void usage(char *a0) {
	printf("usage: %s <seconds> [port]\n", a0);
	printf("  seconds  Measurement duration\n");
	printf("  port     Interface port (default: TIUSB)\n");
	printf("           Examples: TIUSB, USB, COM3, COM4\n");
}

int main(int argc, char *argv[]) {
	if(argc<2) {
		usage(argv[0]);
		return 1;
	}
	unsigned int duration = strtod(argv[1], 0);
	if(duration == 0) {
		usage(argv[0]);
		return 1;
	}

#ifdef _WIN32
	if (LoadMSP430() != 0)
		return 1;
#endif

	STATUS_T status, secure = STATUS_OK;
	char* portNumber;
	int  version;
	long  vcc = 3300;
	union DEVICE_T device;

	portNumber = (argc >= 3) ? argv[2] : "TIUSB";

	printf("#Initializing the interface: ");
	status = MSP430_Initialize(portNumber, &version);
	printf("#MSP430_Initialize(portNumber=%s, version=%d) returns %d\n", portNumber, version, status);
	if(status != STATUS_OK) {
		fprintf(stderr, "Error: %s\n", MSP430_Error_String(MSP430_Error_Number()));
		if(version == -1 || version == -3) {
			fprintf(stderr, "Note: DLL/firmware version mismatch (version=%d).\n"
			                "Consider updating the MSP Debug Stack or FET firmware.\n", version);
		}
		return 1;
	}

	//status = MSP430_Configure(ET_CURRENTDRIVE_FINE, 1);
	//printf("#MSP430_Configure(ET_CURRENTDRIVE_FINE, 1) =%d\n", status);

	// 2. Set the device Vcc.
	printf("#Setting the device Vcc: ");
	status = MSP430_VCC(vcc);
	printf("#MSP430_VCC(%d) returns %d\n", vcc, status);


	// 3. Open the device.
#ifdef _WIN32
	if (MSP430_LoadDeviceDb)
#endif
		MSP430_LoadDeviceDb(NULL); //Required in more recent versions of tilib.
	printf("#Opening the device: ");
	status = MSP430_OpenDevice("DEVICE_UNKNOWN", "", 0, 0, DEVICE_UNKNOWN);
	printf("#MSP430_OpenDevice() returns %d\n", status);
	if(status != STATUS_OK) {
		fprintf(stderr, "Error: %s\n", MSP430_Error_String(MSP430_Error_Number()));
		return 1;
	}

	// 4. Get device information
	status = MSP430_GetFoundDevice((char*)&device, sizeof(device.buffer));
	printf("#MSP430_GetFoundDevice() returns %d\n", status);
	printf("# device.id: %d\n", device.id);
	printf("# device.string: %s\n", device.string);
	printf("# device.mainStart: 0x%04x\n", device.mainStart);
	printf("# device.infoStart: 0x%04x\n", device.infoStart);
	printf("# device.ramEnd: 0x%04x\n", device.ramEnd);
	printf("# device.nBreakpoints: %d\n", device.nBreakpoints);
	printf("# device.emulation: %d\n", device.emulation);
	printf("# device.clockControl: %d\n", device.clockControl);
	printf("# device.lcdStart: 0x%04x\n", device.lcdStart);
	printf("# device.lcdEnd: 0x%04x\n", device.lcdEnd);
	printf("# device.vccMinOp: %d\n", device.vccMinOp);
	printf("# device.vccMaxOp: %d\n", device.vccMaxOp);
	printf("# device.hasTestVpp: %d\n", device.hasTestVpp);


	EnergyTraceSetup ets = {  ET_PROFILING_ANALOG,                // Gives callbacks of with eventID 8
                      ET_PROFILING_1K,                   // N/A
                      ET_ALL,                             // N/A
                      ET_EVENT_WINDOW_100,                // N/A
                      ET_CALLBACKS_ONLY_DURING_RUN };           // Callbacks are continuously
	EnergyTraceHandle ha;
	EnergyTraceCallbacks cbs = {
		.pContext = 0,
		.pPushDataFn = push_cb,
		.pErrorOccurredFn = error_cb
	};
	MSP430_Run(FREE_RUN, 1);
	status = MSP430_EnableEnergyTrace(&ets, &cbs, &ha);
	printf("#MSP430_EnableEnergyTrace=%d\n", status);

	status = MSP430_ResetEnergyTrace(ha);
	printf("#MSP430_ResetEnergyTrace=%d\n", status);

#ifdef _WIN32
	Sleep(duration * 1000);
#else
	sleep(duration);
#endif

	status = MSP430_DisableEnergyTrace(ha);
	printf("#MSP430_DisableEnergyTrace=%d\n", status);

	printf("#Closing the interface: ");
	status = MSP430_Close(0);
	printf("#MSP430_Close(FALSE) returns %d\n", status);

	return 0;
}
