// TrayIcon.h
#pragma once

#include <windows.h>
#include <shellapi.h> // For NOTIFYICONDATA

void InitNotifyIconData(HWND hwnd);
void ShowContextMenu(HWND hwnd);
void RemoveTrayIcon();