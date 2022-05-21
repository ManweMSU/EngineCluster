#include "srvrnet.h"

using namespace Engine;
using namespace Engine::Streaming;
using namespace Engine::Network;
using namespace Engine::Storage;

#define ECF_CP_NODENAME			L"Node Name"
#define ECF_CP_NODEUUID			L"Node UUID"
#define ECF_CP_NODEECFADDRESS	L"Address"
#define ECF_CP_NODEIPADDRESS	L"IP Address"
#define ECF_CP_NODEIPPORT		L"IP Port"
#define ECF_CP_NETNAME			L"Net Name"
#define ECF_CP_NETUUID			L"Net UUID"
#define ECF_CP_NETAUTOCONNECT	L"Autoconnect"
#define ECF_CP_NETTIMESTAMP		L"Timestamp"

// TODO: REMOVE
IO::Console dcon;
// TODO: END REMOVE

namespace Engine
{
	namespace Cluster
	{
		struct NodeConnectionInfo
		{
			NetDesc * net_desc;
			NodeDesc * node_desc;
			SafePointer<Socket> socket;
		};
		struct ServiceEntry
		{
			uint32 prefix;
			string identifier;
			string name;
		};
		struct ClientEntry
		{
			ObjectAddress address;
			SafePointer<Socket> socket;
		};
		struct NewClientDesc
		{
			SafePointer<Socket> socket;
			ObjectAddress address;
		};

		SafeArray<NetDesc> known_nets(0x10);
		Array<IServerEventCallback *> event_callbacks(0x10);
		Array<IServerMessageCallback *> message_callbacks(0x10);
		SafePointer<Semaphore> net_sync;
		string node_name, config_file;
		UUID node_uuid, net_uuid;
		bool config_alternated, join_allowed;
		uint16 self_port;
		ObjectAddress self_addr;
		volatile int net_status; // 0 - offline, 1 - connected, 2 - disconnecting

		Array<NodeConnectionInfo> node_info(0x10);
		Array<ServiceEntry> services(0x10);
		Array<ClientEntry> clients(0x10);
		SafePointer<Socket> incoming_socket;
		SafePointer<Semaphore> net_shutdown_sync;
		uint service_counter;

		template<class T> void RegistryReadBinary(RegistryNode * node, const string & prop, T & t)
		{
			if (node->GetValueBinarySize(prop) == sizeof(T)) node->GetValueBinary(prop, &t);
		}
		template<class T> void RegistryWriteBinary(RegistryNode * node, const string & prop, const T & t)
		{
			node->CreateValue(prop, RegistryValueType::Binary);
			node->SetValue(prop, &t, sizeof(T));
		}

