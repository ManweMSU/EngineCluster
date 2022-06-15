#include <EngineRuntime.h>

#include "../../client/ClusterClient.h"
#include "../../client/ClusterPackage.h"

using namespace Engine;
using namespace Engine::IO;
using namespace Engine::IO::ConsoleControl;
using namespace Engine::Streaming;
using namespace Engine::Cluster;

struct {
	string source_file, node_address, architecture, os, mode;
	string ip;
	uint16 port = ClusterServerDefaultPort;
	ObjectAddress compile_on;
} state;

Console console;
SafePointer<Client> client;
SafePointer<Semaphore> sync_sem;
string activity, error;

void AddDirectory(PackageBuilder * package, handle asset, const string & path, const string & prefix)
{
	SafePointer< Array<string> > files = Search::GetFiles(path + L"/*");
	SafePointer< Array<string> > dirs = Search::GetDirectories(path + L"/*");
	for (auto & f : *files) {
		if (f[0] == L'.' || f[0] == L'_') continue;
		package->AddFile(path + L"/" + f, asset, prefix + f);
	}
	for (auto & d : *dirs) {
		if (d[0] == L'.' || d[0] == L'_') continue;
		package->AddDirectory(asset, prefix + d);
		AddDirectory(package, asset, path + L"/" + d, prefix + d + L"\\");
	}
}
DataBlock * LoadProjectPackage(void)
{
	if (string::CompareIgnoreCase(Path::GetExtension(state.source_file), L"eipf") == 0) {
		try {
			FileStream stream(state.source_file, AccessRead, OpenExisting);
			if (stream.Length() > 0x1000000) {
				error = L"Package size is too large.";
				return 0;
			}
			return stream.ReadAll();
		} catch (...) {
			error = L"Failed to read the package.";
			return 0;
		}
	} else {
		state.source_file = ExpandPath(state.source_file);
		MemoryStream stream(0x10000);
		SafePointer<PackageBuilder> package = new PackageBuilder(&stream);
		auto asset = package->CreateAsset(PackageAssetSources);
		package->SetAssetRootFile(asset, Path::GetFileName(state.source_file));
		auto ext = Path::GetExtension(state.source_file).LowerCase();
		if (ext == L"ertproj" || ext == L"eini" || ext == L"ini") {
			AddDirectory(package, asset, Path::GetDirectory(state.source_file), L"");
		} else {
			package->AddFile(state.source_file, asset, Path::GetFileName(state.source_file));
		}
		package->Finalize();
		package.SetReference(0);
		stream.Seek(0, Begin);
		if (stream.Length() > 0x1000000) {
			error = L"Package size is too large.";
			return 0;
		}
		return stream.ReadAll();
	}
}

