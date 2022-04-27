#pragma once

#include "ClusterClient.h"

namespace Engine
{
	namespace Cluster
	{
		enum TaskFlags : uint32 {
			TaskFlagAllowMigration	= 0x0001,
			TaskFlagFirstPriority	= 0x0002,
			TaskFlagProgressAccount	= 0x0004,
			TaskFlagMask			= TaskFlagAllowMigration | TaskFlagFirstPriority | TaskFlagProgressAccount,
		};

		class IHostEnvironment;
		class ITask;
		class IReflectedTask;
		class ITaskFactory;

		class IHostEnvironment : public Object
		{
		public:
			virtual int GetNodesCount(void) noexcept = 0;
			virtual void GetNodesAddresses(ObjectAddress * addresses) noexcept = 0;
			virtual ObjectAddress GetThisNodeAddress(void) noexcept = 0;
			virtual ObjectAddress GetPrimaryNodeAddress(void) noexcept = 0;

			virtual void PerformTask(const string & class_name, const DataBlock * data, uint32 flags = TaskFlagAllowMigration | TaskFlagProgressAccount) noexcept = 0;
			virtual void PerformTask(const string & class_name, Reflection::Reflected & data, uint32 flags = TaskFlagAllowMigration | TaskFlagProgressAccount) noexcept = 0;
			virtual void PerformTask(ITask & task, uint32 flags = TaskFlagAllowMigration | TaskFlagProgressAccount) noexcept = 0;
			virtual void PerformTask(const string & class_name, const DataBlock * data, ObjectAddress on_node, uint32 flags = TaskFlagProgressAccount) noexcept = 0;
			virtual void PerformTask(const string & class_name, Reflection::Reflected & data, ObjectAddress on_node, uint32 flags = TaskFlagProgressAccount) noexcept = 0;
			virtual void PerformTask(ITask & task, ObjectAddress on_node, uint32 flags = TaskFlagProgressAccount) noexcept = 0;

			virtual void ExplicitlyDeclareProgress(uint32 complete, uint32 total) noexcept = 0;
			virtual void ReportMessage(const string & text) noexcept = 0;

			virtual void DeclareOutput(const DataBlock * data) noexcept = 0;
			virtual void DeclareOutput(Reflection::Reflected & data) noexcept = 0;
			virtual void DeclareOutputAndTerminate(const DataBlock * data) noexcept = 0;
			virtual void DeclareOutputAndTerminate(Reflection::Reflected & data) noexcept = 0;

			virtual void Terminate(void) noexcept = 0;
		};
		class ITask : public Object
		{
		public:
			virtual const widechar * GetClassName(void) const noexcept = 0;
			virtual void Deserialize(const DataBlock * data) noexcept = 0;
			virtual DataBlock * Serialize(void) noexcept = 0;
			virtual void DoTask(IHostEnvironment * environment) noexcept = 0;
		};
		class IReflectedTask : public ITask, public Reflection::Reflected {};
		class ITaskFactory : public Object
		{
		public:
			virtual ITask * DeserializeTask(const string & class_name, const DataBlock * data) const noexcept = 0;
		};
	}
}

#define ENGINE_CLUSTER_BINARY_TASK(name) class name : public ::Engine::Cluster::ITask { \
public: name(const ::Engine::DataBlock * data) { Deserialize(data); } \
public: virtual const ::Engine::widechar * GetClassName(void) const noexcept override final { return ENGINE_STRING(name); }

#define ENGINE_CLUSTER_END_BINARY_TASK };

#define ENGINE_CLUSTER_REFLECTED_TASK(name) ENGINE_REFLECTED_CLASS(name, ::Engine::Cluster::IReflectedTask) \
public: name(const ::Engine::DataBlock * data) { Deserialize(data); } \
public: virtual const ::Engine::widechar * GetClassName(void) const noexcept override final { return ENGINE_STRING(name); } \
public: virtual void Deserialize(const ::Engine::DataBlock * data) noexcept override final { \
try { ::Engine::Streaming::MemoryStream stream(data->GetBuffer(), data->Length()); \
::Engine::Reflection::RestoreFromBinaryObject(*this, &stream); } catch (...) {} \
} \
public: virtual ::Engine::DataBlock * Serialize(void) noexcept override final { \
try { \
::Engine::Streaming::MemoryStream stream(0x1000); \
::Engine::Reflection::SerializeToBinaryObject(*this, &stream); \
stream.Seek(0, ::Engine::Streaming::Begin); \
return stream.ReadAll(); \
} catch (...) { return 0; } }

#define ENGINE_CLUSTER_END_REFLECTED_TASK ENGINE_END_REFLECTED_CLASS

#define ENGINE_CLUSTER_TASK_FACTORY class __cluster_task_factory : public ::Engine::Cluster::ITaskFactory { \
public: __cluster_task_factory(void) {} \
public: virtual ::Engine::Cluster::ITask * DeserializeTask(const ::Engine::string & class_name, const ::Engine::DataBlock * data) const noexcept override {

#define ENGINE_CLUSTER_REGISTER_TASK(name) if (class_name == ENGINE_STRING(name)) return new name(data);

#define ENGINE_CLUSTER_END_TASK_FACTORY return 0; } }; \
ENGINE_EXPORT_API ::Engine::Cluster::ITaskFactory * CreateTaskFactory(void) { return new __cluster_task_factory; } \
void LibraryLoaded(void) {} \
void LibraryUnloaded(void) {}