		void ServiceShutdown(void)
		{
			auto dec = InterlockedDecrement(service_counter);
			if (!dec) {
				net_status = 0;
				net_shutdown_sync->Open();
				for (auto & cb : event_callbacks) cb->SwitchedFromNet(&net_uuid);
				Windows::GetWindowSystem()->SubmitTask(CreateFunctionalTask([]() {
					Power::PreventIdleSleep(Power::Prevent::None);
				}));
				ZeroMemory(&net_uuid, sizeof(UUID));
			}
		}
		void InternalSendMessage(Socket * socket, uint32 verb, ObjectAddress from, ObjectAddress to, const DataBlock * payload)
		{
			uint32 length = payload ? payload->Length() : 0;
			socket->Write(&verb, 4);
			socket->Write(&length, 4);
			socket->Write(&to, 8);
			socket->Write(&from, 8);
			if (payload) socket->WriteArray(payload);
		}
		bool InternalSendMessage(uint32 verb, ObjectAddress from, ObjectAddress to, const DataBlock * payload, bool lock = true)
		{
			if (lock) net_sync->Wait();

			// TODO: IMPLEMENT

			if (lock) net_sync->Open();
			return false;
		}
		void InternalGetMessage(Socket * socket, uint32 & verb, ObjectAddress & from, ObjectAddress & to, DataBlock ** payload)
		{
			while (true) {
				if (socket->Wait(1000)) {
					if (net_status == 2) throw Exception();
					uint32 length;
					socket->Read(&verb, 4);
					socket->Read(&length, 4);
					socket->Read(&to, 8);
					socket->Read(&from, 8);
					SafePointer<DataBlock> block = new DataBlock(0x100);
					block->SetLength(length);
					if (length) socket->Read(block->GetBuffer(), length);
					*payload = block.Inner();
					block->Retain();
					return;
				}
				if (net_status == 2) throw Exception();
			}
		}
		ObjectAddress PerformIncomingHandshake(Socket * socket)
		{
			ObjectAddress result = 0;
			net_sync->Wait();
			try {
				char sign[8];
				uint32 ot;
				socket->Read(&sign, 8);
				socket->Read(&ot, 4);
				if (MemoryCompare(&sign, "ECLFAUTH", 8)) throw Exception();
				try {
					if ((ot & 0xFF000000) == 0x01000000) {
						UUID client_uuid, my_uuid;
						socket->Read(&client_uuid, 8);
						socket->Read(&my_uuid, 8);
						if (MemoryCompare(&my_uuid, &node_uuid, sizeof(UUID))) throw Exception();
						if (IsZeroUUID(&client_uuid)) {
							if (!join_allowed) throw Exception();
							ObjectAddress return_address = MakeObjectAddress(AddressLevelNode, AddressServiceNode, AddressNodeUndefined, AddressInstanceUnique);
							MemoryCopy(&sign, "ECLFRESP", 8);
							socket->Write(&sign, 8);
							socket->Write(&return_address, 8);
							socket->Write(&self_addr, 8);
							result = 0;
						} else {
							NodeConnectionInfo * info = 0;
							for (auto & i : node_info) if (MemoryCompare(&client_uuid, &i.node_desc->uuid, sizeof(UUID)) == 0) {
								info = &i;
								break;
							}
							if (!info) throw Exception();
							MemoryCopy(&sign, "ECLFRESP", 8);
							socket->Write(&sign, 8);
							socket->Write(&info->node_desc->ecf_address, 8);
							socket->Write(&self_addr, 8);
							info->socket.SetRetain(socket);
							result = info->node_desc->ecf_address;
						}
					} else if ((ot & 0xFF000000) == 0x02000000) {
						// TODO: IMPLEMENT ENDPOINT HANDSHAKE (INCOMING SIDE)
					} else throw Exception();
				} catch (...) {
					MemoryCopy(&sign, "ECLFRESP", 8);
					ObjectAddress fail = 0;
					socket->Write(&sign, 8);
					socket->Write(&fail, 8);
					socket->Write(&fail, 8);
					socket->Shutdown(true, true);
					throw;
				}
			} catch (...) { net_sync->Open(); throw; }
			net_sync->Open();
			return result;
		}
		int ClientThread(void * arg_ptr)
		{
			InterlockedIncrement(service_counter);
			auto desc = reinterpret_cast<NewClientDesc *>(arg_ptr);
			dcon.WriteLine(L"Client service started");
			while (true) {
				try {
					SafePointer<DataBlock> payload;
					uint32 verb;
					ObjectAddress addr_from, addr_to;
					InternalGetMessage(desc->socket, verb, addr_from, addr_to, payload.InnerRef());
					dcon.WriteLine(FormatString(L"Got a message %0 from %1 to %2.", string(verb, HexadecimalBase, 8),
						string(addr_from, HexadecimalBase, 16), string(addr_to, HexadecimalBase, 16)));
					if ((verb & 0xFF00) == 0x0100) {
						// TODO: IMPLEMENT
						// ADDRESS:
						//  0 - new node candidate
						//  LEVEL 01 - node
						// SERVER INTERNAL CONVERSATION
						// SERVER TO SERVER ONLY
						//   0000 0101 - server net node join
						//   0001 0101 - server net node join responce
						//   0000 0102 - server net node connect
						//   0001 0102 - server net node connect responce
						//   0000 0103 - server net update node list
					} else if (addr_to == self_addr) {
						if ((verb & 0xFF00) == 0x0000) {
							if (verb = 0x00000001) { // PING
								ServerSendMessage(0x00010001, addr_from, payload);
							}
						} else {
							for (auto & hdlr : message_callbacks) if (hdlr->RespondsToMessage(verb)) {
								hdlr->HandleMessage(addr_from, addr_to, verb, payload);
								break;
							}
						}
					} else InternalSendMessage(verb, addr_from, addr_to, payload);
				} catch (...) { break; }
			}
			desc->socket->Shutdown(true, true);
			dcon.WriteLine(L"Client service is shutting down");
			delete desc;
			ServiceShutdown();
			return 0;
		}
		void RunClientThread(Socket * socket, ObjectAddress address)
		{
			auto client_desc = new NewClientDesc;
			client_desc->socket.SetRetain(socket);
			client_desc->address = address;
			SafePointer<Thread> chat = CreateThread(ClientThread, client_desc);
			if (!chat) delete client_desc;
		}
		int ListenerThread(void * arg_ptr)
		{
			InterlockedIncrement(service_counter);
			while (true) {
				try {
					Address address;
					uint16 port;
					SafePointer<Socket> client = incoming_socket->Accept(address, port);
					if (net_status == 2) break;
					ObjectAddress address_assigned = PerformIncomingHandshake(client);
					RunClientThread(client, address_assigned);
				} catch (...) {
					if (net_status == 2) break;
				}
			}
			net_sync->Wait();
			incoming_socket->Shutdown(true, true);
			incoming_socket.SetReference(0);
			for (auto & node : node_info) if (MemoryCompare(&node.node_desc->uuid, &node_uuid, sizeof(UUID)) == 0) {
				node.node_desc->online = false;
			}
			net_sync->Open();
			ServiceShutdown();
			return 0;
		}
		int ReconnectionThread(void * arg_ptr)
		{
			InterlockedIncrement(service_counter);
			dcon.WriteLine(L"Reconnection service started");
			while (true) {
				// TODO: REWORK
				// net_sync->Wait();
				// for (int i = 0; i < node_info.Length(); i++) {
				// 	if (!node_info[i].node_desc->online) {
				// 		try {
				// 			SafePointer<Socket> socket = CreateSocket(SocketAddressDomain::IPv6, SocketProtocol::TCP);
				// 			socket->Connect(node_info[i].node_desc->ip_address, node_info[i].node_desc->ip_port);
				// 			char sign[8];
				// 			uint32 ot;
				// 			MemoryCopy(&sign, "ECLFAUTH", 8);
				// 			ot = 0x01000000;
				// 			socket->Write(&sign, 8);
				// 			socket->Write(&ot, 4);
				// 			socket->Write(&node_uuid, sizeof(UUID));
				// 			socket->Write(&node_info[i].net_desc->uuid, sizeof(UUID));
				// 			ObjectAddress self, server;
				// 			socket->Read(&sign, 8);
				// 			if (MemoryCompare(&sign, "ECLFRESP", 8)) throw Exception();
				// 			socket->Read(&self, 8);
				// 			socket->Read(&server, 8);
				// 			if (self != self_addr || server != node_info[i].node_desc->ecf_address) throw Exception();
				// 			// HANDSHAKE SUCCESSFUL
				// 			// TODO: IMPLEMENT NODE TABLE EXCHANGE
				// 		} catch (...) { continue; }
				// 	}
				// }
				// net_sync->Open();
				// TODO: END REWORK

				if (net_status != 1) break;
				for (int i = 0; i < 10; i++) {
					Sleep(1000);
					if (net_status != 1) break;
				}
			}
			dcon.WriteLine(L"Reconnection service is shutting down");
			ServiceShutdown();
			return 0;
		}

