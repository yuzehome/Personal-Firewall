// FirewallInterlayer.cpp: 定义 DLL 应用程序的导出函数。
//

#include "stdafx.h"
#include "FirewallInterlayer.h"

GUID  GUID_FILTER = { 0xd3c21122, 0x85e1, 0x48f3,{ 0x9a,0xb6,0x23,0xd9,0x0c,0x73,0x07,0xef } };
GUID  GUID_FILTER_CHAIN = { 0xfdfdfdfd,0x2121,0x5151,{ 0x8f,0xd4,0x21,0x21,0xcc,0x7b,0xd9,0xaa } };
WSAPROTOCOL_INFOW *lpAllProtoInfo;
int TotalNum;

bool GetAllFilter()
{
	DWORD size = 0;
	int Error;
	lpAllProtoInfo = NULL;
	TotalNum = 0;

	::WSCEnumProtocols(NULL, lpAllProtoInfo, &size, &Error);
	if (Error != WSAENOBUFS)
	{
		return false;
	}

	lpAllProtoInfo = new WSAPROTOCOL_INFOW[size];
	memset(lpAllProtoInfo, 0, size);
	if ((TotalNum = ::WSCEnumProtocols(NULL, lpAllProtoInfo, &size, &Error)) == SOCKET_ERROR)
	{
		return false;
	}
	return true;
}

bool FreeFilter()
{
	delete[]lpAllProtoInfo;
	return true;
}


