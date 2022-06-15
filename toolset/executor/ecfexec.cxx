#include <EngineRuntime.h>

#include "../../client/ClusterClient.h"

using namespace Engine;
using namespace Engine::IO;
using namespace Engine::IO::ConsoleControl;
using namespace Engine::Streaming;
using namespace Engine::Cluster;

Console console;
SafePointer<Client> client;
string package_file;
Dictionary::PlainDictionary<string, string> arguments(0x10);

class DictionaryMirror : public Reflection::Reflected
{
	Dictionary::PlainDictionary<string, string> & _dictionary;
public:
	DictionaryMirror(Dictionary::PlainDictionary<string, string> & dictionary) : _dictionary(dictionary) {}
	virtual void EnumerateProperties(Reflection::IPropertyEnumerator & enumerator) override
	{
		for (auto & p : _dictionary.Elements()) {
			enumerator.EnumerateProperty(p.key, &p.value, Reflection::PropertyType::String, Reflection::PropertyType::Unknown, 1, sizeof(string));
		}
	}
	virtual Reflection::PropertyInfo GetProperty(const string & name) override
	{
		auto prop = _dictionary.ElementByKey(name);
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
					auto domain = args->ElementAt(i);
					try {
						SafePointer< Array<Network::AddressEntity> > ents = Network::GetAddressByHost(domain,
							ClusterServerDefaultPort, Network::SocketAddressDomain::IPv6, Network::SocketProtocol::TCP);
						if (!ents || !ents->Length()) throw InvalidArgumentException();
						client->SetConnectionIP(ents->FirstElement().EntityAddress);
					} catch (...) { return false; }
				} else if (a[j] == L'p' && i < args->Length() - 1) {
					i++;
					auto port = args->ElementAt(i);
					try {
						uint32 p = port.ToUInt32();
						if (p > 0xFFFF) throw InvalidArgumentException();
						client->SetConnectionPort(p);
					} catch (...) { return false; }
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
		if (package_file.Length()) {
			// TODO: REMOVE TEST

			// DictionaryMirror mirror(arguments);
			// MemoryStream stream(0x1000);
			// Reflection::JsonSerializer serializer(&stream);
			// mirror.Serialize(&serializer);
			// stream.Seek(0, Begin);
			// TextReader reader(&stream);
			// while (!reader.EofReached()) console.WriteLine(reader.ReadLine());
			// return 66;

			// TODO: END REMOVE TEST

			client->SetConnectionService(ServiceClass::WorkClient);

			// TODO: ADD HANDLER
			//client->SetEventCallback(&handler);

			try {
				client->Connect();
			} catch (...) {
				console.SetTextColor(ConsoleColor::Red);
				console.WriteLine(L"Failed to connect to the cluster node.");
				console.SetTextColor(ConsoleColor::Default);
				return 1;
			}
			SafePointer<DataBlock> data;
			try {
				FileStream stream(package_file, AccessRead, OpenExisting);
				data = stream.ReadAll();
			} catch (...) {
				console.SetTextColor(ConsoleColor::Red);
				console.WriteLine(L"Failed to load the package to execute.");
				console.SetTextColor(ConsoleColor::Default);
				return 2;
			}
			if (data->Length() > 0x1000000) {
				console.SetTextColor(ConsoleColor::Red);
				console.WriteLine(L"The package size is too large.");
				console.SetTextColor(ConsoleColor::Default);
				return 2;
			}
			SafePointer< Array<NodeDesc> > nodes = client->EnumerateNodes();
			
			// TODO: IMPLEMENT

			class TestCallback : public IMessageCallback
			{
			public:
				struct HostDesc
				{
					ObjectAddress node, host;
					Time last_time;
					uint32 status;
				};
				
				SafePointer<Semaphore> sync;
				Array<HostDesc> hosts;

				TestCallback(void)
				{
					sync = CreateSemaphore(1);
				}
				virtual bool RespondsToMessage(uint32 verb) override
				{
					return verb == 0x00010411;
				}
				virtual void HandleMessage(ObjectAddress from, uint32 verb, const DataBlock * data) override
				{
					if (verb == 0x00010411 && data) {
						sync->Wait();
						for (auto & h : hosts) if (h.node == from) {
							h.last_time = Time::GetCurrentTime();
							if (data->Length() >= 4) {
								uint32 status;
								MemoryCopy(&status, data->GetBuffer(), 4);
								if (h.status == 0x0001) h.status = status;
								if (data->Length() >= 12) {
									ObjectAddress address;
									MemoryCopy(&address, data->GetBuffer() + 4, 8);
									h.host = address;
								}
							}
							break;
						}
						sync->Open();
					}
				}
				virtual void CallbackExpired(void) override {}
			};
			TestCallback callback;
			Time current = Time::GetCurrentTime();
			for (auto & n : *nodes) {
				TestCallback::HostDesc host;
				host.last_time = current;
				host.node = n.Address;
				host.host = 0;
				host.status = 0x0001;
				callback.hosts << host;
			}
			client->RegisterCallback(&callback);
			for (auto & h : callback.hosts) client->SendMessage(0x00000411, h.node, data);
			console.AlternateScreenBuffer(true);
			while (true) {
				bool exit = true;
				console.ClearScreen();
				callback.sync->Wait();
				current = Time::GetCurrentTime();
				for (auto & h : callback.hosts) {
					if (h.status == 0x0001 && (current - h.last_time).Ticks > 10000) h.status = 0x8001;
					console << string(h.node, HexadecimalBase, 16) << L" : ";
					if (h.status == 0x0000) {
						console << TextColor(ConsoleColor::Green) << L"READY" << TextColor(ConsoleColor::Default) <<
							L" - " << string(h.host, HexadecimalBase, 16);
					} else if (h.status == 0x0001) {
						exit = false;
						console << TextColor(ConsoleColor::Yellow) << L"STARTING..." << TextColor(ConsoleColor::Default);
					} else {
						console << TextColor(ConsoleColor::Red) << L"ERROR #" << string(h.status, HexadecimalBase, 4) << TextColor(ConsoleColor::Default);
					}
					console << LineFeed();
				}
				callback.sync->Open();
				if (exit) break;
				Sleep(1000);
			}
			console.ReadLine();
			console.AlternateScreenBuffer(false);
			client->UnregisterCallback(&callback);

			// TODO: END IMPLEMENT

			client->Disconnect();
		} else {
			console << ENGINE_VI_APPNAME << LineFeed();
			console << L"Command line syntax:" << LineFeed();
			console << L"  ecfexec <package.eipf> [-a <name> <value>] [-i <ip>] [-p <port>]" << LineFeed();
			console << L"Where:" << LineFeed();
			console << L"  package.eipf - a package file to execute," << LineFeed();
			console << L"  -a           - provides an argument to the primary task," << LineFeed();
			console << L"  name         - a name of value (field name) to provide," << LineFeed();
			console << L"  value        - a value to provide, string only," << LineFeed();
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