		string MakeStringOfUUID(const UUID * uuid)
		{
			DynamicString result;
			for (int i = 0; i < 16; i++) {
				if (i && !(i & 1)) result += L"-";
				result += string(uint32(uuid->bytes[i]), HexadecimalBase, 2);
			}
			return result.ToString();
		}
		bool MakeUUIDOfString(const string & text, UUID * uuid)
		{
			try {
				DynamicString digits;
				for (int i = 0; i < text.Length(); i++) {
					if (text[i] >= L'0' && text[i] <= L'9') digits << text[i];
					else if (text[i] >= L'A' && text[i] <= L'F') digits << text[i];
					else if (text[i] >= L'a' && text[i] <= L'f') digits << text[i];
					else if (text[i] == L'-');
					else throw InvalidArgumentException();
				}
				if (digits.Length() != 32) throw InvalidArgumentException();
				auto sd = digits.ToString();
				for (int i = 0; i < 16; i++) {
					auto frag = sd.Fragment(2 * i, 2);
					uuid->bytes[i] = frag.ToUInt32(HexadecimalBase);
				}
			} catch (...) { return false; }
			return true;
		}
		bool IsZeroUUID(const UUID * uuid)
		{
			bool zero = true;
			for (int i = 0; i < 16; i++) if (uuid->bytes[i]) { zero = false; break; }
			return zero;
		}