void Install()
{
	WCHAR pwszPathName[MAX_PATH] = { 0 };
	::GetCurrentDirectory(MAX_PATH, pwszPathName);
	wcscat(pwszPathName, _T("\\FilewallConcrete.dll"));
	WCHAR wszLSPName[] = L"ZhouYukunLSP";
	WSAPROTOCOL_INFOW OriginalProtocolInfo[3];
	DWORD            dwOrigCatalogId[3];
	int nArrayCount = 0;
	int i = 0;

	DWORD dwLayeredCatalogId;       // 我们分层协议的目录ID号  

	int nError;

	// 找到我们的下层协议，将信息放入数组中  
	// 枚举所有服务程序提供者  
	GetAllFilter();
	BOOL bFindUdp = FALSE;
	BOOL bFindTcp = FALSE;
	BOOL bFindRaw = FALSE;
	for (i = 0; i<TotalNum; i++)
	{
		if (lpAllProtoInfo[i].iAddressFamily == AF_INET)
		{
			if (!bFindUdp && lpAllProtoInfo[i].iProtocol == IPPROTO_UDP)
			{
				memcpy(&OriginalProtocolInfo[nArrayCount], &lpAllProtoInfo[i], sizeof(WSAPROTOCOL_INFOW));
				OriginalProtocolInfo[nArrayCount].dwServiceFlags1 =
					OriginalProtocolInfo[nArrayCount].dwServiceFlags1 & (~XP1_IFS_HANDLES);

				dwOrigCatalogId[nArrayCount++] = lpAllProtoInfo[i].dwCatalogEntryId;

				bFindUdp = TRUE;
			}

			if (!bFindTcp && lpAllProtoInfo[i].iProtocol == IPPROTO_TCP)
			{
				memcpy(&OriginalProtocolInfo[nArrayCount], &lpAllProtoInfo[i], sizeof(WSAPROTOCOL_INFOW));
				OriginalProtocolInfo[nArrayCount].dwServiceFlags1 =
					OriginalProtocolInfo[nArrayCount].dwServiceFlags1 & (~XP1_IFS_HANDLES);

				dwOrigCatalogId[nArrayCount++] = lpAllProtoInfo[i].dwCatalogEntryId;

				bFindTcp = TRUE;
			}
			if (!bFindRaw && lpAllProtoInfo[i].iProtocol == IPPROTO_IP)
			{
				memcpy(&OriginalProtocolInfo[nArrayCount], &lpAllProtoInfo[i], sizeof(WSAPROTOCOL_INFOW));
				OriginalProtocolInfo[nArrayCount].dwServiceFlags1 =
					OriginalProtocolInfo[nArrayCount].dwServiceFlags1 & (~XP1_IFS_HANDLES);

				dwOrigCatalogId[nArrayCount++] = lpAllProtoInfo[i].dwCatalogEntryId;

				bFindRaw = TRUE;
			}
		}
	}

	// 安装我们的分层协议，获取一个dwLayeredCatalogId  
	// 随便找一个下层协议的结构复制过来即可  
	WSAPROTOCOL_INFOW LayeredProtocolInfo;
	memcpy(&LayeredProtocolInfo, &OriginalProtocolInfo[0], sizeof(WSAPROTOCOL_INFOW));
	// 修改协议名称，类型，设置PFL_HIDDEN标志  
	wcscpy(LayeredProtocolInfo.szProtocol, wszLSPName);
	LayeredProtocolInfo.ProtocolChain.ChainLen = LAYERED_PROTOCOL; // 0;  
	LayeredProtocolInfo.dwProviderFlags |= PFL_HIDDEN;
	// 安装  
	if (::WSCInstallProvider(&GUID_FILTER,
		pwszPathName, &LayeredProtocolInfo, 1, &nError) == SOCKET_ERROR)
	{
		OutputDebugString(_T("Install Provider Failed!"));
		return;
	}
	// 重新枚举协议，获取分层协议的目录ID号  
	FreeFilter();
	GetAllFilter();
	for (i = 0; i<TotalNum; i++)
	{
		if (memcmp(&lpAllProtoInfo[i].ProviderId, &GUID_FILTER, sizeof(GUID_FILTER)) == 0)
		{
			dwLayeredCatalogId = lpAllProtoInfo[i].dwCatalogEntryId;
			break;
		}
	}

	// 安装协议链  
	// 修改协议名称，类型  
	WCHAR wszChainName[WSAPROTOCOL_LEN + 1];
	for (i = 0; i<nArrayCount; i++)
	{
		swprintf(wszChainName, L"%ws over %ws", wszLSPName, OriginalProtocolInfo[i].szProtocol);
		wcscpy(OriginalProtocolInfo[i].szProtocol, wszChainName);
		if (OriginalProtocolInfo[i].ProtocolChain.ChainLen == 1)
		{
			OriginalProtocolInfo[i].ProtocolChain.ChainEntries[1] = dwOrigCatalogId[i];
		}
		else
		{
			for (int j = OriginalProtocolInfo[i].ProtocolChain.ChainLen; j>0; j--)
			{
				OriginalProtocolInfo[i].ProtocolChain.ChainEntries[j]
					= OriginalProtocolInfo[i].ProtocolChain.ChainEntries[j - 1];
			}
		}
		OriginalProtocolInfo[i].ProtocolChain.ChainLen++;
		OriginalProtocolInfo[i].ProtocolChain.ChainEntries[0] = dwLayeredCatalogId;
	}
	if (::WSCInstallProvider(&GUID_FILTER_CHAIN,
		pwszPathName, OriginalProtocolInfo, nArrayCount, &nError) == SOCKET_ERROR)
	{
		OutputDebugString(_T("Install Provider Chain Failed!"));
		return;
	}

	// 重新排序Winsock目录，将我们的协议链提前  
	// 重新枚举安装的协议  
	FreeFilter();
	GetAllFilter();

	DWORD dwIds[20];
	int nIndex = 0;
	// 添加我们的协议链  
	for (i = 0; i<TotalNum; i++)
	{
		if ((lpAllProtoInfo[i].ProtocolChain.ChainLen > 1) &&
			(lpAllProtoInfo[i].ProtocolChain.ChainEntries[0] == dwLayeredCatalogId))
			dwIds[nIndex++] = lpAllProtoInfo[i].dwCatalogEntryId;
	}
	// 添加其它协议  
	for (i = 0; i<TotalNum; i++)
	{
		if ((lpAllProtoInfo[i].ProtocolChain.ChainLen <= 1) ||
			(lpAllProtoInfo[i].ProtocolChain.ChainEntries[0] != dwLayeredCatalogId))
			dwIds[nIndex++] = lpAllProtoInfo[i].dwCatalogEntryId;
	}
	// 重新排序Winsock目录  
	if ((nError = ::WSCWriteProviderOrder(dwIds, nIndex)) != ERROR_SUCCESS)
	{
		OutputDebugString(_T("Write Provider Order Failed!"));
		return;
	}
	FreeFilter();
	OutputDebugString(_T("Install Successfully!"));
}

void Remove()
{
	int err = 0;
	if (::WSCDeinstallProvider(&GUID_FILTER_CHAIN, &err) == SOCKET_ERROR)
	{
		OutputDebugString(_T("Remove Chain failed!"));
		return;
	}
	if (::WSCDeinstallProvider(&GUID_FILTER, &err) == SOCKET_ERROR)
	{
		OutputDebugString(_T("Remove Provider failed!"));
		return;
	}
	OutputDebugString(_T("Remove successful!"));
	return;
}