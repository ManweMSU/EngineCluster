#include "ClusterTaskClient.h"

namespace Engine
{
	namespace Cluster
	{
		struct _comtable
		{
			ObjectAddress primary;
			ObjectAddress addresses[1];
		};

		void TaskClient::ConnectionLost(void)
		{
			_sync->Wait();
			if ((_status != TaskErrorSuccess && !(_status & 0x8000)) || _status == 0xFFFF) {
				_status = TaskErrorInternalFailure;
				_finish->Open();
			}
			_sync->Open();
		}
		bool TaskClient::RespondsToMessage(uint32 verb)
		{
			if (verb == 0x00010411) return true;
			else if (verb == 0x00010421) return true;
			else if (verb == 0x00010422) return true;
			else if (verb == 0x00010423) return true;
			else if (verb == 0x00000431) return true;
			else return false;
		}
		void TaskClient::HandleMessage(ObjectAddress from, uint32 verb, const DataBlock * data)
		{
			if (verb == 0x00010411) {
				uint32 len = data ? data->Length() : 0;
				uint32 dta[3];
				if (len) MemoryCopy(&dta, data->GetBuffer(), min(len, 12U));
				_queue->SubmitTask(CreateFunctionalTask([this, from, dta, len]() {
					_sync->Wait();
					for (auto & h : _hosts) if (h._node == from) {
						h._last_message = Time::GetCurrentTime();
						if (len >= 4) {
							uint32 status = dta[0];
							if (h._status == HostIsStarting) h._status = status;
							if (len >= 12) {
								ObjectAddress address;
								MemoryCopy(&address, &dta[1], 8);
								h._host = address;
							}
						}
						break;
					}
					bool all_known = true;
					int num_ok = 0;
					for (auto & h : _hosts) {
						if (h._status == HostStarted) num_ok++;
						else if (h._status == HostIsStarting) all_known = false;
					}
					if (all_known) {
						if (num_ok) {
							SafePointer<DataBlock> data;
							int table_size = sizeof(ObjectAddress) * (1 + 2 * num_ok);
							try {
								data = new DataBlock(1);
								data->SetLength(table_size);
							} catch (...) { data.SetReference(0); }
							if (data) {
								_status = TaskErrorWorking;
								auto table = reinterpret_cast<_comtable *>(data->GetBuffer());
								table->primary = 0;
								int i = 0;
								for (auto & h : _hosts) if (h._status == HostStarted) {
									if (!table->primary) table->primary = h._host;
									table->addresses[2 * i] = h._node;
									table->addresses[2 * i + 1] = h._host;
									i++;
								}
								try {
									SafePointer<DataBlock> data = new DataBlock(0x1000);
									data->SetLength(table_size);
									MemoryCopy(data->GetBuffer(), table, table_size);
									for (auto & h : _hosts) if (h._status == HostStarted) _client->SendMessage(0x00000421, h._host, data);
									data->Clear();
									SafePointer<DataBlock> main_data = string(L"Main").EncodeSequence(Encoding::UTF8, true);
									data->Append(0); data->Append(0); data->Append(0); data->Append(0);
									data->Append(*main_data);
									if (_input) data->Append(*_input);
									_client->SendMessage(0x00000422, table->primary, data);
								} catch (...) {}
							} else {
								_status = TaskErrorInternalFailure;
								_finish->Open();
							}
						} else {
							_status = TaskErrorNoNodesAvailable;
							_finish->Open();
						}
					}
					_sync->Open();
				}));
			} else if (verb == 0x00010421) {
				_queue->SubmitTask(CreateFunctionalTask([this, from]() {
					_sync->Wait();
					for (auto & h : _hosts) if (h._host == from) {
						h._last_message = Time::GetCurrentTime();
						break;
					}
					_sync->Open();
				}));
			} else if (verb == 0x00010422) {
				_queue->SubmitTask(CreateFunctionalTask([this, from]() {
					_sync->Wait();
					for (auto & h : _hosts) if (h._host == from) {
						h._last_message = Time::GetCurrentTime();
						break;
					}
					_sync->Open();
				}));
			} else if (verb == 0x00010423) {
				uint complete, total;
				if (data && data->Length() >= 16) {
					MemoryCopy(&complete, data->GetBuffer() + 8, 4);
					MemoryCopy(&total, data->GetBuffer() + 12, 4);
					_queue->SubmitTask(CreateFunctionalTask([this, from, complete, total]() {
						_sync->Wait();
						if (_status == TaskErrorWorking || _status == TaskErrorInitializing) {
							for (auto & h : _hosts) if (h._host == from) {
								h._last_message = Time::GetCurrentTime();
								h._complete = complete;
								h._total = total;
								break;
							}
							_complete = _total = 0;
							for (auto & h : _hosts) { _complete += h._complete; _total += h._total; }
							if (!_total) _total = 0xFFFFFFFF;
						}
						_sync->Open();
					}));
				}
			} else if (verb == 0x00000431) {
				SafePointer<DataBlock> output;
				try {
					output = new DataBlock(1);
					output->Append(*data);
				} catch (...) { _status = TaskErrorInternalFailure; _finish->Open(); return; }
				if (output) {
					_queue->SubmitTask(CreateFunctionalTask([this, from, output]() {
						_sync->Wait();
						_output = output;
						_status = TaskErrorSuccess;
						_complete = _total = 1;
						for (auto & h : _hosts) { h._complete = h._total = 1; }
						_exec.SetReference(0);
						_input.SetReference(0);
						_finish->Open();
						_sync->Open();
					}));
				}
			}
		}
		void TaskClient::CallbackExpired(void) {}
		int TaskClient::_watchdog_proc(void * arg_ptr)
		{
			auto self = reinterpret_cast<TaskClient *>(arg_ptr);
			self->Retain();
			bool exit = false;
			while (!exit) {
				self->_sync->Wait();
				bool update = false;
				exit = (self->_status == TaskErrorSuccess) || (self->_status & 0x8000);
				auto current_time = Time::GetCurrentTime();
				Time last_time = 0;
				for (auto & h : self->_hosts) {
					if (h._status == HostIsStarting && (current_time - h._last_message).Ticks > 20000) {
						h._status = HostTimedOut;
						update = true;
					}
					if (h._last_message > last_time) last_time = h._last_message;
				}
				if ((current_time - last_time).Ticks > 20000) {
					if (!exit) {
						self->_status = TaskErrorInternalFailure;
						self->_finish->Open();
					}
				}
				self->_sync->Open();
				if (update) self->HandleMessage(0x00010411, 0, 0);
				if (exit) break;
				Sleep(5000);
			}
			self->Release();
			return 0;
		}
		TaskClient::TaskClient(void) : _hosts(0x10)
		{
			_queue = new TaskQueue;
			_client = new Client;
			_sync = CreateSemaphore(1);
			_finish = CreateSemaphore(0);
			if (!_sync || !_finish) throw Exception();
			_status = 0xFFFF;
			_complete = 0;
			_total = 0xFFFFFFFF;
			_client->SetEventCallback(this);
			_client->SetConnectionService(ServiceClass::WorkClient);
			_client->RegisterCallback(this);
			if (!_queue->ProcessAsSeparateThread()) throw Exception();
		}
		TaskClient::~TaskClient(void) { _queue->Break(); _client->Disconnect(); _client->Wait(); }
		void TaskClient::SetConnectionIP(Network::Address ip) { if (_status != 0xFFFF) throw InvalidStateException(); _client->SetConnectionIP(ip); }
		void TaskClient::SetConnectionPort(uint16 port) { if (_status != 0xFFFF) throw InvalidStateException(); _client->SetConnectionPort(port); }
		void TaskClient::SetTaskExecutable(DataBlock * data) { if (_status != 0xFFFF) throw InvalidStateException(); _exec.SetRetain(data); }
		void TaskClient::SetTaskInput(DataBlock * data) { if (_status != 0xFFFF) throw InvalidStateException(); _input.SetRetain(data); }
		void TaskClient::SetTaskInput(Reflection::Reflected & data)
		{
			if (_status != 0xFFFF) throw InvalidStateException();
			Streaming::MemoryStream stream(0x10000);
			Reflection::SerializeToBinaryObject(data, &stream);
			SafePointer<DataBlock> input = new DataBlock(1);
			input->SetLength(stream.Length());
			MemoryCopy(input->GetBuffer(), stream.GetBuffer(), input->Length());
			_input.SetRetain(input);
		}
		void TaskClient::Start(void)
		{
			if (_status != 0xFFFF) throw InvalidStateException();
			if (!_exec) throw InvalidArgumentException();
			_client->Connect();
			_status = TaskErrorInitializing;
			SafePointer< Array<NodeDesc> > nodes = _client->EnumerateNodes();
			auto current_time = Time::GetCurrentTime();
			for (auto & n : *nodes) {
				_host_info host;
				host._last_message = current_time;
				host._node = n.Address;
				host._host = 0;
				host._status = HostIsStarting;
				host._complete = 0;
				host._total = 0;
				_hosts << host;
			}
			if (!_hosts.Length()) {
				_status = TaskErrorNoNodesAvailable;
				_finish->Open();
				return;
			}
			for (auto & h : _hosts) _client->SendMessage(0x00000411, h._node, _exec);
			_watchdog_thread = CreateThread(_watchdog_proc, this);
			if (!_watchdog_thread) throw Exception();
		}
		Client * TaskClient::GetClusterClient(void) { return _client; }
		int TaskClient::GetHostCount(void)
		{
			_sync->Wait();
			auto count = _hosts.Length();
			_sync->Open();
			return count;
		}
		void TaskClient::GetHostInfo(int range_min, int count, TaskHostDesc * desc)
		{
			_sync->Wait();
			for (int i = 0; i < count; i++) {
				desc[i].HostAddress = _hosts[range_min + i]._host;
				desc[i].NodeAddress = _hosts[range_min + i]._node;
				desc[i].TasksComplete = _hosts[range_min + i]._complete;
				desc[i].TasksTotal = _hosts[range_min + i]._total;
				desc[i].HostStatus = _hosts[range_min + i]._status;
			}
			_sync->Open();
		}
		void TaskClient::GetProgressInfo(uint32 * tasks_complete, uint32 * tasks_total)
		{
			_sync->Wait();
			if (tasks_complete) *tasks_complete = _complete;
			if (tasks_total) *tasks_total = _total;
			_sync->Open();
		}
		bool TaskClient::IsFinished(void)
		{
			if (_finish->TryWait()) {
				_finish->Open();
				return true;
			} else return false;
		}
		void TaskClient::Interrupt(void)
		{
			try { _client->SendMessage(0x00000412, _client->GetNodeAddress(), 0); } catch (...) {}
			_client->Disconnect();
		}
		void TaskClient::Wait(void) { _finish->Wait(); }
		uint32 TaskClient::GetTaskResultStatus(void)
		{
			_sync->Wait();
			uint32 retval = _status;
			_sync->Open();
			return retval;
		}
		void TaskClient::GetTaskResult(DataBlock ** data)
		{
			_sync->Wait();
			if (_output) {
				*data = _output.Inner();
				_output->Retain();
			} else *data = 0;
			_sync->Open();
		}
		void TaskClient::GetTaskResult(Reflection::Reflected & data)
		{
			_sync->Wait();
			if (_output) try {
				Streaming::MemoryStream stream(_output->GetBuffer(), _output->Length());
				Reflection::RestoreFromBinaryObject(data, &stream);
			} catch (...) {}
			_sync->Open();
		}
	}
}