#include "../client/ClusterClient.h"

using namespace Engine;
using namespace Engine::Cluster;
using namespace Engine::IO;

int Main(void)
{
    Console console;
    SafePointer<Client> client = new Client;
    try {
        client->SetConnectionServiceID(L"engine.cluster.test.1");
        client->SetConnectionServiceName(L"Some test software (1)");
        client->Connect();
        console.WriteLine(FormatString(L"Local node address: %0", string(client->GetNodeAddress(), HexadecimalBase, 16)));
        console.WriteLine(FormatString(L"Loopback address  : %0", string(client->GetSelfAddress(), HexadecimalBase, 16)));
        SafePointer< Array<NodeDesc> > nodes = client->EnumerateNodes(false);
        for (auto & n : *nodes) {
            console.WriteLine(FormatString(L"Node \"%0\": at %1, online = %2", n.NodeName, string(n.Address, HexadecimalBase, 16), n.Online));
        }
        SafePointer< Array<EndpointDesc> > endpoints = client->EnumerateEndpoints();
        for (auto & e : *endpoints) {
            console.WriteLine(FormatString(L"Endpoint \"%0\": at %1, id = %2", e.ServiceName, string(e.Address, HexadecimalBase, 16), e.ServiceID));
        }
        Sleep(10000);
        client->Disconnect();
    } catch (...) {
        console.WriteLine(L"Test failed with an exception.");
    }
    return 0;
}