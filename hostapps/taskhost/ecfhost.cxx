#include <EngineRuntime.h>

#include "../../client/ClusterClient.h"
#include "../../client/ClusterTaskInterfaces.h"

using namespace Engine;
using namespace Engine::IO;
using namespace Engine::Streaming;
using namespace Engine::Cluster;

typedef ITaskFactory * (* func_CreateTaskFactory) (void);
constexpr uint32 TaskFlagShutdownThread = 0x80000000;

volatile bool interrupt;
handle dl;
SafePointer<Client> client;
SafePointer<IHostEnvironment> environment;

struct TaskRecord
{
public:
	SafePointer<ITask> Task;
	uint32 Flags;
};
class SpreadedThreadPool : public Object
{
	Volumes::Queue<TaskRecord> _queue;
	SafePointer<Semaphore> _sync, _counter;
	ObjectArray<Thread> _workers;
	uint32 _workers_active, _tasks_submitted, _tasks_complete, _tasks_pending, _tasks_pending_moveble;
	static int _thread_proc(void * arg_ptr)
	{
		auto self = reinterpret_cast<SpreadedThreadPool *>(arg_ptr);
		while (true) {
			self->_counter->Wait();
			self->_sync->Wait();
			if (self->_queue.GetFirst()->GetValue().Flags & TaskFlagShutdownThread) {
				self->_counter->Open();
				self->_sync->Open();
				return 0;
			}
			auto task = self->_queue.Pop();
			self->_tasks_pending--;
			if (task.Flags & TaskFlagAllowMigration) self->_tasks_pending_moveble--;
			InterlockedIncrement(self->_workers_active);
			self->_sync->Open();
			if (task.Task) {
				task.Task->DoTask(environment);
				task.Task.SetReference(0);
			}
			InterlockedDecrement(self->_workers_active);
			if (task.Flags & TaskFlagProgressAccount) InterlockedIncrement(self->_tasks_complete);
		}
		return 0;
	}
public:
	SpreadedThreadPool(void) : _workers(0x20), _workers_active(0), _tasks_submitted(0), _tasks_complete(0), _tasks_pending(0), _tasks_pending_moveble(0)
	{
		_sync = CreateSemaphore(1);
		_counter = CreateSemaphore(0);
		if (!_sync || !_counter) throw OutOfMemoryException();
		auto num_thread = GetProcessorsNumber();
		for (int i = 0; i < num_thread; i++) {
			SafePointer<Thread> thread = CreateThread(_thread_proc, this);
			if (!thread) ExitProcess(0xFF);
			_workers.Append(thread);
		}
	}
	virtual ~SpreadedThreadPool(void) override { if (_workers.Length()) ExitProcess(0xFF); }
	void EnqueueTask(ITask * task, uint32 with_flags)
	{
		TaskRecord rec;
		rec.Task.SetRetain(task);
		rec.Flags = with_flags;
		_sync->Wait();
		try { if (with_flags & TaskFlagFirstPriority) _queue.InsertFirst(rec); else _queue.Push(rec); } catch (...) { _sync->Open(); throw; }
		if (with_flags & TaskFlagProgressAccount) _tasks_submitted++;
		_tasks_pending++;
		if (with_flags & TaskFlagAllowMigration) _tasks_pending_moveble++;
		_counter->Open();
		_sync->Open();
	}
	void DequeueTask(ITask ** task, uint32 * flags)
	{
		*task = 0;
		*flags = 0;
		_sync->Wait();
		auto current = _queue.GetFirst();
		while (current) {
			if ((current->GetValue().Flags & TaskFlagAllowMigration) && _counter->TryWait()) {
				*task = current->GetValue().Task.Inner();
				(*task)->Retain();
				*flags = current->GetValue().Flags;
				_queue.Remove(current);
				if (*flags & TaskFlagProgressAccount) _tasks_submitted--;
				_tasks_pending--;
				_tasks_pending_moveble--;
				break;
			}
			current = current->GetNext();
		}
		_sync->Open();
	}
	void GetStatus(uint32 & complete, uint32 & active, uint32 & pending, uint32 & total, uint32 & moveble)
	{
		_sync->Wait();
		total = _tasks_submitted;
		active = _workers_active;
		pending = _tasks_pending;
		moveble = _tasks_pending_moveble;
		complete = _tasks_complete;
		_sync->Open();
	}
	void Shutdown(void)
	{
		EnqueueTask(0, TaskFlagShutdownThread);
		for (auto & t : _workers) t.Wait();
		_workers.Clear();
		_queue.Clear();
	}
};

