#pragma once
#include "windows.h"

#define WM_APP 0x8000
typedef unsigned int UINT;
typedef struct _NOTIFYICONDATA {
  DWORD cbSize;
  HANDLE hWnd;
  UINT uID;
  UINT uFlags;
  UINT uCallbackMessage;
  HANDLE hIcon;
  char szTip[128];
  DWORD dwState;
  DWORD dwStateMask;
  char szInfo[256];
  union {
    UINT uTimeout;
    UINT uVersion;
  };
  char szInfoTitle[64];
  DWORD dwInfoFlags;
  //guidItem
  //hBalloonIcon
} NOTIFYICONDATA, *PNOTIFYICONDATA;
