#pragma once

#include <EngineRuntime.h>

namespace Engine
{
	namespace Cluster
	{
		constexpr uint16 ClusterServerDefaultPort = 10666;
		constexpr uint32 AddressLevelFailure = 0x00;
		constexpr uint32 AddressLevelNode = 0x01;
		constexpr uint32 AddressLevelClient = 0x02;
		constexpr uint32 AddressServiceNode = 0x000000;
		constexpr uint32 AddressServiceWorkClient = 0x000001;
		constexpr uint32 AddressServiceWorkHost = 0x000002;
		constexpr uint32 AddressServiceTextLogger = 0x000003;
		constexpr uint32 AddressServiceUserSpaceBase = 0x000004;
		constexpr uint32 AddressNodeUndefined = 0x0000;
		constexpr uint32 AddressNodeBase = 0x0001;
		constexpr uint32 AddressInstanceUnique = 0x0000;
		constexpr uint32 AddressInstanceBase = 0x0001;

		constexpr widechar * IdentifierServiceNode = L"engine.cluster.node";
		constexpr widechar * IdentifierServiceWorkClient = L"engine.cluster.employer";
		constexpr widechar * IdentifierServiceWorkHost = L"engine.cluster.worker";
		constexpr widechar * IdentifierServiceTextLogger = L"engine.cluster.logger";

		typedef uint64 ObjectAddress;
		struct UUID { uint8 bytes[16]; };
		struct NodeDesc {
			UUID uuid;
			string known_name;
			ObjectAddress ecf_address;
			Network::Address ip_address;
			uint16 ip_port;
			bool online;
		};
		struct NetDesc
		{
			Time desc_time;
			UUID uuid;
			string name;
			bool autoconnect;
			SafeArray<NodeDesc> nodes = SafeArray<NodeDesc>(0x10);
		};
		struct EndpointDesc
		{
			ObjectAddress address;
			string service_id;
			string service_name;
		};

		class IServerEventCallback
		{
		public:
			// This node started the server services locally
			virtual void SwitchedToNet(const UUID * uuid) = 0;
			// This node has begun to shut down its server services
			virtual void SwitchingFromNet(const UUID * uuid) = 0;
			// This node has shutted down its server services
			virtual void SwitchedFromNet(const UUID * uuid) = 0;
			// A node within the current net has connected
			virtual void NodeConnected(ObjectAddress node) = 0;
			// A node within the current net has disconnected
			virtual void NodeDisconnected(ObjectAddress node) = 0;
			// A node with UUID specified has joined or left the current net
			virtual void NetTopologyChanged(const UUID * uuid) = 0;
			// A join request sent was accepted, the joined net UUID is the following
			virtual void NetJoined(const UUID * uuid) = 0;
			// A net with the UUID specified (the current net) must be forgotten
			virtual void NetLeft(const UUID * uuid) = 0;
		};
		class IServerMessageCallback
		{
		public:
			virtual bool RespondsToMessage(uint32 verb) = 0;
			virtual void HandleMessage(ObjectAddress from, ObjectAddress to, uint32 verb, const DataBlock * data) = 0;
		};

		string MakeStringOfUUID(const UUID * uuid);
		bool MakeUUIDOfString(const string & text, UUID * uuid);
		bool IsZeroUUID(const UUID * uuid);

		ObjectAddress MakeObjectAddress(uint32 level, uint32 service, uint32 node, uint32 instance);
		uint32 GetAddressLevel(ObjectAddress address);
		uint32 GetAddressService(ObjectAddress address);
		uint32 GetAddressNode(ObjectAddress address);
		uint32 GetAddressInstance(ObjectAddress address);

		bool ServerInitialize(UI::InterfaceTemplate * interface = 0);
		void ServerPerformAutoconnect(void);
		void ServerShutdown(void);

		void SetServerName(const string & name);
		void SetServerUUID(const UUID * uuid);
		void RegisterServerEventCallback(IServerEventCallback * callback);
		void UnregisterServerEventCallback(IServerEventCallback * callback);
		string GetServerName(void);
		void GetServerUUID(UUID * uuid);
		uint16 GetServerCurrentPort(void);

		bool ServerNetCreate(const string & name, const UUID * uuid, uint16 port);
		bool ServerNetConnect(const UUID * uuid);
		void ServerNetDisconnect(void);
		bool ServerNetAllowJoin(bool allow);
		void ServerNetJoin(const string & to, uint16 port_to, uint16 port_from, IDispatchTask * on_success, IDispatchTask * on_fail);
		void ServerNetForget(const UUID * uuid);
		void ServerNetNodeRemove(ObjectAddress node);

		Array<UUID> * ServerEnumerateKnownNets(void);
		bool GetServerCurrentNet(UUID * uuid);
		string GetServerNetName(const UUID * uuid);
		bool GetServerNetAutoconnectStatus(const UUID * uuid);
		void ServerNetMakeAutoconnect(const UUID * uuid);
		Array<NodeDesc> * ServerEnumerateNetNodes(const UUID * uuid);

		void RegisterServerMessageCallback(IServerMessageCallback * callback);
		void UnregisterServerMessageCallback(IServerMessageCallback * callback);
		bool ServerSendMessage(uint32 verb, ObjectAddress to, const DataBlock * payload);

		Array<EndpointDesc> * ServerEnumerateServices(const string & service_id);
		ObjectAddress GetLoopbackAddress(void);
	}
}