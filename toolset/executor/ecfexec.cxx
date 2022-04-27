#include <EngineRuntime.h>

#include "../../client/ClusterTaskClient.h"

using namespace Engine;
using namespace Engine::IO;
using namespace Engine::IO::ConsoleControl;
using namespace Engine::Streaming;
using namespace Engine::Cluster;

Console console;
SafePointer<TaskClient> client;
string package_file;
string argument_file;
string output_file;
Volumes::Dictionary<string, string> arguments;

class DictionaryMirror : public Reflection::Reflected
{
	Volumes::Dictionary<string, string> & _dictionary;
public:
	DictionaryMirror(Volumes::Dictionary<string, string> & dictionary) : _dictionary(dictionary) {}
	virtual void EnumerateProperties(Reflection::IPropertyEnumerator & enumerator) override
	{
		for (auto & p : _dictionary.Elements()) {
			enumerator.EnumerateProperty(p.key, &p.value, Reflection::PropertyType::String, Reflection::PropertyType::Unknown, 1, sizeof(string));
		}
	}
	virtual Reflection::PropertyInfo GetProperty(const string & name) override
	{
		auto prop = _dictionary.GetElementByKey(name);
		Reflection::PropertyInfo info;
		if (prop) {
			info.Address = prop;
			info.Type = Reflection::PropertyType::String;
			info.InnerType = Reflection::PropertyType::Unknown;
			info.Volume = 1;
			info.ElementSize = sizeof(string);
			info.Name = name;
		} else {
			info.Address = 0;
			info.Type = Reflection::PropertyType::Unknown;
			info.InnerType = Reflection::PropertyType::Unknown;
			info.Volume = -1;
			info.ElementSize = 0;
			info.Name = name;
		}
		return info;
	}
};

bool ParseCommandLine(void)
{
	SafePointer< Array<string> > args = GetCommandLine();
	for (int i = 1; i < args->Length(); i++) {
		auto & a = args->ElementAt(i);
		if (a[0] == L':' || a[0] == L'-') {
			for (int j = 1; j < a.Length(); j++) {
				if (a[j] == L'i' && i < args->Length() - 1) {
					i++;
					try { client->SetConnectionIP(MakeNetworkAddress(args->ElementAt(i))); } catch (...) { return false; }
				} else if (a[j] == L'p' && i < args->Length() - 1) {
					i++;
					auto port = args->ElementAt(i);
					try {
						uint32 p = port.ToUInt32();
						if (p > 0xFFFF) throw InvalidArgumentException();
						client->SetConnectionPort(p);
					} catch (...) { return false; }
				} else if (a[j] == L'f' && i < args->Length() - 1) {
					i++;
					if (argument_file.Length()) return false;
					argument_file = args->ElementAt(i);
				} else if (a[j] == L'o' && i < args->Length() - 1) {
					i++;
					if (output_file.Length()) return false;
					output_file = args->ElementAt(i);
				} else if (a[j] == L'a' && i < args->Length() - 2) {
					i++;
					string argname = args->ElementAt(i);
					i++;
					string argvalue = args->ElementAt(i);
					arguments.Append(argname, argvalue);
				} else return false;
			}
		} else {
			if (package_file.Length()) return false;
			package_file = a;
		}
	}
	return true;
}
string NodeErrorDesc(uint32 code)
{
	if (code == HostStarted) return L"OK";
	else if (code == HostIsStarting) return L"Initializing...";
	else if (code > 0 && !(code & 0x8000)) return L"Status #" + string(code, HexadecimalBase, 4);
	else if (code == HostTimedOut) return L"Node timed out";
	else if (code == HostBusyNow) return L"Node is already busy";
	else if (code == HostDiscarded) return L"Request discarded";
	else if (code == HostNoArchitecture) return L"Node's architecture is not available";
	else if (code == HostExtractionFailed) return L"Package extraction failed";
	else if (code == HostCompilationFailed) return L"Dynamic compilation failed";
	else if (code == HostLaunchFailed) return L"Failed to launch the host";
	else if (code == HostInitFailed) return L"Failed to initialize the host";
	else if (code == TaskErrorNoNodesAvailable) return L"No nodes available";
	else if (code == TaskErrorInternalFailure) return L"Internal failure";
	else return L"Error #" + string(code, HexadecimalBase, 4);	
}
string CommonErrorDesc(uint32 code)
{
	if (code == HostStarted) return L"Finished";
	else if (code == HostIsStarting) return L"Initializing";
	else if (code == TaskErrorWorking) return L"Working";
	else if (code > 0 && !(code & 0x8000)) return L"Status #" + string(code, HexadecimalBase, 4);
	else if (code == HostTimedOut) return L"Node timed out";
	else if (code == HostBusyNow) return L"Node is already busy";
	else if (code == HostDiscarded) return L"Request discarded";
	else if (code == HostNoArchitecture) return L"Node's architecture is not available";
	else if (code == HostExtractionFailed) return L"Package extraction failed";
	else if (code == HostCompilationFailed) return L"Dynamic compilation failed";
	else if (code == HostLaunchFailed) return L"Failed to launch the host";
	else if (code == HostInitFailed) return L"Failed to initialize the host";
	else if (code == TaskErrorNoNodesAvailable) return L"No nodes available";
	else if (code == TaskErrorInternalFailure) return L"Internal failure";
	else return L"Error #" + string(code, HexadecimalBase, 4);	
}
ConsoleColor ErrorColor(uint32 code)
{
	if (!code) return ConsoleColor::Green;
	else if (code & 0x8000) return ConsoleColor::Red;
	else if (code == HostIsStarting) return ConsoleColor::Yellow;
	else return ConsoleColor::Cyan;
}

