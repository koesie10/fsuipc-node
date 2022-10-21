#include "WASMIF.h"
#include <SimConnect.h>
#include <sstream>
#include <iomanip>
#include <cmath>
#include "Logger.h"


using namespace CPlusPlusLogging;

enum WASM_EVENT_ID {
	// Events we send
	EVENT_SET_LVAR = 1,		// used to set unsigned shorts via SimConnect
	EVENT_SET_HVAR,			// used to activat a hvar via SimConnect
	EVENT_UPDATE_CDAS,		// used to request an lvar values update
	EVENT_LIST_LVARS,		// used to generate lvar files. Depracated
	EVENT_RELOAD,			// used to reload lvars/hvars and re-create the CDAs
	EVENT_SET_LVARS,		// used to set signed shorts via SimConnect
	// Events we receive
	EVENT_CONFIG_RECEIVED = 9,  // Config data received from the WASM, giving details of CDAs and sizes required
	EVENT_VALUES_RECEIVED = 10, // Start event number of events received when an lvar value CDA have been updated. Allow for MAX_NO_VALUE_CDAS (2)
	EVENT_LVARS_RECEIVED = 13, // Start event number of events received when an lvar name CDA have been updated. Allow for MAX_NO_LVAR_CDAS (12)
	EVENT_HVARS_RECEIVED = 40, // Start event number of events received when an hvar name CDA have been updated. Allow for MAX_NO_HVAR_CDAS (4)
};

WASMIF* WASMIF::m_Instance = 0;
int WASMIF::nextDefinitionID = 1; // 1 taken by config CDA
Logger* pLogger = nullptr;
static CONFIG_CDA currentConfigSet;


WASMIF::WASMIF() {
	hSimConnect = NULL;
	quit = 0;
	noLvarCDAs = 0;
	noHvarCDAs = 0;
	lvarUpdateFrequency = 0;
	InitializeCriticalSection(&lvarValuesMutex);
	InitializeCriticalSection(&lvarNamesMutex);
	InitializeCriticalSection(&hvarNamesMutex);
	InitializeCriticalSection(&configMutex);
	simConnection = SIMCONNECT_OPEN_CONFIGINDEX_LOCAL; // = -1
}

WASMIF::~WASMIF() {}

void WASMIF::setSimConfigConnection(int connection) {
	simConnection = connection;
}


WASMIF* WASMIF::GetInstance(void (*loggerFunction)(const char* logString)) {
	if (m_Instance == 0)
	{
		m_Instance = new WASMIF();
		if (loggerFunction == nullptr) {
			pLogger = Logger::getInstance(".\\FSUIPC_WASMIF");
		}
		else {
			pLogger = Logger::getInstance(loggerFunction);
		}
	}
	return m_Instance;
}


void WASMIF::setLogLevel(LOGLEVEL logLevel) {
	pLogger->updateLogLevel((LogLevel)logLevel);
}


DWORD WINAPI WASMIF::SimConnectStart() {
	HRESULT hr;
	int CDAId = 1;
	nextDefinitionID = 1;

	hr = SimConnect_MapClientEventToSimEvent(hSimConnect, EVENT_SET_LVAR, SET_LVAR_EVENT); // getEventString(EVENT_SET_LVAR));
	hr = SimConnect_MapClientEventToSimEvent(hSimConnect, EVENT_SET_HVAR, ACTIVATE_HVAR_EVENT); // getEventString(EVENT_SET_HVAR));
	hr = SimConnect_MapClientEventToSimEvent(hSimConnect, EVENT_UPDATE_CDAS, UPDATE_CONFIG_EVENT); // getEventString(EVENT_UPDATE_CDAS));
	hr = SimConnect_MapClientEventToSimEvent(hSimConnect, EVENT_LIST_LVARS, LIST_LVARS_EVENT); // getEventString(EVENT_LIST_LVARS));
	hr = SimConnect_MapClientEventToSimEvent(hSimConnect, EVENT_RELOAD, RELOAD_EVENT); // getEventString(EVENT_RELOAD));
	hr = SimConnect_MapClientEventToSimEvent(hSimConnect, EVENT_SET_LVARS, SET_LVAR_SIGNED_EVENT); // getEventString(EVENT_SET_LVARS));
	
	hr = SimConnect_SetNotificationGroupPriority(hSimConnect, 1, SIMCONNECT_GROUP_PRIORITY_HIGHEST);

	// Now register Client Data Area
	if (!SUCCEEDED(SimConnect_MapClientDataNameToID(hSimConnect, CONFIG_CDA_NAME, CDAId++)))
	{
		LOG_ERROR("SimConnect_MapClientDataNameToID failed!!!!");
	}
	if (!SUCCEEDED(SimConnect_AddToClientDataDefinition(hSimConnect, nextDefinitionID++, SIMCONNECT_CLIENTDATAOFFSET_AUTO, sizeof(CONFIG_CDA), 0, 0)))
	{
		LOG_ERROR("SimConnect_AddToClientDataDefinition failed!!!!");
	}
		
	// Register Lvar Set Values Client Data Area for write
	if (!SUCCEEDED(SimConnect_MapClientDataNameToID(hSimConnect, LVARVALUE_CDA_NAME, CDAId++)))
	{
		LOG_ERROR("SimConnect_MapClientDataNameToID failed!!!!");
	}
	if (!SUCCEEDED(SimConnect_AddToClientDataDefinition(hSimConnect, nextDefinitionID++, SIMCONNECT_CLIENTDATAOFFSET_AUTO, sizeof(CDASETLVAR), 0, 0)))
	{
		LOG_ERROR("SimConnect_AddToClientDataDefinition failed!!!!");
	}

	// Register Execute Calculator Code Client Data Area for write
	if (!SUCCEEDED(SimConnect_MapClientDataNameToID(hSimConnect, CCODE_CDA_NAME, CDAId++)))
	{
		LOG_ERROR("SimConnect_MapClientDataNameToID failed!!!!");
	}
	if (!SUCCEEDED(SimConnect_AddToClientDataDefinition(hSimConnect, nextDefinitionID++, SIMCONNECT_CLIENTDATAOFFSET_AUTO, sizeof(CDACALCCODE), 0, 0)))
	{
		LOG_ERROR("SimConnect_AddToClientDataDefinition failed!!!!");
	}

	// Initialise are CDA Id bank - this is responsible for:
	//     - allocating cda ids
	//     - mapping the id to the name
	//     - creating the CDA
	cdaIdBank = new CDAIdBank(CDAId, hSimConnect);

	// Set timer to request config data
	LOG_DEBUG("Starting config request timer...");
	CreateTimerQueueTimer(&configTimerHandle, nullptr, &WASMIF::StaticConfigTimer, this, 0, 1000, WT_EXECUTEDEFAULT);

	// Start message loop
	LOG_TRACE("Starting message loop...");
	while (0 == quit) {
		hr = WaitForSingleObject(hSimEventHandle, 500);
		switch (hr) {
			case WAIT_TIMEOUT:
				continue;
			case WAIT_OBJECT_0:
				SimConnect_CallDispatch(hSimConnect, MyDispatchProc, this);
				continue;
			default:
				break;
		}
		LOG_ERROR("WaitForSingleObject returned with error, quitting now.");
		break;
	}
	LOG_TRACE("Message loop finished - calling SimConnectEnd()...");
	SimConnectEnd();

	hThread = NULL;

	return 0;
}