		ObjectAddress MakeObjectAddress(uint32 level, uint32 service, uint32 node, uint32 instance)
		{
			ObjectAddress result = level;
			result <<= 24;
			result |= service;
			result <<= 16;
			result |= node;
			result <<= 16;
			result |= instance;
			return result;
		}
		uint32 GetAddressLevel(ObjectAddress address) { return (address >> 56) & 0xFF; }
		uint32 GetAddressService(ObjectAddress address) { return (address >> 32) & 0xFFFFFF; }
		uint32 GetAddressNode(ObjectAddress address) { return (address >> 16) & 0xFFFF; }
		uint32 GetAddressInstance(ObjectAddress address) { return address & 0xFFFF; }

		bool ServerInitialize(void)
		{
			ZeroMemory(&net_uuid, sizeof(net_uuid));
			ZeroMemory(&node_uuid, sizeof(node_uuid));
			config_alternated = join_allowed = false;
			node_name = L"";
			service_counter = 0;
			net_status = 0;
			net_sync = CreateSemaphore(1);
			net_shutdown_sync = CreateSemaphore(1);
			if (!net_sync || !net_shutdown_sync) return false;
			config_file = IO::ExpandPath(IO::Path::GetDirectory(IO::GetExecutablePath()) + L"/ecfsrvr.ecs");
			try {
				SafePointer<Stream> config_stream = new FileStream(config_file, AccessRead, OpenExisting);
				SafePointer<Registry> config = LoadRegistry(config_stream);
				if (!config) throw Exception();
				node_name = config->GetValueString(ECF_CP_NODENAME);
				RegistryReadBinary(config, ECF_CP_NODEUUID, node_uuid);
				for (auto & d : config->GetSubnodes()) {
					SafePointer<RegistryNode> net_node = config->OpenNode(d);
					NetDesc net_desc;
					net_desc.name = net_node->GetValueString(ECF_CP_NETNAME);
					RegistryReadBinary(net_node, ECF_CP_NETUUID, net_desc.uuid);
					net_desc.autoconnect = net_node->GetValueBoolean(ECF_CP_NETAUTOCONNECT);
					net_desc.desc_time = net_node->GetValueTime(ECF_CP_NETTIMESTAMP);
					for (auto & n : net_node->GetSubnodes()) {
						SafePointer<RegistryNode> node_node = net_node->OpenNode(n);
						NodeDesc node_desc;
						node_desc.known_name = node_node->GetValueString(ECF_CP_NODENAME);
						RegistryReadBinary(node_node, ECF_CP_NODEUUID, node_desc.uuid);
						RegistryReadBinary(node_node, ECF_CP_NODEECFADDRESS, node_desc.ecf_address);
						RegistryReadBinary(node_node, ECF_CP_NODEIPADDRESS, node_desc.ip_address);
						node_desc.ip_port = node_node->GetValueInteger(ECF_CP_NODEIPPORT);
						node_desc.online = false;
						net_desc.nodes << node_desc;
					}
					known_nets << net_desc;
				}
			} catch (...) {}
			return true;
		}
		void ServerPerformAutoconnect(void)
		{
			for (auto & net : known_nets) if (net.autoconnect) {
				ServerNetConnect(&net.uuid);
				break;
			}
		}
		void ServerShutdown(void)
		{
			ServerNetDisconnect();
			net_shutdown_sync->Wait();
			net_shutdown_sync->Open();
			if (config_alternated) {
				try {
					SafePointer<Stream> config_stream = new FileStream(config_file, AccessReadWrite, CreateAlways);
					SafePointer<Registry> config = CreateRegistry();
					config->CreateValue(ECF_CP_NODENAME, RegistryValueType::String);
					config->SetValue(ECF_CP_NODENAME, node_name);
					RegistryWriteBinary(config, ECF_CP_NODEUUID, node_uuid);
					for (int i = 0; i < known_nets.Length(); i++) {
						auto & net = known_nets[i];
						auto net_reg_name = string(uint(i + 1), HexadecimalBase, 4);
						config->CreateNode(net_reg_name);
						SafePointer<RegistryNode> net_reg = config->OpenNode(net_reg_name);
						net_reg->CreateValue(ECF_CP_NETNAME, RegistryValueType::String);
						net_reg->SetValue(ECF_CP_NETNAME, net.name);
						RegistryWriteBinary(net_reg, ECF_CP_NETUUID, net.uuid);
						net_reg->CreateValue(ECF_CP_NETAUTOCONNECT, RegistryValueType::Boolean);
						net_reg->SetValue(ECF_CP_NETAUTOCONNECT, net.autoconnect);
						net_reg->CreateValue(ECF_CP_NETTIMESTAMP, RegistryValueType::Time);
						net_reg->SetValue(ECF_CP_NETTIMESTAMP, net.desc_time);
						for (int j = 0; j < net.nodes.Length(); j++) {
							auto & node = net.nodes[j];
							auto node_reg_name = string(uint(j + 1), HexadecimalBase, 4);
							net_reg->CreateNode(node_reg_name);
							SafePointer<RegistryNode> node_reg = net_reg->OpenNode(node_reg_name);
							node_reg->CreateValue(ECF_CP_NODENAME, RegistryValueType::String);
							node_reg->SetValue(ECF_CP_NODENAME, node.known_name);
							RegistryWriteBinary(node_reg, ECF_CP_NODEUUID, node.uuid);
							RegistryWriteBinary(node_reg, ECF_CP_NODEECFADDRESS, node.ecf_address);
							RegistryWriteBinary(node_reg, ECF_CP_NODEIPADDRESS, node.ip_address);
							node_reg->CreateValue(ECF_CP_NODEIPPORT, RegistryValueType::Integer);
							node_reg->SetValue(ECF_CP_NODEIPPORT, node.ip_port);
						}
					}
					config->Save(config_stream);
				} catch (...) {}
			}
		}

