#include "srvrsrvc.h"

namespace Engine
{
	namespace Cluster
	{
		namespace Formats
		{
			ENGINE_REFLECTED_CLASS(NodeInfoStruct, Reflection::Reflected)
				ENGINE_DEFINE_REFLECTED_PROPERTY_VOLUME(BYTE, uuid, 16);
				ENGINE_DEFINE_REFLECTED_PROPERTY(STRING, name);
				ENGINE_DEFINE_REFLECTED_PROPERTY(UINT32, system);
				ENGINE_DEFINE_REFLECTED_PROPERTY(UINT32, architecture);
				ENGINE_DEFINE_REFLECTED_PROPERTY(STRING, processor_name);
				ENGINE_DEFINE_REFLECTED_PROPERTY(UINT32, physical_cores);
				ENGINE_DEFINE_REFLECTED_PROPERTY(UINT32, virtual_cores);
				ENGINE_DEFINE_REFLECTED_PROPERTY(UINT64, clock_frequency);
				ENGINE_DEFINE_REFLECTED_PROPERTY(UINT64, physical_memory);
				ENGINE_DEFINE_REFLECTED_PROPERTY(UINT32, system_version_major);
				ENGINE_DEFINE_REFLECTED_PROPERTY(UINT32, system_version_minor);
			ENGINE_END_REFLECTED_CLASS
			ENGINE_REFLECTED_CLASS(NodeStatusStruct, Reflection::Reflected)
				ENGINE_DEFINE_REFLECTED_PROPERTY(UINT32, battery_status);
				ENGINE_DEFINE_REFLECTED_PROPERTY(UINT32, battery_level);
				ENGINE_DEFINE_REFLECTED_PROPERTY(UINT32, progress_complete);
				ENGINE_DEFINE_REFLECTED_PROPERTY(UINT32, progress_total);
			ENGINE_END_REFLECTED_CLASS
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
		}

		NodeStatusInfo::NodeStatusInfo(void) {}
		NodeStatusInfo::NodeStatusInfo(int) {}
		bool operator == (const NodeStatusInfo & a, const NodeStatusInfo & b)
		{
			return a.Battery == b.Battery && a.BatteryLevel == b.BatteryLevel &&
				a.ProgressComplete == b.ProgressComplete && a.ProgressTotal == b.ProgressTotal;
		}