void CALLBACK WASMIF::StaticConfigTimer(PVOID lpParameter, BOOLEAN TimerOrWaitFired) {
	((WASMIF*)lpParameter)->ConfigTimer();
}

void CALLBACK WASMIF::StaticRequestDataTimer(PVOID lpParameter, BOOLEAN TimerOrWaitFired) {
	((WASMIF*)lpParameter)->RequestDataTimer();
}


void WASMIF::ConfigTimer() {

	if (!SUCCEEDED(SimConnect_RequestClientData(hSimConnect, 1, EVENT_CONFIG_RECEIVED, 1,
		SIMCONNECT_CLIENT_DATA_PERIOD_ONCE, SIMCONNECT_CLIENT_DATA_REQUEST_FLAG_DEFAULT, 0, 0, 0)))
		//		SIMCONNECT_CLIENT_DATA_PERIOD_ONCE, SIMCONNECT_CLIENT_DATA_REQUEST_FLAG_CHANGED, 0, 0, 0)))
	{
		LOG_ERROR("SimConnect_RequestClientData for config failed!!!!");
	}
	else
		LOG_TRACE("Config data requested...");
}


void WASMIF::RequestDataTimer() {
	// Send event to update lvar list
	if (!SUCCEEDED(SimConnect_TransmitClientEvent(hSimConnect, SIMCONNECT_SIMOBJECT_TYPE_USER, EVENT_UPDATE_CDAS, 0, SIMCONNECT_GROUP_PRIORITY_HIGHEST, SIMCONNECT_EVENT_FLAG_GROUPID_IS_PRIORITY)))
	{
		LOG_ERROR("SimConnect_RequestClientData for lvars failed!!!!");
	}
}


DWORD WINAPI WASMIF::StaticSimConnectThreadStart(void* Param) {
	WASMIF* This = (WASMIF*)Param;
	return This->SimConnectStart();
}


bool WASMIF::start() {
	char szLogBuffer[256];
	DWORD workerThreadId = NULL;
	HRESULT hr;

	if (hSimConnect) {
		LOG_ERROR("Already started!");
		return FALSE;
	}

	quit = 0;
	// Log WAPI version
	sprintf_s(szLogBuffer, sizeof(szLogBuffer), "**** Starting FSUIPC7 WASM Interface (WAPI) version %s (WASM version %s)", WAPI_VERSION, WASM_VERSION);
	LOG_INFO(szLogBuffer);

	hSimEventHandle = CreateEvent(nullptr, false, false, NULL);

	if (SUCCEEDED(hr = SimConnect_Open(&hSimConnect, "FSUIPC-WASM-IF", NULL, 0, hSimEventHandle, simConnection)))
	{
		LOG_INFO("Connected to MSFS");

		if (hThread == NULL) {
			hThread = CreateThread(
				NULL,							// default security attributes
				0,								// use default stack size  
				StaticSimConnectThreadStart,	// thread function name
				(void*)this,					// argument to thread function 
				0,								// use default creation flags 
				&workerThreadId);				// returns the thread identifier 

			if (hThread == NULL) {
				LOG_ERROR("Error creating SimConnect thread");
				SimConnectEnd();
				return FALSE;
			}
		}
		else {
			LOG_ERROR("**** SimConnect thread already running ****");
		}
		return TRUE;
	}
	else {
		sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Failed on SimConnect Open: cannot connect: %s", hr == E_INVALIDARG ? "E_INVALIDARG":"E_FAIL");
		LOG_ERROR(szLogBuffer);
		hSimConnect = NULL;
	}

	return FALSE;
}


void WASMIF::end() {
	quit = 1;
}


void WASMIF::SimConnectEnd() {
	char szLogBuffer[256];
	if (requestTimerHandle) {
		DeleteTimerQueueTimer(nullptr, requestTimerHandle, nullptr);
		requestTimerHandle = nullptr;
	}
	if (configTimerHandle) {
		DeleteTimerQueueTimer(nullptr, configTimerHandle, nullptr);
		configTimerHandle = nullptr;
	}

	// Clear Client Data Definitions
	// Drop existing CDAs
	for (int i = 0; i < MAX_NO_VALUE_CDAS; i++) {
		if (value_cda[i]) {
			if (!SUCCEEDED(SimConnect_ClearClientDataDefinition(hSimConnect, value_cda[i]->getDefinitionId())))
			{
				sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Error clearing lvar value data definition with id=%d", value_cda[i]->getId());
				LOG_ERROR(szLogBuffer);
			}
			else {
				delete value_cda[i];
				value_cda[i] = 0;
			}
		}
	}
	for (int i = 0; i < noLvarCDAs; i++)
	{
		if (!SUCCEEDED(SimConnect_ClearClientDataDefinition(hSimConnect, lvar_cdas[i]->getDefinitionId())))
		{
			sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Error clearing lvar data definition with id=%d", lvar_cdas[i]->getId());
			LOG_ERROR(szLogBuffer);
		}
		else {
			delete lvar_cdas[i];
			lvar_cdas[i] = 0;
		}
	}
	noLvarCDAs = 0;
	for (int i = 0; i < noHvarCDAs; i++)
	{
		if (!SUCCEEDED(SimConnect_ClearClientDataDefinition(hSimConnect, hvar_cdas[i]->getDefinitionId())))
		{
			sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Error clearing hvar data definition with id=%d", hvar_cdas[i]->getId());
			LOG_ERROR(szLogBuffer);
		}
		else {
			delete hvar_cdas[i];
			hvar_cdas[i] = 0;
		}
	}
	noHvarCDAs = 0;
	delete cdaIdBank;
	if (!SUCCEEDED(SimConnect_ClearClientDataDefinition(hSimConnect, 1)))
	{
		sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Error clearing config data definition");
		LOG_ERROR(szLogBuffer);
	}

	if (hSimConnect)
	{
		SimConnect_Close(hSimConnect);
		LOG_INFO("SimConnect_Close done");
	}

	hSimConnect = nullptr;
	CloseHandle(hSimEventHandle);
	hSimEventHandle = nullptr;
	currentConfigSet = { 0 };
}


void CALLBACK WASMIF::MyDispatchProc(SIMCONNECT_RECV* pData, DWORD cbData, void* pContext) {
	WASMIF* procThis = reinterpret_cast<WASMIF*>(pContext);
	procThis->DispatchProc(pData, cbData);
}


