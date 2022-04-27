#include <EngineRuntime.h>

#include "../../client/ClusterClient.h"

using namespace Engine;
using namespace Engine::IO;
using namespace Engine::Streaming;
using namespace Engine::Cluster;

Console console;
SafePointer<Client> client;
bool print_sender = true;
bool print_time = false;
ObjectArray<TextWriter> writers(0x10);

struct Message
{
	string contents;
	ObjectAddress sender;
	Time time;
};
class ConsoleHandler : public IMessageCallback, public IEventCallback
{
	Array<Message> queue;
	SafePointer<Semaphore> sync, counter;
	bool exit;
public:
	ConsoleHandler(void) : queue(0x100), exit(false)
	{
		sync = CreateSemaphore(1);
		counter = CreateSemaphore(0);
		if (!sync || !counter) throw Exception();
	}
	virtual bool RespondsToMessage(uint32 verb) override { return verb == 0x00000303; }
	virtual void HandleMessage(ObjectAddress from, uint32 verb, const DataBlock * data) override
	{
		if (!data || data->Length() < sizeof(ObjectAddress) + sizeof(Time)) return;
		ObjectAddress sender;
		Time time;
		MemoryCopy(&sender, data->GetBuffer(), sizeof(sender));
		MemoryCopy(&time, data->GetBuffer() + sizeof(sender), sizeof(time));
		auto line = string(data->GetBuffer() + sizeof(sender) + sizeof(Time),
			data->Length() - sizeof(sender) - sizeof(Time), Encoding::UTF8);
		Message message;
		message.contents = line;
		message.sender = sender;
		message.time = time;
		sync->Wait();
		try {
			queue.Append(message);
			counter->Open();
		} catch (...) {}
		sync->Open();
	}
	virtual void CallbackExpired(void) override {}
	virtual void ConnectionLost(void) override
	{
		sync->Wait();
		exit = true;
		counter->Open();
		sync->Open();
	}
	void Listen(Client * client)
	{
		client->RegisterCallback(this);
		while (true) {
			if (counter->WaitFor(1000)) {
				sync->Wait();
				if (exit) { sync->Open(); break; }
				auto msg = queue.FirstElement();
				queue.RemoveFirst();
				sync->Open();
				bool prefix = false;
				if (print_sender) {
					console.Write(L"[ ");
					console.SetTextColor(ConsoleColor::Cyan);
					console.Write(string(msg.sender, HexadecimalBase, 16));
					console.SetTextColor(ConsoleColor::Default);
					console.Write(L" ]");
					prefix = true;
				}
				if (print_time) {
					if (prefix) console.Write(L" ");
					console.Write(L"(");
					console.SetTextColor(ConsoleColor::Yellow);
					console.Write(msg.time.ToLocal().ToString());
					console.SetTextColor(ConsoleColor::Default);
					console.Write(L")");
					prefix = true;
				}
				if (prefix) console.Write(L" ");
				console.WriteLine(msg.contents);
				for (auto & writer : writers) {
					prefix = false;
					if (print_sender) {
						writer.Write(L"[ ");
						writer.Write(string(msg.sender, HexadecimalBase, 16));
						writer.Write(L" ]");
						prefix = true;
					}
					if (print_time) {
						if (prefix) writer.Write(L" ");
						writer.Write(L"(");
						writer.Write(msg.time.ToLocal().ToString());
						writer.Write(L")");
						prefix = true;
					}
					if (prefix) writer.Write(L" ");
					writer.WriteLine(msg.contents);
				}
			}
		}
		client->UnregisterCallback(this);
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
				} else if (a[j] == L's') {
					print_sender = false;
				} else if (a[j] == L't') {
					print_time = true;
				} else if (a[j] == L'o' && i < args->Length() - 1) {
					i++;
					auto file = args->ElementAt(i);
					try {
						SafePointer<Stream> stream = new FileStream(file, AccessWrite, CreateAlways);
						SafePointer<TextWriter> writer = new TextWriter(stream, Encoding::UTF8);
						writer->WriteEncodingSignature();
						writers.Append(writer);
					} catch (...) {
						console.SetTextColor(ConsoleColor::Red);
						console.WriteLine(FormatString(L"Failed to connect file \"%0\".", file));
						console.SetTextColor(ConsoleColor::Default);
						return false;
					}
				} else return false;
			}
		} else return false;
	}
	return true;
}

ConsoleHandler handler;

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
		client->SetConnectionService(ServiceClass::Logger);
		client->SetEventCallback(&handler);
		try {
			client->Connect();
		} catch (...) {
			console.SetTextColor(ConsoleColor::Red);
			console.WriteLine(L"Failed to connect to the cluster node.");
			console.SetTextColor(ConsoleColor::Default);
			return 1;
		}
		handler.Listen(client);
		client->Disconnect();
	} catch (...) {
		console.SetTextColor(ConsoleColor::Red);
		console.WriteLine(L"General exception. Terminating.");
		console.SetTextColor(ConsoleColor::Default);
		return 1;
	}
	return 0;
}