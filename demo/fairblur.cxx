#include "fairblur.h"

#include "../client/ClusterTaskClient.h"

using namespace Engine;
using namespace Engine::Codec;
using namespace Engine::Streaming;
using namespace Engine::Windows;
using namespace Engine::Graphics;
using namespace Engine::Assembly;
using namespace Engine::UI;

InterfaceTemplate interface;
SafePointer<DataBlock> kernel;

class DialogCallback : public IEventCallback
{
public:
	SafePointer<Frame> input;
	double sigma;
	bool allow_migration, run;
	DialogCallback(void) { sigma = 30.0; allow_migration = true; run = false; }
	virtual void Created(IWindow * window) override
	{
		SafePointer<I2DDeviceContextFactory> factory = CreateDeviceContextFactory();
		SafePointer<IBitmap> bitmap = factory->LoadBitmap(input);
		FindControl(window, 101)->As<Controls::Static>()->SetImage(bitmap);
		GetRootControl(window)->AddDialogStandardAccelerators();
	}
	virtual void HandleControlEvent(IWindow * window, int id, ControlEvent event, Control * sender) override
	{
		if (id == 1) {
			try {
				sigma = FindControl(window, 102)->GetText().ToDouble();
				allow_migration = FindControl(window, 103)->As<Controls::CheckBox>()->IsChecked();
			} catch (...) { return; }
			run = true;
			GetWindowSystem()->ExitModalSession(window);
		} else if (id == 2) WindowClose(window);
	}
	virtual void WindowClose(IWindow * window) override { GetWindowSystem()->ExitModalSession(window); }
};

int Main(void)
{
	{
		SafePointer<IScreen> main = GetDefaultScreen();
		CurrentLocale = GetCurrentUserLocale();
		if (CurrentLocale != L"ru") CurrentLocale = L"en";
		CurrentScaleFactor = main->GetDpiScale();
		if (CurrentScaleFactor > 1.75) CurrentScaleFactor = 2.0;
		else if (CurrentScaleFactor > 1.25) CurrentScaleFactor = 1.5;
		else CurrentScaleFactor = 1.0;
		SafePointer<Stream> com_stream = QueryLocalizedResource(L"COM");
		SafePointer<Storage::StringTable> com = new Storage::StringTable(com_stream);
		SetLocalizedCommonStrings(com);
		SafePointer<Stream> ui = QueryResource(L"UI");
		Loader::LoadUserInterfaceFromBinary(interface, ui);
		SafePointer<Stream> krnl = QueryResource(L"KRNL");
		kernel = krnl->ReadAll();
	}
	OpenFileInfo info;
	info.Formats << FileFormat();
	info.Formats.LastElement().Description = *interface.Strings[L"TextImage"];
	info.Formats.LastElement().Extensions << L"bmp";
	info.Formats.LastElement().Extensions << L"png";
	info.Formats.LastElement().Extensions << L"jpg";
	info.Formats.LastElement().Extensions << L"jpeg";
	GetWindowSystem()->OpenFileDialog(&info, 0, 0);
	if (info.Files.Length()) {
		SafePointer<Frame> input;
		try {
			SafePointer<Stream> stream = new FileStream(info.Files[0], AccessRead, OpenExisting);
			input = DecodeFrame(stream);
			if (!input) throw Exception();
			input = input->ConvertFormat(Codec::PixelFormat::R8G8B8X8, AlphaMode::Straight, ScanOrigin::TopDown, input->GetWidth() * 4);
		} catch (...) {
			GetWindowSystem()->MessageBox(0, *interface.Strings[L"TextLoadFailed"], ENGINE_VI_APPNAME, 0,
				MessageBoxButtonSet::Ok, MessageBoxStyle::Warning, 0);
			return 1;
		}
		DialogCallback callback;
		callback.input = input;
		CreateModalWindow(interface.Dialog[L"Startup"], &callback, Rectangle::Entire());
		if (!callback.run) return 0;
		FairBlurInput input_struct;
		input_struct.Width = input->GetWidth();
		input_struct.Height = input->GetHeight();
		input_struct.Pixel.SetLength(input_struct.Width * input_struct.Height);
		input_struct.Sigma = callback.sigma;
		input_struct.AllowMigration = callback.allow_migration;
		MemoryCopy(input_struct.Pixel.GetBuffer(), input->GetData(), input_struct.Width * input_struct.Height * 4);
		auto start = Time::GetCurrentTime();
		SafePointer<Cluster::TaskClient> client = new Cluster::TaskClient;
		try {
			client->SetTaskExecutable(kernel);
			client->SetTaskInput(input_struct);
			client->Start();
			client->Wait();
		} catch (...) {
			GetWindowSystem()->MessageBox(0, *interface.Strings[L"TextNoCluster"], ENGINE_VI_APPNAME, 0, MessageBoxButtonSet::Ok,
				MessageBoxStyle::Warning, 0);
			return 1;
		}
		auto status = client->GetTaskResultStatus();
		if (status == Cluster::TaskErrorSuccess) {
			auto time = Time::GetCurrentTime() - start;
			SafePointer<DataBlock> result;
			client->GetTaskResult(result.InnerRef());
			MemoryCopy(input->GetData(), result->GetBuffer(), result->Length());
			try {
				SafePointer<Stream> output = new FileStream(info.Files[0] + L".out-" + string(double(time.Ticks) / 60000) + L".png",
					AccessReadWrite, CreateNew);
				Codec::EncodeFrame(output, input, ImageFormatPNG);
			} catch (...) {
				GetWindowSystem()->MessageBox(0, *interface.Strings[L"TextSaveFailed"], ENGINE_VI_APPNAME, 0, MessageBoxButtonSet::Ok,
					MessageBoxStyle::Warning, 0);
			}
		} else if (status == Cluster::TaskErrorNoNodesAvailable) {
			GetWindowSystem()->MessageBox(0, *interface.Strings[L"TextNoNodes"], ENGINE_VI_APPNAME, 0, MessageBoxButtonSet::Ok,
				MessageBoxStyle::Warning, 0);
		} else if (status == Cluster::TaskErrorInternalFailure) {
			GetWindowSystem()->MessageBox(0, *interface.Strings[L"TextFailure"], ENGINE_VI_APPNAME, 0, MessageBoxButtonSet::Ok,
				MessageBoxStyle::Warning, 0);
		} else {
			GetWindowSystem()->MessageBox(0, *interface.Strings[L"TextUnknown"], ENGINE_VI_APPNAME, 0, MessageBoxButtonSet::Ok,
				MessageBoxStyle::Warning, 0);
		}
	}
	return 0;
}