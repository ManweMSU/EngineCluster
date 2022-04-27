#pragma once

#include <EngineRuntime.h>

namespace Engine
{
	namespace Cluster
	{
		constexpr uint32 HostStarted			= 0x0000;
		constexpr uint32 HostIsStarting			= 0x0001;
		constexpr uint32 HostTimedOut			= 0x8001;
		constexpr uint32 HostBusyNow			= 0x8002;
		constexpr uint32 HostDiscarded			= 0x8003;
		constexpr uint32 HostNoArchitecture		= 0x8004;
		constexpr uint32 HostExtractionFailed	= 0x8005;
		constexpr uint32 HostCompilationFailed	= 0x8006;
		constexpr uint32 HostLaunchFailed		= 0x8007;
		constexpr uint32 HostInitFailed			= 0x8008;

		constexpr uint32 TaskErrorSuccess			= 0x0000;
		constexpr uint32 TaskErrorInitializing		= 0x0001;
		constexpr uint32 TaskErrorWorking			= 0x0002;
		constexpr uint32 TaskErrorNoNodesAvailable	= 0xC001;
		constexpr uint32 TaskErrorInternalFailure	= 0xC002;
	}
}