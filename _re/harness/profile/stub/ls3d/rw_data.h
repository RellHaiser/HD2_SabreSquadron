// Harness stub of ls3d/rw_data.h: plain files through stdio (harness.cpp).
#pragma once

#include <cstdint>

constexpr int DTA_INVALID_HANDLE = -1;

int __stdcall dtaOpen(const char *fileName, bool archivesFirst);
int __stdcall dtaOpenWrite(const char *fileName, int flags);
int __stdcall dtaRead(int handle, void *buffer, int size);
int __stdcall dtaWrite(int handle, const void *buffer, int size);
int __stdcall dtaSeek(int handle, int position, int origin);
bool __stdcall dtaClose(int handle);
