#include "srvrsrvc.h"

#include "client/ClusterPackage.h"

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
		namespace Words
		{
			#ifdef ENGINE_WINDOWS
			constexpr widechar * OperatingSystem = PackageAssetWindows;
			#else
			#ifdef ENGINE_MACOSX
			constexpr widechar * OperatingSystem = PackageAssetMacOS;
			#else
			constexpr widechar * OperatingSystem = L"?";
			#endif
			#endif
			#ifdef ENGINE_ARM
			#ifdef ENGINE_X64
			constexpr widechar * Architecture = PackageAssetARM64;
			#else
			constexpr widechar * Architecture = PackageAssetARM;
			#endif
			#else
			#ifdef ENGINE_X64
			constexpr widechar * Architecture = PackageAssetX64;
			#else
			constexpr widechar * Architecture = PackageAssetX86;
			#endif
			#endif
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

		class CompilerHostInstance : public Object
		{
			static ObjectArray<CompilerHostInstance> _compilers;
			static SafePointer<Semaphore> _sync;

			volatile int _status;
			string _words;
			string * _return_exec_path;
			string * _return_root_path;
			ObjectAddress _client;
			SafePointer<Process> _host;
			SafePointer<Streaming::Stream> _package_stream;
			SafePointer<IDispatchTask> _watchdog, _on_exit;
			SafePointer<Thread> _watchdog_thread, _proc_thread;

			CompilerHostInstance(void) : _status(0) {}
			void _unregister(void) noexcept
			{
				_sync->Wait();
				for (int i = 0; i < _compilers.Length(); i++) if (_compilers.ElementAt(i) == this) { _compilers.Remove(i); break; }
				_sync->Open();
			}
			void _terminate(void) noexcept { _status = 2; }
			static int _work_thread_proc(void * arg_ptr)
			{
				auto self = reinterpret_cast<CompilerHostInstance *>(arg_ptr);
				self->Retain();
				try {
					SafePointer<Package> package = new Package(self->_package_stream);
					auto asset = package->FindAsset(PackageAssetSources);
					if (!asset) throw Exception();
					auto root_path = IO::ExpandPath(IO::Path::GetDirectory(IO::GetExecutablePath()) + L"/compile/" +
						IO::Path::GetFileNameWithoutExtension(asset->RootFile));
					auto root_file = IO::ExpandPath(root_path + L"/" + asset->RootFile);
					try { IO::RemoveEntireDirectory(root_path); } catch (...) {}
					IO::CreateDirectoryTree(root_path);
					SafePointer< Array<PackageFileDesc> > files = package->EnumerateFiles(asset->Handle);
					for (auto & f : *files) {
						if (self->_status) throw Exception();
						if (f.Handle) {
							Streaming::FileStream to(root_path + L"/" + f.Path, Streaming::AccessReadWrite, Streaming::CreateAlways);
							SafePointer<Streaming::Stream> from = package->OpenFile(f.Handle);
							from->CopyTo(&to);
							IO::DateTime::SetFileCreationTime(to.Handle(), f.DateCreated);
							IO::DateTime::SetFileAlterTime(to.Handle(), f.DateAltered);
							IO::DateTime::SetFileAccessTime(to.Handle(), f.DateAccessed);
						} else {
							IO::CreateDirectoryTree(root_path + L"/" + f.Path);
						}
					}
					package.SetReference(0);
					self->_package_stream.SetReference(0);
					if (!self->_status) {
						Array<string> cmds(0x10);
						cmds << root_file;
						cmds << string(self->_client, HexadecimalBase, 16);
						cmds << self->_words;
						cmds << string(GetServerCurrentPort());
						self->_host = CreateCommandProcess(L"ecfcomhs", &cmds);
						if (!self->_host) throw Exception();
						while (!self->_host->Exited()) {
							if (self->_status) break;
							Sleep(1000);
						}
						if (self->_status == 2) self->_host->Terminate(); else {
							self->_host->Wait();
							for (int i = 0; i < 100; i++) { if (self->_status) break; Sleep(100); }
						}
					}
				} catch (...) { if (self->_host) self->_host->Terminate(); }
				if (self->_on_exit) self->_on_exit->DoTask(0);
				self->_unregister();
				self->_status = 3;
				self->_watchdog_thread->Wait();
				self->Release();
				return 0;
			}
			static int _watchdog_thread_proc(void * arg_ptr)
			{
				auto self = reinterpret_cast<CompilerHostInstance *>(arg_ptr);
				self->Retain();
				while (self->_status < 2) {
					self->_watchdog->DoTask(0);
					Sleep(1000);
				}
				self->Release();
				return 0;
			}
			void _launch(void)
			{
				_proc_thread = CreateThread(_work_thread_proc, this);
				_watchdog_thread = CreateThread(_watchdog_thread_proc, this);
				if (!_watchdog_thread || !_proc_thread) throw Exception();
			}
			void _handle_message(uint32 status, const string & tx1, const string & tx2) noexcept
			{
				if (_return_exec_path) *_return_exec_path = tx1;
				if (_return_root_path) *_return_root_path = tx2;
				_status = 1;
			}
		public:
			virtual ~CompilerHostInstance(void) override {}
			static void PerformCompilation(const string & words, Streaming::Stream * stream, string * exec_path, string * root_path, IDispatchTask * watchdog, IDispatchTask * exit, ObjectAddress client)
			{
				if (exec_path) *exec_path = L"";
				if (root_path) *root_path = L"";
				_sync->Wait();
				try {
					SafePointer<CompilerHostInstance> new_host = new CompilerHostInstance;
					new_host->_words = words;
					new_host->_return_exec_path = exec_path;
					new_host->_return_root_path = root_path;
					new_host->_client = client;
					new_host->_package_stream.SetRetain(stream);
					new_host->_watchdog.SetRetain(watchdog);
					new_host->_on_exit.SetRetain(exit);
					new_host->_launch();
					_compilers.Append(new_host);
				} catch (...) {
					if (exit) exit->DoTask(0);
				}
				_sync->Open();
			}
			static void TerminateCompiler(ObjectAddress client)
			{
				CompilerHostInstance * instance = 0;
				_sync->Wait();
				for (auto & c : _compilers) if (c._client == client) { instance = &c; break; }
				_sync->Open();
				if (instance) instance->_terminate();
			}
			static void TerminateAll(void)
			{
				_sync->Wait();
				for (auto & c : _compilers) c._terminate();
				_sync->Open();
			}
			static void HandleCompilerResponce(const DataBlock * data)
			{
				if (!data || data->Length() < 12) return;
				ObjectAddress client;
				uint32 status;
				string tx1, tx2;
				MemoryCopy(&client, data->GetBuffer(), 8);
				MemoryCopy(&status, data->GetBuffer() + 8, 4);
				try {
					if (data->Length() > 12) for (int i = 12; i < data->Length(); i++) if (!data->ElementAt(i)) {
						tx1 = string(data->GetBuffer() + 12, i - 12, Encoding::UTF8);
						tx2 = string(data->GetBuffer() + i + 1, data->Length() - i - 1, Encoding::UTF8);
						break;
					}
				} catch (...) {}
				CompilerHostInstance * instance = 0;
				_sync->Wait();
				for (auto & c : _compilers) if (c._client == client) { instance = &c; break; }
				_sync->Open();
				if (instance) instance->_handle_message(status, tx1, tx2);
			}
			static void PackageAddDirectory(PackageBuilder * package, handle asset, const string & path, const string & prefix)
			{
				SafePointer< Array<string> > files = IO::Search::GetFiles(path + L"/*");
				SafePointer< Array<string> > dirs = IO::Search::GetDirectories(path + L"/*");
				for (auto & f : *files) {
					if (f[0] == L'.' || f[0] == L'_') continue;
					package->AddFile(path + L"/" + f, asset, prefix + f);
				}
				for (auto & d : *dirs) {
					if (d[0] == L'.' || d[0] == L'_') continue;
					package->AddDirectory(asset, prefix + d);
					PackageAddDirectory(package, asset, path + L"/" + d, prefix + d + L"\\");
				}
			}
			static void MakePackage(DataBlock * data, const string & dir_from, const string & xfile)
			{
				try {
					SafePointer<Streaming::Stream> stream = new Streaming::MemoryStream(0x10000);
					SafePointer<PackageBuilder> package = new PackageBuilder(stream);
					string os, arch;
					for (auto & w : IO::Path::GetFileName(dir_from).Split(L'_')) {
						if (w == L"windows" || w == L"macosx") os = w;
						else if (w == L"x86" || w == L"x64" || w == L"arm" || w == L"arm64") arch = w;
					}
					auto asset = package->CreateAsset(os, arch);
					package->SetAssetRootFile(asset, xfile.Fragment(dir_from.Length() + 1, -1));
					PackageAddDirectory(package, asset, dir_from, L"");
					if (IO::FileExists(dir_from + L"/_obj/" + IO::Path::GetFileNameWithoutExtension(xfile) + L".formats.ini")) {
						package->AddDirectory(asset, L"_obj");
						package->AddFile(dir_from + L"/_obj/" + IO::Path::GetFileNameWithoutExtension(xfile) + L".formats.ini", asset,
							L"_obj/" + IO::Path::GetFileNameWithoutExtension(xfile) + L".formats.ini");
					}
					package->Finalize();
					package.SetReference(0);
					int base = data->Length();
					int length = stream->Length();
					data->SetLength(base + length);
					stream->Seek(0, Streaming::Begin);
					stream->Read(data->GetBuffer() + base, length);
				} catch (...) {}
			}
		};
		class ComputationRentClass
		{
			static SafePointer<Process> _process;
			static SafePointer<Semaphore> _sync;
			static SafePointer<DataBlock> _data;
			static int _state; // 0 - idle, 1 - init state, 2 - ready
			static ObjectAddress _client, _localhost;
			static bool _allowed;

			static void SendRentResponce(ObjectAddress client, uint status, ObjectAddress host_address)
			{
				try {
					SafePointer<DataBlock> data = new DataBlock(1);
					if (host_address) {
						data->SetLength(12);
						MemoryCopy(data->GetBuffer(), &status, 4);
						MemoryCopy(data->GetBuffer() + 4, &host_address, 8);
					} else {
						data->SetLength(4);
						MemoryCopy(data->GetBuffer(), &status, 4);
					}
					ServerSendMessage(0x00010411, client, data);
				} catch (...) {}
			}
			static int WorkerThread(void * arg_ptr)
			{
				string dlfile;
				ObjectAddress client;
				SafePointer<Process> process;
				ServerServiceUpdateProgressStatus(0, 0xFFFFFFFF);
				try {
					SafePointer<DataBlock> data;
					_sync->Wait();
					data.SetRetain(_data);
					client = _client;
					_data.SetReference(0);
					_sync->Open();
					SafePointer<Streaming::MemoryStream> stream = new Streaming::MemoryStream(data->GetBuffer(), data->Length());
					data.SetReference(0);
					SafePointer<Package> package = new Package(stream);
					auto binary_asset = package->FindAsset(Words::OperatingSystem, Words::Architecture);
					auto sources_asset = package->FindAsset(PackageAssetSources);
					if (binary_asset) {
						Time last_time = Time::GetCurrentTime();
						auto root_path = IO::ExpandPath(IO::Path::GetDirectory(IO::GetExecutablePath()) + L"/binary/" +
							IO::Path::GetFileNameWithoutExtension(binary_asset->RootFile));
						dlfile = IO::ExpandPath(root_path + L"/" + binary_asset->RootFile);
						IO::CreateDirectoryTree(root_path);
						SafePointer< Array<PackageFileDesc> > files = package->EnumerateFiles(binary_asset->Handle);
						for (auto & f : *files) {
							Time time = Time::GetCurrentTime();
							if ((time - last_time).Ticks >= 1000) {
								SendRentResponce(client, HostIsStarting, 0);
								last_time = time;
							}
							if (f.Handle) {
								try {
									Streaming::FileStream to(root_path + L"/" + f.Path, Streaming::AccessRead, Streaming::OpenExisting);
									if (IO::DateTime::GetFileAlterTime(to.Handle()) >= f.DateAltered) continue;
								} catch (...) {}
								Streaming::FileStream to(root_path + L"/" + f.Path, Streaming::AccessReadWrite, Streaming::CreateAlways);
								SafePointer<Streaming::Stream> from = package->OpenFile(f.Handle);
								from->CopyTo(&to);
								IO::DateTime::SetFileCreationTime(to.Handle(), f.DateCreated);
								IO::DateTime::SetFileAlterTime(to.Handle(), f.DateAltered);
								IO::DateTime::SetFileAccessTime(to.Handle(), f.DateAccessed);
							} else {
								IO::CreateDirectoryTree(root_path + L"/" + f.Path);
							}
						}
					} else if (sources_asset) {
						SafePointer<Semaphore> con_sem = CreateSemaphore(0);
						string dlpath;
						string words = string(Words::OperatingSystem) + L"-" + string(Words::Architecture);
						auto watchdog = CreateFunctionalTask([client]() { SendRentResponce(client, HostIsStarting, 0); });
						auto on_complete = CreateFunctionalTask([con_sem]() { con_sem->Open(); });
						CompilerHostInstance::PerformCompilation(words, stream, &dlfile, &dlpath, watchdog, on_complete, GetLoopbackAddress());
						con_sem->Wait();
						if (!dlpath.Length()) {
							_sync->Wait();
							if (dlfile.Length()) SendRentResponce(_client, HostCompilationFailed, 0);
							else SendRentResponce(_client, HostExtractionFailed, 0);
							ServerServiceUpdateProgressStatus(0, 0);
							_state = 0;
							_client = _localhost = 0;
							_sync->Open();
							return 0;
						}
					} else {
						_sync->Wait();
						SendRentResponce(_client, HostNoArchitecture, 0);
						ServerServiceUpdateProgressStatus(0, 0);
						_state = 0;
						_client = _localhost = 0;
						_sync->Open();
						return 0;
					}
					SendRentResponce(client, HostIsStarting, 0);
				} catch (...) {
					_sync->Wait();
					SendRentResponce(_client, HostExtractionFailed, 0);
					ServerServiceUpdateProgressStatus(0, 0);
					_state = 0;
					_client = _localhost = 0;
					_sync->Open();
					return 0;
				}
				try {
					Array<string> cmds(0x10);
					cmds << dlfile;
					cmds << string(GetServerCurrentPort());
					cmds << string(client, HexadecimalBase, 16);
					process = CreateCommandProcess(L"ecfhost", &cmds);
					if (!process) throw Exception();
				} catch (...) {
					_sync->Wait();
					SendRentResponce(_client, HostLaunchFailed, 0);
					ServerServiceUpdateProgressStatus(0, 0);
					_state = 0;
					_client = _localhost = 0;
					_sync->Open();
					return 0;
				}
				while (true) {
					_sync->Wait();
					if (_state == 2) {
						_sync->Open();
						break;
					} else if (_state == 1 && process->Exited()) {
						SendRentResponce(_client, HostInitFailed, 0);
						ServerServiceUpdateProgressStatus(0, 0);
						_state = 0;
						_client = _localhost = 0;
						_sync->Open();
						return 0;
					}
					_sync->Open();
					Sleep(1000);
					SendRentResponce(client, HostIsStarting, 0);
				}
				_sync->Wait();
				_process.SetRetain(process);
				_state = 2;
				SendRentResponce(_client, HostStarted, _localhost);
				_sync->Open();
				process->Wait();
				_sync->Wait();
				_process.SetReference(0);
				_data.SetReference(0);
				_state = 0;
				_client = _localhost = 0;
				ServerServiceUpdateProgressStatus(0, 0);
				_sync->Open();
				return 0;
			}
		public:
			static void PerformRent(ObjectAddress client, const DataBlock * binaries)
			{
				_sync->Wait();
				if (_state || _process) {
					SendRentResponce(client, HostBusyNow, 0);
					_sync->Open();
					return;
				}
				if (!_allowed) {
					SendRentResponce(client, HostDiscarded, 0);
					_sync->Open();
					return;
				}
				try {
					SafePointer<DataBlock> data = new DataBlock(*binaries);
					_client = client;
					_data.SetRetain(data);
					SafePointer<Thread> thread = CreateThread(WorkerThread);
					if (!thread) throw Exception();
				} catch (...) {
					_client = 0;
					_data.SetReference(0);
					SendRentResponce(client, HostInitFailed, 0);
					_sync->Open();
					return;
				}
				_state = 1;
				_sync->Open();
			}
			static void HandleHostStarted(ObjectAddress host)
			{
				_sync->Wait();
				if (_state == 1) {
					_localhost = host;
					_state = 2;
				}
				_sync->Open();
			}
			static void KillHost(ObjectAddress sender)
			{
				_sync->Wait();
				if (_state == 2 && _process) {
					_process->Terminate();
					_process.SetReference(0);
					_data.SetReference(0);
					_state = 0;
					_client = _localhost = 0;
					ServerServiceUpdateProgressStatus(0, 0);
				}
				_sync->Open();
				if (GetAddressLevel(sender) != AddressLevelNode) ServerSendMessage(0x00000412, MakeObjectAddress(
					AddressLevelNode, AddressServiceNode, AddressNodeUndefined, AddressInstanceUnique), 0);
			}
			static void AllowTasks(bool allow) { _sync->Wait(); _allowed = allow; _sync->Open(); }
		};
		
		ObjectArray<CompilerHostInstance> CompilerHostInstance::_compilers(0x10);
		SafePointer<Semaphore> CompilerHostInstance::_sync = CreateSemaphore(1);
		SafePointer<Process> ComputationRentClass::_process;
		SafePointer<Semaphore> ComputationRentClass::_sync = CreateSemaphore(1);
		SafePointer<DataBlock> ComputationRentClass::_data;
		int ComputationRentClass::_state = 0;
		ObjectAddress ComputationRentClass::_client = 0;
		ObjectAddress ComputationRentClass::_localhost = 0;
		bool ComputationRentClass::_allowed = false;

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
			virtual void SwitchedToNet(const UUID * uuid) override
			{
				active = true;
				thread = CreateThread(_server_service_thread, this);
				ComputationRentClass::AllowTasks(true);
			}
			virtual void SwitchingFromNet(const UUID * uuid) override
			{
				active = false;
				CompilerHostInstance::TerminateAll();
				ComputationRentClass::KillHost(0);
			}
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
				else if (verb == 0x00000206) return true;
				else if (verb == 0x00000301) return true;
				else if (verb == 0x00000302) return true;
				else if (verb == 0x00000303) return true;
				else if (verb == 0x00000411) return true;
				else if (verb == 0x00010411) return true;
				else if (verb == 0x00000412) return true;
				else if (verb == 0x00000413) return true;
				else if (verb == 0x00000414) return true;
				else if (verb == 0x00000415) return true;
				else if (verb == 0x00000416) return true;
				else if (verb == 0x00010423) return true;
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
					} else if (verb == 0x00000206) {
						sync->Wait();
						NodeStatusInfo status;
						status.Battery = Power::GetBatteryStatus();
						status.BatteryLevel = uint32(Power::GetBatteryChargeLevel() * 1000.0);
						status.ProgressComplete = progress_complete;
						status.ProgressTotal = progress_total;
						sync->Open();
						SafePointer<DataBlock> status_data = SerializeNodeStatus(status);
						ServerSendMessage(0x00000203, from, status_data);
					} else if (verb == 0x00000301) {
						ServerSendMessage(0x00000302, MakeObjectAddress(AddressLevelNode, AddressServiceNode, AddressNodeUndefined, AddressInstanceUnique), data);
						ServerSendMessage(0x00000303, MakeObjectAddress(AddressLevelClient, AddressServiceTextLogger, GetAddressNode(GetLoopbackAddress()), AddressInstanceUnique), data);
					} else if (verb == 0x00000302) {
						ServerSendMessage(0x00000303, MakeObjectAddress(AddressLevelClient, AddressServiceTextLogger, GetAddressNode(GetLoopbackAddress()), AddressInstanceUnique), data);
					} else if (verb == 0x00000411) {
						ComputationRentClass::PerformRent(from, data);
					} else if (verb == 0x00010411) {
						ComputationRentClass::HandleHostStarted(from);
					} else if (verb == 0x00000412) {
						ComputationRentClass::KillHost(from);
					} else if (verb == 0x00000413) {
						int sep = -1;
						for (int i = 0; i < data->Length(); i++) if (data->ElementAt(i) == 0) { sep = i; break; }
						if (sep >= 0) {
							SafePointer<Streaming::Stream> stream;
							auto client_from = from;
							auto words = string(data->GetBuffer(), -1, Encoding::UTF8);
							auto watchdog = CreateFunctionalTask([client_from]() {
								ServerSendMessage(0x00010413, client_from, 0);
							});
							auto exit = CreateStructuredTask<string, string>([client_from](const string & exec, const string & root) {
								SafePointer<DataBlock> result = new DataBlock(0x1000);
								if (root.Length()) {
									result->Append(1);
									CompilerHostInstance::MakePackage(result, root, exec);
								} else {
									result->Append(0);
									SafePointer<DataBlock> error = exec.EncodeSequence(Encoding::UTF8, false);
									result->Append(*error);
								}
								ServerSendMessage(0x00010413, client_from, result);
							});
							try {
								stream = new Streaming::MemoryStream(data->GetBuffer() + sep + 1, data->Length() - sep - 1);
							} catch (...) { return; }
							CompilerHostInstance::PerformCompilation(words, stream, &exit->Value1, &exit->Value2, watchdog, exit, client_from);
						}
					} else if (verb == 0x00000414) {
						CompilerHostInstance::TerminateCompiler(from);
					} else if (verb == 0x00000415) {
						CompilerHostInstance::HandleCompilerResponce(data);
					} else if (verb == 0x00000416) {
						if (data && data->Length()) ServerServiceAllowTasks(data->ElementAt(0));
					} else if (verb == 0x00010423) {
						uint pending, inproc, complete, total;
						if (data && data->Length() >= 16) {
							MemoryCopy(&pending, data->GetBuffer(), 4);
							MemoryCopy(&inproc, data->GetBuffer() + 4, 4);
							MemoryCopy(&complete, data->GetBuffer() + 8, 4);
							MemoryCopy(&total, data->GetBuffer() + 12, 4);
							ServerServiceUpdateProgressStatus(complete, total);
						}
					}
				} catch (...) {}
			}
			void UpdateProgressStatus(uint32 complete, uint32 total)
			{
				sync->Wait();
				progress_complete = complete;
				progress_total = total;
				sync->Open();
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
		void ServerServiceUpdateProgressStatus(uint32 complete, uint32 total) { if (server_service_callback) server_service_callback->UpdateProgressStatus(complete, total); }
		void ServerServiceTerminateHost(void) { ComputationRentClass::KillHost(0); }
		void ServerServiceAllowTasks(bool allow) { ComputationRentClass::AllowTasks(allow); }
		void ServerServiceAllowTasks(ObjectAddress node_on, bool allow)
		{
			try {
				SafePointer<DataBlock> data = new DataBlock(1);
				data->Append(allow ? 1 : 0);
				ServerSendMessage(0x00000416, node_on, data);
			} catch (...) {}
		}
		void ServerServiceRegisterCallback(IServerServiceNotificationCallback * callback) { if (server_service_callback) server_service_callback->RegisterCallback(callback); }
		void ServerServiceUnregisterCallback(IServerServiceNotificationCallback * callback) { if (server_service_callback) server_service_callback->UnregisterCallback(callback); }
	}
}