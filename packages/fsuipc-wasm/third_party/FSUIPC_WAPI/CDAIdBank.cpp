#include "CDAIdBank.h"
#include "SimConnect.h"
#include "Logger.h"

using namespace std;
using namespace CDAIdBankMSFS;
using namespace CPlusPlusLogging;

CDAIdBank::CDAIdBank(int id, HANDLE hSimConnect) {
	nextId = id;
	this->hSimConnect = hSimConnect;
}

CDAIdBank::~CDAIdBank() {

}


pair<string, int> CDAIdBank::getId(int size, string name) {
	char szLogBuffer[512];
	pair<string, int> returnVal;
	std::multimap<string, pair<int, int>>::iterator it;
	DWORD dwLastID;

	// First, check if we have one available
	it = availableBank.find(name);
	if (it != availableBank.end()) {
		// Check size matches
		if (size == it->second.first) {
			returnVal = make_pair(it->first, it->second.second);
			availableBank.erase(it);
			pair<string, pair<int, int>> out = make_pair(returnVal.first, make_pair(returnVal.second, size));
			outBank.insert(out);
			return returnVal;
		}
		else { // Lets remove
			availableBank.erase(it);
		}
	}

		// Create new CDA
		string newName = string(name);
		returnVal = make_pair(newName, nextId);
		pair<string, pair<int, int>> out = make_pair(newName, make_pair(nextId, size));
		outBank.insert(out);

		// Set-up CDA
		if (!SUCCEEDED(SimConnect_MapClientDataNameToID(hSimConnect, newName.c_str(), nextId))) {
			sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Error mapping CDA name %s to ID %d", newName.c_str(), nextId);
			LOG_ERROR(szLogBuffer);
		}
		else {
			SimConnect_GetLastSentPacketID(hSimConnect, &dwLastID);
			sprintf_s(szLogBuffer, sizeof(szLogBuffer), "CDA name %s mapped to ID %d [requestId=%lu]", newName.c_str(), nextId, dwLastID);
			LOG_DEBUG(szLogBuffer);
		}

		// Finally, Create the client data if it doesn't already exist
		if (!SUCCEEDED(SimConnect_CreateClientData(hSimConnect, nextId, size, SIMCONNECT_CREATE_CLIENT_DATA_FLAG_READ_ONLY))) {
			sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Error creating client data area with id=%d and size=%d", nextId, size);
			LOG_ERROR(szLogBuffer);
		}
		else {
			SimConnect_GetLastSentPacketID(hSimConnect, &dwLastID);
			sprintf_s(szLogBuffer, sizeof(szLogBuffer), "Client data area created with id=%d (size=%d) [requestID=%lu]", nextId, size, dwLastID);
			LOG_DEBUG(szLogBuffer);
		}
		nextId++;


	return returnVal;

}


void CDAIdBank::returnId(string name) {
	std::map<string, pair<int, int>>::iterator it;
	// First, check if we have one available
	it = outBank.find(name);
	if (it != outBank.end()) {
		pair<int, int> p = outBank.at(name);
		pair<string, pair<int, int>> returnedItem = make_pair(name, p);
		outBank.erase(it);
		availableBank.insert(returnedItem);
	}
}
