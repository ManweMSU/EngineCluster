#include <EngineRuntime.h>

#include "srvrnet.h"
#include "srvrsrvc.h"
#include "docview.h"

#define ECF_IPC_VERB_ACTIVATE	L"ACTIVATE"
#define ECF_IPC_VERB_TERMINATE	L"TERMINATE"
#define ECF_IPC_VERB_GETPORT	L"PORT"

using namespace Engine;
using namespace Engine::Windows;
using namespace Engine::Graphics;
using namespace Engine::UI;
using namespace Engine::Assembly;
using namespace Engine::Streaming;
using namespace Engine::Cluster;

Array<IWindow *> windows(0x10);
IBitmap * greenlight, * graylight, * power_self, * power_charging, * power_discharging, * power_net;
IWindow * main_window, * about_window;
uint window_visibility_counter;
uint modal_counter;
bool is_init_mode;
InterfaceTemplate interface;

namespace Structures
{
	ENGINE_REFLECTED_CLASS(NetElement, Reflection::Reflected)
		ENGINE_DEFINE_REFLECTED_PROPERTY(TEXTURE, Greenlight);
		ENGINE_DEFINE_REFLECTED_PROPERTY(STRING, Name);
		ENGINE_DEFINE_REFLECTED_PROPERTY(STRING, Autoconnect);
		ENGINE_DEFINE_REFLECTED_PROPERTY(STRING, UUID);
	ENGINE_END_REFLECTED_CLASS
	ENGINE_REFLECTED_CLASS(NodeElement, Reflection::Reflected)
		ENGINE_DEFINE_REFLECTED_PROPERTY(TEXTURE, Greenlight);
		ENGINE_DEFINE_REFLECTED_PROPERTY(STRING, Name);
		ENGINE_DEFINE_REFLECTED_PROPERTY(TEXTURE, PowerSource);
		ENGINE_DEFINE_REFLECTED_PROPERTY(STRING, PowerLevel);
		ENGINE_DEFINE_REFLECTED_PROPERTY(STRING, Load);
		ENGINE_DEFINE_REFLECTED_PROPERTY(DOUBLE, Progress);
	ENGINE_END_REFLECTED_CLASS
}

void RegisterWindow(IWindow * window)
{
	windows << window;
}
void UnregisterWindow(IWindow * window)
{
	for (int i = 0; i < windows.Length(); i++) if (windows[i] == window) { windows.Remove(i); break; }
	if (!windows.Length() && !is_init_mode) GetWindowSystem()->ExitMainLoop();
}
void DisplayWindow(IWindow * window)
{
	if (!window->IsVisible()) {
		window->Show(true);
		auto inc = InterlockedIncrement(window_visibility_counter);
		if (inc == 1) GetWindowSystem()->SetApplicationIconVisibility(true);
	} else window->Activate();
}
void HideWindow(IWindow * window)
{
	if (window->IsVisible()) {
		window->Show(false);
		auto dec = InterlockedDecrement(window_visibility_counter);
		if (dec == 0 && !is_init_mode) GetWindowSystem()->SetApplicationIconVisibility(false);
	}
}
void RunHelp(void) { RunDocumentationWindow(interface); }

