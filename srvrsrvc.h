#pragma once

#include "client/ClusterTaskBase.h"
#include "srvrnet.h"

namespace Engine
{
	namespace Cluster
	{
		enum class OperatingSystem { Unknown, Windows, MacOS };
		enum ServerControlFlag {
			ServerControlShutdownPC = 0x0001,
			ServerControlRestartPC = 0x0002,
			ServerControlLogoutPC = 0x0004,
			ServerControlHybernatePC = 0x0008,
			ServerControlShutdownSoftware = 0x0010,
		};

		struct NodeSystemInfo {
			UUID Identifier;
			string NodeName;
			OperatingSystem System;
			Platform Architecture;
			string ProcessorName;
			uint PhysicalCores;
			uint VirtualCores;
			uint64 ClockFrequency;
			uint64 PhysicalMemory;
			uint SystemVersionMajor;
			uint SystemVersionMinor;
		};
		struct NodeStatusInfo {
			NodeStatusInfo(void);
			NodeStatusInfo(int);
			bool friend operator == (const NodeStatusInfo & a, const NodeStatusInfo & b);
			
			Power::BatteryStatus Battery;
			uint BatteryLevel;
			uint ProgressComplete;
			uint ProgressTotal;
		};

		class IServerServiceNotificationCallback
		{
		public:
			virtual void ProcessServicesList(ObjectAddress from, const Array<EndpointDesc> & endpoints) = 0;
			virtual void ProcessNodeSystemInfo(ObjectAddress from, const NodeSystemInfo & info) = 0;
			virtual void ProcessNodeStatusInfo(ObjectAddress from, const NodeStatusInfo & info) = 0;
			virtual void ProcessControlMessage(ObjectAddress from, uint control_verb) = 0;
		};

		void ServerServiceInitialize(void);
		void ServerServiceShutdown(void);

		void ServerServiceSendServicesRequest(ObjectAddress to);
		void ServerServiceSendInformationRequest(ObjectAddress to);
		void ServerServiceSendControlMessage(ObjectAddress to, uint control_verb);
		void ServerServiceUpdateProgressStatus(uint32 complete, uint32 total);
		void ServerServiceTerminateHost(void);
		void ServerServiceAllowTasks(bool allow);
		void ServerServiceAllowTasks(ObjectAddress node_on, bool allow);
		void ServerServiceRegisterCallback(IServerServiceNotificationCallback * callback);
		void ServerServiceUnregisterCallback(IServerServiceNotificationCallback * callback);
	}
}