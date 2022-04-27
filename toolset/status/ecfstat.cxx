#include <EngineRuntime.h>

#include "../../client/ClusterClient.h"

using namespace Engine;
using namespace Engine::IO;
using namespace Engine::IO::ConsoleControl;
using namespace Engine::Streaming;
using namespace Engine::Cluster;

Console console;
SafePointer<Client> client;
bool print_endpoints = false;
bool print_offline = false;
bool print_status = false;

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
				} else if (a[j] == L'o') {
					print_offline = true;
				} else if (a[j] == L'e') {
					print_endpoints = true;
				} else if (a[j] == L's') {
					print_status = true;
				} else return false;
			}
		} else return false;
	}
	return true;
}

int Main(void)
{
	try {
		client = new Client;
		if (!ParseCommandLine()) {
			console.SetTextColor(ConsoleColor::Red);
			console.WriteLine(L"Invalid command line.");
			console.SetTextColor(ConsoleColor::Default);
			return 2;
		}
		client->SetConnectionServiceName(L"Cluster Status Writer");
		client->SetConnectionServiceID(L"engine.cluster.status");
		try {
			client->Connect();
		} catch (...) {
			console.SetTextColor(ConsoleColor::Red);
			console.WriteLine(L"Failed to connect to the cluster node.");
			console.SetTextColor(ConsoleColor::Default);
			return 1;
		}
		SafePointer< Array<NodeDesc> > nodes = client->EnumerateNodes(false);
		for (auto & n : *nodes) {
			if (!n.Online && !print_offline) continue;
			console << L"Node \"" << TextColor(ConsoleColor::Yellow) << n.NodeName << TextColor(ConsoleColor::Default) <<
				L"\" [ " << TextColor(ConsoleColor::Cyan) << string(n.Address, HexadecimalBase, 16) << TextColor(ConsoleColor::Default) <<
				L" ] ";
			if (n.Online) {
				console << TextColor(ConsoleColor::Green) << L"ONLINE" << TextColor(ConsoleColor::Default);
				if (print_status) {
					try {
						auto status = client->QueryNodeStatus(n.Address);
						if (status.Battery == Power::BatteryStatus::Charging) {
							console << L", battery: " << TextColor(ConsoleColor::Green) <<
								string(status.BatteryLevel / 10) << L"%" << TextColor(ConsoleColor::Default);
						} else if (status.Battery == Power::BatteryStatus::InUse) {
							console << L", battery: " << TextColor(ConsoleColor::Yellow) <<
								string(status.BatteryLevel / 10) << L"%" << TextColor(ConsoleColor::Default);
						}
						if (status.ProgressTotal) {
							console << L", work complete: " << TextColor(ConsoleColor::Cyan) <<
								string(status.ProgressComplete * 100 / status.ProgressTotal) << L"%" << TextColor(ConsoleColor::Default);
						} else console << L", idle";
					} catch (...) {
						console << L", no status available";
					}
				}
				console << LineFeed();
			} else {
				console << TextColor(ConsoleColor::Blue) << L"OFFLINE" << TextColor(ConsoleColor::Default) << LineFeed();
			}
			if (print_endpoints && n.Online) {
				SafePointer< Array<EndpointDesc> > endpoints = client->EnumerateEndpoints(n.Address);
				if (endpoints->Length()) {
					for (auto & e : *endpoints) {
						console << L"  [ " << TextColor(ConsoleColor::Cyan) << string(e.Address, HexadecimalBase, 16) <<
							TextColor(ConsoleColor::Default) << L" ] " << TextColor(ConsoleColor::Yellow) <<
							e.ServiceName << TextColor(ConsoleColor::Default) << L" / " << e.ServiceID << L" /" << LineFeed();
					}
				} else {
					console << L"  no endpoints found" << LineFeed();
				}
			}
		}
		client->Disconnect();
	} catch (...) {
		console.SetTextColor(ConsoleColor::Red);
		console.WriteLine(L"General exception. Terminating.");
		console.SetTextColor(ConsoleColor::Default);
		return 1;
	}
	return 0;
}