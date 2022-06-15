#include <EngineRuntime.h>

#include "../../client/ClusterClient.h"

using namespace Engine;
using namespace Engine::IO;
using namespace Engine::Streaming;
using namespace Engine::Cluster;

handle dl;

int Main(void)
{
	uint16 port;
	ObjectAddress employer;
	try {
		SafePointer< Array<string> > args = GetCommandLine();
		if (args->Length() < 4) return 1;
		dl = LoadLibrary(args->ElementAt(1));
		port = args->ElementAt(2).ToUInt32();
		employer = args->ElementAt(3).ToUInt64();
		if (!dl) return 2;
	} catch (...) {
		return 1;
	}
	try {
		SafePointer<Client> client = new Client;
		client->SetConnectionPort(port);
		client->SetConnectionService(ServiceClass::WorkHost);
		client->Connect();

		// TODO: IMPLEMENT
		client->SendMessage(0x00010411, client->GetNodeAddress(), 0);
		{
			SafePointer<ITextWriter> wr = client->CreateLoggingService();
			wr->WriteLine(FormatString(L"A host service has started on address %0, client = %1",
				string(client->GetSelfAddress(), HexadecimalBase, 16), string(employer, HexadecimalBase, 16)));
		}
		Sleep(30000);
		// TODO: END IMPLEMENT

		client->Disconnect();
	} catch (...) {
		ReleaseLibrary(dl);
		return 3;
	}
	return 0;
}