#pragma once

#include "ClusterTaskBase.h"
#include "ClusterClient.h"

namespace Engine
{
	namespace Cluster
	{
		struct TaskHostDesc
		{
			ObjectAddress NodeAddress;
			ObjectAddress HostAddress;
			uint32 TasksComplete;
			uint32 TasksTotal;
			uint32 HostStatus;
		};

		class TaskClient : public Object
		{
		public:
			TaskClient(void);
			virtual ~TaskClient(void) override;

			void SetConnectionIP(Network::Address ip);
			void SetConnectionPort(uint16 port);
			void SetTaskExecutable(DataBlock * data);
			void SetTaskInput(DataBlock * data);
			void SetTaskInput(Reflection::Reflected & data);
			void Start(void);

			Client * GetClusterClient(void);
			int GetHostCount(void);
			void GetHostInfo(int range_min, int count, TaskHostDesc * desc);
			void GetProgressInfo(uint32 * tasks_complete, uint32 * tasks_total);

			bool IsFinished(void);
			void Interrupt(void);
			void Wait(void);
			
			uint32 GetTaskResultStatus(void);
			void GetTaskResult(DataBlock ** data);
			void GetTaskResult(Reflection::Reflected & data);
		};
	}
}