#include "ClusterClient.h"

namespace Engine
{
	namespace Cluster
	{
		namespace Formats
		{
			ENGINE_REFLECTED_CLASS(NodeEndpointStruct, Reflection::Reflected)
				ENGINE_DEFINE_REFLECTED_PROPERTY(UINT64, address);
				ENGINE_DEFINE_REFLECTED_PROPERTY(STRING, service_id);
				ENGINE_DEFINE_REFLECTED_PROPERTY(STRING, service_name);
			ENGINE_END_REFLECTED_CLASS
			ENGINE_REFLECTED_CLASS(NodeEndpointsStruct, Reflection::Reflected)
				ENGINE_DEFINE_REFLECTED_GENERIC_ARRAY(NodeEndpointStruct, endpoints);
			ENGINE_END_REFLECTED_CLASS
			ENGINE_REFLECTED_CLASS(NodeStruct, Reflection::Reflected)
				ENGINE_DEFINE_REFLECTED_PROPERTY(UINT64, address);
				ENGINE_DEFINE_REFLECTED_PROPERTY(STRING, name);
				ENGINE_DEFINE_REFLECTED_PROPERTY(BOOLEAN, online);
			ENGINE_END_REFLECTED_CLASS
			ENGINE_REFLECTED_CLASS(NodesStruct, Reflection::Reflected)
				ENGINE_DEFINE_REFLECTED_GENERIC_ARRAY(NodeStruct, nodes);
			ENGINE_END_REFLECTED_CLASS
			ENGINE_REFLECTED_CLASS(NodeStatusStruct, Reflection::Reflected)
				ENGINE_DEFINE_REFLECTED_PROPERTY(UINT32, battery_status);
				ENGINE_DEFINE_REFLECTED_PROPERTY(UINT32, battery_level);
				ENGINE_DEFINE_REFLECTED_PROPERTY(UINT32, progress_complete);
				ENGINE_DEFINE_REFLECTED_PROPERTY(UINT32, progress_total);
			ENGINE_END_REFLECTED_CLASS
		}

		Network::Address MakeNetworkAddress(const string & from)
		{
			if (from.FindFirst(L'.') != -1) {
				auto parts = from.Split(L'.');
				if (parts.Length() != 4) throw InvalidArgumentException();
				uint32 p1 = parts[0].ToUInt32();
				uint32 p2 = parts[1].ToUInt32();
				uint32 p3 = parts[2].ToUInt32();
				uint32 p4 = parts[3].ToUInt32();
				if (p1 > 0xFF || p2 > 0xFF || p3 > 0xFF || p4 > 0xFF) throw InvalidArgumentException();
				return Network::Address::CreateIPv4((p1 << 24) | (p2 << 16) | (p3 << 8) | p4);
			} else if (from.FindFirst(L':') != -1) {
				auto parts = from.Split(L':');
				int zeros_index = -1;
				for (int i = 0; i < parts.Length(); i++) if (!parts[i].Length()) {
					zeros_index = i;
					for (int j = i + 1; j < parts.Length(); j++) if (!parts[j].Length()) {
						parts.Remove(j);
						j--;
					}
					parts.Remove(i);
					break;
				}
				if (parts.Length() > 8) throw InvalidArgumentException();
				if (parts.Length() < 8 && zeros_index == -1) throw InvalidArgumentException();
				if (zeros_index >= 0) while (parts.Length() < 8) parts.Insert(L"0", zeros_index);
				Array<uint32> words(0x8);
				for (auto & p : parts) {
					uint32 w = p.ToUInt32(HexadecimalBase);
					if (w > 0xFFFF) throw InvalidArgumentException();
					words << w;
				}
				return Network::Address::CreateIPv6((words[6] << 16) | words[7], (words[4] << 16) | words[5], (words[2] << 16) | words[3], (words[0] << 16) | words[1]);
			} else throw InvalidArgumentException();
		}

		class RemoteLogger : public Streaming::ITextWriter
		{
			SafePointer<Client> _client;
			SafePointer<DynamicString> _buffer;
			ObjectAddress _self, _node;

