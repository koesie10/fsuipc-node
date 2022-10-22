#pragma once

#include <windows.h>
#include <string>
#include <map>
#include <utility>

#define CDA_NAME_TEMPLATE	"FSUIPC_VNAME"

using namespace std;

namespace CDAIdBankMSFS {
  class CDAIdBank {
	public:
		CDAIdBank(int id, HANDLE hSimConnect);
		~CDAIdBank();
		pair<string, int> getId(int size, string);
		void returnId(string name);

	protected:

	private:
		int nextId;
		multimap<string, pair<int, int>> availableBank; // keyed on name
		map<string, pair<int, int>> outBank; // keyed on name
		HANDLE  hSimConnect;
  };
} // End of namespace
