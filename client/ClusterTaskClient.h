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

		class TaskClient : public Object, IEventCallback, IMessageCallback
		{
			struct _host_info
			{
				ObjectAddress _node, _host;
				uint32 _complete, _total, _status;
				Time _last_message;
			};

			SafePointer<Client> _client;
			SafePointer<Semaphore> _sync, _finish;
			SafePointer<DataBlock> _exec, _input, _output;
			SafePointer<Thread> _watchdog_thread;
			SafePointer<TaskQueue> _queue;
			Array<_host_info> _hosts;
			uint32 _status, _complete, _total;

			virtual void ConnectionLost(void) override;
			virtual bool RespondsToMessage(uint32 verb) override;
			virtual void HandleMessage(ObjectAddress from, uint32 verb, const DataBlock * data) override;
			virtual void CallbackExpired(void) override;
			static int _watchdog_proc(void * arg_ptr);
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