SafePointer<ITaskFactory> factory;
SafePointer<SpreadedThreadPool> pool;
SafePointer<TaskQueue> async_dispatch;

class HostEnvironment : public IHostEnvironment
{
	struct _node_desc
	{
		ObjectAddress host, node;
		uint32 pending, active;
	};

	SafePointer<ITextWriter> _writer;
	SafePointer<Semaphore> _sync;
	ObjectAddress _this, _primary, _employer;
	int64 _ex_complete, _ex_total;
	Array<_node_desc> _nodes;
	int _load_pointer;

	void _perform_task(const string & class_name, const void * data, uint32 size, ObjectAddress on_node, uint32 flags) noexcept
	{
		if (on_node == _this) {
			try {
				SafePointer<DataBlock> block = new DataBlock(1);
				block->SetLength(size);
				MemoryCopy(block->GetBuffer(), data, size);
				SafePointer<ITask> task = factory->DeserializeTask(class_name, block);
				if (task) pool->EnqueueTask(task, flags); else {
					ReportMessage(FormatString(L"Failed to restore a task class %0", class_name));
					Terminate();
				}
			} catch (...) {}
		} else {
			try {
				int cls_len = class_name.GetEncodedLength(Encoding::UTF8) + 1;
				SafePointer<DataBlock> block = new DataBlock(1);
				block->SetLength(4 + cls_len + size);
				MemoryCopy(block->GetBuffer(), &flags, 4);
				class_name.Encode(block->GetBuffer() + 4, Encoding::UTF8, true);
				MemoryCopy(block->GetBuffer() + 4 + cls_len, data, size);
				client->SendMessage(0x00000422, on_node, block);
			} catch (...) {}
		}
	}
	void _declare_output(const void * data, uint32 size) noexcept
	{
		_sync->Wait();
		try {
			SafePointer<DataBlock> block = new DataBlock(0x100);
			block->SetLength(size);
			MemoryCopy(block->GetBuffer(), data, size);
			client->SendMessage(0x00000431, _employer, block);
		} catch (...) {}
		_sync->Open();
	}
public:
	HostEnvironment(void) : _nodes(0x10), _load_pointer(0), _ex_complete(-1), _ex_total(-1) { _sync = CreateSemaphore(1); if (!_sync) throw Exception(); }
	virtual ~HostEnvironment(void) override {}
	virtual int GetNodesCount(void) noexcept override
	{
		_sync->Wait();
		auto result = _nodes.Length();
		_sync->Open();
		return result;
	}
	virtual void GetNodesAddresses(ObjectAddress * addresses) noexcept override
	{
		_sync->Wait();
		for (int i = 0; i < _nodes.Length(); i++) addresses[i] = _nodes[i].host;
		_sync->Open();
	}
	virtual ObjectAddress GetThisNodeAddress(void) noexcept override
	{
		_sync->Wait();
		auto result = _this;
		_sync->Open();
		return result;
	}
	virtual ObjectAddress GetPrimaryNodeAddress(void) noexcept override
	{
		_sync->Wait();
		auto result = _primary;
		_sync->Open();
		return result;
	}
	virtual void PerformTask(const string & class_name, const DataBlock * data, uint32 flags) noexcept override
	{
		_sync->Wait();
		_perform_task(class_name, data->GetBuffer(), data->Length(), _nodes[_load_pointer].host, flags & TaskFlagMask);
		_load_pointer = (_load_pointer + 1) % _nodes.Length();
		_sync->Open();
	}
	virtual void PerformTask(const string & class_name, Reflection::Reflected & data, uint32 flags) noexcept override
	{
		MemoryStream stream(0x1000);
		try {
			Reflection::SerializeToBinaryObject(data, &stream);
		} catch (...) {
			ReportMessage(FormatString(L"Task serialization failure: %0", class_name));
			Terminate();
		}
		_sync->Wait();
		_perform_task(class_name, stream.GetBuffer(), stream.Length(), _nodes[_load_pointer].host, flags & TaskFlagMask);
		_load_pointer = (_load_pointer + 1) % _nodes.Length();
		_sync->Open();
	}
	virtual void PerformTask(ITask & task, uint32 flags) noexcept override
	{
		SafePointer<DataBlock> data = task.Serialize();
		if (!data) {
			ReportMessage(FormatString(L"Task serialization failure: %0", task.GetClassName()));
			Terminate();
		}
		PerformTask(task.GetClassName(), data, flags);
	}
	virtual void PerformTask(const string & class_name, const DataBlock * data, ObjectAddress on_node, uint32 flags) noexcept override
	{
		_sync->Wait();
		_perform_task(class_name, data->GetBuffer(), data->Length(), on_node, flags & TaskFlagMask);
		_sync->Open();
	}
	virtual void PerformTask(const string & class_name, Reflection::Reflected & data, ObjectAddress on_node, uint32 flags) noexcept override
	{
		MemoryStream stream(0x1000);
		try {
			Reflection::SerializeToBinaryObject(data, &stream);
		} catch (...) {
			ReportMessage(FormatString(L"Task serialization failure: %0", class_name));
			Terminate();
		}
		_sync->Wait();
		_perform_task(class_name, stream.GetBuffer(), stream.Length(), on_node, flags & TaskFlagMask);
		_sync->Open();
	}
	virtual void PerformTask(ITask & task, ObjectAddress on_node, uint32 flags) noexcept override
	{
		SafePointer<DataBlock> data = task.Serialize();
		if (!data) {
			ReportMessage(FormatString(L"Task serialization failure: %0", task.GetClassName()));
			Terminate();
		}
		PerformTask(task.GetClassName(), data, on_node, flags);
	}
	virtual void ExplicitlyDeclareProgress(uint32 complete, uint32 total) noexcept override
	{
		_sync->Wait();
		_ex_complete = complete;
		_ex_total = total;
		_sync->Open();
	}
	virtual void ReportMessage(const string & text) noexcept override
	{
		_sync->Wait();
		try {
			if (!_writer) _writer = client->CreateLoggingService();
			if (_writer) _writer->WriteLine(text);
		} catch (...) {}
		_sync->Open();
	}
	virtual void DeclareOutput(const DataBlock * data) noexcept override { _declare_output(data->GetBuffer(), data->Length()); }
	virtual void DeclareOutput(Reflection::Reflected & data) noexcept override
	{
		try {
			MemoryStream stream(0x1000);
			Reflection::SerializeToBinaryObject(data, &stream);
			_declare_output(stream.GetBuffer(), stream.Length());
		} catch (...) {
			ReportMessage(L"Failed to serialize the result.");
			Terminate();
		}
	}
	virtual void DeclareOutputAndTerminate(const DataBlock * data) noexcept override { DeclareOutput(data); Terminate(); }
	virtual void DeclareOutputAndTerminate(Reflection::Reflected & data) noexcept override { DeclareOutput(data); Terminate(); }
	virtual void Terminate(void) noexcept override
	{
		interrupt = true;
		for (auto & n : _nodes) if (n.host != _this) client->SendMessage(0x00000424, n.host, 0);
	}
	void GetExplicitProgress(int64 & complete, int64 & total)
	{
		_sync->Wait();
		complete = _ex_complete;
		total = _ex_total;
		_sync->Open();
	}
	void ProcessInitPackage(const DataBlock * data) noexcept
	{
		_sync->Wait();
		try {
			_this = client->GetSelfAddress();
			auto addrs = reinterpret_cast<const ObjectAddress *>(data->GetBuffer());
			auto num_addrs = data->Length() / sizeof(ObjectAddress);
			if ((num_addrs & 1) != 1) throw Exception();
			_primary = addrs[0];
			_nodes.SetLength((num_addrs - 1) / 2);
			for (int i = 0; i < _nodes.Length(); i++) {
				_nodes[i].active = _nodes[i].pending = 0;
				_nodes[i].node = addrs[2 * i + 1];
				_nodes[i].host = addrs[2 * i + 2];
			}
		} catch (...) {}
		_sync->Open();
	}
	void SetEmployer(ObjectAddress addr) noexcept
	{
		_sync->Wait();
		_employer = addr;
		_sync->Open();
	}
	DataBlock * MakeStatusPackage(uint32 * ret_moveble = 0, uint32 * ret_pending = 0, uint32 * ret_active = 0) noexcept
	{
		try {
			SafePointer<DataBlock> result = new DataBlock(1);
			result->SetLength(16);
			uint32 real_inproc, real_pending, real_complete, real_total, real_moveble;
			int64 emul_complete, emul_total;
			pool->GetStatus(real_complete, real_inproc, real_pending, real_total, real_moveble);
			if (ret_moveble) *ret_moveble = real_moveble;
			if (ret_pending) *ret_pending = real_pending;
			if (ret_active) *ret_active = real_inproc;
			GetExplicitProgress(emul_complete, emul_total);
			if (emul_complete >= 0 && emul_total >= 0) { real_complete = emul_complete; real_total = emul_total; }
			if (!real_complete && !real_total) real_total = 0xFFFFFFFF;
			MemoryCopy(result->GetBuffer(), &real_pending, 4);
			MemoryCopy(result->GetBuffer() + 4, &real_inproc, 4);
			MemoryCopy(result->GetBuffer() + 8, &real_complete, 4);
			MemoryCopy(result->GetBuffer() + 12, &real_total, 4);
			result->Retain();
			return result;
		} catch (...) { return 0; }
	}
	void PerformQueueRevision(void) noexcept
	{
		uint32 self_moveble, self_pending, self_active;
		SafePointer<DataBlock> data = MakeStatusPackage(&self_moveble, &self_pending, &self_active);
		ObjectAddress least_loaded = 0;
		uint32 least_tasks = 0xFFFFFFFF;
		_sync->Wait();
		for (auto & n : _nodes) if (n.host != _this) {
			client->SendMessage(0x00010423, n.host, data);
			if (n.pending < least_tasks) { least_tasks = n.pending; least_loaded = n.host; }
		}
		_sync->Open();
		client->SendMessage(0x00010423, client->GetNodeAddress(), data);
		client->SendMessage(0x00010423, _employer, data);
		if (self_pending / 2 > least_tasks && self_moveble && least_loaded) {
			SafePointer<ITask> task;
			uint32 flags;
			pool->DequeueTask(task.InnerRef(), &flags);
			if (task) PerformTask(*task, least_loaded, flags);
		}
	}
	void ProcessStatusPackage(ObjectAddress from, const DataBlock * data) noexcept
	{
		if (!data || data->Length() < 8) return;
		_sync->Wait();
		for (auto & n : _nodes) if (n.host == from) {
			MemoryCopy(&n.pending, data->GetBuffer(), 4);
			MemoryCopy(&n.active, data->GetBuffer() + 4, 4);
			break;
		}
		_sync->Open();
	}
};
class EventHandler : public IEventCallback, public IMessageCallback
{
	HostEnvironment * _env;
public:
	EventHandler(void) : _env(0) {}
	virtual void ConnectionLost(void) override { interrupt = true; }
	virtual bool RespondsToMessage(uint32 verb) override
	{
		if (verb == 0x00000421) return true;
		else if (verb == 0x00010421) return true;
		else if (verb == 0x00000422) return true;
		else if (verb == 0x00010422) return true;
		else if (verb == 0x00000423) return true;
		else if (verb == 0x00010423) return true;
		else if (verb == 0x00000424) return true;
		else return false;
	}
	virtual void HandleMessage(ObjectAddress from, uint32 verb, const DataBlock * data) override
	{
		if (verb == 0x00000421) {
			try {
				SafePointer<DataBlock> copy = new DataBlock(*data);
				async_dispatch->SubmitTask(CreateFunctionalTask([copy, env = _env, from = from]() {
					env->ProcessInitPackage(copy);
					client->SendMessage(0x00010421, from, 0);
				}));
			} catch (...) {}
		} else if (verb == 0x00000422) {
			if (data && data->Length() >= 4) {
				uint32 flags;
				MemoryCopy(&flags, data->GetBuffer(), 4);
				int ne = -1;
				for (int i = 4; i < data->Length(); i++) if (!data->ElementAt(i)) { ne = i + 1; break; }
				try {
					if (ne >= 0) {
						auto class_name = string(data->GetBuffer() + 4, -1, Encoding::UTF8);
						SafePointer<DataBlock> block = new DataBlock(1);
						block->SetLength(data->Length() - ne);
						MemoryCopy(block->GetBuffer(), data->GetBuffer() + ne, data->Length() - ne);
						SafePointer<ITask> task = factory->DeserializeTask(class_name, block);
						if (task) {
							async_dispatch->SubmitTask(CreateFunctionalTask([task, flags]() {
								pool->EnqueueTask(task, flags & TaskFlagMask);
							}));
						} else {
							async_dispatch->SubmitTask(CreateFunctionalTask([env = _env, class_name]() {
								env->ReportMessage(FormatString(L"Failed to restore a task class %0", class_name));
								env->Terminate();
							}));
						}
					}
				} catch (...) {}
			}
			async_dispatch->SubmitTask(CreateFunctionalTask([from = from]() {
				client->SendMessage(0x00010422, from, 0);
			}));
		} else if (verb == 0x00000423) {
			async_dispatch->SubmitTask(CreateFunctionalTask([env = _env, from = from]() {
				SafePointer<DataBlock> status = env->MakeStatusPackage();
				if (status) client->SendMessage(0x00010423, from, status);
			}));
		} else if (verb == 0x00010423) {
			try {
				SafePointer<DataBlock> copy = new DataBlock(*data);
				async_dispatch->SubmitTask(CreateFunctionalTask([env = _env, from = from, copy]() {
					env->ProcessStatusPackage(from, copy);
				}));
			} catch (...) {}
		} else if (verb == 0x00000424) {
			interrupt = true;
		}
	}
	virtual void CallbackExpired(void) override {}
	void SetEnvironment(HostEnvironment * env) { _env = env; }
};