void WASMIF::DispatchProc(SIMCONNECT_RECV* pData, DWORD cbData) {
	char szLogBuffer[512];
	static int noLvarCDAsReceived = 0;

	switch (pData->dwID)
	{
	case SIMCONNECT_RECV_ID_CLIENT_DATA:
	{
		SIMCONNECT_RECV_CLIENT_DATA* pObjData = (SIMCONNECT_RECV_CLIENT_DATA*)pData;

		switch (pObjData->dwRequestID)
		{
		case EVENT_CONFIG_RECEIVED:
		{
			// Need lock to make sure lvars are not being received
			EnterCriticalSection(&configMutex);

			CONFIG_CDA* configData = (CONFIG_CDA*)&(pObjData->dwData);
			if (configTimerHandle) {
				DeleteTimerQueueTimer(nullptr, configTimerHandle, nullptr);
				configTimerHandle = nullptr;
			}
			else if (!memcmp(&currentConfigSet, configData, sizeof(CONFIG_CDA))) // We didn't request this - check it has changed
			{
				LOG_TRACE("Ignoring EVENT_CONFIG_RECEIVED event as no change");
				LeaveCriticalSection(&configMutex);
				break;
			}
			if (requestTimerHandle) {
				DeleteTimerQueueTimer(nullptr, requestTimerHandle, nullptr);
				requestTimerHandle = nullptr;
			}

			// Clear current lvar/hvar names and lvar values - these will be rebuilt when we receive the data
			EnterCriticalSection(&lvarValuesMutex);
			EnterCriticalSection(&lvarNamesMutex);
			EnterCriticalSection(&hvarNamesMutex);
			lvarNames.clear();
			hvarNames.clear();
			lvarFlaggedForCallback.clear();
			lvarValues.clear();
			LeaveCriticalSection(&hvarNamesMutex);
			LeaveCriticalSection(&lvarNamesMutex);
			LeaveCriticalSection(&lvarValuesMutex);

			// Drop existing CDAs
			for (int i = 0; i < MAX_NO_VALUE_CDAS; i++) {
				if (value_cda[i]) {
					if (!SUCCEEDED(SimConnect_ClearClientDataDefinition(hSimConnect, value_cda[i]->getDefinitionId())))
					{
						sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Error clearing lvar value data definition with id=%d", value_cda[i]->getId());
						LOG_ERROR(szLogBuffer);
					}
					cdaIdBank->returnId(value_cda[i]->getName());
					delete value_cda[i];
					value_cda[i] = 0;
				}
			}
			
			for (int i = 0; i < noLvarCDAs; i++)
			{
				if (!SUCCEEDED(SimConnect_ClearClientDataDefinition(hSimConnect, lvar_cdas[i]->getDefinitionId())))
				{
					sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Error clearing lvar data definition with id=%d", lvar_cdas[i]->getId());
					LOG_ERROR(szLogBuffer);
				}
				cdaIdBank->returnId(lvar_cdas[i]->getName());
				delete lvar_cdas[i];
				lvar_cdas[i] = 0;
			}
			noLvarCDAs = 0;

			for (int i = 0; i < noHvarCDAs; i++)
			{
				if (!SUCCEEDED(SimConnect_ClearClientDataDefinition(hSimConnect, hvar_cdas[i]->getDefinitionId())))
				{
					sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Error clearing hvar data definition with id=%d", hvar_cdas[i]->getId());
					LOG_ERROR(szLogBuffer);
				}
				cdaIdBank->returnId(hvar_cdas[i]->getName());
				delete hvar_cdas[i];
				hvar_cdas[i] = 0;
			}
			noHvarCDAs = 0;

			memcpy(&currentConfigSet, configData, sizeof(CONFIG_CDA));

			for (int i = 0; i < MAX_NO_LVAR_CDAS + MAX_NO_HVAR_CDAS + MAX_NO_VALUE_CDAS; i++)
			{
				if (!configData->CDA_Size[i]) break;
				sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Config Data %d: name=%s, size=%d, type=%d", i, configData->CDA_Names[i], configData->CDA_Size[i], configData->CDA_Type[i]);
				LOG_DEBUG(szLogBuffer);
				if (configData->CDA_Type[i] == LVARF) noLvarCDAs++;
				else if (configData->CDA_Type[i] == HVARF) noHvarCDAs++;
			}

			if (!(noLvarCDAs + noHvarCDAs)) {
				LOG_TRACE("SIMCONNECT_RECV_ID_CLIENT_DATA received: Empty EVENT_CONFIG_RECEIVED - requesting again");
				CreateTimerQueueTimer(&configTimerHandle, nullptr, &WASMIF::StaticConfigTimer, this, 0, 1000, WT_EXECUTEDEFAULT);
				LeaveCriticalSection(&configMutex);
				break;
			}

			LOG_DEBUG("SIMCONNECT_RECV_ID_CLIENT_DATA received: EVENT_CONFIG_RECEIVED");

			// Check WASM version compatibility
			if (strcmp(configData->version, WASM_VERSION) != 0) {
				sprintf_s(szLogBuffer, sizeof(szLogBuffer), "**** The installed WASM version is %s while the WAPI is expecting WASM version %s. Please update the WASM module as this may cause issues.", configData->version, WASM_VERSION);
				LOG_ERROR(szLogBuffer);
			}

			// For each config CDA, we need to set a CDA element and request
			int lvarCount = 0;
			int hvarCount = 0;
			int valuesCount = 0;
			for (int i = 0; i < noLvarCDAs + noHvarCDAs + MAX_NO_VALUE_CDAS; i++)
			{
				// Need to allocate a CDA
				pair<string, int> cdaDetails = cdaIdBank->getId(configData->CDA_Size[i], configData->CDA_Names[i]);
				ClientDataArea* cda = new ClientDataArea(cdaDetails.first.c_str(), configData->CDA_Size[i], configData->CDA_Type[i]);
				cda->setId(cdaDetails.second);
				switch (configData->CDA_Type[i]) {
					case LVARF:
						lvar_cdas[lvarCount] = cda;
						break;
					case HVARF:
						hvar_cdas[hvarCount] = cda;
						break;
					case VALUEF:
						value_cda[valuesCount] = cda;
						break;
				}
				// Now set-up the definition
				if (!SUCCEEDED(SimConnect_AddToClientDataDefinition(hSimConnect, nextDefinitionID, SIMCONNECT_CLIENTDATAOFFSET_AUTO, configData->CDA_Size[i], 0, 0)))
				{
					sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Error adding client data definition id %d (%d)", nextDefinitionID, i);
					LOG_ERROR(szLogBuffer);
				}
				else
				{
					switch (configData->CDA_Type[i])
					{
					case LVARF:
						lvar_cdas[lvarCount]->setDefinitionId(nextDefinitionID);
						break;
					case HVARF:
						hvar_cdas[hvarCount]->setDefinitionId(nextDefinitionID);
						break;
					case VALUEF:
						value_cda[valuesCount]->setDefinitionId(nextDefinitionID);
						break;
					}
					sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Client data definition added with id=%d (size=%d)", nextDefinitionID, configData->CDA_Size[i]);
					LOG_DEBUG(szLogBuffer);
				}

				// Now, add lvars to data area
				HRESULT hr;
				switch (configData->CDA_Type[i]) {
					case LVARF:
						hr = SimConnect_RequestClientData(hSimConnect, cda->getId(),
								EVENT_LVARS_RECEIVED + lvarCount++, nextDefinitionID++, SIMCONNECT_CLIENT_DATA_PERIOD_ONCE, SIMCONNECT_CLIENT_DATA_REQUEST_FLAG_DEFAULT);
						break;
					case HVARF:
						hr = SimConnect_RequestClientData(hSimConnect, cda->getId(),
								EVENT_HVARS_RECEIVED + hvarCount++, nextDefinitionID++, SIMCONNECT_CLIENT_DATA_PERIOD_ONCE, SIMCONNECT_CLIENT_DATA_REQUEST_FLAG_DEFAULT); // SIMCONNECT_CLIENT_DATA_PERIOD_ON_SET?
						break;
					case VALUEF:
						hr = SimConnect_RequestClientData(hSimConnect, cda->getId(),
								EVENT_VALUES_RECEIVED + valuesCount++, nextDefinitionID++, SIMCONNECT_CLIENT_DATA_PERIOD_ON_SET, SIMCONNECT_CLIENT_DATA_REQUEST_FLAG_CHANGED);
						break;
				}
				if (hr != S_OK) {
					sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Error requesting CDA '%s' with id=%d and definitionId=%d", configData->CDA_Names[i], cda->getId(), nextDefinitionID-1);
					LOG_ERROR(szLogBuffer);
					break;
				}
				else {
					sprintf_s(szLogBuffer, sizeof(szLogBuffer), "CDA '%s with id=%d and definitionId=%d requested", configData->CDA_Names[i], cda->getId(), nextDefinitionID - 1);
					LOG_DEBUG(szLogBuffer);
				}
			}

			// Request data on timer if set
			if (noLvarCDAs && this->getLvarUpdateFrequency()) {
				CreateTimerQueueTimer(&requestTimerHandle, nullptr, &WASMIF::StaticRequestDataTimer, this, 0, 1000 / getLvarUpdateFrequency(), WT_EXECUTEDEFAULT);
			}


			// Request config data again when set and changed
			if (!SUCCEEDED(SimConnect_RequestClientData(hSimConnect, 1, EVENT_CONFIG_RECEIVED, 1,
					SIMCONNECT_CLIENT_DATA_PERIOD_ON_SET, SIMCONNECT_CLIENT_DATA_REQUEST_FLAG_CHANGED, 0, 0, 0))) {
				LOG_ERROR("SimConnect_RequestClientData for config updates failed!");
			}
			else
				LOG_TRACE("Config data updates requested.");
			// Reset lvars received counter
			noLvarCDAsReceived = 0;
			LeaveCriticalSection(&configMutex);
			break;
		}
		case EVENT_LVARS_RECEIVED: // Allow for 21 distinct lvar CDAs
		case EVENT_LVARS_RECEIVED + 1:
		case EVENT_LVARS_RECEIVED + 2:
		case EVENT_LVARS_RECEIVED + 3:
		case EVENT_LVARS_RECEIVED + 4:
		case EVENT_LVARS_RECEIVED + 5:
		case EVENT_LVARS_RECEIVED + 6:
		case EVENT_LVARS_RECEIVED + 7:
		case EVENT_LVARS_RECEIVED + 8:
		case EVENT_LVARS_RECEIVED + 9:
		case EVENT_LVARS_RECEIVED + 10:
		case EVENT_LVARS_RECEIVED + 11:
		case EVENT_LVARS_RECEIVED + 12:
		case EVENT_LVARS_RECEIVED + 13:
		case EVENT_LVARS_RECEIVED + 14:
		case EVENT_LVARS_RECEIVED + 15:
		case EVENT_LVARS_RECEIVED + 16:
		case EVENT_LVARS_RECEIVED + 17:
		case EVENT_LVARS_RECEIVED + 18:
		case EVENT_LVARS_RECEIVED + 19:
		case EVENT_LVARS_RECEIVED + 20:
		{
			// Need lock to make sure new config data is not processed when we are adding lvars
			EnterCriticalSection(&configMutex);
			CDAName* lvars = (CDAName*)&(pObjData->dwData);
			// Find id of CDA
			int cdaId = 0;
			for (cdaId = 0; cdaId < MAX_NO_LVAR_CDAS; cdaId++)
			{
				if (lvar_cdas[cdaId] != NULL && lvar_cdas[cdaId]->getDefinitionId() == pObjData->dwDefineID) break;
			}
			sprintf_s(szLogBuffer, sizeof(szLogBuffer), "cda=%d (noLvarCDAs=%d)", cdaId, noLvarCDAs);
			LOG_TRACE(szLogBuffer);
			if (cdaId < noLvarCDAs)
			{
				noLvarCDAsReceived++;
				sprintf_s(szLogBuffer, sizeof(szLogBuffer), "EVENT_LVARS_RECEIVED:%d of %d: dwObjectID=%d, dwDefineID=%d, dwDefineCount=%d, dwentrynumber=%d, dwoutof=%d",
					noLvarCDAsReceived, noLvarCDAs, pObjData->dwObjectID, pObjData->dwDefineID, pObjData->dwDefineCount, pObjData->dwentrynumber, pObjData->dwoutof);
				LOG_DEBUG(szLogBuffer);
				for (int i = 0; i < lvar_cdas[cdaId]->getNoItems(); i++)
				{
					sprintf_s(szLogBuffer, sizeof(szLogBuffer), "LVAR Data: name='%s'", lvars[i].name);
					LOG_TRACE(szLogBuffer);
					EnterCriticalSection(&lvarValuesMutex);
					EnterCriticalSection(&lvarNamesMutex);
					lvarNames.push_back(string(lvars[i].name));
					lvarValues.push_back(0.0);
					LeaveCriticalSection(&lvarNamesMutex);
					LeaveCriticalSection(&lvarValuesMutex);
					lvarFlaggedForCallback.push_back(FALSE);
				}
				if (noLvarCDAsReceived == noLvarCDAs && cdaCbFunction != NULL) {
					// All lvar names received - call CDA update callback if registered
					cdaCbFunction();
				}
			}
			else {
				sprintf_s(szLogBuffer, sizeof(szLogBuffer), "EVENT_LVARS_RECEIVED but id not found:%d of %d: dwObjectID=%d, dwDefineID=%d, dwDefineCount=%d, dwentrynumber=%d, dwoutof=%d",
					noLvarCDAsReceived, noLvarCDAs, pObjData->dwObjectID, pObjData->dwDefineID, pObjData->dwDefineCount, pObjData->dwentrynumber, pObjData->dwoutof);
				LOG_DEBUG(szLogBuffer);
				sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Error: CDA with id=%d not found", pObjData->dwObjectID);
				LOG_ERROR(szLogBuffer);
			}
			LeaveCriticalSection(&configMutex);
			break;
		}
		case EVENT_HVARS_RECEIVED:
		case EVENT_HVARS_RECEIVED+1:
		case EVENT_HVARS_RECEIVED+2:
		case EVENT_HVARS_RECEIVED+3:
		{
			EnterCriticalSection(&configMutex);
			sprintf_s(szLogBuffer, sizeof(szLogBuffer), "EVENT_HVARS_RECEIVED: dwObjectID=%d, dwDefineID=%d, dwDefineCount=%d, dwentrynumber=%d, dwoutof=%d",
				pObjData->dwObjectID, pObjData->dwDefineID, pObjData->dwDefineCount, pObjData->dwentrynumber, pObjData->dwoutof);
			LOG_DEBUG(szLogBuffer);
			CDAName* hvars = (CDAName*)&(pObjData->dwData);
			// Find id of CDA
			int cdaId = 0;
			EnterCriticalSection(&hvarNamesMutex);
			for (cdaId = 0; cdaId < MAX_NO_HVAR_CDAS; cdaId++)
			{
				if (hvar_cdas[cdaId] != NULL && hvar_cdas[cdaId]->getDefinitionId() == pObjData->dwDefineID) break;
			}
			if (cdaId < noHvarCDAs)
			{
				for (int i = 0; i < hvar_cdas[cdaId]->getNoItems(); i++)
				{
					sprintf_s(szLogBuffer, sizeof(szLogBuffer), "HVAR Data: ID=%03d, name='%s'", i, hvars[i].name);
					LOG_TRACE(szLogBuffer);
					hvarNames.push_back(string(hvars[i].name));
				}
			}
			else {
				sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Error: CDA with id=%d not found", pObjData->dwDefineID);
				LOG_ERROR(szLogBuffer);
			}
			LeaveCriticalSection(&hvarNamesMutex);
			LeaveCriticalSection(&configMutex);
			break;
		}
		case EVENT_VALUES_RECEIVED:
		{
			// Check values match definition
			if (value_cda[0]->getDefinitionId() != pObjData->dwDefineID) break;
			sprintf_s(szLogBuffer, sizeof(szLogBuffer), "EVENT_VALUES_RECEIVED: dwObjectID=%d, dwDefineID=%d, dwDefineCount=%d, dwentrynumber=%d, dwoutof=%d",
				pObjData->dwObjectID, pObjData->dwDefineID, pObjData->dwDefineCount, pObjData->dwentrynumber, pObjData->dwoutof);
			LOG_TRACE(szLogBuffer);
			vector<int> flaggedLvarIds;
			vector<const char*> flaggedLvarNames;
			vector<double> flaggedLvarValues;
			CDAValue* values = (CDAValue*)&(pObjData->dwData);
			EnterCriticalSection(&lvarValuesMutex);
			EnterCriticalSection(&lvarNamesMutex);
			for (int i = 0; i < value_cda[0]->getNoItems() && i < lvarNames.size(); i++)
			{
				sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Lvar value: ID=%03d, value=%lf", i, values[i].value);
				LOG_TRACE(szLogBuffer);
				if (lvarValues.at(i) != values[i].value && lvarFlaggedForCallback.at(i) && (lvarCbFunctionId != NULL || lvarCbFunctionName != NULL)) {
					sprintf(szLogBuffer, "Flagging lvar for callback: id=%d", i);
					LOG_DEBUG(szLogBuffer);
					flaggedLvarIds.push_back(i);
					flaggedLvarValues.push_back(values[i].value);
					flaggedLvarNames.push_back(lvarNames.at(i).c_str());
				}
				lvarValues.at(i) = values[i].value;
			}
			LeaveCriticalSection(&lvarNamesMutex);
			LeaveCriticalSection(&lvarValuesMutex);
			if (lvarCbFunctionId != NULL && flaggedLvarIds.size()) {
				// Add a terminating element
				flaggedLvarIds.push_back(-1);
				flaggedLvarValues.push_back(-1.0);
				lvarCbFunctionId(flaggedLvarIds.data(), flaggedLvarValues.data());
			}
			if (lvarCbFunctionName != NULL && flaggedLvarIds.size()) {
				// Add a terminating element
				flaggedLvarNames.push_back(NULL);
				if (lvarCbFunctionId == NULL) {
					// Add a terminating value element
					flaggedLvarValues.push_back(-1.0);
				}
				lvarCbFunctionName(flaggedLvarNames.data(), flaggedLvarValues.data());
			}
			break;
		}
		case EVENT_VALUES_RECEIVED + 1:
		{
			// Check values match definition
			if (value_cda[1]->getDefinitionId() != pObjData->dwDefineID) break;
			EnterCriticalSection(&lvarNamesMutex);
			if (lvarNames.size() <= 1024) {
				sprintf_s(szLogBuffer, sizeof(szLogBuffer), "EVENT_VALUES_RECEIVED+1: Ignoring as we only have %llu lvars (dwObjectID=%d, dwDefineID=%d, dwDefineCount=%d, dwentrynumber=%d, dwoutof=%d)",
					lvarNames.size(), pObjData->dwObjectID, pObjData->dwDefineID, pObjData->dwDefineCount, pObjData->dwentrynumber, pObjData->dwoutof);
				LOG_DEBUG(szLogBuffer);
				break;
			}
			LeaveCriticalSection(&lvarNamesMutex);
			sprintf_s(szLogBuffer, sizeof(szLogBuffer), "EVENT_VALUES_RECEIVED+1: dwObjectID=%d, dwDefineID=%d, dwDefineCount=%d, dwentrynumber=%d, dwoutof=%d",
				pObjData->dwObjectID, pObjData->dwDefineID, pObjData->dwDefineCount, pObjData->dwentrynumber, pObjData->dwoutof);
			LOG_TRACE(szLogBuffer);
			vector<int> flaggedLvarIds;
			vector<const char*> flaggedLvarNames;
			vector<double> flaggedLvarValues;

			CDAValue* values = (CDAValue*)&(pObjData->dwData);
			EnterCriticalSection(&lvarValuesMutex);
			EnterCriticalSection(&lvarNamesMutex);
			for (int i = 0; i < value_cda[1]->getNoItems() && i+1024 < lvarNames.size(); i++)
			{
				sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Lvar value: ID=%03d, value=%lf", i+1024, values[i].value);
				LOG_TRACE(szLogBuffer);
				if (lvarValues.at(1024 + i) != values[i].value && lvarFlaggedForCallback.at(1024 + i) && (lvarCbFunctionId != NULL || lvarCbFunctionName != NULL)) {
					sprintf(szLogBuffer, "Flagging lvar for callback: id=%d", 1024+i);
					LOG_DEBUG(szLogBuffer);
					flaggedLvarIds.push_back(1024 + i);
					flaggedLvarValues.push_back(values[i].value);
					flaggedLvarNames.push_back(lvarNames.at(1024 + i).c_str());
				}
				lvarValues.at(1024 + i) = values[i].value;
			}
			LeaveCriticalSection(&lvarNamesMutex);
			LeaveCriticalSection(&lvarValuesMutex);
			if (lvarCbFunctionId != NULL && flaggedLvarIds.size()) {
				// Add a terminating element
				flaggedLvarIds.push_back(-1);
				flaggedLvarValues.push_back(-1.0);
				lvarCbFunctionId(flaggedLvarIds.data(), flaggedLvarValues.data());
			}
			else if (lvarCbFunctionName != NULL && flaggedLvarIds.size()) {
				// Add a terminating element
				flaggedLvarNames.push_back(NULL);
				if (lvarCbFunctionId == NULL) {
					flaggedLvarValues.push_back(-1.0);
				}
				lvarCbFunctionName(flaggedLvarNames.data(), flaggedLvarValues.data());

			}
			break;
		}
		case EVENT_VALUES_RECEIVED + 2:
		{
			// Check values match definition
			if (value_cda[2]->getDefinitionId() != pObjData->dwDefineID) break;
			EnterCriticalSection(&lvarNamesMutex);
			if (lvarNames.size() <= 2048) {
				sprintf_s(szLogBuffer, sizeof(szLogBuffer), "EVENT_VALUES_RECEIVED+2: Ignoring as we only have %llu lvars (dwObjectID=%d, dwDefineID=%d, dwDefineCount=%d, dwentrynumber=%d, dwoutof=%d)",
					lvarNames.size(), pObjData->dwObjectID, pObjData->dwDefineID, pObjData->dwDefineCount, pObjData->dwentrynumber, pObjData->dwoutof);
				LOG_DEBUG(szLogBuffer);
				break;
			}
			LeaveCriticalSection(&lvarNamesMutex);
			sprintf_s(szLogBuffer, sizeof(szLogBuffer), "EVENT_VALUES_RECEIVED+2: dwObjectID=%d, dwDefineID=%d, dwDefineCount=%d, dwentrynumber=%d, dwoutof=%d",
				pObjData->dwObjectID, pObjData->dwDefineID, pObjData->dwDefineCount, pObjData->dwentrynumber, pObjData->dwoutof);
			LOG_TRACE(szLogBuffer);
			vector<int> flaggedLvarIds;
			vector<const char*> flaggedLvarNames;
			vector<double> flaggedLvarValues;

			CDAValue* values = (CDAValue*)&(pObjData->dwData);
			EnterCriticalSection(&lvarValuesMutex);
			EnterCriticalSection(&lvarNamesMutex);
			for (int i = 0; i < value_cda[2]->getNoItems() && i + 2048 < lvarNames.size(); i++)
			{
				sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Lvar value: ID=%03d, value=%lf", i + 2048, values[i].value);
				LOG_TRACE(szLogBuffer);
				if (lvarValues.at(2048 + i) != values[i].value && lvarFlaggedForCallback.at(2048 + i) && (lvarCbFunctionId != NULL || lvarCbFunctionName != NULL)) {
					sprintf(szLogBuffer, "Flagging lvar for callback: id=%d", 2048 + i);
					LOG_DEBUG(szLogBuffer);
					flaggedLvarIds.push_back(2048 + i);
					flaggedLvarValues.push_back(values[i].value);
					flaggedLvarNames.push_back(lvarNames.at(2048 + i).c_str());
				}
				lvarValues.at(2048 + i) = values[i].value;
			}
			LeaveCriticalSection(&lvarNamesMutex);
			LeaveCriticalSection(&lvarValuesMutex);
			if (lvarCbFunctionId != NULL && flaggedLvarIds.size()) {
				// Add a terminating element
				flaggedLvarIds.push_back(-1);
				flaggedLvarValues.push_back(-1.0);
				lvarCbFunctionId(flaggedLvarIds.data(), flaggedLvarValues.data());
			}
			else if (lvarCbFunctionName != NULL && flaggedLvarIds.size()) {
				// Add a terminating element
				flaggedLvarNames.push_back(NULL);
				if (lvarCbFunctionId == NULL) {
					flaggedLvarValues.push_back(-1.0);
				}
				lvarCbFunctionName(flaggedLvarNames.data(), flaggedLvarValues.data());

			}
			break;
		}
		case SIMCONNECT_RECV_ID_EXCEPTION: {
			SIMCONNECT_RECV_EXCEPTION* except = (SIMCONNECT_RECV_EXCEPTION*)pData;
			sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Simconnect Exception received: %d (dwSendID=%d)", except->dwException, except->dwSendID);
			LOG_ERROR(szLogBuffer);
			break;
		}

		default:
			LOG_TRACE("SIMCONNECT_RECV_ID_CLIENT_DATA received: default");
			break;
		}
		break;
	}

/***
	case SIMCONNECT_RECV_ID_EVENT:
	{
		SIMCONNECT_RECV_EVENT* evt = (SIMCONNECT_RECV_EVENT*)pData;

		switch (evt->uEventID)
		{
		default:
			sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Unknow event received %d [%X]: %d", evt->uEventID, evt->uEventID, evt->dwData);
			LOG_TRACE(szLogBuffer);
			break;
		}

		break;
	}
***/

	case SIMCONNECT_RECV_ID_QUIT:
	{
		quit = 1;
		break;
	}

	default:
		break;
	}
}