			void _send(const string & line) const
			{
				SafePointer<DataBlock> data = new DataBlock(1);
				Time time = Time::GetCurrentTime();
				data->SetLength(sizeof(_self) + sizeof(time) + line.GetEncodedLength(Encoding::UTF8));
				MemoryCopy(data->GetBuffer(), &_self, sizeof(_self));
				MemoryCopy(data->GetBuffer() + sizeof(_self), &time, sizeof(time));
				line.Encode(data->GetBuffer() + sizeof(_self) + sizeof(time), Encoding::UTF8, false);
				_client->SendMessage(0x00000301, _node, data);
			}
			void _drain(void) const
			{
				int pos_from = 0;
				for (int i = 0; i < _buffer->Length(); i++) if (_buffer->CharAt(i) == L'\n') {
					DynamicString fragment;
					for (int j = pos_from; j < i; j++) {
						if (_buffer->CharAt(j) >= 32 || _buffer->CharAt(j) == L'\t') fragment << _buffer->CharAt(j);
					}
					_send(fragment.ToString());
					pos_from = i + 1;
				}
				if (pos_from) _buffer->RemoveRange(0, pos_from);
			}
		public:
			RemoteLogger(Client * client)
			{
				_buffer = new DynamicString;
				_client.SetRetain(client);
				_self = client->GetSelfAddress();
				_node = client->GetNodeAddress();
			}
			virtual ~RemoteLogger(void) override { if (_buffer->Length()) try { _send(_buffer->ToString()); } catch (...) {} }
			virtual void Write(const string & text) const override
			{
				_buffer->Concatenate(text);
				_drain();
			}
			virtual void WriteLine(const string & text) const override
			{
				_buffer->Concatenate(text);
				_buffer->Concatenate(L'\n');
				_drain();
			}
			virtual void WriteEncodingSignature(void) const override {}
		};

