// FilewallConcrete.cpp: 定义 DLL 应用程序的导出函数。
//

#include "stdafx.h"
#include "FirewallConcrete.h"

#pragma comment(lib, "Ws2_32.lib")

WSPUPCALLTABLE g_UpCallTable;

WSPPROC_TABLE g_NextProcTable;
WCHAR g_szCurrentApp[MAX_PATH];

WCHAR	m_sProcessName[MAX_PATH];

FilterRules gFilterRules;

//枚举各协议的服务提供者

LPWSAPROTOCOL_INFOW GetProvider(LPINT lpnTotalProtocols)
{
	DWORD dwSize = 0;
	int nError;
	LPWSAPROTOCOL_INFOW pProtoInfo = NULL;

	// 取得需要的长度,即通过将WSCEnumProtocols函数的dwSize参数置0进行第一次调用，后
	//以获得枚举服务提供者所需的缓冲区大小，置于dwSize变量中；
	if (::WSCEnumProtocols(NULL, pProtoInfo, &dwSize, &nError) == SOCKET_ERROR)
	{
		if (nError != WSAENOBUFS)
			return NULL;
	}

	//根据dwSize中的值来申请内存空间；
	pProtoInfo = (LPWSAPROTOCOL_INFOW)::GlobalAlloc(GPTR, dwSize);

	//第二次通过WSCEnumProtocols()正式枚举到各服务提供者并存放于pProtoInfo(数组)中，
	//并将服务提供者的个数存到lpnTotalProtocols中；
	*lpnTotalProtocols = ::WSCEnumProtocols(NULL, pProtoInfo, &dwSize, &nError);


	return pProtoInfo;
}

void FreeProvider(LPWSAPROTOCOL_INFOW pProtoInfo)
{
	//释放用于存放服务提供者数组的内存空间，pProtoInfo；
	::GlobalFree(pProtoInfo);
}