		DataBlock * SerializeReflected(Reflection::Reflected & object)
		{
			Streaming::MemoryStream stream(0x10000);
			Reflection::SerializeToBinaryObject(object, &stream);
			stream.Seek(0, Streaming::Begin);
			return stream.ReadAll();
		}
		DataBlock * SerializeNodeInfo(const NodeSystemInfo & info)
		{
			Formats::NodeInfoStruct _info;
			MemoryCopy(&_info.uuid, &info.Identifier, sizeof(UUID));
			_info.name = info.NodeName;
			if (info.System == OperatingSystem::Windows) _info.system = 1;
			else if (info.System == OperatingSystem::MacOS) _info.system = 2;
			else _info.system = 0;
			if (info.Architecture == Platform::X86) _info.architecture = 1;
			else if (info.Architecture == Platform::X64) _info.architecture = 2;
			else if (info.Architecture == Platform::ARM) _info.architecture = 3;
			else if (info.Architecture == Platform::ARM64) _info.architecture = 4;
			else _info.architecture = 0;
			_info.processor_name = info.ProcessorName;
			_info.physical_cores = info.PhysicalCores;
			_info.virtual_cores = info.VirtualCores;
			_info.clock_frequency = info.ClockFrequency;
			_info.physical_memory = info.PhysicalMemory;
			_info.system_version_major = info.SystemVersionMajor;
			_info.system_version_minor = info.SystemVersionMinor;
			return SerializeReflected(_info);
		}
		DataBlock * SerializeNodeStatus(const NodeStatusInfo & info)
		{
			Formats::NodeStatusStruct _info;
			if (info.Battery == Power::BatteryStatus::NoBattery) _info.battery_status = 1;
			else if (info.Battery == Power::BatteryStatus::Charging) _info.battery_status = 2;
			else if (info.Battery == Power::BatteryStatus::InUse) _info.battery_status = 3;
			else _info.battery_status = 0;
			_info.battery_level = info.BatteryLevel;
			_info.progress_complete = info.ProgressComplete;
			_info.progress_total = info.ProgressTotal;
			return SerializeReflected(_info);
		}
		DataBlock * SerializeNodeEndpoints(const Array<EndpointDesc> & endpoints)
		{
			Formats::NodeEndpointsStruct _info;
			for (auto & e : endpoints) {
				_info.endpoints.AppendNew();
				auto & _e = _info.endpoints.InnerArray.LastElement();
				_e.address = e.address;
				_e.service_id = e.service_id;
				_e.service_name = e.service_name;
			}
			return SerializeReflected(_info);
		}
		template<class T> T DeserializeReflected(const DataBlock * data)
		{
			Streaming::MemoryStream stream(0x10000);
			stream.WriteArray(data);
			stream.Seek(0, Streaming::Begin);
			T t;
			Reflection::RestoreFromBinaryObject(t, &stream);
			return t;
		}
		NodeSystemInfo DeserializeNodeInfo(const DataBlock * data)
		{
			NodeSystemInfo info;
			auto _info = DeserializeReflected<Formats::NodeInfoStruct>(data);
			MemoryCopy(&info.Identifier, &_info.uuid, sizeof(UUID));
			info.NodeName = _info.name;
			if (_info.system == 1) info.System = OperatingSystem::Windows;
			else if (_info.system == 2) info.System = OperatingSystem::MacOS;
			else info.System = OperatingSystem::Unknown;
			if (_info.architecture == 1) info.Architecture = Platform::X86;
			else if (_info.architecture == 2) info.Architecture = Platform::X64;
			else if (_info.architecture == 3) info.Architecture = Platform::ARM;
			else if (_info.architecture == 4) info.Architecture = Platform::ARM64;
			else info.Architecture = Platform::Unknown;
			info.ProcessorName = _info.processor_name;
			info.PhysicalCores = _info.physical_cores;
			info.VirtualCores = _info.virtual_cores;
			info.ClockFrequency = _info.clock_frequency;
			info.PhysicalMemory = _info.physical_memory;
			info.SystemVersionMajor = _info.system_version_major;
			info.SystemVersionMinor = _info.system_version_minor;
			return info;
		}
		NodeStatusInfo DeserializeNodeStatus(const DataBlock * data)
		{
			NodeStatusInfo info;
			auto _info = DeserializeReflected<Formats::NodeStatusStruct>(data);
			if (_info.battery_status == 1) info.Battery = Power::BatteryStatus::NoBattery;
			else if (_info.battery_status == 2) info.Battery = Power::BatteryStatus::Charging;
			else if (_info.battery_status == 3) info.Battery = Power::BatteryStatus::InUse;
			else info.Battery = Power::BatteryStatus::Unknown;
			info.BatteryLevel = _info.battery_level;
			info.ProgressComplete = _info.progress_complete;
			info.ProgressTotal = _info.progress_total;
			return info;
		}
		Array<EndpointDesc> * DeserializeNodeEndpoints(const DataBlock * data)
		{
			SafePointer< Array<EndpointDesc> > result = new Array<EndpointDesc>(0x10);
			auto _info = DeserializeReflected<Formats::NodeEndpointsStruct>(data);
			for (auto & e : _info.endpoints) {
				EndpointDesc desc;
				desc.address = e.address;
				desc.service_name = e.service_name;
				desc.service_id = e.service_id;
				result->Append(desc);
			}
			result->Retain();
			return result;
		}
		NodeSystemInfo GetThisMachineSystemInfo(void)
		{
			NodeSystemInfo info;
			SystemDesc desc;
			GetSystemInformation(desc);
			GetServerUUID(&info.Identifier);
			info.NodeName = GetServerName();
			#ifdef ENGINE_WINDOWS
			info.System = OperatingSystem::Windows;
			#else
			#ifdef ENGINE_MACOSX
			info.System = OperatingSystem::MacOS;
			#else
			info.System = OperatingSystem::Unknown;
			#endif
			#endif
			info.Architecture = ApplicationPlatform;
			info.ProcessorName = desc.ProcessorName;
			info.PhysicalCores = desc.PhysicalCores;
			info.VirtualCores = desc.VirtualCores;
			info.ClockFrequency = desc.ClockFrequency;
			info.PhysicalMemory = desc.PhysicalMemory;
			info.SystemVersionMajor = desc.SystemVersionMajor;
			info.SystemVersionMinor = desc.SystemVersionMinor;
			return info;
		}

