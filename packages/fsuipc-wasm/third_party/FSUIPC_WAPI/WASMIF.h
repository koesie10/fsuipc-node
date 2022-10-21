#pragma once
#include <windows.h>
#include <stdio.h>
#include <map>
#include <unordered_map>
#include <vector>
#include "SimConnect.h"
#include "WASM.h"
#include "ClientDataArea.h"
#include "CDAIdBank.h"

#define WAPI_VERSION			"0.9.1"

using namespace ClientDataAreaMSFS;
using namespace CDAIdBankMSFS;

using namespace std;

enum LOGLEVEL
{
	DISABLE_LOG = 1,
	LOG_LEVEL_INFO = 2,
	LOG_LEVEL_BUFFER = 3,
	LOG_LEVEL_DEBUG = 4,
	LOG_LEVEL_TRACE = 5,
	ENABLE_LOG = 6,
};

class WASMIF
{
	public:
		static class WASMIF* GetInstance(void (*loggerFunction)(const char* logString) = nullptr);
		// The 2 methods below are kept for backwards compatibility. The HWND handle is no longer required.
		static class WASMIF* GetInstance(HWND hWnd, void (*loggerFunction)(const char* logString) = nullptr);

		bool start(); // Startrs the connection to the WASM. 
		bool isRunning(); // Returns True if cpnnected to the WASM 
		void end(); // Terminates the connection to the WASM
		void createAircraftLvarFile(); // Depracated. This re-scans for lvars and puts the output into files (in the WASM work area), with the number of lvars per file being the number that one CDA can hold
		void reload(); // This sends a request to the WASM ro re-scan for lvars and hvars, and drop/recreate the CDAs
		void setLvarUpdateFrequency(int freq); // Sets the lvar update frequency in the client. This must be called before start, and only takes affect if lvar update is disabled in the WASM module
		void setSimConfigConnection(int connection); // Used to set the SomConnect connection number to be used, from your SimConnect.cfg file.
		int getLvarUpdateFrequency(); // Returns the lvar update frequency set in the client.
		void setLogLevel(LOGLEVEL logLevel); // Changs the log level used
		double getLvar(int lvarID); // Returns an lvar value by lvar ID
		double getLvar(const char * lvarName); // Returns an lvar value by lvar name. Note that the name should NOT be prefixed by 'L:'
		void setLvar(unsigned short id, const char* value); // Conveniance function. Sets the lvar value by id. The value is parsed and then either the short, unsigned short or double setLvar function is used 
		void setLvar(unsigned short id, double value); // Sets the value of an lvar by id as a double. This request goes via a CDA
		void setLvar(unsigned short id, short value); // Sets the value of an lvar by id as a (signed) short. This request goes via an event
		void setLvar(unsigned short id, unsigned short value);// Sets the value of an lvar by id as an unsigned short. This request goes via an event
		void setLvar(const char* lvarName, const char* value); 
		void setLvar(const char *lvarName, double value);
		void setLvar(const char *lvarName, short value);
		void setLvar(const char *lvarName, unsigned short value);
		void setHvar(int id); // Activates a HTML variable by ID
		void setHvar(const char* hvarName); // Activates a HTML variable by name. Note that, unlike lvars, the hvar name must be preceeded by 'H:'
		void logLvars(); // Logs all lvars and values (to the defined logger)
		void getLvarValues(map<string, double >& returnMap); // Returnes a map of all lvar values keyed on the lvar name
		void logHvars(); // Just print to log for now
		void getLvarList(unordered_map<int, string >& returnMap); // Returns a list of lvar names keyed on the lvar id
		void getHvarList(unordered_map<int, string >& returnMap); // Returns a list of hvar names keyed on the lvar id
		void executeCalclatorCode(const char *code); // Executes the argument calculator code. Max allowed length of the code is defined in the WASM.h by MAX_CALC_CODE_SIZE
		int getLvarIdFromName(const char* lvarName); // Utility function to get the id of an lvar. Lvar name must not be preceded by 'L:'. -1 retuned if lvar not found
		void getLvarNameFromId(int id, char* name); // Gets the name of an lvar from an id. The name MUST be allocated to hold a minimum of MAX_VAR_NAME_SIZE (56) bytes
		int getHvarIdFromName(const char* hvarName); // Utility function to get the id of a hvar. Hvar name must be preceded by 'H:'. -1 retuned if hvar not found
		void getHvarNameFromId(int id, char* name); // Gets the name of a hvar from an id. The name MUST be allocated to hold a minimum of MAX_VAR_NAME_SIZE (56) bytes
		bool createLvar(const char* lvarName, double value); // Creates an lvar and sets its initial value. The lvar name should NOT be preceded by 'L:'
		void registerUpdateCallback(void (*callbackFunction)(void)); // Register for a callback function that is called once all lvar / hvar CDAs have been loaded and are available
		void registerLvarUpdateCallback(void (*callbackFunction)(int id[], double newValue[])); // Register for a callback to be received when lvar values changed. Note that only lvars flagged for this callback will be retuned. A terminating elements of -1 and -1.0 are added to each array returned. Recommened to be used in your UpdateCallback
		void registerLvarUpdateCallback(void (*callbackFunction)(const char* lvarName[], double newValue[])); // As above bit returns the lvar the lvar name instead of the id, with the terminating element being NULL. Recommened to be used in your UpdateCallback
		void flagLvarForUpdateCallback(int lvarId); // Flags an lvar to be included in the lvarUpdateCallback, by ID. Recommened to be used in your UpdateCallback
		void flagLvarForUpdateCallback(const char* lvarName); // Flags an lvar to be included in the lvarUpdateCallback, by name. Recommened to be used in your UpdateCallback