int Main(void)
{
	try {
		client = new TaskClient;
		if (!ParseCommandLine()) {
			console.SetTextColor(ConsoleColor::Red);
			console.WriteLine(L"Invalid command line.");
			console.SetTextColor(ConsoleColor::Default);
			return 2;
		}
		if (package_file.Length()) {
			SafePointer<DataBlock> exec;
			try {
				FileStream stream(package_file, AccessRead, OpenExisting);
				exec = stream.ReadAll();
			} catch (...) {
				console.SetTextColor(ConsoleColor::Red);
				console.WriteLine(L"Failed to load the package to execute.");
				console.SetTextColor(ConsoleColor::Default);
				return 2;
			}
			if (exec->Length() > 0x1000000) {
				console.SetTextColor(ConsoleColor::Red);
				console.WriteLine(L"The package size is too large.");
				console.SetTextColor(ConsoleColor::Default);
				return 2;
			}
			client->SetTaskExecutable(exec);
			if (argument_file.Length()) {
				SafePointer<DataBlock> input;
				try {
					FileStream stream(argument_file, AccessRead, OpenExisting);
					input = stream.ReadAll();
				} catch (...) {
					console.SetTextColor(ConsoleColor::Red);
					console.WriteLine(L"Failed to load the input file.");
					console.SetTextColor(ConsoleColor::Default);
					return 2;
				}
				client->SetTaskInput(input);
			} else {
				DictionaryMirror mirror(arguments);
				client->SetTaskInput(mirror);
			}
			auto time_started = Time::GetCurrentTime();
			try {
				client->Start();
			} catch (...) {
				console.SetTextColor(ConsoleColor::Red);
				console.WriteLine(L"Failed to connect to the cluster node.");
				console.SetTextColor(ConsoleColor::Default);
				return 1;
			}
			SafePointer< Array<NodeDesc> > nodes = client->GetClusterClient()->EnumerateNodes(true);
			int max_name = 0;
			uint run_number = 0;
			bool success;
			for (auto & n : *nodes) if (n.NodeName.Length() > max_name) max_name = n.NodeName.Length();
			console.AlternateScreenBuffer(true);
			while (true) {
				bool exit = client->IsFinished();
				console.ClearScreen();
				Array<TaskHostDesc> hosts(0x10);
				hosts.SetLength(client->GetHostCount());
				client->GetHostInfo(0, hosts.Length(), hosts.GetBuffer());
				for (auto & h : hosts) {
					string name;
					for (auto & n : *nodes) if (n.Address == h.NodeAddress) { name = n.NodeName; break; }
					console << name << string(L' ', max_name - name.Length()) << L" : ";
					console << TextColor(ErrorColor(h.HostStatus)) << NodeErrorDesc(h.HostStatus) << TextColor(ConsoleColor::Default);
					if (h.HostStatus == HostStarted) {
						console << L" : [ " << TextColor(ConsoleColor::Cyan);
						if (h.TasksTotal) console << string(h.TasksComplete * 100 / h.TasksTotal); else console << L"0";
						console << L"%" << TextColorDefault() << L" ]";
					}
					console << LineFeed();
				}
				auto com_status = client->GetTaskResultStatus();
				uint32 com_complete, com_total;
				console << L"Common status: " << TextColor(ErrorColor(com_status)) << CommonErrorDesc(com_status) << TextColor(ConsoleColor::Default);
				client->GetProgressInfo(&com_complete, &com_total);
				if (!(com_status & 0x8000)) {
					if (com_total != 0xFFFFFFFF) {
						console << L" : [ " << TextColor(ConsoleColor::Cyan);
						if (com_total) console << string(com_complete * 100 / com_total); else console << L"0";
						console << L"%" << TextColorDefault() << L" ]";
					}
				}
				console << LineFeed() << LineFeed();
				if (com_total && com_total != 0xFFFFFFFF) {
					if (com_complete) {
						auto time_now = Time::GetCurrentTime();
						auto time_elapsed = time_now - time_started;
						auto est_left = Time(time_elapsed.Ticks * (com_total - com_complete) / com_complete);
						auto time_elapsed_str = time_elapsed.ToShortString();
						auto est_left_str = est_left.ToShortString();
						if (run_number & 1) {
							time_elapsed_str = time_elapsed_str.Replace(L':', L' ');
							est_left_str = est_left_str.Replace(L':', L' ');
						}
						console << L"Time elapsed : " << TextColor(ConsoleColor::Cyan) << time_elapsed_str << TextColorDefault() << LineFeed();
						console << L"Time left    : " << TextColor(ConsoleColor::Cyan) << est_left_str << TextColorDefault() << LineFeed();
						console << LineFeed();
					}
					int w, h;
					console.GetScreenBufferDimensions(w, h);
					int space = w - 3;
					if (space > 1) {
						int fill = com_complete * space / com_total;
						console << L"\x2554" << string(L'\x2550', space) << L"\x2557" << LineFeed();
						console << L"\x2551" << string(L' ', space) << L"\x2551" << LineFeed();
						console << L"\x2551" << TextColor(ConsoleColor::Green);
						console << string(L'\x2588', fill) << TextColorDefault() << string(L'\x2592', space - fill) << L"\x2551" << LineFeed();
						console << L"\x2551" << string(L' ', space) << L"\x2551" << LineFeed();
						console << L"\x255A" << string(L'\x2550', space) << L"\x255D" << LineFeed();
					}
				}
				if (exit) {
					success = !(com_status & 0x8000);
					break;
				}
				Sleep(1000);
				run_number++;
			}
			auto time_spent = Time::GetCurrentTime() - time_started;
			client->Wait();
			Sleep(5000);
			console.AlternateScreenBuffer(false);
			if (success) {
				if (output_file.Length()) try {
					SafePointer<DataBlock> data;
					client->GetTaskResult(data.InnerRef());
					FileStream output(output_file, AccessWrite, CreateAlways);
					if (data) output.WriteArray(data);
				} catch (...) {}
				console << TextColor(ConsoleColor::Green) << FormatString(L"Successfully finished within %0!", time_spent.ToShortString()) << TextColor(ConsoleColor::Default) << LineFeed();
			} else {
				auto code = client->GetTaskResultStatus();
				console << TextColor(ConsoleColor::Red) << L"Computation failed." << TextColor(ConsoleColor::Default) << LineFeed();
				console << L"Reason: " << TextColor(ErrorColor(code)) << CommonErrorDesc(code) << TextColor(ConsoleColor::Default) << LineFeed();
			}
		} else {
			console << ENGINE_VI_APPNAME << LineFeed();
			console << L"Command line syntax:" << LineFeed();
			console << L"  ecfexec <package.eipf> [-a <name> <value>] [-f <input>] [-o <output>] [-i <ip>] [-p <port>]" << LineFeed();
			console << L"Where:" << LineFeed();
			console << L"  package.eipf - a package file to execute," << LineFeed();
			console << L"  -a           - provides an argument to the primary task," << LineFeed();
			console << L"  name         - a name of value (field name) to provide," << LineFeed();
			console << L"  value        - a value to provide, string only," << LineFeed();
			console << L"  -f           - provides a binary input file to the primary task," << LineFeed();
			console << L"  input        - a name of the binary input file," << LineFeed();
			console << L"  -o           - sets a binary output file name," << LineFeed();
			console << L"  output       - a binary output file name," << LineFeed();
			console << L"  -i           - overrides the default (127.0.0.1) IP address of the primary node," << LineFeed();
			console << L"  ip           - IP address of the primary node," << LineFeed();
			console << L"  -p           - overrides the default (10666) port of the primary node," << LineFeed();
			console << L"  port         - port of the primary node." << LineFeed() << LineFeed();
		}
	} catch (...) {
		console.SetTextColor(ConsoleColor::Red);
		console.WriteLine(L"General exception. Terminating.");
		console.SetTextColor(ConsoleColor::Default);
		return 1;
	}
	return 0;
}