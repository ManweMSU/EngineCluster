#pragma once

#include <EngineRuntime.h>

namespace Engine
{
	namespace Cluster
	{
		constexpr uint16 ClusterServerDefaultPort = 10666;

		constexpr widechar * IdentifierServiceNode = L"engine.cluster.node";
		constexpr widechar * IdentifierServiceWorkClient = L"engine.cluster.employer";
		constexpr widechar * IdentifierServiceWorkHost = L"engine.cluster.worker";
		constexpr widechar * IdentifierServiceTextLogger = L"engine.cluster.logger";

		enum class ServiceClass { Unknown, Node, WorkClient, WorkHost, Logger };
		
		typedef uint64 ObjectAddress;

		struct EndpointDesc
		{
			ObjectAddress Address;
			string ServiceID;
			string ServiceName;
		};
		struct NodeDesc
		{
			ObjectAddress Address;
			string NodeName;
			bool Online;
		};
		struct NodeStatus
		{
			Power::BatteryStatus Battery;
			uint BatteryLevel;
			uint ProgressComplete;
			uint ProgressTotal;
		};

		Network::Address MakeNetworkAddress(const string & from);

		class IMessageCallback
		{
		public:
			virtual bool RespondsToMessage(uint32 verb) = 0;
			virtual void HandleMessage(ObjectAddress from, uint32 verb, const DataBlock * data) = 0;
			virtual void CallbackExpired(void) = 0;
		};
		class IEventCallback
		{
		public:
			virtual void ConnectionLost(void) = 0;
		};
		class Client : public Object
		{
			struct _message_handler {
				IMessageCallback * callback;
				uint32 expiration_time;
				bool run_once, expires;
			};

			Network::Address _ip;
			uint16 _port;
			ServiceClass _service;
			string _service_name, _service_id;
			IEventCallback * _callback;
			ObjectAddress _loopback, _node;
			SafePointer<Network::Socket> _socket;
			SafePointer<Semaphore> _sync;
			SafePointer<Thread> _dispatch;
			Array<_message_handler> _handlers;
			volatile bool _interrupt;

			static int _thread_proc(void * arg_ptr);
		public:
			Client(void);
			virtual ~Client(void) override;

			void SetConnectionIP(Network::Address ip);
			void SetConnectionPort(uint16 port);
			void SetConnectionServiceID(const string & id);
			void SetConnectionServiceName(const string & name);
			void SetConnectionService(ServiceClass service);
			void Connect(void);
			void Disconnect(void);
			void Wait(void);

			void SetEventCallback(IEventCallback * callback);
			IEventCallback * GetEventCallback(void);

			ObjectAddress GetSelfAddress(void) const;
			ObjectAddress GetNodeAddress(void) const;

			Array<NodeDesc> * EnumerateNodes(bool online_only = true);
			NodeStatus QueryNodeStatus(ObjectAddress node);
			Array<EndpointDesc> * EnumerateEndpoints(void);
			Array<EndpointDesc> * EnumerateEndpoints(const string & service_id);
			Array<EndpointDesc> * EnumerateEndpoints(ObjectAddress node_on);
			Array<EndpointDesc> * EnumerateEndpoints(ObjectAddress node_on, const string & service_id);
			Streaming::ITextWriter * CreateLoggingService(void);

			void SendMessage(uint32 verb, ObjectAddress to, const DataBlock * payload);
			void RegisterCallback(IMessageCallback * callback);
			void RegisterCallback(IMessageCallback * callback, uint32 expiration_time);
			void RegisterCallback(IMessageCallback * callback, uint32 expiration_time, bool run_once);
			void RegisterOneTimeCallback(IMessageCallback * callback);
			void RegisterOneTimeCallback(IMessageCallback * callback, uint32 expiration_time);
			void UnregisterCallback(IMessageCallback * callback);
		};
	}
}