	public:
		// Internal functions that need to be public. Do not use.
		static void CALLBACK MyDispatchProc(SIMCONNECT_RECV* pData, DWORD cbData, void* pContext);
		static void CALLBACK StaticConfigTimer(PVOID lpParameter, BOOLEAN TimerOrWaitFired);
		static void CALLBACK StaticRequestDataTimer(PVOID lpParameter, BOOLEAN TimerOrWaitFired);

	protected:
		WASMIF();
		~WASMIF();
	private:
		static DWORD WINAPI StaticSimConnectThreadStart(void* Param);
		void DispatchProc(SIMCONNECT_RECV* pData, DWORD cbData);
		void ConfigTimer();
		void RequestDataTimer();
		DWORD WINAPI SimConnectStart();
		void SimConnectEnd();
		const char* getEventString(int eventNo);
		void setLvar(DWORD param);
		void setLvarS(DWORD param);

	private:
		static WASMIF* m_Instance;
		HANDLE  hSimConnect;
		volatile HANDLE hThread = nullptr;
		HANDLE hSimEventHandle = nullptr;
		int quit, noLvarCDAs, noHvarCDAs, lvarUpdateFrequency;
		HANDLE configTimerHandle = nullptr;
		HANDLE requestTimerHandle = nullptr;
		ClientDataArea* lvar_cdas[MAX_NO_LVAR_CDAS];
		ClientDataArea* hvar_cdas[MAX_NO_HVAR_CDAS];
		ClientDataArea* value_cda[MAX_NO_VALUE_CDAS];
		static int nextDefinitionID;
		vector<string> lvarNames;
		vector<double> lvarValues;
		vector<bool> lvarFlaggedForCallback;
		vector<string> hvarNames;
		CDAIdBank* cdaIdBank;
		int simConnection;
		CRITICAL_SECTION lvarValuesMutex, lvarNamesMutex, hvarNamesMutex, configMutex;
		void (*cdaCbFunction)(void) = NULL;
		void (*lvarCbFunctionId)(int id[], double newValue[]) = NULL;
		void (*lvarCbFunctionName)(const char* lvarName[], double newValue[]) = NULL;
};