		int Client::_thread_proc(void * arg_ptr)
		{
			auto self = reinterpret_cast<Client *>(arg_ptr);
			while (true) {
				if (self->_interrupt) {
					self->_sync->Wait();
					if (self->_callback) self->_callback->ConnectionLost();
					self->_sync->Open();
					break;
				}
				try {
					if (self->_socket->Wait(1000)) {
						if (self->_interrupt) throw Exception();
						uint32 length, verb;
						ObjectAddress from, to;
						self->_socket->Read(&verb, 4);
						self->_socket->Read(&length, 4);
						self->_socket->Read(&to, 8);
						self->_socket->Read(&from, 8);
						SafePointer<DataBlock> block = new DataBlock(0x100);
						block->SetLength(length);
						if (length) self->_socket->Read(block->GetBuffer(), length);
						self->_sync->Wait();
						for (int i = 0; i < self->_handlers.Length(); i++) if (self->_handlers[i].callback->RespondsToMessage(verb)) {
							self->_handlers[i].callback->HandleMessage(from, verb, block);
							if (self->_handlers[i].run_once) self->_handlers.Remove(i);
							break;
						}
						self->_sync->Open();
					}
					self->_sync->Wait();
					uint32 now = GetTimerValue();
					for (int i = 0; i < self->_handlers.Length(); i++) if (self->_handlers[i].expires) {
						int pending = self->_handlers[i].expiration_time - now;
						if (pending < 0) {
							self->_handlers[i].callback->CallbackExpired();
							self->_handlers.Remove(i);
							i--;
						}
					}
					self->_sync->Open();
				} catch (...) {
					self->_sync->Wait();
					if (self->_callback) self->_callback->ConnectionLost();
					self->_sync->Open();
					break;
				}
			}
			self->_sync->Wait();
			for (int i = 0; i < self->_handlers.Length(); i++) if (self->_handlers[i].expires) {
				self->_handlers[i].callback->CallbackExpired();
				self->_handlers.Remove(i);
				i--;
			}
			self->_sync->Open();
			return 0;
		}
		Client::Client(void) : _callback(0), _loopback(0), _node(0), _handlers(0x10), _interrupt(false)
		{
			_ip = Network::Address::CreateLoopBackIPv6();
			_port = ClusterServerDefaultPort;
			_service = ServiceClass::Unknown;
			_service_name = _service_id = L"";
		}
		Client::~Client(void) { Disconnect(); Wait(); }
		void Client::SetConnectionIP(Network::Address ip) { if (!_sync) _ip = ip; }
		void Client::SetConnectionPort(uint16 port) { if (!_sync) _port = port; }
		void Client::SetConnectionServiceID(const string & id) { if (!_sync) _service_id = id; }
		void Client::SetConnectionServiceName(const string & name) { if (!_sync) _service_name = name; }
		void Client::SetConnectionService(ServiceClass service) { if (!_sync) _service = service; }
		void Client::Connect(void)
		{
			if (_service == ServiceClass::Unknown && (!_service_id.Length() || !_service_name.Length())) throw InvalidArgumentException();
			if (_service != ServiceClass::Unknown && (_service_id.Length() || _service_name.Length())) throw InvalidArgumentException();
			_sync = CreateSemaphore(1);
			if (!_sync) throw Exception();
			_socket = Network::CreateSocket(Network::SocketAddressDomain::IPv6, Network::SocketProtocol::TCP);
			try {
				_socket->Connect(_ip, _port);
				SafePointer<DataBlock> name_data, id_data;
				char sign[8];
				uint32 ot;
				MemoryCopy(&sign, "ECLFAUTH", 8);
				if (_service != ServiceClass::Unknown) {
					if (_service == ServiceClass::WorkClient) ot = 0x02000001;
					else if (_service == ServiceClass::WorkHost) ot = 0x02000002;
					else if (_service == ServiceClass::Logger) ot = 0x02000003;
					else throw InvalidArgumentException();
				} else {
					ot = 0x02800000;
					name_data = _service_name.EncodeSequence(Encoding::UTF8, false);
					id_data = _service_id.EncodeSequence(Encoding::UTF8, false);
					if (name_data->Length() > 0xFFF) throw InvalidArgumentException();
					if (id_data->Length() > 0xFF) throw InvalidArgumentException();
					ot |= name_data->Length() << 8;
					ot |= id_data->Length();
				}
				_socket->Write(&sign, 8);
				_socket->Write(&ot, 4);
				if (id_data) _socket->WriteArray(id_data);
				if (name_data) _socket->WriteArray(name_data);
				_socket->Read(&sign, 8);
				_socket->Read(&_loopback, 8);
				_socket->Read(&_node, 8);
				if (MemoryCompare(&sign, "ECLFRESP", 8)) throw Exception();
				_dispatch = CreateThread(_thread_proc, this);
				if (!_dispatch) throw Exception();
			} catch (...) { _socket.SetReference(0); _sync.SetReference(0); _loopback = _node = 0; throw; }
		}
		void Client::Disconnect(void) { _interrupt = true; if (_socket) _socket->Shutdown(true, true); }
		void Client::Wait(void) { if (_dispatch) _dispatch->Wait(); }
		void Client::SetEventCallback(IEventCallback * callback) { if (!_sync) _callback = callback; }
		IEventCallback * Client::GetEventCallback(void) { return _callback; }
		ObjectAddress Client::GetSelfAddress(void) const { return _loopback; }
		ObjectAddress Client::GetNodeAddress(void) const { return _node; }
		Array<NodeDesc> * Client::EnumerateNodes(bool online_only)
		{
			if (!_sync) throw InvalidStateException();
			class _node_enum : public IMessageCallback
			{
				bool _online_only;
				SafePointer<Semaphore> _sync;
				SafePointer< Array<NodeDesc> > _result;
			public:
				_node_enum(bool online_only) : _online_only(online_only) { _sync = CreateSemaphore(0); if (!_sync) throw Exception(); }
				virtual bool RespondsToMessage(uint32 verb) override { return verb == 0x00010201; }
				virtual void HandleMessage(ObjectAddress from, uint32 verb, const DataBlock * data) override
				{
					try {
						Streaming::MemoryStream stream(0x1000);
						stream.WriteArray(data);
						stream.Seek(0, Streaming::Begin);
						Formats::NodesStruct nodes;
						Reflection::RestoreFromBinaryObject(nodes, &stream);
						_result = new Array<NodeDesc>(0x10);
						for (auto & n : nodes.nodes) {
							if (_online_only && !n.online) continue;
							NodeDesc desc;
							desc.Address = n.address;
							desc.NodeName = n.name;
							desc.Online = n.online;
							_result->Append(desc);
						}
					} catch (...) { _result.SetReference(0); }
					_sync->Open();
				}
				virtual void CallbackExpired(void) override { _sync->Open(); }
				Array<NodeDesc> * Send(Client * client)
				{
					client->RegisterOneTimeCallback(this, 2000);
					client->SendMessage(0x00000201, client->GetNodeAddress(), 0);
					_sync->Wait();
					if (_result) {
						_result->Retain();
						return _result;
					} else throw Exception();
				}
			};
			_node_enum enumerator(online_only);
			return enumerator.Send(this);
		}
		NodeStatus Client::QueryNodeStatus(ObjectAddress node)
		{
			if (!_sync) throw InvalidStateException();
			class _node_status : public IMessageCallback
			{
				SafePointer<Semaphore> _sync;
				NodeStatus _result;
			public:
				_node_status(void) { _sync = CreateSemaphore(0); if (!_sync) throw Exception(); _result.ProgressComplete = 0xFFFFFFFF; }
				virtual bool RespondsToMessage(uint32 verb) override { return verb == 0x00000203; }
				virtual void HandleMessage(ObjectAddress from, uint32 verb, const DataBlock * data) override
				{
					try {
						Streaming::MemoryStream stream(0x1000);
						stream.WriteArray(data);
						stream.Seek(0, Streaming::Begin);
						Formats::NodeStatusStruct status;
						Reflection::RestoreFromBinaryObject(status, &stream);
						if (status.battery_status == 1) _result.Battery = Power::BatteryStatus::NoBattery;
						else if (status.battery_status == 2) _result.Battery = Power::BatteryStatus::Charging;
						else if (status.battery_status == 3) _result.Battery = Power::BatteryStatus::InUse;
						else _result.Battery = Power::BatteryStatus::Unknown;
						_result.BatteryLevel = status.battery_level;
						_result.ProgressComplete = status.progress_complete;
						_result.ProgressTotal = status.progress_total;
					} catch (...) { _result.ProgressComplete = 0xFFFFFFFF; }
					_sync->Open();
				}
				virtual void CallbackExpired(void) override { _sync->Open(); }
				NodeStatus Send(Client * client, ObjectAddress node)
				{
					client->RegisterOneTimeCallback(this, 2000);
					client->SendMessage(0x00000206, node, 0);
					_sync->Wait();
					if (_result.ProgressComplete != 0xFFFFFFFF) return _result; else throw Exception();
				}
			};
			_node_status status;
			return status.Send(this, node);
		}
		Array<EndpointDesc> * Client::EnumerateEndpoints(void) { return EnumerateEndpoints(0, L""); }
		Array<EndpointDesc> * Client::EnumerateEndpoints(const string & service_id) { return EnumerateEndpoints(0, service_id); }
		Array<EndpointDesc> * Client::EnumerateEndpoints(ObjectAddress node_on) { return EnumerateEndpoints(node_on, L""); }
		Array<EndpointDesc> * Client::EnumerateEndpoints(ObjectAddress node_on, const string & service_id)
		{
			class _srvc_enum : public IMessageCallback
			{
				SafePointer<Semaphore> _sync;
				SafePointer< Array<EndpointDesc> > _result;
				SafePointer<DataBlock> _request;
				uint32 _wait_counter;
			public:
				_srvc_enum(const string & id) : _wait_counter(1)
				{
					_sync = CreateSemaphore(1);
					if (!_sync) throw Exception();
					_result = new Array<EndpointDesc>(0x10);
					_request = id.EncodeSequence(Encoding::UTF8, false);
				}
				virtual bool RespondsToMessage(uint32 verb) override { return verb == 0x00010202; }
				virtual void HandleMessage(ObjectAddress from, uint32 verb, const DataBlock * data) override
				{
					try {
						Streaming::MemoryStream stream(0x1000);
						stream.WriteArray(data);
						stream.Seek(0, Streaming::Begin);
						Formats::NodeEndpointsStruct endpoints;
						Reflection::RestoreFromBinaryObject(endpoints, &stream);
						for (auto & e : endpoints.endpoints) {
							EndpointDesc desc;
							desc.Address = e.address;
							desc.ServiceName = e.service_name;
							desc.ServiceID = e.service_id;
							_result->Append(desc);
						}
					} catch (...) { _result.SetReference(0); }
					_sync->Open();
				}
				virtual void CallbackExpired(void) override { _sync->Open(); }
				void Enumerate(Client * client, ObjectAddress node)
				{
					client->RegisterOneTimeCallback(this, 2000);
					client->SendMessage(0x00000202, node, _request);
					InterlockedIncrement(_wait_counter);
				}
				Array<EndpointDesc> * Finalize(void)
				{
					do { _sync->Wait(); } while (InterlockedDecrement(_wait_counter));
					_result->Retain();
					return _result;
				}
			};
			_srvc_enum enumerator(service_id);
			if (node_on) {
				enumerator.Enumerate(this, node_on);
			} else {
				SafePointer< Array<NodeDesc> > nodes = EnumerateNodes();
				for (auto & n : *nodes) enumerator.Enumerate(this, n.Address);
			}
			return enumerator.Finalize();
		}
		Streaming::ITextWriter * Client::CreateLoggingService(void)
		{
			if (!_sync) throw InvalidStateException();
			return new RemoteLogger(this);
		}
		void Client::SendMessage(uint32 verb, ObjectAddress to, const DataBlock * payload)
		{
			if (!_socket || !_sync) throw InvalidStateException();
			uint32 length = payload ? payload->Length() : 0;
			_sync->Wait();
			try {
				_socket->Write(&verb, 4);
				_socket->Write(&length, 4);
				_socket->Write(&to, 8);
				_socket->Write(&_loopback, 8);
				if (payload) _socket->WriteArray(payload);
			} catch (...) { _sync->Open(); Disconnect(); throw; }
			_sync->Open();
		}
		void Client::RegisterCallback(IMessageCallback * callback) { RegisterCallback(callback, 0, false); }
		void Client::RegisterCallback(IMessageCallback * callback, uint32 expiration_time) { RegisterCallback(callback, expiration_time, false); }
		void Client::RegisterCallback(IMessageCallback * callback, uint32 expiration_time, bool run_once)
		{
			if (_sync) _sync->Wait();
			try {
				_message_handler hdlr;
				hdlr.callback = callback;
				hdlr.expiration_time = GetTimerValue() + expiration_time;
				hdlr.run_once = run_once;
				hdlr.expires = expiration_time != 0;
				_handlers << hdlr;
			} catch (...) { if (_sync) _sync->Open(); throw; }
			if (_sync) _sync->Open();
		}
		void Client::RegisterOneTimeCallback(IMessageCallback * callback) { RegisterCallback(callback, 0, true); }
		void Client::RegisterOneTimeCallback(IMessageCallback * callback, uint32 expiration_time) { RegisterCallback(callback, expiration_time, true); }
		void Client::UnregisterCallback(IMessageCallback * callback)
		{
			if (_sync) _sync->Wait();
			for (int i = 0; i < _handlers.Length(); i++) if (_handlers[i].callback == callback) { _handlers.Remove(i); break; }
			if (_sync) _sync->Open();
		}
	}
}