#pragma once
/*
 * Main WASM Interface. This is the file that is shared
 * between the WASM module and the Client.
 */
#define WASM_VERSION			"0.9.1"
#define MAX_VAR_NAME_SIZE		56 // Max size of a CDA is 8k. So Max no lvars per CDK is 8192/(this valuw) = 146
#define MAX_CDA_NAME_SIZE		64 
#define MAX_NO_VALUE_CDAS		3 // Allows for 3*1024 lvars 
#define MAX_NO_LVAR_CDAS		21 // 21 is max and allows for 3066 lvars - the max allowed for the 3 value areas (8k/8) is 3072
#define MAX_NO_HVAR_CDAS		4 // We can have more of these if needed
#define CONFIG_CDA_NAME			"FSUIPC_config"
#define LVARVALUE_CDA_NAME		"FSUIPC_SetLvar"
#define CCODE_CDA_NAME			"FSUIPC_CalcCode"
#define MAX_CALC_CODE_SIZE		1024 // Up to 8k

// Custom Event Names
#define RELOAD_EVENT "FSUIPC.Reload"
#define SET_LVAR_EVENT "FSUIPC.SetLvar"
#define ACTIVATE_HVAR_EVENT "FSUIPC.SetHvar"
#define UPDATE_CONFIG_EVENT "FSUIPC.UpdateCDAs"
#define LIST_LVARS_EVENT "FSUIPC.ListLvars"
#define SET_LVAR_SIGNED_EVENT "FSUIPC.SetLvarS"

#pragma pack(push, 1)
namespace WASM {
	typedef struct _CDASETLVAR
	{
		int id;
		double lvarValue;
	} CDASETLVAR;

	typedef struct _CDACALCCODE
	{
		char calcCode[MAX_CALC_CODE_SIZE];
	} CDACALCCODE;

	typedef struct _CDAName
	{
		char name[MAX_VAR_NAME_SIZE];
	} CDAName;

	typedef struct _CDAValue
	{
		double value;
	} CDAValue;

	typedef enum {
		LVARF, HVARF, VALUEF
	} CDAType;

	typedef struct _CONFIG_CDA
	{
		char version[8];
		char CDA_Names[MAX_NO_LVAR_CDAS + MAX_NO_HVAR_CDAS + MAX_NO_VALUE_CDAS][MAX_CDA_NAME_SIZE];
		int CDA_Size[MAX_NO_LVAR_CDAS + MAX_NO_HVAR_CDAS + MAX_NO_VALUE_CDAS];
		CDAType CDA_Type[MAX_NO_LVAR_CDAS + MAX_NO_HVAR_CDAS + MAX_NO_VALUE_CDAS];
	} CONFIG_CDA;
}
#pragma pack(pop)