void WASMIF::createAircraftLvarFile() {
	// Send event to create aircraft lvar file
	if (!SUCCEEDED(SimConnect_TransmitClientEvent(hSimConnect, SIMCONNECT_SIMOBJECT_TYPE_USER, EVENT_LIST_LVARS, 0, SIMCONNECT_GROUP_PRIORITY_HIGHEST, SIMCONNECT_EVENT_FLAG_GROUPID_IS_PRIORITY)))
	{
		LOG_ERROR("SimConnect_TransmitClientEvent for EVENT_LIST_LVARS failed!!!!");
	}

}


void WASMIF::reload() {
	// Send event to reload lvars (and re-create CDAs)
	if (!SUCCEEDED(SimConnect_TransmitClientEvent(hSimConnect, SIMCONNECT_SIMOBJECT_TYPE_USER, EVENT_RELOAD, 0, SIMCONNECT_GROUP_PRIORITY_HIGHEST, SIMCONNECT_EVENT_FLAG_GROUPID_IS_PRIORITY)))
	{
		LOG_ERROR("SimConnect_TransmitClientEvent for EVENT_RELOAD failed!!!!");
	}

}


void WASMIF::setLvarUpdateFrequency(int freq) {
	lvarUpdateFrequency = freq;
}


int WASMIF::getLvarUpdateFrequency() {
	return lvarUpdateFrequency;
}


