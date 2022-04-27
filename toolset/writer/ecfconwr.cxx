#include <EngineRuntime.h>

#include "../../client/ClusterClient.h"

using namespace Engine;
using namespace Engine::IO;
using namespace Engine::Streaming;
using namespace Engine::Cluster;

Console console;
SafePointer<Client> client;
SafePointer<ITextWriter> logger;

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
		client->SetConnectionServiceName(L"Cluster Console Writer");
		client->SetConnectionServiceID(L"engine.cluster.logger.writer");
		try {
			client->Connect();
			logger = client->CreateLoggingService();
		} catch (...) {
			console.SetTextColor(ConsoleColor::Red);
			console.WriteLine(L"Failed to connect to the cluster node.");
			console.SetTextColor(ConsoleColor::Default);
			return 1;
		}
		while (true) {
			console.SetTextColor(ConsoleColor::Green);
			console.Write(L"> ");
			console.SetTextColor(ConsoleColor::Default);
			auto line = console.ReadLine();
			if (!line.Length()) break;
			try { logger->WriteLine(line); } catch (...) {
				console.SetTextColor(ConsoleColor::Red);
				console.WriteLine(L"Failed to write a line.");
				console.SetTextColor(ConsoleColor::Default);
				return 1;
			}
		}
		logger.SetReference(0);
		client->Disconnect();
		client.SetReference(0);
	} catch (...) {
		console.SetTextColor(ConsoleColor::Red);
		console.WriteLine(L"General exception. Terminating.");
		console.SetTextColor(ConsoleColor::Default);
		return 1;
	}
	return 0;
}