		void SetServerName(const string & name) { node_name = name; config_alternated = true; }
		void SetServerUUID(const UUID * uuid) { MemoryCopy(&node_uuid, uuid, sizeof(UUID)); config_alternated = true; }
		void RegisterServerEventCallback(IServerEventCallback * callback)
		{
			net_sync->Wait();
			bool present = false;
			for (auto & cb : event_callbacks) if (cb == callback) { present = true; break; }
			if (!present) event_callbacks << callback;
			net_sync->Open();
		}
		void UnregisterServerEventCallback(IServerEventCallback * callback)
		{
			net_sync->Wait();
			for (int i = 0; i < event_callbacks.Length(); i++) if (event_callbacks[i] == callback) {
				event_callbacks.Remove(i);
				break;
			}
			net_sync->Open();
		}
		string GetServerName(void) { return node_name; }
		void GetServerUUID(UUID * uuid) { MemoryCopy(uuid, &node_uuid, sizeof(UUID)); }
		uint16 GetServerCurrentPort(void) { return self_port; }

		bool ServerNetCreate(const string & name, const UUID * uuid, uint16 port)
		{
			net_sync->Wait();
			for (auto & net : known_nets) if (MemoryCompare(&net.uuid, uuid, sizeof(UUID)) == 0) {
				net_sync->Open();
				return false;
			}
			NetDesc net;
			NodeDesc node;
			node.uuid = node_uuid;
			node.known_name = node_name;
			node.ecf_address = MakeObjectAddress(AddressLevelNode, AddressServiceNode, AddressNodeBase, AddressInstanceUnique);
			node.ip_address = Network::Address::CreateLoopBackIPv6();
			node.ip_port = port;
			node.online = false;
			net.autoconnect = false;
			net.desc_time = Time::GetCurrentTime();
			net.name = name;
			net.nodes << node;
			net.uuid = *uuid;
			known_nets << net;
			config_alternated = true;
			net_sync->Open();
			return true;
		}
		bool ServerNetConnect(const UUID * uuid)
		{
			if (net_status) return false;
			NetDesc * desired_net = 0;
			for (auto & net : known_nets) if (MemoryCompare(&net.uuid, uuid, sizeof(UUID)) == 0) {
				desired_net = &net;
				break;
			}
			if (!desired_net) return false;
			net_sync->Wait();
			net_uuid = *uuid;
			node_info.Clear();
			for (auto & node : desired_net->nodes) {
				NodeConnectionInfo info;
				info.node_desc = &node;
				info.net_desc = desired_net;
				node_info << info;
			}
			NodeDesc * self = 0;
			try {
				uint16 port = 0;
				for (auto & node : desired_net->nodes) if (MemoryCompare(&node.uuid, &node_uuid, sizeof(UUID)) == 0) {
					port = node.ip_port;
					self = &node;
					break;
				}
				if (!port) throw Exception();
				self_port = port;
				self_addr = self->ecf_address;
				incoming_socket = CreateSocket(SocketAddressDomain::IPv6, SocketProtocol::TCP);
				incoming_socket->Bind(Address::CreateAny(), port);
				incoming_socket->Listen();
				SafePointer<Thread> listener = CreateThread(ListenerThread);
				SafePointer<Thread> recon = CreateThread(ReconnectionThread);
			} catch (...) {
				incoming_socket.SetReference(0);
				ZeroMemory(&net_uuid, sizeof(net_uuid));
				net_sync->Open();
				return false;
			}
			self->online = true;
			join_allowed = false;
			net_status = 1;
			net_shutdown_sync->Wait();
			for (auto & cb : event_callbacks) cb->SwitchedToNet(uuid);
			Windows::GetWindowSystem()->SubmitTask(CreateFunctionalTask([]() {
				Power::PreventIdleSleep(Power::Prevent::IdleSystemSleep);
			}));
			net_sync->Open();
			return true;
		}
		void ServerNetDisconnect(void)
		{
			if (net_status == 1) {
				net_status = 2;
				join_allowed = false;
				for (auto & cb : event_callbacks) cb->SwitchingFromNet(&net_uuid);
				try {
					SafePointer<Socket> socket = CreateSocket(SocketAddressDomain::IPv6, SocketProtocol::TCP);
					socket->Connect(Address::CreateLoopBackIPv6(), self_port);
					socket->Shutdown(true, true);
				} catch (...) {}
			}
		}
		bool ServerNetAllowJoin(bool allow)
		{
			if (net_status == 1) {
				join_allowed = allow;
				return true;
			} else return false;
		}
		void ServerNetJoin(Network::Address to, uint16 port_to, uint16 port_from)
		{
			// TODO: IMPLEMENT
		}
		void ServerNetLeave(void)
		{
			// TODO: IMPLEMENT
		}
		void ServerNetForget(const UUID * uuid)
		{
			net_sync->Wait();
			if (MemoryCompare(uuid, &net_uuid, sizeof(UUID)) && net_status != 2) {
				for (int i = 0; i < known_nets.Length(); i++) {
					if (MemoryCompare(uuid, &known_nets[i].uuid, sizeof(UUID)) == 0) {
						known_nets.Remove(i);
						config_alternated = true;
						break;
					}
				}
			}
			net_sync->Open();
		}
		bool ServerNetNodeRemove(ObjectAddress node)
		{
			// TODO: IMPLEMENT
			return false;
		}