double WASMIF::getLvar(int lvarID) {
	EnterCriticalSection(&lvarValuesMutex);
	if (lvarID < 0 || lvarID >= lvarValues.size())
	{
		LeaveCriticalSection(&lvarValuesMutex);
		return NULL;
	}
	double result = lvarValues.at(lvarID);
	LeaveCriticalSection(&lvarValuesMutex);

	return result;
}


double WASMIF::getLvar(const char* lvarName) {
	int lvarId;
	EnterCriticalSection(&lvarValuesMutex);
	EnterCriticalSection(&lvarNamesMutex);
	for (lvarId = 0; lvarId < lvarNames.size(); lvarId++)
		if (!strcmp(lvarName, lvarNames.at(lvarId).c_str())) break;

	double result = lvarId < lvarValues.size() ? lvarValues.at(lvarId) : 0.0;
	LeaveCriticalSection(&lvarNamesMutex);
	LeaveCriticalSection(&lvarValuesMutex);

	return result;
}

void WASMIF::getLvarValues(map<string, double >& returnMap) {

	EnterCriticalSection(&lvarValuesMutex);
	EnterCriticalSection(&lvarNamesMutex);
	for (int lvarId = 0; lvarId < lvarNames.size(); lvarId++) {
		returnMap.insert(make_pair(lvarNames.at(lvarId), lvarValues.at(lvarId)));
	}
	LeaveCriticalSection(&lvarNamesMutex);
	LeaveCriticalSection(&lvarValuesMutex);
}