class ServerSetupCallback : public IEventCallback, public IApplicationCallback
{
	IWindow * self;
	UUID uuid;
	string name;
public:
	ServerSetupCallback(void) { self = 0; }
	bool IsValidNameUUID(void) { return name.Length() && !IsZeroUUID(&uuid); }
	virtual bool IsHandlerEnabled(ApplicationHandler event) override
	{
		if (event == ApplicationHandler::Terminate) return true;
		else return false;
	}
	virtual bool IsWindowEventAccessible(WindowHandler handler) override
	{
		if (handler == WindowHandler::Copy) return true;
		else if (handler == WindowHandler::Cut) return true;
		else if (handler == WindowHandler::Delete) return true;
		else if (handler == WindowHandler::Paste) return true;
		else if (handler == WindowHandler::Redo) return true;
		else if (handler == WindowHandler::SelectAll) return true;
		else if (handler == WindowHandler::Undo) return true;
		else return false;
	}
	virtual bool Terminate(void) override
	{
		if (self) HandleControlEvent(self, 2, ControlEvent::AcceleratorCommand, 0);
		return true;
	}
	virtual void Created(IWindow * window) override
	{
		self = window;
		GetRootControl(window)->AddDialogStandardAccelerators();
		FindControl(window, 1)->Enable(false);
		name = L"";
		SafePointer<DataBlock> random = Cryptography::CreateSecureRandom(sizeof(UUID));
		MemoryCopy(&uuid, random->GetBuffer(), sizeof(UUID));
		FindControl(window, 102)->SetText(MakeStringOfUUID(&uuid));
	}
	virtual void WindowClose(IWindow * window) override { HandleControlEvent(window, 2, ControlEvent::AcceleratorCommand, 0); }
	virtual void HandleControlEvent(Windows::IWindow * window, int ID, ControlEvent event, Control * sender) override
	{
		if (ID == 1) {
			if (IsValidNameUUID()) {
				SetServerName(name);
				SetServerUUID(&uuid);
				window->Show(false);
				GetWindowSystem()->ExitMainLoop();
			}
		} else if (ID == 2) {
			window->Show(false);
			GetWindowSystem()->ExitMainLoop();
		} else if (ID == 3) {
			RunHelp();
		} else if (ID == 101 && event == ControlEvent::ValueChange) {
			name = sender->GetText();
			FindControl(window, 1)->Enable(IsValidNameUUID());
		} else if (ID == 102 && event == ControlEvent::ValueChange) {
			if (!MakeUUIDOfString(sender->GetText(), &uuid)) ZeroMemory(&uuid, sizeof(uuid));
			FindControl(window, 1)->Enable(IsValidNameUUID());
		}
		
	}
	static bool RunSetup(void)
	{
		is_init_mode = true;
		ServerSetupCallback callback;
		auto stored_callback = GetWindowSystem()->GetCallback();
		GetWindowSystem()->SetCallback(&callback);
		GetWindowSystem()->SetApplicationIconVisibility(true);
		auto window = CreateWindow(interface.Dialog[L"SelfSetup"], &callback, Rectangle::Entire());
		window->Show(true);
		GetWindowSystem()->RunMainLoop();
		window->Destroy();
		GetWindowSystem()->SetCallback(stored_callback);
		GetWindowSystem()->SetApplicationIconVisibility(false);
		CloseDocumentationWindow();
		is_init_mode = false;
		return GetServerName().Length() > 0;
	}
};
class NewNetSetupCallback : public IEventCallback
{
	SafePointer<IDispatchTask> _task;
	UUID uuid;
	string name;
	uint16 port;
public:
	bool IsValidNamePortUUID(void) { return name.Length() && !IsZeroUUID(&uuid) && port; }
	virtual void Created(IWindow * window) override
	{
		InterlockedIncrement(modal_counter);
		GetRootControl(window)->AddDialogStandardAccelerators();
		FindControl(window, 1)->Enable(false);
		name = L"";
		SafePointer<DataBlock> random = Cryptography::CreateSecureRandom(sizeof(UUID));
		MemoryCopy(&uuid, random->GetBuffer(), sizeof(UUID));
		FindControl(window, 102)->SetText(MakeStringOfUUID(&uuid));
		port = ClusterServerDefaultPort;
		FindControl(window, 103)->SetText(uint32(port));
	}
	virtual void Destroyed(IWindow * window) override { InterlockedDecrement(modal_counter); delete this; }
	virtual void WindowClose(IWindow * window) override { HandleControlEvent(window, 2, ControlEvent::AcceleratorCommand, 0); }
	virtual void HandleControlEvent(Windows::IWindow * window, int ID, ControlEvent event, Control * sender) override
	{
		if (ID == 1) {
			if (IsValidNamePortUUID() && ServerNetCreate(name, &uuid, port)) {
				GetWindowSystem()->SubmitTask(_task);
				GetWindowSystem()->ExitModalSession(window);
			}
		} else if (ID == 2) {
			GetWindowSystem()->ExitModalSession(window);
		} else if (ID == 101 && event == ControlEvent::ValueChange) {
			name = sender->GetText();
			FindControl(window, 1)->Enable(IsValidNamePortUUID());
		} else if (ID == 102 && event == ControlEvent::ValueChange) {
			if (!MakeUUIDOfString(sender->GetText(), &uuid)) ZeroMemory(&uuid, sizeof(uuid));
			FindControl(window, 1)->Enable(IsValidNamePortUUID());
		} else if (ID == 103 && event == ControlEvent::ValueChange) {
			try {
				port = sender->GetText().ToUInt32();
				if (port > 0xFFFF) throw InvalidArgumentException();
			} catch (...) { port = 0; }
			FindControl(window, 1)->Enable(IsValidNamePortUUID());
		}
	}
	static void NewNetDialog(IWindow * parent, IDispatchTask * on_created)
	{
		auto callback = new NewNetSetupCallback;
		callback->_task.SetRetain(on_created);
		CreateModalWindow(interface.Dialog[L"NewNetSetup"], callback, Rectangle::Entire(), parent);
	}
};
class JoinNetSetupCallback : public IEventCallback
{
public:
	string dest;
	uint16 my_port, dest_port;
	bool connecting;
	bool IsValidAddressPort(void) { return my_port && dest_port && dest.Length() && !connecting; }
	virtual void Created(IWindow * window) override
	{
		connecting = false;
		InterlockedIncrement(modal_counter);
		GetRootControl(window)->AddDialogStandardAccelerators();
		FindControl(window, 1)->Enable(false);
		ZeroMemory(&dest, sizeof(dest));
		my_port = dest_port = ClusterServerDefaultPort;
		FindControl(window, 102)->SetText(uint32(my_port));
		FindControl(window, 103)->SetText(uint32(my_port));
	}
	virtual void Destroyed(IWindow * window) override { InterlockedDecrement(modal_counter); delete this; }
	virtual void WindowClose(IWindow * window) override { HandleControlEvent(window, 2, ControlEvent::AcceleratorCommand, 0); }
	virtual void HandleControlEvent(Windows::IWindow * window, int ID, ControlEvent event, Control * sender) override
	{
		if (ID == 1 && !connecting) {
			if (IsValidAddressPort()) {
				connecting = true;
				FindControl(window, 1)->Enable(false);
				FindControl(window, 2)->Enable(false);
				FindControl(window, 101)->Enable(false);
				FindControl(window, 102)->Enable(false);
				FindControl(window, 103)->Enable(false);
				window->SetCloseButtonState(CloseButtonState::Disabled);
				auto wnd = window;
				auto self = this;
				ServerNetJoin(dest, dest_port, my_port, CreateFunctionalTask([wnd]() {
					GetWindowSystem()->ExitModalSession(wnd);
				}), CreateFunctionalTask([wnd, self]() {
					self->connecting = false;
					FindControl(wnd, 1)->Enable(true);
					FindControl(wnd, 2)->Enable(true);
					FindControl(wnd, 101)->Enable(true);
					FindControl(wnd, 102)->Enable(true);
					FindControl(wnd, 103)->Enable(true);
					wnd->SetCloseButtonState(CloseButtonState::Enabled);
					GetWindowSystem()->MessageBox(0, *interface.Strings[L"TextFailedToJoin"], ENGINE_VI_APPNAME,
						wnd, MessageBoxButtonSet::Ok, MessageBoxStyle::Warning, 0);
				}));
			}
		} else if (ID == 2 && !connecting) {
			GetWindowSystem()->ExitModalSession(window);
		} else if (ID == 101 && event == ControlEvent::ValueChange) {
			dest = sender->GetText();
			FindControl(window, 1)->Enable(IsValidAddressPort());
		} else if (ID == 102 && event == ControlEvent::ValueChange) {
			try {
				dest_port = sender->GetText().ToUInt32();
				if (dest_port > 0xFFFF) throw InvalidArgumentException();
			} catch (...) { dest_port = 0; }
			FindControl(window, 1)->Enable(IsValidAddressPort());
		} else if (ID == 103 && event == ControlEvent::ValueChange) {
			try {
				my_port = sender->GetText().ToUInt32();
				if (my_port > 0xFFFF) throw InvalidArgumentException();
			} catch (...) { my_port = 0; }
			FindControl(window, 1)->Enable(IsValidAddressPort());
		}
	}
	static void JoinNetDialog(IWindow * parent)
	{
		auto callback = new JoinNetSetupCallback;
		CreateModalWindow(interface.Dialog[L"JoinNetSetup"], callback, Rectangle::Entire(), parent);
	}
};
class ShowNodeInfoCallback : public IEventCallback
{
	ObjectAddress _address;
	NodeSystemInfo _info;
	ENGINE_REFLECTED_CLASS(_list_element, Reflection::Reflected)
		ENGINE_DEFINE_REFLECTED_PROPERTY(STRING, Property);
		ENGINE_DEFINE_REFLECTED_PROPERTY(STRING, Value);
	ENGINE_END_REFLECTED_CLASS
	void _add_list_item(Controls::ListView * view, const string & prop, const string & value)
	{
		_list_element element;
		element.Property = prop;
		element.Value = value;
		view->AddItem(element);
	}
public:
	virtual void Created(IWindow * window) override
	{
		InterlockedIncrement(modal_counter);
		GetRootControl(window)->AddDialogStandardAccelerators();
		auto list = FindControl(window, 101)->As<Controls::ListView>();
		_add_list_item(list, *interface.Strings[L"TextNodeName"], _info.NodeName);
		_add_list_item(list, *interface.Strings[L"TextNodeAddress"], string(_address, HexadecimalBase, 16));
		_add_list_item(list, *interface.Strings[L"TextUUIDNS"], MakeStringOfUUID(&_info.Identifier));
		if (_info.System == OperatingSystem::Windows) {
			_add_list_item(list, *interface.Strings[L"TextOS"], *interface.Strings[L"TextWindowsOS"]);
		} else if (_info.System == OperatingSystem::MacOS) {
			_add_list_item(list, *interface.Strings[L"TextOS"], *interface.Strings[L"TextMacOS"]);
		} else {
			_add_list_item(list, *interface.Strings[L"TextOS"], *interface.Strings[L"TextUnknown"]);
		}
		_add_list_item(list, *interface.Strings[L"TextVersionOS"], FormatString(L"%0.%1", _info.SystemVersionMajor, _info.SystemVersionMinor));
		_add_list_item(list, *interface.Strings[L"TextMemory"], FormatString(*interface.Strings[L"TextMemoryStringMB"], _info.PhysicalMemory / 0x100000));
		_add_list_item(list, *interface.Strings[L"TextNameCPU"], _info.ProcessorName);
		if (_info.Architecture == Platform::X86) {
			_add_list_item(list, *interface.Strings[L"TextArchCPU"], *interface.Strings[L"TextArchX86"]);
		} else if (_info.Architecture == Platform::X64) {
			_add_list_item(list, *interface.Strings[L"TextArchCPU"], *interface.Strings[L"TextArchX64"]);
		} else if (_info.Architecture == Platform::ARM) {
			_add_list_item(list, *interface.Strings[L"TextArchCPU"], *interface.Strings[L"TextArchARM"]);
		} else if (_info.Architecture == Platform::ARM64) {
			_add_list_item(list, *interface.Strings[L"TextArchCPU"], *interface.Strings[L"TextArchARM64"]);
		} else {
			_add_list_item(list, *interface.Strings[L"TextArchCPU"], *interface.Strings[L"TextUnknown"]);
		}
		string ghz = string(_info.ClockFrequency / 1000000000UL);
		string mhz = string(_info.ClockFrequency % 1000000000UL / 100000000UL);
		_add_list_item(list, *interface.Strings[L"TextClockCPU"], FormatString(*interface.Strings[L"TextClockCPUHZ"], ghz, mhz));
		_add_list_item(list, *interface.Strings[L"TextCoresPhysical"], _info.PhysicalCores);
		_add_list_item(list, *interface.Strings[L"TextCoresVirtual"], _info.VirtualCores);
	}
	virtual void Destroyed(IWindow * window) override { InterlockedDecrement(modal_counter); delete this; }
	virtual void WindowClose(IWindow * window) override { HandleControlEvent(window, 2, ControlEvent::AcceleratorCommand, 0); }
	virtual void HandleControlEvent(Windows::IWindow * window, int ID, ControlEvent event, Control * sender) override
	{
		if (ID == 1 || ID == 2) GetWindowSystem()->ExitModalSession(window);
	}
	static void ShowNodeInfoDialog(IWindow * parent, ObjectAddress address, const NodeSystemInfo & info)
	{
		auto callback = new ShowNodeInfoCallback;
		callback->_info = info;
		callback->_address = address;
		CreateModalWindow(interface.Dialog[L"ShowNodeInfo"], callback, Rectangle::Entire(), parent);
	}
};
class ShowServicesCallback : public IEventCallback
{
	Array<EndpointDesc> _endpoints;
	ENGINE_REFLECTED_CLASS(_list_element, Reflection::Reflected)
		ENGINE_DEFINE_REFLECTED_PROPERTY(STRING, Service);
		ENGINE_DEFINE_REFLECTED_PROPERTY(STRING, ServiceID);
		ENGINE_DEFINE_REFLECTED_PROPERTY(STRING, Address);
	ENGINE_END_REFLECTED_CLASS
public:
	ShowServicesCallback(void) : _endpoints(0x10) {}
	virtual void Created(IWindow * window) override
	{
		InterlockedIncrement(modal_counter);
		GetRootControl(window)->AddDialogStandardAccelerators();
		auto list = FindControl(window, 101)->As<Controls::ListView>();
		for (auto & srvc : _endpoints) {
			_list_element element;
			element.Address = string(srvc.address, HexadecimalBase, 16);
			element.Service = srvc.service_name;
			element.ServiceID = srvc.service_id;
			list->AddItem(element);
		}
	}
	virtual void Destroyed(IWindow * window) override { InterlockedDecrement(modal_counter); delete this; }
	virtual void WindowClose(IWindow * window) override { HandleControlEvent(window, 2, ControlEvent::AcceleratorCommand, 0); }
	virtual void HandleControlEvent(Windows::IWindow * window, int ID, ControlEvent event, Control * sender) override
	{
		if (ID == 1 || ID == 2) GetWindowSystem()->ExitModalSession(window);
	}
	static void ShowServicesDialog(IWindow * parent, const Array<EndpointDesc> & endpoints)
	{
		auto callback = new ShowServicesCallback;
		callback->_endpoints = endpoints;
		CreateModalWindow(interface.Dialog[L"ShowServices"], callback, Rectangle::Entire(), parent);
	}
};
class SelectNodesCallback : public IEventCallback
{
	Array<NodeDesc> _nodes;
public:
	SelectNodesCallback(void) : _nodes(0x10) {}
	virtual void Created(IWindow * window) override
	{
		InterlockedIncrement(modal_counter);
		GetRootControl(window)->AddDialogStandardAccelerators();
		auto list = FindControl(window, 101)->As<Controls::ListBox>();
		UUID uuid;
		if (GetServerCurrentNet(&uuid)) {
			SafePointer< Array<NodeDesc> > nodes = ServerEnumerateNetNodes(&uuid);
			for (auto & n : *nodes) if (n.online) {
				_nodes << n;
				list->AddItem(n.known_name);
			}
		}
	}
	virtual void Destroyed(IWindow * window) override { InterlockedDecrement(modal_counter); delete this; }
	virtual void WindowClose(IWindow * window) override { HandleControlEvent(window, 2, ControlEvent::AcceleratorCommand, 0); }
	virtual void HandleControlEvent(Windows::IWindow * window, int ID, ControlEvent event, Control * sender) override
	{
		if (ID == 1) {
			auto list = FindControl(window, 101)->As<Controls::ListBox>();
			for (int i = 0; i < _nodes.Length(); i++) {
				ServerServiceAllowTasks(_nodes[i].ecf_address, list->IsItemSelected(i));
			}
			GetWindowSystem()->ExitModalSession(window);
		} else if (ID == 2) GetWindowSystem()->ExitModalSession(window);
	}
	static void SelectNodesDialog(IWindow * parent)
	{
		auto callback = new SelectNodesCallback;
		CreateModalWindow(interface.Dialog[L"SelectNodes"], callback, Rectangle::Entire(), parent);
	}
};
class AboutCallback : public IEventCallback
{
	#ifdef ENGINE_WINDOWS
	#define ABOUT_OS L"Windows"
	#endif
	#ifdef ENGINE_MACOSX
	#define ABOUT_OS L"Mac OS"
	#endif
	#ifdef ENGINE_X64
	#ifdef ENGINE_ARM
	#define ABOUT_ARCH L"ARM64"
	#else
	#define ABOUT_ARCH L"x86-64"
	#endif
	#else
	#ifdef ENGINE_ARM
	#define ABOUT_ARCH L"ARM"
	#else
	#define ABOUT_ARCH L"x86"
	#endif
	#endif
	class AnimatedView : public Control
	{
		uint32 duration;
		ObjectArray<IBitmap> frames;
		ObjectArray<IBitmapBrush> brushes;
		Array<uint32> ends;
	public:
		AnimatedView(void) : frames(0x20), brushes(0x20), ends(0x20)
		{
			try {
				SafePointer<Stream> arc_stream = new FileStream(IO::Path::GetDirectory(IO::GetExecutablePath()) + L"/ecfsrvr.ecsa", AccessRead, OpenExisting);
				SafePointer<Storage::Archive> arc = Storage::OpenArchive(arc_stream);
				if (!arc) throw Exception();
				uint32 id = 1;
				if (CurrentScaleFactor > 1.75) id = 3;
				else if (CurrentScaleFactor > 1.25) id = 2;
				auto file = arc->FindArchiveFile(L"IMG", id);
				if (!file) throw Exception();
				SafePointer<Stream> ani_stream = arc->QueryFileStream(file);
				if (!ani_stream) throw Exception();
				SafePointer<Codec::Image> ani = Codec::DecodeImage(ani_stream);
				if (!ani) throw Exception();
				duration = 0;
				SafePointer<I2DDeviceContextFactory> factory = CreateDeviceContextFactory();
				for (auto & f : ani->Frames) {
					duration += f.Duration;
					ends << duration;
					SafePointer<IBitmap> frame = factory->LoadBitmap(&f);
					frames.Append(frame);
					brushes.Append(0);
				}
			} catch (...) { duration = 0; }
		}
		virtual ~AnimatedView(void) override {}
		virtual void Render(Graphics::I2DDeviceContext * device, const Box & at) override
		{
			if (duration) {
				auto time = device->GetAnimationTime() % duration;
				int i = 0, j = ends.Length() - 1;
				while (i != j) {
					int c = (i + j) / 2;
					if (ends[c] > time) j = c; else i = c + 1;
				}
				if (!brushes.ElementAt(i)) {
					auto bitmap = frames.ElementAt(i);
					SafePointer<IBitmapBrush> brush = device->CreateBitmapBrush(bitmap, Box(0, 0, bitmap->GetWidth(), bitmap->GetHeight()), false);
					brushes.SetElement(brush, i);
				}
				device->Render(brushes.ElementAt(i), at);
			}
		}
		virtual void ResetCache(void) override { for (int i = 0; i < brushes.Length(); i++) brushes.SetElement(0, i); }
		virtual Rectangle GetRectangle(void) override { return Rectangle(Coordinate(0, 15.0, 0.0), Coordinate(0, 15.0, 0.0), Coordinate(0, 95.0, 0.0), Coordinate(0, 95.0, 0.0)); }
		virtual string GetControlClass(void) override { return L"AnimatedView"; }
	};
public:
	virtual void Created(IWindow * window) override
	{
		RegisterWindow(window);
		GetRootControl(window)->AddDialogStandardAccelerators();
		auto vt = FindControl(window, 103)->GetText();
		auto pt = FindControl(window, 104)->GetText();
		FindControl(window, 101)->SetText(ENGINE_VI_APPNAME);
		FindControl(window, 102)->SetText(ENGINE_VI_COPYRIGHT);
		FindControl(window, 103)->SetText(FormatString(vt, ENGINE_VI_APPVERSION));
		FindControl(window, 104)->SetText(FormatString(pt, ABOUT_OS, ABOUT_ARCH));
		auto group = FindControl(window, 100);
		SafePointer<AnimatedView> view = new AnimatedView;
		group->AddChild(view);
		group->ArrangeChildren();
		GetControlSystem(window)->SetRefreshPeriod(ControlRefreshPeriod::Cinematic);
	}
	virtual void Destroyed(IWindow * window) override
	{
		UnregisterWindow(window);
		delete this;
		about_window = 0;
	}
	virtual void HandleControlEvent(IWindow * window, int id, ControlEvent event, Control * sender) override { if (id == 1 || id == 2) WindowClose(window); }
	virtual void WindowClose(IWindow * window) override
	{
		HideWindow(window);
		window->Destroy();
	}
	virtual void WindowHelp(IWindow * window) override { GetWindowSystem()->GetCallback()->ShowHelp(); }
	static void Run(void)
	{
		if (!about_window) {
			auto self = new AboutCallback;
			about_window = CreateWindow(interface.Dialog[L"About"], self, Rectangle::Entire());
			DisplayWindow(about_window);
		} else about_window->Activate();
	}
};

