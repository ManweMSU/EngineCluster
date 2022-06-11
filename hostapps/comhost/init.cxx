#include "init.h"

#ifdef ENGINE_WINDOWS

#include <Windows.h>

void CompilerHostEnvironmentInit(void) noexcept
{
    AllocConsole();
    SetConsoleTitleW(L"Cluster Compiler Host");
    SECURITY_ATTRIBUTES attr;
    attr.bInheritHandle = TRUE;
    attr.lpSecurityDescriptor = 0;
    attr.nLength = sizeof(attr);
    HANDLE out = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &attr, OPEN_EXISTING, 0, 0);
    HANDLE err = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &attr, OPEN_EXISTING, 0, 0);
    HANDLE in = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &attr, OPEN_EXISTING, 0, 0);
    SetStdHandle(STD_OUTPUT_HANDLE, out);
    SetStdHandle(STD_ERROR_HANDLE, err);
    SetStdHandle(STD_INPUT_HANDLE, in);
}

#endif

// TODO: UPDATE PATH ENVIRONMENT [ MAC OS ]