		Array<UUID> * ServerEnumerateKnownNets(void)
		{
			SafePointer< Array<UUID> > result = new Array<UUID>(0x10);
			for (auto & net : known_nets) result->Append(net.uuid);
			result->Retain();
			return result;
		}
		bool GetServerCurrentNet(UUID * uuid)
		{
			if (net_status == 1) {
				MemoryCopy(uuid, &net_uuid, sizeof(UUID));
				return true;
			} else return false;
		}
		string GetServerNetName(const UUID * uuid)
		{
			for (auto & net : known_nets) if (MemoryCompare(uuid, &net.uuid, sizeof(UUID)) == 0) return net.name;
		}
		bool GetServerNetAutoconnectStatus(const UUID * uuid)
		{
			for (auto & net : known_nets) if (MemoryCompare(uuid, &net.uuid, sizeof(UUID)) == 0) return net.autoconnect;
			return false;
		}
		void ServerNetMakeAutoconnect(const UUID * uuid)
		{
			config_alternated = true;
			for (auto & net : known_nets) {
				if (uuid && MemoryCompare(uuid, &net.uuid, sizeof(UUID)) == 0) net.autoconnect = true;
				else net.autoconnect = false;
			}
		}
		Array<NodeDesc> * ServerEnumerateNetNodes(const UUID * uuid)
		{
			for (auto & net : known_nets) if (MemoryCompare(uuid, &net.uuid, sizeof(UUID)) == 0) {
				SafePointer< Array<NodeDesc> > result = new Array<NodeDesc>(0x10);
				for (auto & node : net.nodes) result->Append(node);
				result->Retain();
				return result;
			}
			return 0;
		}

		void RegisterServerMessageCallback(IServerMessageCallback * callback)
		{
			net_sync->Wait();
			bool present = false;
			for (auto & cb : message_callbacks) if (cb == callback) { present = true; break; }
			if (!present) message_callbacks << callback;
			net_sync->Open();
		}
		void UnregisterServerMessageCallback(IServerMessageCallback * callback)
		{
			net_sync->Wait();
			for (int i = 0; i < message_callbacks.Length(); i++) if (message_callbacks[i] == callback) {
				message_callbacks.Remove(i);
				break;
			}
			net_sync->Open();
		}
		bool ServerSendMessage(uint32 verb, ObjectAddress to, const DataBlock * payload)
		{
			if (net_status == 1) return InternalSendMessage(verb, self_addr, to, payload);
			else return false;
		}
	}
}