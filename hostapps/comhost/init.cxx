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
#ifdef ENGINE_MACOSX

#include <stdlib.h>

using namespace Engine;
using namespace Engine::IO;
using namespace Engine::Streaming;

void IncludePath(DynamicString & path, const string & file)
{
	FileStream stream(file, AccessRead, OpenExisting);
	TextReader reader(&stream, Encoding::UTF8);
	while (!reader.EofReached()) {
		auto line = reader.ReadLine();
		if (line.Length()) {
			if (path.Length()) path << L':';
			path << line;
		}
	}
}
void CompilerHostEnvironmentInit(void) noexcept
{
	DynamicString path;
	IncludePath(path, L"/etc/paths");
	try {
		SafePointer< Array<string> > files = Search::GetFiles(L"/etc/paths.d/*", false);
		if (!files) throw Exception();
		for (auto & f : *files) IncludePath(path, L"/etc/paths.d/" + f);
	} catch (...) {}
	SafePointer<DataBlock> utf8 = path.ToString().EncodeSequence(Encoding::UTF8, true);
	setenv("PATH", reinterpret_cast<const char *>(utf8->GetBuffer()), true);
	handle null = CreateFile(L"/dev/null", AccessReadWrite, OpenExisting);
	SetStandardInput(null);
	SetStandardOutput(null);
	SetStandardError(null);
	CloseHandle(null);
}

#endif