int WSPAPI WSPStartup(
	WORD wVersionRequested,
	LPWSPDATA lpWSPData,
	LPWSAPROTOCOL_INFO lpProtocolInfo,
	WSPUPCALLTABLE UpcallTable,
	LPWSPPROC_TABLE lpProcTable
)
{
	OutputDebugString(L" WSPStartup.");

	gFilterRules.ReadFromFile();

	//判断WSPStartup函数是否被调用LSP（分层服务者）调用的；
	if (lpProtocolInfo->ProtocolChain.ChainLen <= 1)
	{
		return WSAEPROVIDERFAILEDINIT;
	}

	g_UpCallTable = UpcallTable;

	//枚举各协议服务提供者，找到LSP下层协议服务提供者的WSAPROTOCOL_INFOW结构
	WSAPROTOCOL_INFOW NextProtocolInfo;
	int nTotalProtos;


	LPWSAPROTOCOL_INFOW pProtoInfo = GetProvider(&nTotalProtos);


	int i = 0;
	//获得自己所在的层
	GUID				GUID_PROVIDER = { 0xd3c21122, 0x85e1, 0x48f3,{ 0x9a,0xb6,0x23,0xd9,0x0c,0x73,0x07,0xef } };
	DWORD			layerid = 0;
	DWORD			nextlayerid = 0;

	for (i = 0; i<nTotalProtos; i++)
	{
		if (memcmp(&pProtoInfo[i].ProviderId, &GUID_PROVIDER, sizeof(GUID)) == 0)
		{
			layerid = pProtoInfo[i].dwCatalogEntryId;
			break;
		}
	}
	for (i = 0; i<lpProtocolInfo->ProtocolChain.ChainLen; i++)
	{
		if (lpProtocolInfo->ProtocolChain.ChainEntries[i] == layerid)
		{
			nextlayerid = lpProtocolInfo->ProtocolChain.ChainEntries[i + 1];
			break;
		}
	}
	for (i = 0; i<nTotalProtos; i++)
	{
		if (pProtoInfo[i].dwCatalogEntryId == nextlayerid)
		{
			memcpy(&NextProtocolInfo, &pProtoInfo[i], sizeof(NextProtocolInfo));
			break;
		}
	}

	/*
	DWORD dwBaseEntryId=lpProtocolInfo->ProtocolChain.ChainEntries[1];
	for(i=0;i<nTotalProtos;i++)
	{
	if(pProtoInfo[i].dwCatalogEntryId==dwBaseEntryId)
	{
	memcpy(&NextProtocolInfo,&pProtoInfo,sizeof(NextProtocolInfo));
	break;
	}
	}
	*/



	if (i >= nTotalProtos)
	{
		OutputDebugString(L"WSPStartup:  Can not find underlying protocol!\n");
		return WSAEPROVIDERFAILEDINIT;
	}


	//通过以上遍历得到的下层服务提供者(现存放于NextProtocolInfo中)的GUID来确定其DLL路径；
	int nError;
	TCHAR szBaseProviderDll[MAX_PATH];
	int nLen = MAX_PATH;

	//故需要通过ExpandEnvironmentStrings()来扩展成绝对路径；
	if (::WSCGetProviderPath(&NextProtocolInfo.ProviderId, szBaseProviderDll, &nLen, &nError) == SOCKET_ERROR)
	{
		OutputDebugString(L"WSPStartup:WSCGetProviderPath() failed.");
		return WSAEPROVIDERFAILEDINIT;
	}


	if (!::ExpandEnvironmentStrings(szBaseProviderDll, szBaseProviderDll, MAX_PATH))
	{
		OutputDebugString(L"WSPStartup:  ExpandEnvironmentStrings() failed.");
		return WSAEPROVIDERFAILEDINIT;
	}


	//通过上面已获取到的DLL绝对路径,来加载下层服务提供者，即加载其下载服务提供者的DLL；
	HMODULE hModule = ::LoadLibrary(szBaseProviderDll);
	if (hModule == NULL)
	{
		OutputDebugString(L"WSPStartup:  LoadLibrary() failed.");
		return WSAEPROVIDERFAILEDINIT;
	}


	//通过自定义的一个指向WSPSTARTUP函数指针，来启动下一层服务提供者；
	LPWSPSTARTUP pfnWSPStartup = NULL;

	pfnWSPStartup = (LPWSPSTARTUP)::GetProcAddress(hModule, "WSPStartup");
	if (pfnWSPStartup == NULL)
	{
		OutputDebugString(L"WSPStartup:  GetProcAddress() failed.\n");
		return WSAEPROVIDERFAILEDINIT;
	}


	//调用下层服务提供者的WSPStartup函数;

	LPWSAPROTOCOL_INFOW pInfo = lpProtocolInfo;
	if (NextProtocolInfo.ProtocolChain.ChainLen == BASE_PROTOCOL)
	{
		pInfo = &NextProtocolInfo;
	}

	//通过自定义的pfnWSPStartup()来调用下层服务提供者的WSPStartup函数，调用成功后其中
	//lpProcTable变量将在后期进行Hook时经常用到；
	int nRet = pfnWSPStartup(wVersionRequested, lpWSPData, pInfo, UpcallTable, lpProcTable);
	if (nRet != ERROR_SUCCESS)
	{
		OutputDebugString(L"WSPStartup:  underlying provider`s WSPStartup() failed.\n");
		return nRet;
	}

	//保存下层服务提供者的函数列表;
	g_NextProcTable = *lpProcTable;

	//通过修改传递给下层服务提供者的函数列表，Hook相关WSP函数；
	lpProcTable->lpWSPRecv				= MyWSPRecv;
	lpProcTable->lpWSPSend				= MyWSPSend;
	lpProcTable->lpWSPRecvFrom		= MyWSPRecvFrom;
	lpProcTable->lpWSPSendTo			= MyWSPSendTo;

	FreeProvider(pProtoInfo);

	return nRet;

}


void XfShutdown(SOCKET s)
{
	int		iError;
	if (g_NextProcTable.lpWSPShutdown(s, SD_BOTH, &iError) != 0)
		::WSASetLastError(iError);
}