void WASMIF::setLvar(unsigned short id, const char* value) {
	char szLogBuffer[512];
	char* p;
	double converted = strtod(value, &p);
	if (*p) {
		// conversion failed because the input wasn't a number
		memcpy(&converted, value, sizeof(double));
		setLvar(id, converted);
		sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Setting lvar value as string: %.*s", 8, (char*)&converted);
		LOG_DEBUG(szLogBuffer);
	}
	else {
		// use converted
		// Check if we have an integer
		int i, r, n;
		r = sscanf_s(value, "%d%n", &i, &n);
		if (r == 1 && n == strlen(value) && converted < 65536 && converted > -32769) {
			// converted is short integer
			if (converted > 0 && converted < 65536) {
				unsigned short value = (unsigned short)converted;
				setLvar(id, value);
				sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Setting lvar value as unsigned short: %u", value);
				LOG_DEBUG(szLogBuffer);
			}
			else if (converted > -32769 && converted < 32768) {
				short value = (short)converted;
				setLvar(id, value);
				sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Setting lvar value as short: %d", value);
				LOG_DEBUG(szLogBuffer);
			}
		}
		else { // use double
			setLvar(id, converted);
			sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Setting lvar value as double: %f", converted);
			LOG_DEBUG(szLogBuffer);
		}
	}
}


