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
			UUID uuid;
			ObjectAddress address;
			Network::Address ip_addr;
			uint16 ip_port;
		};
		struct JoinNetRequest
		{
			string address_notation;
			uint16 port_to, port_my;
			SafePointer<IDispatchTask> on_success;
			SafePointer<IDispatchTask> on_fail;
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
		void SerializeNetDesc(RegistryNode * node_to, const NetDesc & net)
		{
			node_to->CreateValue(ECF_CP_NETNAME, RegistryValueType::String);
			node_to->SetValue(ECF_CP_NETNAME, net.name);
			RegistryWriteBinary(node_to, ECF_CP_NETUUID, net.uuid);
			node_to->CreateValue(ECF_CP_NETAUTOCONNECT, RegistryValueType::Boolean);
			node_to->SetValue(ECF_CP_NETAUTOCONNECT, net.autoconnect);
			node_to->CreateValue(ECF_CP_NETTIMESTAMP, RegistryValueType::Time);
			node_to->SetValue(ECF_CP_NETTIMESTAMP, net.desc_time);
			for (int j = 0; j < net.nodes.Length(); j++) {
				auto & node = net.nodes[j];
				auto node_reg_name = string(uint(j + 1), HexadecimalBase, 4);
				node_to->CreateNode(node_reg_name);
				SafePointer<RegistryNode> node_reg = node_to->OpenNode(node_reg_name);
				node_reg->CreateValue(ECF_CP_NODENAME, RegistryValueType::String);
				node_reg->SetValue(ECF_CP_NODENAME, node.known_name);
				RegistryWriteBinary(node_reg, ECF_CP_NODEUUID, node.uuid);
				RegistryWriteBinary(node_reg, ECF_CP_NODEECFADDRESS, node.ecf_address);
				RegistryWriteBinary(node_reg, ECF_CP_NODEIPADDRESS, node.ip_address);
				node_reg->CreateValue(ECF_CP_NODEIPPORT, RegistryValueType::Integer);
				node_reg->SetValue(ECF_CP_NODEIPPORT, node.ip_port);
			}
		}
		NetDesc DeserializeNetDesc(RegistryNode * node_from)
		{
			NetDesc net_desc;
			net_desc.name = node_from->GetValueString(ECF_CP_NETNAME);
			RegistryReadBinary(node_from, ECF_CP_NETUUID, net_desc.uuid);
			net_desc.autoconnect = node_from->GetValueBoolean(ECF_CP_NETAUTOCONNECT);
			net_desc.desc_time = node_from->GetValueTime(ECF_CP_NETTIMESTAMP);
			for (auto & n : node_from->GetSubnodes()) {
				SafePointer<RegistryNode> node_node = node_from->OpenNode(n);
				NodeDesc node_desc;
				node_desc.known_name = node_node->GetValueString(ECF_CP_NODENAME);
				RegistryReadBinary(node_node, ECF_CP_NODEUUID, node_desc.uuid);
				RegistryReadBinary(node_node, ECF_CP_NODEECFADDRESS, node_desc.ecf_address);
				RegistryReadBinary(node_node, ECF_CP_NODEIPADDRESS, node_desc.ip_address);
				node_desc.ip_port = node_node->GetValueInteger(ECF_CP_NODEIPPORT);
				node_desc.online = false;
				net_desc.nodes << node_desc;
			}
			return net_desc;
		}
		DataBlock * SerializeNetDesc(const NetDesc & net)
		{
			SafePointer<Registry> reg = CreateRegistry();
			MemoryStream stream(0x1000);
			SerializeNetDesc(reg, net);
			reg->Save(&stream);
			stream.Seek(0, Begin);
			return stream.ReadAll();
		}
		NetDesc DeserializeNetDesc(const DataBlock * data)
		{
			MemoryStream stream(0x1000);
			stream.WriteArray(data);
			stream.Seek(0, Begin);
			SafePointer<Registry> reg = LoadRegistry(&stream);
			if (!reg) throw InvalidFormatException();
			return DeserializeNetDesc(reg);
		}

		void ServiceShutdown(void)
		{
			auto dec = InterlockedDecrement(service_counter);
			if (!dec) {
				net_status = 0;
				for (auto & cb : event_callbacks) cb->SwitchedFromNet(&net_uuid);
				Windows::GetWindowSystem()->SubmitTask(CreateFunctionalTask([]() {
					Power::PreventIdleSleep(Power::Prevent::None);
				}));
				ZeroMemory(&net_uuid, sizeof(UUID));
				net_shutdown_sync->Open();
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
			bool status = false;
			if (to != self_addr) {
				if (lock) net_sync->Wait();
				if (GetAddressNode(to) == 0) {
					status = true;
					for (auto & i : node_info) if (GetAddressNode(i.node_desc->ecf_address) != GetAddressNode(self_addr) && i.socket) {
						try {
							InternalSendMessage(i.socket, verb, from, i.node_desc->ecf_address, payload);
						} catch (...) { status = false; }
					}
				} else {
					if (GetAddressNode(to) != GetAddressNode(self_addr)) {
						for (auto & i : node_info) if (GetAddressNode(i.node_desc->ecf_address) == GetAddressNode(to)) {
							if (i.socket) {
								try {
									InternalSendMessage(i.socket, verb, from, to, payload);
									status = true;
								} catch (...) {}
							}
							break;
						}
					} else {
						// TODO: IMPLEMENT ENDPOINT SEARCH
					}
				}
				if (lock) net_sync->Open();
			} else {
				for (auto & hdlr : message_callbacks) if (hdlr->RespondsToMessage(verb)) {
					hdlr->HandleMessage(from, to, verb, payload);
					status = true;
					break;
				}
			}
			return status;
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
		ObjectAddress ConnectNode(Socket * socket, const UUID * uuid)
		{
			NodeConnectionInfo * info = 0;
			for (auto & i : node_info) if (MemoryCompare(uuid, &i.node_desc->uuid, sizeof(UUID)) == 0) {
				info = &i;
				break;
			}
			if (!info) throw Exception();
			if (info->socket) throw Exception();
			info->socket.SetRetain(socket);
			info->node_desc->online = true;
			for (auto & cb : event_callbacks) cb->NodeConnected(info->node_desc->ecf_address);
			return info->node_desc->ecf_address;
		}
		ObjectAddress PerformIncomingHandshake(Socket * socket, UUID * ret_client_uuid)
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
						socket->Read(&client_uuid, sizeof(UUID));
						socket->Read(&my_uuid, sizeof(UUID));
						MemoryCopy(ret_client_uuid, &client_uuid, sizeof(UUID));
						if (IsZeroUUID(&my_uuid)) {
							if (!join_allowed) throw Exception();
							join_allowed = false;
							ObjectAddress return_address = MakeObjectAddress(AddressLevelNode, AddressServiceNode, AddressNodeUndefined, AddressInstanceUnique);
							MemoryCopy(&sign, "ECLFRESP", 8);
							socket->Write(&sign, 8);
							socket->Write(&return_address, 8);
							socket->Write(&self_addr, 8);
							result = 0;
						} else {
							if (MemoryCompare(&my_uuid, &node_uuid, sizeof(UUID))) throw Exception();
							result = ConnectNode(socket, &client_uuid);
							MemoryCopy(&sign, "ECLFRESP", 8);
							socket->Write(&sign, 8);
							socket->Write(&result, 8);
							socket->Write(&self_addr, 8);
						}
					} else if ((ot & 0xFF000000) == 0x02000000) {
						ZeroMemory(ret_client_uuid, sizeof(UUID));
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
		int LeaveCurrentNetThread(void * arg_ptr)
		{
			UUID uuid;
			net_sync->Wait();
			MemoryCopy(&uuid, &net_uuid, sizeof(UUID));
			net_sync->Open();
			ServerNetDisconnect();
			net_shutdown_sync->Wait();
			net_shutdown_sync->Open();
			ServerNetForget(&uuid);
			return 0;
		}
		void UpdateNetNodesWithNet(NetDesc * net_update, const NetDesc * with)
		{
			bool net_leave = false;
			net_sync->Wait();
			for (auto & nn : with->nodes) {
				bool present = false;
				for (auto & on : net_update->nodes) if (MemoryCompare(&on.uuid, &nn.uuid, sizeof(UUID)) == 0) {
					present = true;
					break;
				}
				if (!present) try {
					net_update->nodes << nn;
					NodeConnectionInfo ni;
					ni.net_desc = net_update;
					ni.node_desc = &net_update->nodes.LastElement();
					node_info << ni;
				} catch (...) {}
			}
			for (int i = node_info.Length() - 1; i >= 0; i--) {
				auto & ni = node_info[i];
				auto on = ni.node_desc;
				bool present = false;
				for (auto & nn : with->nodes) if (MemoryCompare(&on->uuid, &nn.uuid, sizeof(UUID)) == 0) {
					present = true;
					break;
				}
				if (!present) {
					if (MemoryCompare(&node_uuid, &on->uuid, sizeof(UUID)) == 0) net_leave = true;
					for (int j = 0; j < net_update->nodes.Length(); j++) {
						if (MemoryCompare(&net_update->nodes[j].uuid, &on->uuid, sizeof(UUID)) == 0) {
							net_update->nodes.Remove(j);
							break;
						}
					}
					node_info.Remove(i);
				}
			}
			net_update->desc_time = with->desc_time;
			net_update->name = with->name;
			config_alternated = true;
			net_sync->Open();
			for (auto & cb : event_callbacks) cb->NetTopologyChanged(&net_update->uuid);
			if (net_leave) {
				SafePointer<Thread> thread = CreateThread(LeaveCurrentNetThread);
			}
		}
		int ClientThread(void * arg_ptr)
		{
			InterlockedIncrement(service_counter);
			auto desc = reinterpret_cast<NewClientDesc *>(arg_ptr);
			while (true) {
				try {
					SafePointer<DataBlock> payload;
					uint32 verb;
					ObjectAddress addr_from, addr_to;
					InternalGetMessage(desc->socket, verb, addr_from, addr_to, payload.InnerRef());
					if ((verb & 0xFF00) == 0x0100) {
						if (verb == 0x00000101) {
							Network::Address my_ip;
							uint16 client_port;
							string client_name;
							MemoryCopy(&my_ip, payload->GetBuffer(), sizeof(Network::Address));
							MemoryCopy(&client_port, payload->GetBuffer() + sizeof(Network::Address), sizeof(uint16));
							client_name = string(payload->GetBuffer() + sizeof(Network::Address) + sizeof(uint16),
								payload->Length() - sizeof(Network::Address) - sizeof(uint16), Encoding::UTF8);
							net_sync->Wait();
							NetDesc * net = 0;
							uint32 mx_node = 0;
							for (auto & i : node_info) {
								auto node_addr = GetAddressNode(i.node_desc->ecf_address);
								if (node_addr > mx_node) mx_node = node_addr;
								if (MemoryCompare(&i.node_desc->uuid, &node_uuid, sizeof(UUID)) == 0) {
									Network::Address loopback = Network::Address::CreateLoopBackIPv6();
									if (MemoryCompare(&loopback, &i.node_desc->ip_address, sizeof(Network::Address)) == 0) {
										i.node_desc->ip_address = my_ip;
										config_alternated = true;
									}
									net = i.net_desc;
								}
							}
							if (!net) {
								net_sync->Open();
								throw Exception();
							}
							SafePointer<DataBlock> net_desc_serialized;
							try {
								mx_node++;
								NodeDesc new_node;
								NodeConnectionInfo con_info;
								new_node.ecf_address = MakeObjectAddress(AddressLevelNode, AddressServiceNode, mx_node, AddressInstanceUnique);
								new_node.ip_address = desc->ip_addr;
								new_node.ip_port = client_port;
								new_node.known_name = client_name;
								new_node.online = false;
								MemoryCopy(&new_node.uuid, &desc->uuid, sizeof(UUID));
								net->nodes << new_node;
								net->desc_time = Time::GetCurrentTime();
								con_info.net_desc = net;
								con_info.node_desc = &net->nodes.LastElement();
								node_info << con_info;
								net_desc_serialized = SerializeNetDesc(*net);
								config_alternated = true;
							} catch (...) {
								net_sync->Open();
								throw;
							}
							net_sync->Open();
							for (auto & cb : event_callbacks) cb->NetTopologyChanged(&net_uuid);
							InternalSendMessage(desc->socket, 0x00010101, self_addr, desc->address, net_desc_serialized);
							InternalSendMessage(0x00000103, self_addr, MakeObjectAddress(AddressLevelNode, AddressServiceNode,
								AddressNodeUndefined, AddressInstanceUnique), net_desc_serialized);
						} else if (verb == 0x00000102) {
							auto advised_net = DeserializeNetDesc(payload);
							NetDesc * current_net = 0;
							for (auto & i : node_info) if (MemoryCompare(&i.node_desc->uuid, &node_uuid, sizeof(UUID)) == 0) {
								current_net = i.net_desc;
								break;
							}
							if (!current_net) throw Exception();
							if (advised_net.desc_time > current_net->desc_time) {
								UpdateNetNodesWithNet(current_net, &advised_net);
								InternalSendMessage(0x00000103, self_addr, MakeObjectAddress(AddressLevelNode,
									AddressServiceNode, AddressNodeUndefined, AddressInstanceUnique), payload);
							} else if (advised_net.desc_time < current_net->desc_time) {
								SafePointer<DataBlock> my_net_data = SerializeNetDesc(*current_net);
								InternalSendMessage(0x00000103, self_addr, desc->address, my_net_data);
							}
						} else if (verb == 0x00000103) {
							auto advised_net = DeserializeNetDesc(payload);
							NetDesc * current_net = 0;
							for (auto & i : node_info) if (MemoryCompare(&i.node_desc->uuid, &node_uuid, sizeof(UUID)) == 0) {
								current_net = i.net_desc;
								break;
							}
							if (!current_net) throw Exception();
							if (advised_net.desc_time > current_net->desc_time) {
								UpdateNetNodesWithNet(current_net, &advised_net);
							}
						}
					} else if (addr_to == self_addr) {
						if ((verb & 0xFF00) == 0x0000) {
							if (verb = 0x00000001) ServerSendMessage(0x00010001, addr_from, payload);
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
			net_sync->Wait();
			if (GetAddressLevel(desc->address) == AddressLevelNode) {
				for (auto & i : node_info) {
					if (i.node_desc->ecf_address == desc->address) {
						i.socket.SetReference(0);
						i.node_desc->online = false;
						for (auto & cb : event_callbacks) cb->NodeDisconnected(i.node_desc->ecf_address);
						break;
					}
				}
			} else if (GetAddressLevel(desc->address) == AddressLevelClient) {
				// TODO: UNREGISTER CLIENT AND SOCKET
			}
			net_sync->Open();
			delete desc;
			ServiceShutdown();
			return 0;
		}
		void RunClientThread(Socket * socket, const UUID * uuid, ObjectAddress address, Network::Address ip_addr, uint16 ip_port)
		{
			auto client_desc = new NewClientDesc;
			client_desc->socket.SetRetain(socket);
			client_desc->address = address;
			client_desc->ip_addr = ip_addr;
			client_desc->ip_port = ip_port;
			MemoryCopy(&client_desc->uuid, uuid, sizeof(UUID));
			SafePointer<Thread> chat = CreateThread(ClientThread, client_desc);
			if (!chat) delete client_desc;
		}
		int ListenerThread(void * arg_ptr)
		{
			InterlockedIncrement(service_counter);
			while (true) {
				try {
					UUID uuid;
					Address address;
					uint16 port;
					SafePointer<Socket> client = incoming_socket->Accept(address, port);
					if (net_status == 2) break;
					ObjectAddress address_assigned = PerformIncomingHandshake(client, &uuid);
					RunClientThread(client, &uuid, address_assigned, address, port);
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
			while (true) {
				Array<UUID> try_connect(0x10);
				net_sync->Wait();
				for (auto & i : node_info) if (!i.socket && MemoryCompare(&i.node_desc->uuid, &node_uuid, sizeof(UUID))) {
					try { try_connect << i.node_desc->uuid; } catch (...) {}
				}
				net_sync->Open();
				if (net_status != 1) break;
				while (try_connect.Length()) {
					net_sync->Wait();
					auto conn = try_connect.LastElement();
					try_connect.RemoveLast();
					try {
						for (auto & i : node_info) if (MemoryCompare(&conn, &i.node_desc->uuid, sizeof(UUID)) == 0 && !i.socket) {
							if (net_status != 1) break;
							SafePointer<Socket> socket = CreateSocket(SocketAddressDomain::IPv6, SocketProtocol::TCP);
							socket->Connect(i.node_desc->ip_address, i.node_desc->ip_port);
							char sign[8];
							uint32 ot;
							MemoryCopy(&sign, "ECLFAUTH", 8);
							ot = 0x01000000;
							socket->Write(&sign, 8);
							socket->Write(&ot, 4);
							socket->Write(&node_uuid, sizeof(UUID));
							socket->Write(&i.node_desc->uuid, sizeof(UUID));
							ObjectAddress self, server;
							socket->Read(&sign, 8);
							if (MemoryCompare(&sign, "ECLFRESP", 8)) throw Exception();
							socket->Read(&self, 8);
							socket->Read(&server, 8);
							if (self != self_addr || server != i.node_desc->ecf_address) throw Exception();
							auto addr = ConnectNode(socket, &i.node_desc->uuid);
							RunClientThread(socket, &i.node_desc->uuid, i.node_desc->ecf_address, i.node_desc->ip_address, i.node_desc->ip_port);
							SafePointer<DataBlock> net_data = SerializeNetDesc(*i.net_desc);
							InternalSendMessage(socket, 0x00000102, self_addr, i.node_desc->ecf_address, net_data);
							break;
						}
					} catch (...) {}
					net_sync->Open();
				}
				if (net_status != 1) break;
				for (int i = 0; i < 10; i++) {
					Sleep(1000);
					if (net_status != 1) break;
				}
			}
			ServiceShutdown();
			return 0;
		}
		int JoinThread(void * arg_ptr)
		{
			auto req = reinterpret_cast<JoinNetRequest *>(arg_ptr);
			try {
				SafePointer< Array<Network::AddressEntity> > addr = Network::GetAddressByHost(req->address_notation,
					req->port_to, Network::SocketAddressDomain::IPv6, Network::SocketProtocol::TCP);
				if (!addr || !addr->Length()) throw Exception();
				auto address = addr->FirstElement().EntityAddress;
				SafePointer<Socket> socket = CreateSocket(Network::SocketAddressDomain::IPv6, Network::SocketProtocol::TCP);
				socket->Connect(address, req->port_to);
				UUID zero;
				ZeroMemory(&zero, sizeof(zero));
				char sign[8];
				uint32 ot;
				ObjectAddress ecfa_server, ecfa_my;
				MemoryCopy(&sign, "ECLFAUTH", 8);
				ot = 0x01000000;
				socket->Write(&sign, 8);
				socket->Write(&ot, 4);
				socket->Write(&node_uuid, sizeof(UUID));
				socket->Write(&zero, sizeof(UUID));
				socket->Read(&sign, 8);
				socket->Read(&ecfa_my, 8);
				socket->Read(&ecfa_server, 8);
				if (MemoryCompare(&sign, "ECLFRESP", 8)) throw Exception();
				SafePointer<DataBlock> data = new DataBlock(1);
				data->SetLength(sizeof(Network::Address) + sizeof(uint16) + node_name.GetEncodedLength(Encoding::UTF8));
				MemoryCopy(data->GetBuffer(), &address, sizeof(address));
				MemoryCopy(data->GetBuffer() + sizeof(address), &req->port_my, sizeof(req->port_my));
				node_name.Encode(data->GetBuffer() + sizeof(address) + sizeof(uint16), Encoding::UTF8, false);
				InternalSendMessage(socket, 0x00000101, ecfa_my, ecfa_server, data);
				UUID new_net;
				while (true) {
					uint32 verb;
					ObjectAddress a1, a2;
					data.SetReference(0);
					InternalGetMessage(socket, verb, a1, a2, data.InnerRef());
					if (verb == 0x00010101) {
						socket->Shutdown(true, true);
						auto net = DeserializeNetDesc(data);
						net.autoconnect = false;
						net_sync->Wait();
						try {
							for (auto & n : known_nets) if (MemoryCompare(&n.uuid, &net.uuid, sizeof(UUID)) == 0) throw Exception();
							known_nets << net;
							config_alternated = true;
							net_sync->Open();
						} catch (...) { net_sync->Open(); throw; }
						MemoryCopy(&new_net, &net.uuid, sizeof(UUID));
						break;
					} else throw Exception();
				}
				for (auto & cb : event_callbacks) cb->NetJoined(&new_net);
				Windows::GetWindowSystem()->SubmitTask(req->on_success);
			} catch (...) {
				Windows::GetWindowSystem()->SubmitTask(req->on_fail);
			}
			delete req;
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
					NetDesc net_desc = DeserializeNetDesc(net_node);
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
						SerializeNetDesc(net_reg, net);
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
		void ServerNetJoin(const string & to, uint16 port_to, uint16 port_from, IDispatchTask * on_success, IDispatchTask * on_fail)
		{
			if (net_status) { on_fail->DoTask(0); return; }
			auto req = new (std::nothrow) JoinNetRequest;
			if (!req) { on_fail->DoTask(0); return; }
			try {
				req->address_notation = to;
				req->port_to = port_to;
				req->port_my = port_from;
				req->on_success.SetRetain(on_success);
				req->on_fail.SetRetain(on_fail);
			} catch (...) { delete req; on_fail->DoTask(0); return; }
			SafePointer<Thread> thread = CreateThread(JoinThread, req);
			if (!thread) { delete req; on_fail->DoTask(0); return; }
		}
		void ServerNetForget(const UUID * uuid)
		{
			net_sync->Wait();
			if (MemoryCompare(uuid, &net_uuid, sizeof(UUID)) && net_status != 2) {
				for (int i = 0; i < known_nets.Length(); i++) {
					if (MemoryCompare(uuid, &known_nets[i].uuid, sizeof(UUID)) == 0) {
						known_nets.Remove(i);
						for (auto & cb : event_callbacks) cb->NetLeft(uuid);
						config_alternated = true;
						break;
					}
				}
			}
			net_sync->Open();
		}
		void ServerNetNodeRemove(ObjectAddress node)
		{
			if (net_status == 1) {
				SafePointer<DataBlock> data;
				NetDesc * current_net = 0;
				net_sync->Wait();
				try {
					for (auto & n : known_nets) if (MemoryCompare(&n.uuid, &net_uuid, sizeof(UUID)) == 0) {
						current_net = &n;
						data = SerializeNetDesc(n);
						break;
					}
				} catch (...) { net_sync->Open(); return; }
				net_sync->Open();
				if (!data) return;
				try {
					auto net_edit = DeserializeNetDesc(data);
					bool edited = false;
					for (int i = 0; i < net_edit.nodes.Length(); i++) {
						if (net_edit.nodes[i].ecf_address == node) {
							net_edit.nodes.Remove(i);
							edited = true;
							break;
						}
					}
					if (edited) {
						net_edit.desc_time = Time::GetCurrentTime();
						data = SerializeNetDesc(net_edit);
						InternalSendMessage(0x00000103, self_addr, MakeObjectAddress(AddressLevelNode,
							AddressServiceNode, AddressNodeUndefined, AddressInstanceUnique), data);
						UpdateNetNodesWithNet(current_net, &net_edit);
					}
				} catch (...) {}
			}
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

		Array<EndpointDesc> * ServerEnumerateServices(const string & service_id)
		{
			// TODO: IMPLEMENT
			return 0;
		}
		ObjectAddress GetLoopbackAddress(void) { return self_addr; }
	}
}