int WSPAPI MyWSPRecv(
	SOCKET			s,
	LPWSABUF		lpBuffers,
	DWORD			dwBufferCount,
	LPDWORD			lpNumberOfBytesRecvd,
	LPDWORD			lpFlags,
	LPWSAOVERLAPPED	lpOverlapped,
	LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine,
	LPWSATHREADID	lpThreadId,
	LPINT			lpErrno
)
{
	OutputDebugString(_T("MyWSPRecv."));

	sockaddr addr;
	int len = sizeof(addr);

	getpeername(s, &addr, &len);

	sockaddr_in addr_in;
	memcpy(&addr_in, &addr, sizeof(addr));

	char* recvFromIP = inet_ntoa(addr_in.sin_addr);
	USHORT recvPortTarget = ntohs(addr_in.sin_port);

	OutputDebugStringA(recvFromIP);

	char portStr[8];
	sprintf(portStr, "%u", recvPortTarget);
	OutputDebugStringA(portStr);

	sockaddr addrLocal;

	getsockname(s, &addrLocal, &len);
	sockaddr_in addr_in_local;
	memcpy(&addr_in_local, &addrLocal, sizeof(addrLocal));

	USHORT recvPortSelf = ntohs(addr_in_local.sin_port);
	sprintf(portStr, "%u", recvPortSelf);
	OutputDebugStringA(portStr);

	if (gFilterRules.FitFilterRules(recvFromIP, recvPortTarget, recvPortSelf, "TCP", "接收")) {
		int nError = 0;
		OutputDebugString(_T("Filtered!")); 
		g_NextProcTable.lpWSPShutdown(s, SD_BOTH, &nError);
		//设置错误信息  
		*lpErrno = WSAECONNABORTED;
		return SOCKET_ERROR;
	}

	OutputDebugString(_T("Not Filtered!"));

	int	iRet = g_NextProcTable.lpWSPRecv(s, lpBuffers, dwBufferCount, lpNumberOfBytesRecvd, lpFlags, lpOverlapped
		, lpCompletionRoutine, lpThreadId, lpErrno);

	OutputDebugString(_T("MyWSPRecv End."));
	return iRet;
}

int WSPAPI MyWSPSend(
	_In_ SOCKET s,
	_In_reads_(dwBufferCount) LPWSABUF lpBuffers,
	_In_ DWORD dwBufferCount,
	_Out_opt_ LPDWORD lpNumberOfBytesSent,
	_In_ DWORD dwFlags,
	_Inout_opt_ LPWSAOVERLAPPED lpOverlapped,
	_In_opt_ LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine,
	_In_opt_ LPWSATHREADID lpThreadId,
	_Out_ LPINT lpErrno
)
{
	OutputDebugString(_T("MyWSPSend."));

	sockaddr addr;
	int len = sizeof(addr);

	getpeername(s, &addr, &len);

	sockaddr_in addr_in;
	memcpy(&addr_in, &addr, sizeof(addr));

	char* sendToIP = inet_ntoa(addr_in.sin_addr);
	USHORT sendPort = ntohs(addr_in.sin_port);

	OutputDebugStringA(sendToIP);

	char portStr[8];
	sprintf(portStr, "%u", sendPort);
	OutputDebugStringA(portStr); 
	
	sockaddr addrLocal;

	getsockname(s, &addrLocal, &len);
	sockaddr_in addr_in_local;
	memcpy(&addr_in_local, &addrLocal, sizeof(addrLocal));

	USHORT sendPortSelf = ntohs(addr_in_local.sin_port);
	sprintf(portStr, "%u", sendPortSelf);
	OutputDebugStringA(portStr);

	if (gFilterRules.FitFilterRules(sendToIP, sendPort, sendPortSelf, "TCP", "发送")) {
		OutputDebugString(_T("Filtered!"));
		int nError = 0;
		g_NextProcTable.lpWSPShutdown(s, SD_BOTH, &nError);
		//设置错误信息  
		*lpErrno = WSAECONNABORTED;
		return SOCKET_ERROR;
	}

	OutputDebugString(_T("Not Filtered!"));

	int	iRet = g_NextProcTable.lpWSPSend(s, lpBuffers, dwBufferCount, lpNumberOfBytesSent, dwFlags, lpOverlapped
		, lpCompletionRoutine, lpThreadId, lpErrno);

	OutputDebugString(_T("MyWSPSend End."));
	return iRet;
}