		class ServerServiceCallback : public IServerEventCallback, public IServerMessageCallback
		{
			SafePointer<Semaphore> sync;
			SafePointer<Thread> thread;
			Array<IServerServiceNotificationCallback *> callbacks;
			volatile bool active;
			uint32 progress_complete, progress_total;
			static int _server_service_thread(void * arg_ptr)
			{
				auto self = reinterpret_cast<ServerServiceCallback *>(arg_ptr);
				while (self->active) {
					self->sync->Wait();
					NodeStatusInfo status;
					status.Battery = Power::GetBatteryStatus();
					status.BatteryLevel = uint32(Power::GetBatteryChargeLevel() * 1000.0);
					status.ProgressComplete = self->progress_complete;
					status.ProgressTotal = self->progress_total;
					for (auto & cb : self->callbacks) cb->ProcessNodeStatusInfo(GetLoopbackAddress(), status);
					self->sync->Open();
					SafePointer<DataBlock> status_data = SerializeNodeStatus(status);
					ServerSendMessage(0x00000203, MakeObjectAddress(AddressLevelNode, AddressServiceNode, AddressNodeUndefined, AddressInstanceUnique), status_data);
					for (int i = 0; i < 10; i++) { if (!self->active) break; Sleep(1000); }
				}
				return 0;
			}
		public:
			ServerServiceCallback(void) : active(false), callbacks(0x10) { progress_complete = progress_total = 0; sync = CreateSemaphore(1); }
			~ServerServiceCallback(void) { if (thread) thread->Wait(); }
			virtual void SwitchedToNet(const UUID * uuid) override { active = true; thread = CreateThread(_server_service_thread, this); }
			virtual void SwitchingFromNet(const UUID * uuid) override { active = false; }
			virtual void SwitchedFromNet(const UUID * uuid) override {}
			virtual void NodeConnected(ObjectAddress node) override {}
			virtual void NodeDisconnected(ObjectAddress node) override {}
			virtual void NetTopologyChanged(const UUID * uuid) override {}
			virtual void NetJoined(const UUID * uuid) override {}
			virtual void NetLeft(const UUID * uuid) override {}
			virtual bool RespondsToMessage(uint32 verb) override
			{
				if (verb == 0x00000201) return true;
				else if (verb == 0x00010201) return true;
				else if (verb == 0x00000202) return true;
				else if (verb == 0x00010202) return true;
				else if (verb == 0x00000203) return true;
				else if (verb == 0x00000204) return true;
				else if (verb == 0x00010204) return true;
				else if (verb == 0x00000205) return true;
				else if (verb == 0x00000301) return true;
				else if (verb == 0x00000302) return true;
				else if (verb == 0x00000303) return true;
				else return false;
			}
			virtual void HandleMessage(ObjectAddress from, ObjectAddress to, uint32 verb, const DataBlock * data) override
			{
				try {
					if (verb == 0x00000201) {
						Formats::NodesStruct result;
						UUID current;
						if (GetServerCurrentNet(&current)) {
							SafePointer< Array<NodeDesc> > nodes = ServerEnumerateNetNodes(&current);
							for (auto & n : *nodes) {
								result.nodes.AppendNew();
								auto & nn = result.nodes.InnerArray.LastElement();
								nn.address = n.ecf_address;
								nn.name = n.known_name;
								nn.online = n.online;
							}
							SafePointer<DataBlock> responce = SerializeReflected(result);
							ServerSendMessage(0x00010201, from, responce);
						}
					} else if (verb == 0x00010201) {
					} else if (verb == 0x00000202) {
						auto service_id = data ? string(data->GetBuffer(), data->Length(), Encoding::UTF8) : string(L"");
						SafePointer< Array<EndpointDesc> > srvc = ServerEnumerateServices(service_id);
						SafePointer<DataBlock> responce = SerializeNodeEndpoints(*srvc);
						ServerSendMessage(0x00010202, from, responce);
					} else if (verb == 0x00010202) {
						SafePointer< Array<EndpointDesc> > srvc = DeserializeNodeEndpoints(data);
						sync->Wait();
						for (auto & cb : callbacks) cb->ProcessServicesList(from, *srvc);
						sync->Open();
					} else if (verb == 0x00000203) {
						auto status = DeserializeNodeStatus(data);
						sync->Wait();
						for (auto & cb : callbacks) cb->ProcessNodeStatusInfo(from, status);
						sync->Open();
					} else if (verb == 0x00000204) {
						SafePointer<DataBlock> responce = SerializeNodeInfo(GetThisMachineSystemInfo());
						ServerSendMessage(0x00010204, from, responce);
					} else if (verb == 0x00010204) {
						auto info = DeserializeNodeInfo(data);
						sync->Wait();
						for (auto & cb : callbacks) cb->ProcessNodeSystemInfo(from, info);
						sync->Open();
					} else if (verb == 0x00000205) {
						uint32 verb = 0;
						if (data->Length() >= 4) MemoryCopy(&verb, data->GetBuffer(), 4);
						sync->Wait();
						for (auto & cb : callbacks) cb->ProcessControlMessage(from, verb);
						sync->Open();
					} else if (verb == 0x00000301) {
						ServerSendMessage(0x00000302, MakeObjectAddress(AddressLevelNode, AddressServiceNode, AddressNodeUndefined, AddressInstanceUnique), data);
						ServerSendMessage(0x00000303, MakeObjectAddress(AddressLevelClient, AddressServiceTextLogger, GetAddressNode(GetLoopbackAddress()), AddressInstanceUnique), data);
					} else if (verb == 0x00000302) {
						ServerSendMessage(0x00000303, MakeObjectAddress(AddressLevelClient, AddressServiceTextLogger, GetAddressNode(GetLoopbackAddress()), AddressInstanceUnique), data);
					}
				} catch (...) {}
			}
			void RegisterCallback(IServerServiceNotificationCallback * callback)
			{
				sync->Wait();
				callbacks.Append(callback);
				sync->Open();
			}
			void UnregisterCallback(IServerServiceNotificationCallback * callback)
			{
				sync->Wait();
				for (int i = 0; i < callbacks.Length(); i++) if (callbacks[i] == callback) { callbacks.Remove(i); break; }
				sync->Open();
			}
		};
		ServerServiceCallback * server_service_callback = 0;
		void ServerServiceInitialize(void)
		{
			if (server_service_callback) return;
			server_service_callback = new ServerServiceCallback;
			RegisterServerEventCallback(server_service_callback);
			RegisterServerMessageCallback(server_service_callback);
		}
		void ServerServiceShutdown(void)
		{
			if (!server_service_callback) return;
			UnregisterServerEventCallback(server_service_callback);
			UnregisterServerMessageCallback(server_service_callback);
			delete server_service_callback;
			server_service_callback = 0;
		}
		void ServerServiceSendServicesRequest(ObjectAddress to) { ServerSendMessage(0x00000202, to, 0); }
		void ServerServiceSendInformationRequest(ObjectAddress to) { ServerSendMessage(0x00000204, to, 0); }
		void ServerServiceSendControlMessage(ObjectAddress to, uint control_verb)
		{
			SafePointer<DataBlock> data = new DataBlock(1);
			data->SetLength(4);
			MemoryCopy(data->GetBuffer(), &control_verb, 4);
			ServerSendMessage(0x00000205, to, data);
		}
		void ServerServiceRegisterCallback(IServerServiceNotificationCallback * callback) { if (server_service_callback) server_service_callback->RegisterCallback(callback); }
		void ServerServiceUnregisterCallback(IServerServiceNotificationCallback * callback) { if (server_service_callback) server_service_callback->UnregisterCallback(callback); }
	}
}