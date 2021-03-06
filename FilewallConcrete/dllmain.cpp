// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "stdafx.h"
#include "FirewallConcrete.h"

BOOL APIENTRY DllMain(HANDLE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
		//获取调用wskfilter.dll的进程名称；
	case DLL_PROCESS_ATTACH:
	{
		::GetModuleFileName(NULL, g_szCurrentApp, MAX_PATH);
		GetModuleFileName(NULL, m_sProcessName, MAX_PATH);
		OutputDebugString(_T("DLL_PROCESS_ATTACH"));
		OutputDebugString(g_szCurrentApp);
		OutputDebugString(m_sProcessName);
		break;
	}

	case  DLL_PROCESS_DETACH:
	{
		::GetModuleFileName(NULL, g_szCurrentApp, MAX_PATH);
		OutputDebugString(_T("DLL_PROCESS_DETACH"));
		break;
	}
	}

	return TRUE;
}