int WSPAPI MyWSPRecvFrom(
	_In_ SOCKET s,
	_In_reads_(dwBufferCount) LPWSABUF lpBuffers,
	_In_ DWORD dwBufferCount,
	_Out_opt_ LPDWORD lpNumberOfBytesRecvd,
	_Inout_ LPDWORD lpFlags,
	_Out_writes_bytes_to_opt_(*lpFromlen, *lpFromlen) struct sockaddr FAR * lpFrom,
	_Inout_opt_ LPINT lpFromlen,
	_Inout_opt_ LPWSAOVERLAPPED lpOverlapped,
	_In_opt_ LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine,
	_In_opt_ LPWSATHREADID lpThreadId,
	_Out_ LPINT lpErrno
)
{
	OutputDebugString(_T("MyWSPRecvFrom."));

	int	iRet = g_NextProcTable.lpWSPRecvFrom(s, lpBuffers, dwBufferCount, lpNumberOfBytesRecvd, lpFlags, lpFrom, lpFromlen, lpOverlapped
		, lpCompletionRoutine, lpThreadId, lpErrno);

	sockaddr_in addr_in;
	memcpy(&addr_in, lpFrom, sizeof(sockaddr));

	char* recvFromIP = inet_ntoa(addr_in.sin_addr);
	USHORT recvPortTarget = ntohs(addr_in.sin_port);

	OutputDebugStringA(recvFromIP);

	char portStr[8];
	sprintf(portStr, "%u", recvPortTarget);
	OutputDebugStringA(portStr);

	sockaddr addrLocal;
	int len = sizeof(sockaddr);

	getsockname(s, &addrLocal, &len);
	sockaddr_in addr_in_local;
	memcpy(&addr_in_local, &addrLocal, sizeof(addrLocal));

	USHORT recvPortSelf = ntohs(addr_in_local.sin_port);
	sprintf(portStr, "%u", recvPortSelf);
	OutputDebugStringA(portStr);

	if (gFilterRules.FitFilterRules(recvFromIP, recvPortTarget, recvPortSelf, "UDP", "Recv")) {
		OutputDebugString(_T("Filtered!"));
		int nError = 0;
		g_NextProcTable.lpWSPShutdown(s, SD_BOTH, &nError);
		//设置错误信息  
		*lpErrno = WSAECONNABORTED;
		return SOCKET_ERROR;
	}

	OutputDebugString(_T("Not Filtered!"));

	OutputDebugString(_T("MyWSPRecvFrom End."));
	return iRet;
}

int WSPAPI MyWSPSendTo(
	_In_ SOCKET s,
	_In_reads_(dwBufferCount) LPWSABUF lpBuffers,
	_In_ DWORD dwBufferCount,
	_Out_opt_ LPDWORD lpNumberOfBytesSent,
	_In_ DWORD dwFlags,
	_In_reads_bytes_opt_(iTolen) const struct sockaddr FAR * lpTo,
	_In_ int iTolen,
	_Inout_opt_ LPWSAOVERLAPPED lpOverlapped,
	_In_opt_ LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine,
	_In_opt_ LPWSATHREADID lpThreadId,
	_Out_ LPINT lpErrno
)
{
	OutputDebugString(_T("MyWSPSendTo."));

	sockaddr_in addr_in;
	memcpy(&addr_in, lpTo, sizeof(sockaddr));

	char* sendToIP = inet_ntoa(addr_in.sin_addr);
	USHORT sendPort = ntohs(addr_in.sin_port);

	OutputDebugStringA(sendToIP);

	char portStr[8];
	sprintf(portStr, "%u", sendPort);
	OutputDebugStringA(portStr);
	sockaddr addrLocal;
	int len = sizeof(addrLocal);

	getsockname(s, &addrLocal, &len);
	sockaddr_in addr_in_local;
	memcpy(&addr_in_local, &addrLocal, sizeof(addrLocal));

	USHORT sendPortSelf = ntohs(addr_in_local.sin_port);
	sprintf(portStr, "%u", sendPortSelf);
	OutputDebugStringA(portStr);

	if (gFilterRules.FitFilterRules(sendToIP, sendPort, sendPortSelf, "UDP", "Send")) {
		OutputDebugString(_T("Filtered!"));
		int nError = 0;
		g_NextProcTable.lpWSPShutdown(s, SD_BOTH, &nError);
		//设置错误信息  
		*lpErrno = WSAECONNABORTED;
		return SOCKET_ERROR;
	}

	int	iRet = g_NextProcTable.lpWSPSendTo(s, lpBuffers, dwBufferCount, lpNumberOfBytesSent, dwFlags, lpTo, iTolen, lpOverlapped
		, lpCompletionRoutine, lpThreadId, lpErrno);

	OutputDebugString(_T("Not Filtered!"));

	OutputDebugString(_T("MyWSPSendTo End."));
	return iRet;
}