bool ParseCommandLine(void)
{
	SafePointer< Array<string> > args = GetCommandLine();
	for (int i = 1; i < args->Length(); i++) {
		auto & a = args->ElementAt(i);
		if (a[0] == L':' || a[0] == L'-') {
			for (int j = 1; j < a.Length(); j++) {
				if (a[j] == L'r') {
					state.mode = L"release";
				} else if (a[j] == L'd') {
					state.mode = L"debug";
				} else if (a[j] == L'a' && i < args->Length() - 1) {
					if (state.architecture.Length()) return false;
					state.architecture = args->ElementAt(i + 1);
					i++;
				} else if (a[j] == L'i' && i < args->Length() - 1) {
					if (state.ip.Length()) return false;
					state.ip = args->ElementAt(i + 1);
					i++;
				} else if (a[j] == L'p' && i < args->Length() - 1) {
					try {
						state.port = args->ElementAt(i + 1).ToUInt32();
						if (state.port > 0xFFFF) throw InvalidArgumentException();
					} catch (...) { return false; }
					i++;
				} else if (a[j] == L'o' && i < args->Length() - 1) {
					if (state.os.Length()) return false;
					state.os = args->ElementAt(i + 1);
					i++;
				} else return false;
			}
		} else {
			if (!state.source_file.Length()) {
				state.source_file = a;
			} else if (!state.node_address.Length()) {
				state.node_address = a;
			} else return false;
		}
	}
	return true;
}
class MessageHandler : public IMessageCallback, public IEventCallback
{
	SafePointer<Semaphore> sync;
	SafePointer<DataBlock> package_data;
	string error_message;
	Time last_ping;
public:
	MessageHandler(void) { sync = CreateSemaphore(1); }
	virtual bool RespondsToMessage(uint32 verb) override { return verb == 0x00010413; }
	virtual void HandleMessage(ObjectAddress from, uint32 verb, const DataBlock * data) override
	{
		sync->Wait();
		last_ping = Time::GetCurrentTime();
		if (data && data->Length()) {
			try {
				if (data->ElementAt(0)) {
					package_data = new DataBlock(1);
					package_data->SetLength(data->Length() - 1);
					MemoryCopy(package_data->GetBuffer(), data->GetBuffer() + 1, data->Length() - 1);
				} else {
					error_message = string(data->GetBuffer() + 1, data->Length() - 1, Encoding::UTF8);
				}
			} catch (...) {}
		}
		sync->Open();
	}
	virtual void CallbackExpired(void) override {}
	virtual void ConnectionLost(void) override
	{
		sync->Wait();
		error_message = L"Primary node connection lost";
		sync->Open();
	}
	void Prepare(void) { last_ping = Time::GetCurrentTime(); }
	void CheckTimeout(void)
	{
		sync->Wait();
		if ((Time::GetCurrentTime() - last_ping).Ticks > 10000) error_message = L"Timed out";
		sync->Open();
	}
	bool Ready(void)
	{
		sync->Wait();
		bool result = error_message.Length() || package_data;
		sync->Open();
		return result;
	}
	string GetError(void)
	{
		sync->Wait();
		string result = error_message;
		sync->Open();
		return result;
	}
	DataBlock * GetPackage(void)
	{
		sync->Wait();
		auto result = package_data;
		sync->Open();
		result->Retain();
		return result;
	}
};
MessageHandler handler;
int WorkerThread(void * arg_ptr)
{
	try {
		SafePointer<DataBlock> package = LoadProjectPackage();
		if (!package) return 4;
		DynamicString words;
		if (state.architecture.Length()) {
			if (words.Length()) words << L'-';
			words << state.architecture;
		}
		if (state.os.Length()) {
			if (words.Length()) words << L'-';
			words << state.os;
		}
		if (state.mode.Length()) {
			if (words.Length()) words << L'-';
			words << state.mode;
		}
		SafePointer<DataBlock> payload = words.ToString().EncodeSequence(Encoding::UTF8, true);
		payload->Append(*package);
		sync_sem->Wait();
		activity = L"Waiting for the remote compiler";
		sync_sem->Open();
		handler.Prepare();
		client->SendMessage(0x00000413, state.compile_on, payload);
		package.SetReference(0);
		payload.SetReference(0);
		while (!handler.Ready()) { Sleep(1000); handler.CheckTimeout(); }
		auto err = handler.GetError();
		if (err.Length()) { error = err; return 5; }
		SafePointer<DataBlock> data = handler.GetPackage();
		sync_sem->Wait();
		activity = L"Unpacking the package built";
		sync_sem->Open();
		SafePointer<Stream> package_stream = new MemoryStream(data->GetBuffer(), data->Length());
		data.SetReference(0);
		SafePointer<Package> result = new Package(package_stream);
		package_stream.SetReference(0);
		SafePointer< Array<PackageAssetDesc> > assets = result->EnumerateAssets();
		if (!assets->Length()) throw Exception();
		auto & asset = assets->FirstElement();
		words.Clear();
		for (auto & w : asset.Words) {
			if (words.Length()) words << L"_";
			words << w;
		}
		if (state.mode.Length()) {
			words << L"_" << state.mode;
		} else {
			words << L"_release";
		}
		auto extract_root = ExpandPath(Path::GetDirectory(state.source_file) + L"/_build/" + words.ToString());
		CreateDirectoryTree(extract_root);
		SafePointer< Array<PackageFileDesc> > files = result->EnumerateFiles(asset.Handle);
		for (auto & f : *files) {
			if (f.Handle) {
				FileStream to(extract_root + L"/" + f.Path, AccessReadWrite, CreateAlways);
				SafePointer<Stream> from = result->OpenFile(f.Handle);
				from->CopyTo(&to);
				DateTime::SetFileCreationTime(to.Handle(), f.DateCreated);
				DateTime::SetFileAlterTime(to.Handle(), f.DateAltered);
				DateTime::SetFileAccessTime(to.Handle(), f.DateAccessed);
			} else {
				CreateDirectoryTree(extract_root + L"/" + f.Path);
			}
		}
		result.SetReference(0);
	} catch (...) { return 1; }
	return 0;
}

