#include <EngineRuntime.h>

#include "../../client/ClusterTaskInterfaces.h"

using namespace Engine;
using namespace Engine::Cluster;
using namespace Engine::Codec;

SafePointer<Codec::Frame> input, output;
SafePointer<Semaphore> init;
uint32 rows_left;

ENGINE_CLUSTER_REFLECTED_TASK(MergeTask)
	ENGINE_DEFINE_REFLECTED_PROPERTY(INTEGER, y);
	ENGINE_DEFINE_REFLECTED_ARRAY(UINT32, data);
	MergeTask(void) {}
	virtual void DoTask(IHostEnvironment * environment) noexcept override
	{
		MemoryCopy(output->GetData() + y * output->GetScanLineLength(), data.GetBuffer(), data.Length() * 4);
		if (!InterlockedDecrement(rows_left)) {
			SafePointer<DataBlock> result = new DataBlock(1);
			auto length = output->GetWidth() * output->GetHeight() * 4;
			result->SetLength(length);
			MemoryCopy(result->GetBuffer(), output->GetData(), length);
			environment->DeclareOutputAndTerminate(result);
		}
	}
ENGINE_CLUSTER_END_REFLECTED_TASK
ENGINE_CLUSTER_REFLECTED_TASK(RowTask)
	ENGINE_DEFINE_REFLECTED_PROPERTY(INTEGER, y);
	ENGINE_DEFINE_REFLECTED_PROPERTY(DOUBLE, sigma);
	RowTask(void) {}
	virtual void DoTask(IHostEnvironment * environment) noexcept override
	{
		init->Wait();
		init->Open();
		MergeTask finalize;
		finalize.y = y;
		finalize.data.SetLength(input->GetWidth());
		int bound = int(3.0 * sigma);
		double D = sigma * sigma;
		double norm = 1.0 / (2.0 * ENGINE_PI * D);
		double denom = -1.0 / (2.0 * D);
		int w = input->GetWidth();
		int h = input->GetHeight();
		for (int x = 0; x < w; x++) {
			double weight = 0.0;
			Math::ColorF color = Math::ColorF(0.0, 0.0, 0.0, 0.0);
			for (int j = -bound; j <= bound; j++) for (int i = -bound; i <= bound; i++) {
				if (x + i < 0 || x + i >= w || y + j < 0 || y + j >= h) continue;
				double rr = double(j * j + i * i);
				double p = norm * exp(denom * rr);
				Math::ColorF current = Color(input->GetPixel(x + i, y + j));
				color += p * current;
				weight += p;
			}
			if (weight) color /= weight;
			color.a = 0.0;
			finalize.data[x] = Color(color).Value;
		}
		environment->PerformTask(finalize, environment->GetPrimaryNodeAddress(), TaskFlagFirstPriority);
	}
ENGINE_CLUSTER_END_REFLECTED_TASK
ENGINE_CLUSTER_REFLECTED_TASK(Main)
	ENGINE_DEFINE_REFLECTED_PROPERTY(INTEGER, Width);
	ENGINE_DEFINE_REFLECTED_PROPERTY(INTEGER, Height);
	ENGINE_DEFINE_REFLECTED_ARRAY(UINT32, Pixel);
	ENGINE_DEFINE_REFLECTED_PROPERTY(DOUBLE, Sigma);
	ENGINE_DEFINE_REFLECTED_PROPERTY(BOOLEAN, AllowMigration);
	virtual void DoTask(IHostEnvironment * environment) noexcept override
	{
		auto self = environment->GetThisNodeAddress();
		auto primary = environment->GetPrimaryNodeAddress();
		input = new Codec::Frame(Width, Height, 4 * Width, Codec::PixelFormat::R8G8B8X8, Codec::AlphaMode::Straight, Codec::ScanOrigin::TopDown);
		MemoryCopy(input->GetData(), Pixel.GetBuffer(), Width * Height * 4);
		init->Open();
		if (self == primary) {
			Array<ObjectAddress> nodes(1);
			nodes.SetLength(environment->GetNodesCount());
			environment->GetNodesAddresses(nodes);
			for (auto & n : nodes) if (n != self) environment->PerformTask(*this, n, TaskFlagFirstPriority);
			Pixel.SetLength(0);
			rows_left = input->GetHeight();
			output = new Codec::Frame(input);
			for (int i = 0; i < input->GetHeight(); i++) {
				RowTask row;
				row.y = i;
				row.sigma = Sigma;
				uint32 flags = TaskFlagProgressAccount;
				if (AllowMigration) flags |= TaskFlagAllowMigration;
				environment->PerformTask(row, flags);
			}
		}
	}
ENGINE_CLUSTER_END_REFLECTED_TASK
ENGINE_CLUSTER_BINARY_TASK(Init)
	virtual void Deserialize(const DataBlock * data) noexcept override {}
	virtual DataBlock * Serialize(void) noexcept override { return 0; }
	virtual void DoTask(IHostEnvironment * environment) noexcept override { init = CreateSemaphore(0); }
ENGINE_CLUSTER_END_BINARY_TASK

ENGINE_CLUSTER_TASK_FACTORY
	ENGINE_CLUSTER_REGISTER_TASK(Init)
	ENGINE_CLUSTER_REGISTER_TASK(Main)
	ENGINE_CLUSTER_REGISTER_TASK(RowTask)
	ENGINE_CLUSTER_REGISTER_TASK(MergeTask)
ENGINE_CLUSTER_END_TASK_FACTORY