void WASMIF::setLvar(unsigned short id, double value) {
	char szLogBuffer[256];
	DWORD dwLastID;
	CDASETLVAR lvar;
	lvar.id = id;
	lvar.lvarValue = value;
	if (!SUCCEEDED(SimConnect_SetClientData(hSimConnect, 2, 2, 0, 0, sizeof(CDASETLVAR), &lvar))) {
		sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Error setting Client Data lvar value: %d=%f", lvar.id, lvar.lvarValue);
		LOG_ERROR(szLogBuffer);
	}
	else {
		SimConnect_GetLastSentPacketID(hSimConnect, &dwLastID);
		sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Lvar set Client Data Area updated [requestID=%d]", dwLastID);
		LOG_TRACE(szLogBuffer);
		// Now send an empty request. This is needed to clear the CDA in case the same lvar value is resent
		lvar.id = -1;
		lvar.lvarValue = 0;
		SimConnect_SetClientData(hSimConnect, 2, 2, 0, 0, sizeof(CDASETLVAR), &lvar);
	}
}


void WASMIF::setLvar(unsigned short id, short value) {
	DWORD param;
	BYTE* p = (BYTE*)&param;

	memcpy(p, &id, 2);
	memcpy(p + 2, &value, 2);
	setLvarS(param);
}

void WASMIF::setLvar(unsigned short id, unsigned short value) {

	DWORD param;
	BYTE* p = (BYTE*)&param;

	memcpy(p, &id, 2);
	memcpy(p + 2, &value, 2);
	setLvar(param);
}

void WASMIF::setLvar(const char* lvarName, double value) {
	int id = getLvarIdFromName(lvarName);

	if (id < 0) {
		char szLogBuffer[256];
		sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Error setting Client Data lvar value: %s=%f (No lvar with that name found)", lvarName, value);
		LOG_ERROR(szLogBuffer);
		return;
	}
	setLvar(id, value);
}


void WASMIF::setLvar(const char* lvarName, short value) {
	int id = getLvarIdFromName(lvarName);

	if (id < 0) {
		char szLogBuffer[256];
		sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Error setting Client Data lvar value: %s=%d (No lvar with that name found)", lvarName, value);
		LOG_ERROR(szLogBuffer);
		return;
	}
	setLvar(id, value);
}


void WASMIF::setLvar(const char* lvarName, const char* value) {
	char szLogBuffer[512];
	int id = getLvarIdFromName(lvarName);

	if (id < 0) {
		sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Error setting Client Data lvar value: %s=%s (No lvar with that name found)", lvarName, value);
		LOG_ERROR(szLogBuffer);
		return;
	}
	setLvar(id, value);
}


void WASMIF::setLvar(const char* lvarName, unsigned short value) {
	int id = getLvarIdFromName(lvarName);

	if (id < 0) {
		char szLogBuffer[256];
		sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Error setting Client Data lvar value: %s=%hu (No lvar with that name found)", lvarName, value);
		LOG_ERROR(szLogBuffer);
		return;
	}
	setLvar(id, value);
}

void WASMIF::setLvar(DWORD param) {
	char szLogBuffer[256];
	if (!SUCCEEDED(SimConnect_TransmitClientEvent(hSimConnect, SIMCONNECT_SIMOBJECT_TYPE_USER, EVENT_SET_LVAR, param, SIMCONNECT_GROUP_PRIORITY_HIGHEST, SIMCONNECT_EVENT_FLAG_GROUPID_IS_PRIORITY)))
	{
		LOG_ERROR("SimConnect_TransmitClientEvent for EVENT_SET_LVAR failed!!!!");
	}
	else {
		unsigned short value = static_cast<unsigned short>(param >> (2 * 8));
		unsigned short id = static_cast<unsigned short>(param % (1 << (2 * 8)));
		sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Control sent to set lvars with parameter %d (%X): lvarId=%u (%X), value=%u (%X)", param, param,
			id, id, value, value);
		LOG_DEBUG(szLogBuffer);
	}

}
void WASMIF::setLvarS(DWORD param) {
	char szLogBuffer[256];
	if (!SUCCEEDED(SimConnect_TransmitClientEvent(hSimConnect, SIMCONNECT_SIMOBJECT_TYPE_USER, EVENT_SET_LVARS, param, SIMCONNECT_GROUP_PRIORITY_HIGHEST, SIMCONNECT_EVENT_FLAG_GROUPID_IS_PRIORITY)))
	{
		LOG_ERROR("SimConnect_TransmitClientEvent for EVENT_SET_LVARS failed!!!!");
	}
	else {
		short value = static_cast<short>(param >> (2 * 8));
		unsigned short id = static_cast<unsigned short>(param % (1 << (2 * 8)));
		sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Control sent to set lvars with parameter %d (%X): lvarId=%u (%X), value=%d (%X)", param, param,
			id, id, value, value);
		LOG_DEBUG(szLogBuffer);
	}

}


void WASMIF::executeCalclatorCode(const char* code) {
	char szLogBuffer[MAX_CALC_CODE_SIZE + 64];
	DWORD dwLastID;
	CDACALCCODE ccode;

	// First, check size of provided code
	if (code == NULL || strlen(code) > MAX_CALC_CODE_SIZE - 1) {
		sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Error setting Client Data Calculator Code: code contains %zd characters, max allowed is %d",
			code == NULL ? (size_t)0 : strlen(code), MAX_CALC_CODE_SIZE - 1);
		LOG_ERROR(szLogBuffer);
		return;
	}
	strncpy_s(ccode.calcCode, sizeof(ccode.calcCode), code, MAX_CALC_CODE_SIZE);
	ccode.calcCode[MAX_CALC_CODE_SIZE - 1] = '\0';
	if (!SUCCEEDED(SimConnect_SetClientData(hSimConnect, 3, 3, 0, 0, sizeof(CDACALCCODE), &ccode))) {
		sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Error setting Client Data Calculator Code: '%s'", ccode.calcCode);
		LOG_ERROR(szLogBuffer);
	}
	else {
		SimConnect_GetLastSentPacketID(hSimConnect, &dwLastID);
		sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Calcultor Code Client Data Area updated [requestID=%d]", dwLastID);
		LOG_TRACE(szLogBuffer);
		// Now send an empty request. This is needed to clear the CDA in case the same calc code is resent
		strcpy(ccode.calcCode, "1");
		SimConnect_SetClientData(hSimConnect, 3, 3, 0, 0, sizeof(CDACALCCODE), &ccode);
	}
}