class ServerPanelCallback : public IEventCallback, public IServerEventCallback, public IServerServiceNotificationCallback
{
	Array<UUID> _nets;
	Array<ObjectAddress> _nodes;
	Volumes::Dictionary<ObjectAddress, NodeStatusInfo> _statuses;
	SafePointer<Windows::IMenu> _menu;
public:
	ServerPanelCallback(void) : _nets(0x10), _nodes(0x10) {}
	void FillNodeElement(Structures::NodeElement & element, const NodeDesc & node)
	{
		element.Greenlight.SetRetain(node.online ? greenlight : graylight);
		element.Name = node.known_name;
		element.Progress = 0.0;
		auto sd = _statuses[node.ecf_address];
		if (sd && node.online) {
			if (sd->ProgressTotal) {
				if (sd->ProgressTotal == 0xFFFFFFFF) {
					element.Load = *interface.Strings[L"TextInit"];
				} else {
					element.Load = FormatString(*interface.Strings[L"TextReady"], sd->ProgressComplete * 100 / sd->ProgressTotal);
					element.Progress = double(sd->ProgressComplete) / double(sd->ProgressTotal);
				}
			} else {
				element.Load = *interface.Strings[L"TextIdle"];
			}
			if (sd->Battery == Power::BatteryStatus::Charging || sd->Battery == Power::BatteryStatus::InUse) {
				element.PowerLevel = string((sd->BatteryLevel + 5) / 10) + L"%";
			} else {
				element.PowerLevel = L"";
			}
			if (node.ecf_address == GetLoopbackAddress()) {
				element.PowerSource.SetRetain(power_self);
			} else if (sd->Battery == Power::BatteryStatus::Charging) {
				element.PowerSource.SetRetain(power_charging);
			} else if (sd->Battery == Power::BatteryStatus::InUse) {
				element.PowerSource.SetRetain(power_discharging);
			} else {
				element.PowerSource.SetRetain(power_net);
			}
		} else {
			if (node.online) {
				element.Load = *interface.Strings[L"TextUnknown"];
			} else {
				element.Load = L"";
			}
		}
	}
	void UpdateNetList(IWindow * window)
	{
		auto list = FindControl(window, 101)->As<Controls::ListView>();
		auto index = list->GetSelectedIndex();
		UUID selected, active;
		if (index >= 0) selected = _nets[index];
		else ZeroMemory(&selected, sizeof(selected));
		GetServerCurrentNet(&active);
		_nets.Clear();
		list->ClearItems();
		SafePointer< Array<UUID> > uuids = ServerEnumerateKnownNets();
		if (uuids) for (int i = 0; i < uuids->Length(); i++) {
			Structures::NetElement element;
			bool is_selected = MemoryCompare(&uuids->ElementAt(i), &selected, sizeof(UUID)) == 0;
			bool is_active = MemoryCompare(&uuids->ElementAt(i), &active, sizeof(UUID)) == 0;
			bool is_autoconnect = GetServerNetAutoconnectStatus(&uuids->ElementAt(i));
			element.Greenlight.SetRetain(is_active ? greenlight : graylight);
			element.Name = GetServerNetName(&uuids->ElementAt(i));
			element.Autoconnect = is_autoconnect ? L"A" : L"";
			element.UUID = MakeStringOfUUID(&uuids->ElementAt(i));
			list->AddItem(element);
			_nets << uuids->ElementAt(i);
			if (is_selected) list->SetSelectedIndex(i);
		}
	}
	void UpdateNetButtons(IWindow * window)
	{
		auto list = FindControl(window, 101)->As<Controls::ListView>();
		auto index = list->GetSelectedIndex();
		UUID current_uuid, selected_uuid;
		auto connected = GetServerCurrentNet(&current_uuid);
		if (index >= 0) selected_uuid = _nets[index]; else ZeroMemory(&selected_uuid, sizeof(UUID));
		FindControl(window, 104)->Enable(index >= 0);
		FindControl(window, 105)->Enable(index >= 0);
		FindControl(window, 106)->Enable(index >= 0 && !connected);
		FindControl(window, 107)->Enable(connected);
		FindControl(window, 202)->Enable(index >= 0 && !connected);
		FindControl(window, 203)->Enable(connected && MemoryCompare(&selected_uuid, &current_uuid, sizeof(UUID)) == 0);
		FindControl(window, 204)->Enable(connected && MemoryCompare(&selected_uuid, &current_uuid, sizeof(UUID)) == 0);
		FindControl(window, 206)->Enable(connected && MemoryCompare(&selected_uuid, &current_uuid, sizeof(UUID)) == 0);
	}
	void UpdateNetNodes(IWindow * window)
	{
		auto net_list = FindControl(window, 101)->As<Controls::ListView>();
		auto net_index = net_list->GetSelectedIndex();
		auto node_list = FindControl(window, 201)->As<Controls::ListView>();
		auto node_index = node_list->GetSelectedIndex();
		auto node_selected = (node_index >= 0) ? _nodes[node_index] : 0;
		_nodes.Clear();
		node_list->ClearItems();
		if (net_index >= 0) {
			SafePointer< Array<NodeDesc> > nodes = ServerEnumerateNetNodes(&_nets[net_index]);
			if (nodes) for (int i = 0; i < nodes->Length(); i++) {
				auto & node = nodes->ElementAt(i);
				Structures::NodeElement element;
				FillNodeElement(element, node);
				node_list->AddItem(element);
				_nodes << node.ecf_address;
				if (node_selected == node.ecf_address) node_list->SetSelectedIndex(i);
			}
		}
	}
	void UpdateNodeButtons(IWindow * window)
	{
		auto net_list = FindControl(window, 101)->As<Controls::ListView>();
		auto net_index = net_list->GetSelectedIndex();
		auto node_list = FindControl(window, 201)->As<Controls::ListView>();
		auto node_index = node_list->GetSelectedIndex();
		UUID current_net_uuid, selected_net_uuid;
		ObjectAddress selected_node_addr;
		auto connected = GetServerCurrentNet(&current_net_uuid);
		if (net_index >= 0) selected_net_uuid = _nets[net_index];
		else ZeroMemory(&selected_net_uuid, sizeof(UUID));
		if (node_index >= 0) selected_node_addr = _nodes[node_index];
		else selected_node_addr = 0;
		FindControl(window, 205)->Enable(connected && MemoryCompare(&selected_net_uuid, &current_net_uuid, sizeof(UUID)) == 0 && selected_node_addr);
	}
	virtual void Created(IWindow * window) override
	{
		_menu = CreateMenu(interface.Dialog[L"NodeContextMenu"]);
		UUID current_net;
		if (!GetServerCurrentNet(&current_net)) ZeroMemory(&current_net, sizeof(current_net));
		RegisterWindow(window);
		RegisterServerEventCallback(this);
		ServerServiceRegisterCallback(this);
		main_window = window;
		GetRootControl(window)->GetAcceleratorTable() << Accelerators::AcceleratorCommand(1, KeyCodes::F1, false);
		UpdateNetList(window);
		UpdateNetButtons(window);
		UpdateNetNodes(window);
		UpdateNodeButtons(window);
	}
	virtual void Destroyed(IWindow * window) override
	{
		UnregisterWindow(window);
		UnregisterServerEventCallback(this);
		ServerServiceUnregisterCallback(this);
		main_window = 0;
	}
	virtual void WindowClose(IWindow * window) override { HideWindow(window); }
	virtual void HandleControlEvent(Windows::IWindow * window, int ID, ControlEvent event, Control * sender) override
	{
		if (ID == 1) {
			auto callback = GetWindowSystem()->GetCallback();
			if (callback) callback->ShowHelp();
		} else if (ID == 2) {
			auto callback = GetWindowSystem()->GetCallback();
			if (callback) callback->Terminate();
		} else if (ID == 3) {
			auto callback = GetWindowSystem()->GetCallback();
			if (callback) callback->ShowAbout();
		} else if (ID == 101) {
			if (event == ControlEvent::ValueChange) {
				UpdateNetButtons(window);
				UpdateNetNodes(window);
				UpdateNodeButtons(window);
			} else if (event == ControlEvent::DoubleClick) {
				HandleControlEvent(window, 106, ControlEvent::AcceleratorCommand, 0);
			}
		} else if (ID == 102) {
			class _update_list_task : public Task
			{
				IWindow * _window;
				ServerPanelCallback * _callback;
			public:
				_update_list_task(IWindow * window, ServerPanelCallback * callback) : _window(window), _callback(callback) {}
				virtual void DoTask(void) noexcept override
				{
					_callback->UpdateNetList(_window);
					_callback->UpdateNetButtons(_window);
				}
			};
			SafePointer<Task> task = new _update_list_task(window, this);
			NewNetSetupCallback::NewNetDialog(window, task);
		} else if (ID == 103) {
			JoinNetSetupCallback::JoinNetDialog(window);
		} else if (ID == 104) {
			auto wnd = window;
			auto self = this;
			auto & nets = _nets;
			auto task = CreateStructuredTask<MessageBoxResult>([wnd, self, & nets](MessageBoxResult result) {
				if (result != MessageBoxResult::Yes) return;
				UUID current_net, forget_net;
				auto list = FindControl(wnd, 101)->As<Controls::ListView>();
				auto index = list->GetSelectedIndex();
				auto connected_now = GetServerCurrentNet(&current_net);
				forget_net = nets[index];
				if (connected_now && MemoryCompare(&forget_net, &current_net, sizeof(UUID)) == 0) {
					GetWindowSystem()->MessageBox(0, *interface.Strings[L"TextNetCannotForgetActiveNet"], ENGINE_VI_APPNAME,
						wnd, MessageBoxButtonSet::Ok, MessageBoxStyle::Warning, 0);
				} else {
					ServerNetForget(&forget_net);
					self->OnNetStatusUpdated();
				}
			});
			GetWindowSystem()->MessageBox(&task->Value1, *interface.Strings[L"TextNetForgetConfirmation"], ENGINE_VI_APPNAME,
				window, MessageBoxButtonSet::YesNo, MessageBoxStyle::Warning, task);
		} else if (ID == 105) {
			auto list = FindControl(window, 101)->As<Controls::ListView>();
			auto index = list->GetSelectedIndex();
			if (index >= 0) {
				auto selected_uuid = _nets[index];
				if (GetServerNetAutoconnectStatus(&selected_uuid)) ZeroMemory(&selected_uuid, sizeof(UUID));
				ServerNetMakeAutoconnect(&selected_uuid);
				UpdateNetList(window);
			}
		} else if (ID == 106 || ID == 202) {
			UUID current_net;
			auto list = FindControl(window, 101)->As<Controls::ListView>();
			auto index = list->GetSelectedIndex();
			auto connected_now = GetServerCurrentNet(&current_net);
			if (index >= 0 && !connected_now) {
				auto status = ServerNetConnect(&_nets[index]);
				if (!status) {
					GetWindowSystem()->MessageBox(0, *interface.Strings[L"TextFailedToConnect"], ENGINE_VI_APPNAME,
						window, MessageBoxButtonSet::Ok, MessageBoxStyle::Warning, 0);
				}
			}
		} else if (ID == 107 || ID == 203) {
			ServerNetDisconnect();
		} else if (ID == 201) {
			if (event == ControlEvent::ValueChange) {
				UpdateNodeButtons(window);
			} else if (event == ControlEvent::ContextClick) {
				auto at = GetWindowSystem()->GetCursorPosition();
				at = window->PointGlobalToClient(at);
				at = GetControlSystem(window)->ConvertClientToControl(GetRootControl(window), at);
				RunMenu(_menu, GetRootControl(window), at);
			}
		} else if (ID == 204) {
			auto task = CreateStructuredTask<MessageBoxResult>([](MessageBoxResult result) {
				if (result == MessageBoxResult::Yes) ServerNetAllowJoin(true);
				else if (result == MessageBoxResult::No) ServerNetAllowJoin(false);
			});
			GetWindowSystem()->MessageBox(&task->Value1, *interface.Strings[L"TextAllowJoinConfirmation"], ENGINE_VI_APPNAME,
				window, MessageBoxButtonSet::YesNoCancel, MessageBoxStyle::Warning, task);
		} else if (ID == 205) {
			auto list = FindControl(window, 201)->As<Controls::ListView>();
			auto index = list->GetSelectedIndex();
			auto address = (index >= 0) ? _nodes[index] : 0;
			if (address) {
				auto task = CreateStructuredTask<MessageBoxResult>([address](MessageBoxResult result) {
					if (result == MessageBoxResult::Yes) ServerNetNodeRemove(address);
				});
				GetWindowSystem()->MessageBox(&task->Value1, *interface.Strings[L"TextNodeRemoveConfirmation"], ENGINE_VI_APPNAME,
					window, MessageBoxButtonSet::YesNo, MessageBoxStyle::Warning, task);
			}
		} else if (ID == 206) {
			SelectNodesCallback::SelectNodesDialog(window);
		} else if (ID == 301) {
			auto list = FindControl(window, 201)->As<Controls::ListView>();
			auto index = list->GetSelectedIndex();
			if (index >= 0) ServerServiceSendServicesRequest(_nodes[index]);
		} else if (ID == 302) {
			auto list = FindControl(window, 201)->As<Controls::ListView>();
			auto index = list->GetSelectedIndex();
			if (index >= 0) ServerServiceSendInformationRequest(_nodes[index]);
		} else if (ID == 303) {
			ServerServiceTerminateHost();
		} else if (ID == 304) {
			auto list = FindControl(window, 201)->As<Controls::ListView>();
			auto index = list->GetSelectedIndex();
			if (index >= 0) ServerServiceSendControlMessage(_nodes[index], ServerControlShutdownSoftware);
		} else if (ID == 305) {
			auto list = FindControl(window, 201)->As<Controls::ListView>();
			auto index = list->GetSelectedIndex();
			if (index >= 0) ServerServiceSendControlMessage(_nodes[index], ServerControlShutdownPC);
		} else if (ID == 306) {
			auto list = FindControl(window, 201)->As<Controls::ListView>();
			auto index = list->GetSelectedIndex();
			if (index >= 0) ServerServiceSendControlMessage(_nodes[index], ServerControlRestartPC);
		} else if (ID == 307) {
			auto list = FindControl(window, 201)->As<Controls::ListView>();
			auto index = list->GetSelectedIndex();
			if (index >= 0) ServerServiceSendControlMessage(_nodes[index], ServerControlLogoutPC);
		} else if (ID == 308) {
			auto list = FindControl(window, 201)->As<Controls::ListView>();
			auto index = list->GetSelectedIndex();
			if (index >= 0) ServerServiceSendControlMessage(_nodes[index], ServerControlHybernatePC);
		}
	}
	void OnNetStatusUpdated(void)
	{
		if (main_window) {
			UpdateNetList(main_window);
			UpdateNetButtons(main_window);
			UpdateNetNodes(main_window);
			UpdateNodeButtons(main_window);
		}
	}
	void OnNodeStatusUpdated(void)
	{
		if (main_window) {
			UpdateNetNodes(main_window);
			UpdateNodeButtons(main_window);
		}
	}
	void UpdateProgressState(void)
	{
		if (!main_window) return;
		uint com = 0, tot = 0;
		bool indet = false;
		for (auto & v : _statuses.Elements()) {
			if (v.value.ProgressTotal == 0xFFFFFFFF) {
				indet = true;
				break;
			}
			com += v.value.ProgressComplete;
			tot += v.value.ProgressTotal;
		}
		if (indet) {
			main_window->SetProgressMode(ProgressDisplayMode::Indeterminated);
			GetWindowSystem()->SetApplicationBadge(L"...");
		} else {
			if (tot) {
				main_window->SetProgressValue(double(com) / double(tot));
				main_window->SetProgressMode(ProgressDisplayMode::Normal);
				GetWindowSystem()->SetApplicationBadge(string(com * 100 / tot) + L"%");
			} else {
				main_window->SetProgressMode(ProgressDisplayMode::Hide);
				GetWindowSystem()->SetApplicationBadge(L"");
			}
		}
	}
	void SetNodeStatus(ObjectAddress address, const NodeStatusInfo & status)
	{
		if (!main_window) return;
		auto sent = _statuses.GetElementByKey(address);
		if (sent) *sent = status; else _statuses.Append(address, status);
		auto list = FindControl(main_window, 101)->As<Controls::ListView>();
		auto index = list->GetSelectedIndex();
		UUID current_uuid, selected_uuid;
		auto connected = GetServerCurrentNet(&current_uuid);
		if (index >= 0) selected_uuid = _nets[index]; else ZeroMemory(&selected_uuid, sizeof(UUID));
		if (MemoryCompare(&selected_uuid, &current_uuid, sizeof(UUID)) == 0) {
			for (int i = 0; i < _nodes.Length(); i++) if (_nodes[i] == address) {
				SafePointer< Array<NodeDesc> > nodes = ServerEnumerateNetNodes(&current_uuid);
				NodeDesc * node_ptr = 0;
				for (auto & n : *nodes) if (n.ecf_address == address) { node_ptr = &n; break; }
				if (node_ptr) {
					Structures::NodeElement element;
					FillNodeElement(element, *node_ptr);
					auto list = FindControl(main_window, 201)->As<Controls::ListView>();
					if (list->ItemCount() > i) list->ResetItem(i, element);
				}
			}
		}
		UpdateProgressState();
	}
	void ClearNodeStatuses(void) { _statuses.Clear(); UpdateProgressState(); }
	void ShowSystemInfo(ObjectAddress from, const NodeSystemInfo & info)
	{
		if (!main_window || modal_counter || !main_window->IsVisible()) return;
		ShowNodeInfoCallback::ShowNodeInfoDialog(main_window, from, info);
	}
	void ShowEndpointInfo(ObjectAddress from, const Array<EndpointDesc> & endpoints)
	{
		if (!main_window || modal_counter || !main_window->IsVisible()) return;
		ShowServicesCallback::ShowServicesDialog(main_window, endpoints);
	}
	virtual void SwitchedToNet(const UUID * uuid) override
	{
		auto self = this;
		auto nets = &_nets;
		UUID local_uuid;
		if (uuid) local_uuid = *uuid;
		else ZeroMemory(&local_uuid, sizeof(UUID));
		GetWindowSystem()->SubmitTask(CreateFunctionalTask([self, nets, local_uuid]() {
			if (!main_window) return;
			self->OnNetStatusUpdated();
			auto list = FindControl(main_window, 101)->As<Controls::ListView>();
			for (int i = 0; i < nets->Length(); i++) if (MemoryCompare(&nets->ElementAt(i), &local_uuid, sizeof(UUID)) == 0) {
				list->SetSelectedIndex(i, true);
				self->HandleControlEvent(main_window, list->GetID(), ControlEvent::ValueChange, list);
				break;
			}
		}));
	}
	virtual void SwitchingFromNet(const UUID * uuid) override
	{
		auto self = this;
		GetWindowSystem()->SubmitTask(CreateFunctionalTask([self]() { self->OnNetStatusUpdated(); }));
	}
	virtual void SwitchedFromNet(const UUID * uuid) override
	{
		auto self = this;
		GetWindowSystem()->SubmitTask(CreateFunctionalTask([self]() {
			self->OnNetStatusUpdated();
			self->ClearNodeStatuses();
		}));
	}
	virtual void NodeConnected(ObjectAddress node) override
	{
		auto self = this;
		GetWindowSystem()->SubmitTask(CreateFunctionalTask([self]() { self->OnNodeStatusUpdated(); }));
	}
	virtual void NodeDisconnected(ObjectAddress node) override
	{
		auto self = this;
		GetWindowSystem()->SubmitTask(CreateFunctionalTask([self]() { self->OnNodeStatusUpdated(); }));
	}
	virtual void NetTopologyChanged(const UUID * uuid) override
	{
		auto self = this;
		GetWindowSystem()->SubmitTask(CreateFunctionalTask([self]() { self->OnNodeStatusUpdated(); }));
	}
	virtual void NetJoined(const UUID * uuid) override
	{
		auto self = this;
		GetWindowSystem()->SubmitTask(CreateFunctionalTask([self]() { self->OnNetStatusUpdated(); }));
		ServerNetConnect(uuid);
	}
	virtual void NetLeft(const UUID * uuid) override
	{
		auto self = this;
		GetWindowSystem()->SubmitTask(CreateFunctionalTask([self]() { self->OnNetStatusUpdated(); }));
	}
	virtual void ProcessServicesList(ObjectAddress from, const Array<EndpointDesc> & endpoints) override
	{
		auto self = this;
		auto addr = from;
		auto se = endpoints;
		GetWindowSystem()->SubmitTask(CreateFunctionalTask([self, addr, se]() { self->ShowEndpointInfo(addr, se); }));
	}
	virtual void ProcessNodeSystemInfo(ObjectAddress from, const NodeSystemInfo & info) override
	{
		auto self = this;
		auto addr = from;
		auto si = info;
		GetWindowSystem()->SubmitTask(CreateFunctionalTask([self, addr, si]() { self->ShowSystemInfo(addr, si); }));
	}
	virtual void ProcessNodeStatusInfo(ObjectAddress from, const NodeStatusInfo & info) override
	{
		auto self = this;
		auto addr = from;
		auto si = info;
		GetWindowSystem()->SubmitTask(CreateFunctionalTask([self, addr, si]() { self->SetNodeStatus(addr, si); }));
	}
	virtual void ProcessControlMessage(ObjectAddress from, uint control_verb) override
	{
		if (control_verb & ServerControlShutdownSoftware) {
			GetWindowSystem()->SubmitTask(CreateFunctionalTask([]() {
				auto callback = GetWindowSystem()->GetCallback();
				if (callback) callback->Terminate();
			}));
		}
		if (control_verb & ServerControlShutdownPC) {
			GetWindowSystem()->SubmitTask(CreateFunctionalTask([]() { Power::ExitSystem(Power::Exit::Shutdown); }));
		}
		if (control_verb & ServerControlRestartPC) {
			GetWindowSystem()->SubmitTask(CreateFunctionalTask([]() { Power::ExitSystem(Power::Exit::Reboot); }));
		}
		if (control_verb & ServerControlLogoutPC) {
			GetWindowSystem()->SubmitTask(CreateFunctionalTask([]() { Power::ExitSystem(Power::Exit::Logout); }));
		}
		if (control_verb & ServerControlHybernatePC) {
			GetWindowSystem()->SubmitTask(CreateFunctionalTask([]() { Power::SuspendSystem(true); }));
		}
	}
};

