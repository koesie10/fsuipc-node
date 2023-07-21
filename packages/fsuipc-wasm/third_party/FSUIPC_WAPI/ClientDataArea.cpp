#include "ClientDataArea.h"
#include <stdio.h>
#include <string.h>
#include <istream>
#include "WASM.h"

using namespace std;
using namespace ClientDataAreaMSFS;


ClientDataArea::ClientDataArea(const string& cdaName, int size, CDAType type)
{
	switch (type) {
		case LVARF:
			noItems = size / sizeof(CDAName);
			break;
		case HVARF:
			noItems = size / sizeof(CDAName);
			break;
		case VALUEF:
			noItems = size / sizeof(CDAValue);
			break;
	}

	this->id = 0;
	this->name = string(cdaName);
	this->noItems = noItems;
	this->size = size;
	this->type = type;
}

ClientDataArea::ClientDataArea()
{
	this->id = 0;
	this->size = 0;
	this->noItems = 0;
};



int ClientDataArea::getNoItems()
{
	return noItems;
}

string ClientDataArea::getName()
{
	return name;
}

int ClientDataArea::getId()
{
	return id;
}

void ClientDataArea::setId(int id)
{
	this->id = id;
}

int ClientDataArea::getDefinitionId()
{
	return this->definitionId;
}

int ClientDataArea::getSize()
{
	return size;
}

CDAType ClientDataArea::getType()
{
	return type;
}

void ClientDataArea::setDefinitionId(int id)
{
	definitionId = id;
}
