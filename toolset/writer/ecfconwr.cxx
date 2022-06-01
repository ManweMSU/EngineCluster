#include <EngineRuntime.h>

#include "../../client/ClusterClient.h"

using namespace Engine;
using namespace Engine::IO;
using namespace Engine::Streaming;
using namespace Engine::Cluster;

Console console;
SafePointer<Client> client;

bool ParseCommandLine(void)
{
	SafePointer< Array<string> > args = GetCommandLine();
	for (int i = 1; i < args->Length(); i++) {
		auto & a = args->ElementAt(i);
		if (a[0] == L':' || a[0] == L'-') {
			for (int j = 1; j < a.Length(); j++) {
				if (a[j] == L'i') {
					i++;
					auto domain = args->ElementAt(i);
					try {
						SafePointer< Array<Network::AddressEntity> > ents = Network::GetAddressByHost(domain,
							ClusterServerDefaultPort, Network::SocketAddressDomain::IPv6, Network::SocketProtocol::TCP);
						if (!ents || !ents->Length()) throw InvalidArgumentException();
						client->SetConnectionIP(ents->FirstElement().EntityAddress);
					} catch (...) { return false; }
				} else if (a[j] == L'p') {
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
		} catch (...) {
			console.SetTextColor(ConsoleColor::Red);
			console.WriteLine(L"Failed to connect to the cluster node.");
			console.SetTextColor(ConsoleColor::Default);
			return 1;
		}
		auto self = client->GetSelfAddress();
		auto node = client->GetNodeAddress();
		while (true) {
			console.SetTextColor(ConsoleColor::Green);
			console.Write(L"> ");
			console.SetTextColor(ConsoleColor::Default);
			auto line = console.ReadLine();
			if (!line.Length()) break;
			SafePointer<DataBlock> data = new DataBlock(1);
			Time time = Time::GetCurrentTime();
			data->SetLength(sizeof(self) + sizeof(time) + line.GetEncodedLength(Encoding::UTF8));
			MemoryCopy(data->GetBuffer(), &self, sizeof(self));
			MemoryCopy(data->GetBuffer() + sizeof(self), &time, sizeof(time));
			line.Encode(data->GetBuffer() + sizeof(self) + sizeof(time), Encoding::UTF8, false);
			try {
				client->SendMessage(0x00000301, node, data);
			} catch (...) {
				console.SetTextColor(ConsoleColor::Red);
				console.WriteLine(L"Failed to write a line.");
				console.SetTextColor(ConsoleColor::Default);
				return 1;
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