class ServerMainCallback : public IApplicationCallback, public IStatusCallback
{
public:
	virtual bool IsHandlerEnabled(ApplicationHandler event) override
	{
		if (event == ApplicationHandler::CreateFile) return true;
		else if (event == ApplicationHandler::ShowAbout) return true;
		else if (event == ApplicationHandler::ShowHelp) return true;
		else if (event == ApplicationHandler::Terminate) return true;
		else return false;
	}
	virtual bool IsWindowEventAccessible(WindowHandler handler) override
	{
		if (handler == WindowHandler::Copy) return true;
		else if (handler == WindowHandler::Cut) return true;
		else if (handler == WindowHandler::Delete) return true;
		else if (handler == WindowHandler::Paste) return true;
		else if (handler == WindowHandler::Redo) return true;
		else if (handler == WindowHandler::SelectAll) return true;
		else if (handler == WindowHandler::Undo) return true;
		else return false;
	}
	virtual void CreateNewFile(void) override { if (main_window) DisplayWindow(main_window); }
	virtual void ShowHelp(void) override { RunHelp(); }
	virtual void ShowAbout(void) override { AboutCallback::Run(); }
	virtual bool DataExchangeReceive(handle client, const string & verb, const DataBlock * data) override
	{
		if (verb == ECF_IPC_VERB_ACTIVATE) {
			CreateNewFile();
			if (main_window) main_window->RequireAttention();
			return true;
		} else if (verb == ECF_IPC_VERB_TERMINATE) {
			Terminate();
			return true;
		} else return false;
	}
	virtual DataBlock * DataExchangeRespond(handle client, const string & verb) override
	{
		if (verb == ECF_IPC_VERB_GETPORT) {
			UUID uuid;
			if (!GetServerCurrentNet(&uuid)) return 0;
			uint16 port = GetServerCurrentPort();
			SafePointer<DataBlock> result = new DataBlock(2);
			result->SetLength(2);
			MemoryCopy(result->GetBuffer(), &port, 2);
			result->Retain();
			return result;
		} else return 0;
	}
	virtual void DataExchangeDisconnect(handle client) override {}
	virtual bool Terminate(void) override
	{
		if (modal_counter) return false;
		auto list = windows;
		for (auto & w : list) w->Destroy();
		return true;
	}
	virtual void StatusIconCommand(IStatusBarIcon * icon, int id) override { if (id == 1) CreateNewFile(); }
};