int Main(void)
{
	try {
		if (!ParseCommandLine()) {
			console << TextColor(ConsoleColor::Red) << L"Invalid command line." << TextColor(ConsoleColor::Default) << LineFeed();
			return 2;
		}
		if (state.source_file.Length()) {
			ObjectAddress node;
			try {
				client = new Client;
				if (state.ip.Length()) client->SetConnectionIP(MakeNetworkAddress(state.ip));
				client->SetConnectionPort(state.port);
				client->SetConnectionServiceID(L"engine.cluster.compiler.client");
				client->SetConnectionServiceName(L"Cluster Compiler Client");
				client->SetEventCallback(&handler);
				client->Connect();
				client->RegisterCallback(&handler);
			} catch (...) {
				console << TextColor(ConsoleColor::Red) << L"Failed to connect to the primary cluster node." << TextColor(ConsoleColor::Default) << LineFeed();
				return 3;
			}
			try {
				if (!state.node_address.Length()) throw InvalidArgumentException();
				node = state.node_address.ToUInt64(HexadecimalBase);
				if (node < 0x10000) node = 0x0100000000000000UL | (node << 16);
			} catch (...) {
				try {
					node = 0;
					SafePointer< Array<NodeDesc> > nodes = client->EnumerateNodes();
					for (auto & n : *nodes) if (n.NodeName == state.node_address) { node = n.Address; break; }
					if (!node) throw Exception();
				} catch (...) {
					console << TextColor(ConsoleColor::Red) << L"Node ECF address is invalid." << TextColor(ConsoleColor::Default) << LineFeed();
					return 2;
				}
			}
			state.compile_on = node;
			activity = L"Creating a source file package";
			sync_sem = CreateSemaphore(1);
			SafePointer<Thread> thread = CreateThread(WorkerThread);
			if (!thread) throw Exception();
			int counter = 0;
			while (!thread->Exited()) {
				sync_sem->Wait();
				console.ClearLine();
				console << TextColor(ConsoleColor::Yellow) << activity << TextColor(ConsoleColor::Default) << string(L'.', counter + 1);
				sync_sem->Open();
				counter = (counter + 1) % 3;
				Sleep(500);
			}
			thread->Wait();
			auto code = thread->GetExitCode();
			console.ClearLine();
			if (code) {
				console << TextColor(ConsoleColor::Red) << L"Failed with an error: " << error << TextColor(ConsoleColor::Default) << LineFeed();
				client->Disconnect();
				client.SetReference(0);
				return code;
			}
			console << TextColor(ConsoleColor::Green) << L"Succeed" << TextColor(ConsoleColor::Default) << LineFeed();
			client->Disconnect();
			client.SetReference(0);
		} else {
			console << ENGINE_VI_APPNAME << LineFeed();
			console << L"Command line syntax:" << LineFeed();
			console << L"  ecfremc <project> <node> [-a <architecture>] [-o <os>] [-r | -d] [-i <ip>] [-p <port>]" << LineFeed();
			console << L"Where:" << LineFeed();
			console << L"  project - either .eipf, .ertproj or C++ source file to build," << LineFeed();
			console << L"  node    - ECF compiler node address (either 16-digit, 4-digit or node name)," << LineFeed();
			console << L"  -a      - specifies an architecture to build for," << LineFeed();
			console << L"  -o      - specifies an operating system to build for," << LineFeed();
			console << L"  -r      - uses a release configuration to build," << LineFeed();
			console << L"  -d      - uses a debug configuration to build," << LineFeed();
			console << L"  -i      - overrides IP address of the primary node to connect," << LineFeed();
			console << L"  -p      - overrides port of the primary node to connect." << LineFeed() << LineFeed();
		}
	} catch (...) {
		client->Disconnect();
		client.SetReference(0);
		console << TextColor(ConsoleColor::Red) << L"Failed with an exception." << TextColor(ConsoleColor::Default) << LineFeed();
		return 1;
	}
	return 0;
}