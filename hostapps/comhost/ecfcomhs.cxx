#include <EngineRuntime.h>

#include "../../client/ClusterClient.h"
#include "init.h"

using namespace Engine;
using namespace Engine::IO;
using namespace Engine::Streaming;
using namespace Engine::Cluster;

void SendAnswer(Client * client, ObjectAddress client_entity, uint32 status, const string & tx1, const string & tx2)
{
	SafePointer<DataBlock> data = new DataBlock(1);
	data->SetLength(12);
	MemoryCopy(data->GetBuffer(), &client_entity, 8);
	MemoryCopy(data->GetBuffer() + 8, &status, 4);
	SafePointer<DataBlock> s1 = tx1.EncodeSequence(Encoding::UTF8, false);
	SafePointer<DataBlock> s2 = tx2.EncodeSequence(Encoding::UTF8, false);
	if (s1->Length() || s2->Length()) {
		data->Append(*s1);
		data->Append(0);
		data->Append(*s2);
	}
	client->SendMessage(0x00000415, client->GetNodeAddress(), data);
}

int Main(void)
{
	try {
		SafePointer< Array<string> > args = GetCommandLine();
		if (args->Length() < 5) return 1;
		CompilerHostEnvironmentInit();
		auto project = args->ElementAt(1);
		auto client_addr = args->ElementAt(2);
		auto words = args->ElementAt(3);
		auto port = args->ElementAt(4);
		ObjectAddress client_endpoint = client_addr.ToUInt64(HexadecimalBase);
		SafePointer<Client> client = new Client;
		client->SetConnectionPort(port.ToUInt32());
		client->SetConnectionServiceID(L"engine.cluster.compiler.host");
		client->SetConnectionServiceName(L"Cluster Compiler Host");
		client->Connect();
		handle con_out, con_err, pipe_in, pipe_out;
		con_out = CloneHandle(GetStandardOutput());
		con_err = CloneHandle(GetStandardError());
		CreatePipe(&pipe_in, &pipe_out);
		SetStandardOutput(pipe_in);
		SetStandardError(pipe_in);
		string arch, os, mode;
		for (auto & w : words.Split(L'-')) {
			if (w == L"windows" || w == L"macosx") os = w;
			else if (w == L"x86" || w == L"x64" || w == L"arm" || w == L"arm64") arch = w;
			else if (w == L"release" || w == L"debug") mode = w;
		}
		Array<string> cl(0x10);
		cl << project; cl << L"-CN";
		if (arch.Length()) { cl << L"-a"; cl << arch; }
		if (mode.Length()) { cl << L"-c"; cl << mode; }
		if (os.Length()) { cl << L"-o"; cl << os; }
		SafePointer<Process> process = CreateCommandProcess(L"ertbuild", &cl);
		if (!process) {
			SendAnswer(client, client_endpoint, 1, L"#toolchain", L"");
			return 2;
		}
		SetStandardOutput(con_out);
		SetStandardError(con_err);
		CloseHandle(pipe_in);
		{
			SafePointer<ITextWriter> writer = client->CreateLoggingService();
			SafePointer<Stream> stream = new FileStream(pipe_out, true);
			TextReader reader(stream, Encoding::UTF8);
			while (!reader.EofReached()) writer->WriteLine(reader.ReadLine());
		}
		process->Wait();
		if (process->GetExitCode()) {
			SendAnswer(client, client_endpoint, 1, L"#" + string(process->GetExitCode()), L"");
			return 3;
		}
		cl << L"-OS";
		CreatePipe(&pipe_in, &pipe_out);
		SetStandardOutput(pipe_in);
		SetStandardError(pipe_in);
		process = CreateCommandProcess(L"ertbuild", &cl);
		if (!process) {
			SendAnswer(client, client_endpoint, 1, L"#toolchain", L"");
			return 2;
		}
		SetStandardOutput(con_out);
		SetStandardError(con_err);
		CloseHandle(pipe_in);
		string xfile, root;
		{
			SafePointer<Stream> stream = new FileStream(pipe_out, true);
			TextReader reader(stream, Encoding::UTF8);
			while (!reader.EofReached()) {
				auto line = reader.ReadLine();
				if (line.Length()) xfile = line;
			}
		}
		process->Wait();
		if (process->GetExitCode()) {
			SendAnswer(client, client_endpoint, 1, L"#" + string(process->GetExitCode()), L"");
			return 3;
		}
		root = xfile;
		while (root.Length() > 6)
		{
			if (Path::GetFileName(Path::GetDirectory(root)) == L"_build") break;
			root = ExpandPath(root + L"/..");
		}
		SendAnswer(client, client_endpoint, 0, xfile, root);
		client->Disconnect();
	} catch (...) {
		return 1;
	}
	return 0;
}