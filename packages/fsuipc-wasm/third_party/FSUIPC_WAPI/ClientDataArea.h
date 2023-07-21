#pragma once

#include <windows.h>
#include <string>
#include "WASM.h"

using namespace std;
using namespace WASM;

namespace ClientDataAreaMSFS
{
	class ClientDataArea
	{
	public:
		int getNoItems();
		void setDefinitionId(int id);
		int getDefinitionId();
		ClientDataArea(const string& cdaName, int size, CDAType type);
		ClientDataArea();
		string getName();
		int getSize();
		CDAType getType();
		int getId();
		void setId(int id);	

	protected:

	private:
		string name;
		int id;
		int size;
		int noItems;
		int definitionId;
		CDAType type;
	};
} // End of namespace