EventHandler handler;

int Main(void)
{
	uint16 port;
	ObjectAddress employer;
	try {
		SafePointer< Array<string> > args = GetCommandLine();
		if (args->Length() < 4) return 1;
		dl = LoadLibrary(args->ElementAt(1));
		port = args->ElementAt(2).ToUInt32();
		employer = args->ElementAt(3).ToUInt64(HexadecimalBase);
		if (!dl) return 2;
	} catch (...) { return 1; }
	try {
		func_CreateTaskFactory CreateTaskFactory = reinterpret_cast<func_CreateTaskFactory>(GetLibraryRoutine(dl, "CreateTaskFactory"));
		if (!CreateTaskFactory) return 3;
		factory = CreateTaskFactory();
		if (!factory) return 4;
		SafePointer<ITask> init = factory->DeserializeTask(L"Init", 0);
		if (init) init->DoTask(0);
		init.SetReference(0);
		pool = new SpreadedThreadPool;
	} catch (...) {
		factory.SetReference(0);
		ReleaseLibrary(dl);
		return 3;
	}
	try {
		interrupt = false;
		async_dispatch = new TaskQueue;
		if (!async_dispatch->ProcessAsSeparateThread()) throw Exception();
		SafePointer<HostEnvironment> env = new HostEnvironment;
		environment.SetRetain(env);
		handler.SetEnvironment(env);
		env->SetEmployer(employer);
		client = new Client;
		client->SetConnectionPort(port);
		client->SetConnectionService(ServiceClass::WorkHost);
		client->SetEventCallback(&handler);
		client->RegisterCallback(&handler);
		client->Connect();
		client->SendMessage(0x00010411, client->GetNodeAddress(), 0);
		while (!interrupt) {
			env->PerformQueueRevision();
			Sleep(1000);
		}
		async_dispatch->Break();
		client->Disconnect();
		client->Wait();
		pool->Shutdown();
		pool.SetReference(0);
		factory.SetReference(0);
		environment.SetReference(0);
		handler.SetEnvironment(0);
		async_dispatch.SetReference(0);
	} catch (...) {
		factory.SetReference(0);
		ReleaseLibrary(dl);
		return 5;
	}
	ReleaseLibrary(dl);
	return 0;
}