void WASMIF::logLvars() {
	char szLogBuffer[256];
	EnterCriticalSection(&lvarValuesMutex);
	EnterCriticalSection(&lvarNamesMutex);
	sprintf(szLogBuffer, "We have %03llu lvars: ", lvarNames.size());
	LOG_INFO(szLogBuffer);
	for (int i = 0; i < lvarNames.size(); i++) {
		sprintf_s(szLogBuffer, sizeof(szLogBuffer), "    ID=%03d %s = %f", i, lvarNames.at(i).c_str(), lvarValues.at(i));
		LOG_INFO(szLogBuffer);
	}
	LeaveCriticalSection(&lvarNamesMutex);
	LeaveCriticalSection(&lvarValuesMutex);
}


void WASMIF::getLvarList(unordered_map<int, string >& returnMap) {
	EnterCriticalSection(&lvarNamesMutex);
	for (int i = 0; i < lvarNames.size(); i++) {
		returnMap.insert(make_pair(i, lvarNames.at(i)));
	}
	LeaveCriticalSection(&lvarNamesMutex);
}


void WASMIF::setHvar(int id) {
	char szLogBuffer[256];
	if (!SUCCEEDED(SimConnect_TransmitClientEvent(hSimConnect, SIMCONNECT_SIMOBJECT_TYPE_USER, EVENT_SET_HVAR, id, SIMCONNECT_GROUP_PRIORITY_HIGHEST, SIMCONNECT_EVENT_FLAG_GROUPID_IS_PRIORITY)))
	{
		LOG_ERROR("SimConnect_TransmitClientEvent for EVENT_SET_HVAR failed!!!!");
	}
	else {
		sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Control sent to set hvar with id=%d (%X)", id, id);
		LOG_DEBUG(szLogBuffer);
	}
}

void WASMIF::setHvar(const char* hvarName) {
	char szLogBuffer[256];
	int id = getHvarIdFromName(hvarName);

	if (id < 0) {
		sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Error activating hvar '%s': No hvar with that name found", hvarName);
		LOG_ERROR(szLogBuffer);
		return;
	}

	if (!SUCCEEDED(SimConnect_TransmitClientEvent(hSimConnect, SIMCONNECT_SIMOBJECT_TYPE_USER, EVENT_SET_HVAR, id, SIMCONNECT_GROUP_PRIORITY_HIGHEST, SIMCONNECT_EVENT_FLAG_GROUPID_IS_PRIORITY)))
	{
		LOG_ERROR("SimConnect_TransmitClientEvent for EVENT_SET_HVAR failed!!!!");
	}
	else {
		sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Control sent to set hvar with id=%d (%X)", id, id);
		LOG_DEBUG(szLogBuffer);
	}
}


void WASMIF::logHvars() {
	char szLogBuffer[256];
	EnterCriticalSection(&hvarNamesMutex);
	for (int i = 0; i < hvarNames.size(); i++) {
		sprintf_s(szLogBuffer, sizeof(szLogBuffer), "ID=%03d %s", i, hvarNames.at(i).c_str());
		LOG_INFO(szLogBuffer);
	}
	LeaveCriticalSection(&hvarNamesMutex);
}


void WASMIF::getHvarList(unordered_map<int, string >& returnMap) {
	EnterCriticalSection(&hvarNamesMutex);
	for (int i = 0; i < hvarNames.size(); i++) {
		returnMap.insert(make_pair(i, hvarNames.at(i)));
	}
	LeaveCriticalSection(&hvarNamesMutex);
}

int WASMIF::getLvarIdFromName(const char* lvarName) {
	EnterCriticalSection(&lvarNamesMutex);
	for (int i = 0; i < lvarNames.size(); i++) {
		if (lvarNames.at(i) == string(lvarName))
		{
			LeaveCriticalSection(&lvarNamesMutex);
			return i;
		}
	}
	LeaveCriticalSection(&lvarNamesMutex);
	return -1;
}

void WASMIF::getLvarNameFromId(int id, char* name) {
	EnterCriticalSection(&lvarNamesMutex);
	if (id >= 0 && id < lvarNames.size())
		strcpy(name, lvarNames.at(id).c_str());
	else name = NULL;
	LeaveCriticalSection(&lvarNamesMutex);
}

int WASMIF::getHvarIdFromName(const char* hvarName) {
	EnterCriticalSection(&hvarNamesMutex);
	for (int i = 0; i < hvarNames.size(); i++) {
		if (hvarNames.at(i) == string(hvarName))
		{
			LeaveCriticalSection(&hvarNamesMutex);
			return i;
		}
	}
	LeaveCriticalSection(&hvarNamesMutex);
	return -1;
}

void WASMIF::getHvarNameFromId(int id, char* name) {
	EnterCriticalSection(&hvarNamesMutex);
	if (id >= 0 && id < hvarNames.size())
		strcpy(name, hvarNames.at(id).c_str());
	else name[0] = 0;
	LeaveCriticalSection(&hvarNamesMutex);
}

bool WASMIF::createLvar(const char* lvarName, double value) {
	if (strlen(lvarName) > MAX_VAR_NAME_SIZE)
		return FALSE;

	char ccode[MAX_CALC_CODE_SIZE];
	sprintf_s(ccode, sizeof(ccode), "::%s = %lf", lvarName, value);
	executeCalclatorCode(ccode);
	return TRUE;
}

void  WASMIF::registerUpdateCallback(void (*callbackFunction)(void)) {
	cdaCbFunction = callbackFunction;
}
void  WASMIF::registerLvarUpdateCallback(void (*callbackFunction)(int id[], double newValue[])) {
	lvarCbFunctionId = callbackFunction;
}
void  WASMIF::registerLvarUpdateCallback(void (*callbackFunction)(const char* lvarName[], double newValue[])) {
	lvarCbFunctionName = callbackFunction;
}
void  WASMIF::flagLvarForUpdateCallback(int lvarId) {
	if (lvarId < 0) { // Flag all lvars for update
		for (int i = 0; i < lvarFlaggedForCallback.size(); i++)
			lvarFlaggedForCallback.at(i) = TRUE;
	}
	else if (lvarId >= 0 && lvarId < lvarFlaggedForCallback.size())
		lvarFlaggedForCallback.at(lvarId) = TRUE;
}

void  WASMIF::flagLvarForUpdateCallback(const char* lvarName) {
	int id = getLvarIdFromName(lvarName);

	if (id < 0) {
		char szLogBuffer[256];
		sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Error flagging lvar for update callback: %s (No lvar with that name found)", lvarName);
		LOG_ERROR(szLogBuffer);
		return;
	}
	flagLvarForUpdateCallback(id);
}

bool  WASMIF::isRunning() { return hSimConnect != NULL; }
