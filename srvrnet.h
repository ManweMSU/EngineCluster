#pragma once

#include <EngineRuntime.h>

namespace Engine
{
	namespace Cluster
	{
		constexpr uint16 ClusterServerDefaultPort = 10666;

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

		class IServerEventCallback
		{
		public:
			virtual void SwitchedToNet(const UUID * uuid) = 0;
			virtual void SwitchingFromNet(const UUID * uuid) = 0;
			virtual void SwitchedFromNet(const UUID * uuid) = 0;
			virtual void NodeConnected(ObjectAddress node) = 0;
			virtual void NodeDisconnected(ObjectAddress node) = 0;
			virtual void NetTopologyChanged(const UUID * uuid) = 0;
			virtual void NetJoined(const UUID * uuid) = 0;
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

		bool ServerInitialize(void);
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
		void ServerNetJoin(Network::Address to, uint16 port_to, uint16 port_from);
		void ServerNetLeave(void);
		void ServerNetForget(const UUID * uuid);
		bool ServerNetNodeRemove(ObjectAddress node);

		Array<UUID> * ServerEnumerateKnownNets(void);
		bool GetServerCurrentNet(UUID * uuid);
		string GetServerNetName(UUID * uuid);
		bool GetServerNetAutoconnectStatus(UUID * uuid);
		void ServerNetMakeAutoconnect(UUID * uuid);
		Array<NodeDesc> * ServerEnumerateNetNodes(UUID * uuid);

		void RegisterServerMessageCallback(IServerMessageCallback * callback);
		void UnregisterServerMessageCallback(IServerMessageCallback * callback);
		bool ServerSendMessage(uint32 verb, ObjectAddress to, const DataBlock * payload);
	}
}