ServerMainCallback main_callback;
ServerPanelCallback panel_callback;
SafePointer<IStatusBarIcon> status_icon;

int Main(void)
{
	about_window = 0;
	try {
		SafePointer<Codec::Image> status_image;
		Codec::InitializeDefaultCodecs();
		{
			window_visibility_counter = modal_counter = 0;
			main_window = 0;
			is_init_mode = false;
			SafePointer<IScreen> main = GetDefaultScreen();
			CurrentLocale = GetCurrentUserLocale();
			if (CurrentLocale != L"ru") CurrentLocale = L"en";
			CurrentScaleFactor = main->GetDpiScale();
			if (CurrentScaleFactor > 1.75) CurrentScaleFactor = 2.0;
			else if (CurrentScaleFactor > 1.25) CurrentScaleFactor = 1.5;
			else CurrentScaleFactor = 1.0;
			SafePointer<Stream> icon = QueryResource(L"TRAY");
			status_image = Codec::DecodeImage(icon);
			SafePointer<Stream> com_stream = QueryLocalizedResource(L"COM");
			SafePointer<Storage::StringTable> com = new Storage::StringTable(com_stream);
			Assembly::SetLocalizedCommonStrings(com);
			SafePointer<Stream> ui = QueryResource(L"UI");
			Loader::LoadUserInterfaceFromBinary(interface, ui);
			greenlight = interface.Texture[L"IconGreenlight"];
			graylight = interface.Texture[L"IconGreenlightI"];
			power_self = interface.Texture[L"IconPowerSelf"];
			power_charging = interface.Texture[L"IconPowerCharging"];
			power_discharging = interface.Texture[L"IconPowerDischarging"];
			power_net = interface.Texture[L"IconPowerNet"];
		}
		GetWindowSystem()->SetCallback(&main_callback);
		{
			SafePointer<IIPCClient> client = GetWindowSystem()->CreateIPCClient(ENGINE_VI_APPIDENT, ENGINE_VI_COMPANYIDENT);
			if (client) {
				if (client->SendData(ECF_IPC_VERB_ACTIVATE, 0, 0, 0)) return 0;
			}
		}
		if (!GetWindowSystem()->LaunchIPCServer(ENGINE_VI_APPIDENT, ENGINE_VI_COMPANYIDENT)) return 1;
		if (!ServerInitialize(&interface)) return 2;
		if (!GetServerName().Length()) {
			if (!ServerSetupCallback::RunSetup()) return 0;
		}
		ServerServiceInitialize();
		CreateWindow(interface.Dialog[L"Main"], &panel_callback, Rectangle::Entire());
		status_icon = GetWindowSystem()->CreateStatusBarIcon();
		status_icon->SetCallback(&main_callback);
		status_icon->SetIconColorUsage(StatusBarIconColorUsage::Colourfull);
		status_icon->SetIcon(status_image);
		status_icon->SetEventID(1);
		status_icon->PresentIcon(true);
		status_image.SetReference(0);
		ServerPerformAutoconnect();
		GetWindowSystem()->RunMainLoop();
		status_icon->PresentIcon(false);
		status_icon.SetReference(0);
		ServerShutdown();
		ServerServiceShutdown();
	} catch (...) { return 1; }
	return 0;
}