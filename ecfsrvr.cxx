#include <EngineRuntime.h>

#include "srvrnet.h"

#define ECF_IPC_VERB_ACTIVATE	L"ACTIVATE"
#define ECF_IPC_VERB_GETPORT	L"PORT"

using namespace Engine;
using namespace Engine::Windows;
using namespace Engine::UI;
using namespace Engine::Assembly;
using namespace Engine::Streaming;
using namespace Engine::Cluster;

Array<IWindow *> windows(0x10);
ITexture * greenlight, * graylight;
IWindow * main_window;
uint window_visibility_counter;
uint modal_counter;
InterfaceTemplate interface;

namespace Structures
{
	ENGINE_REFLECTED_CLASS(NetElement, Reflection::Reflected)
		ENGINE_DEFINE_REFLECTED_PROPERTY(TEXTURE, Greenlight);
		ENGINE_DEFINE_REFLECTED_PROPERTY(STRING, Name);
		ENGINE_DEFINE_REFLECTED_PROPERTY(STRING, Autoconnect);
		ENGINE_DEFINE_REFLECTED_PROPERTY(STRING, UUID);
	ENGINE_END_REFLECTED_CLASS
}

void RegisterWindow(IWindow * window)
{
	windows << window;
}
void UnregisterWindow(IWindow * window)
{
	for (int i = 0; i < windows.Length(); i++) if (windows[i] == window) { windows.Remove(i); break; }
	if (!windows.Length()) GetWindowSystem()->ExitMainLoop();
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
		if (dec == 0) GetWindowSystem()->SetApplicationIconVisibility(false);
	}
}

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
				GetWindowSystem()->ExitMainLoop();
			}
		} else if (ID == 2) {
			GetWindowSystem()->ExitMainLoop();
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

class ServerPanelCallback : public IEventCallback, public IServerEventCallback
{
	Array<UUID> _nets;
public:
	ServerPanelCallback(void) : _nets(0x10) {}
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
		for (int i = 0; i < uuids->Length(); i++) {
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
		UUID current_uuid;
		auto connected = GetServerCurrentNet(&current_uuid);
		FindControl(window, 104)->Enable(index >= 0);
		FindControl(window, 105)->Enable(index >= 0);
		FindControl(window, 106)->Enable(index >= 0 && !connected);
		FindControl(window, 107)->Enable(connected);
		FindControl(window, 202)->Enable(index >= 0 && !connected);
		FindControl(window, 203)->Enable(connected);
	}
	virtual void Created(IWindow * window) override
	{
		RegisterWindow(window);
		RegisterServerEventCallback(this);
		main_window = window;
		GetRootControl(window)->GetAcceleratorTable() << Accelerators::AcceleratorCommand(1, KeyCodes::F1, false);
		UpdateNetList(window);
		UpdateNetButtons(window);

		// TODO: IMPLEMENT
	}
	virtual void Destroyed(IWindow * window) override
	{
		UnregisterWindow(window);
		UnregisterServerEventCallback(this);
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
				// TODO: UPDATE NODE LIST AND BUTTONS
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
			// TODO: NET JOIN
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
			// TODO: NET AUTOCONNECT
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
		}
		// TODO: IMPLEMENT PAGE 1
		// TODO: TIER 2
		// bool GetServerNetAutoconnectStatus(UUID * uuid); + ICON
		// void ServerNetMakeAutoconnect(UUID * uuid);
		// TODO: TIER 3
		// void ServerNetJoin(Network::Address to, uint16 port_to, uint16 port_from); + ICON
		// TODO: IMPLEMENT PAGE 2
		// TODO: TIER 2
		// bool GetServerCurrentNet(UUID * uuid);
		// Array<NodeDesc> * ServerEnumerateNetNodes(UUID * uuid);
		// TODO: TIER 3
		// bool ServerNetAllowJoin(bool allow);
		// void ServerNetLeave(void);
		// bool ServerNetNodeRemove(ObjectAddress node);
		// TODO: TIER 4 (STATUS API)
		// TODO: IMPLEMENT
		// TODO: ICON (SHADOW IS ALPHA=0,5, BLUR=UNIFORM-2/1/1)
	}
	void OnNetStatusUpdated(void)
	{
		if (main_window) {
			UpdateNetList(main_window);
			UpdateNetButtons(main_window);
			// TODO: UPDATE NODE LISTS AND BUTTONS TOO
		}
	}
	virtual void SwitchedToNet(const UUID * uuid) override
	{
		auto self = this;
		GetWindowSystem()->SubmitTask(CreateFunctionalTask([self]() { self->OnNetStatusUpdated(); }));
	}
	virtual void SwitchingFromNet(const UUID * uuid) override
	{
		auto self = this;
		GetWindowSystem()->SubmitTask(CreateFunctionalTask([self]() { self->OnNetStatusUpdated(); }));
	}
	virtual void SwitchedFromNet(const UUID * uuid) override
	{
		auto self = this;
		GetWindowSystem()->SubmitTask(CreateFunctionalTask([self]() { self->OnNetStatusUpdated(); }));
	}
	virtual void NodeConnected(ObjectAddress node) override
	{
		// TODO: IMPLEMENT
		// TODO: UPDATE NODE LISTS AND BUTTONS
	}
	virtual void NodeDisconnected(ObjectAddress node) override
	{
		// TODO: IMPLEMENT
		// TODO: UPDATE NODE LISTS AND BUTTONS
	}
	virtual void NetTopologyChanged(const UUID * uuid) override
	{
		// TODO: IMPLEMENT
		// TODO: UPDATE NODE LISTS AND BUTTONS
	}
	virtual void NetJoined(const UUID * uuid) override
	{
		// TODO: IMPLEMENT
		// TODO: UPDATE NODE LISTS AND BUTTONS
	}
	virtual void NetLeft(const UUID * uuid) override
	{
		// TODO: IMPLEMENT
		// TODO: UPDATE NODE LISTS AND BUTTONS
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
	virtual void ShowHelp(void) override
	{
		// TODO: IMPLEMENT
	}
	virtual void ShowAbout(void) override
	{
		// TODO: IMPLEMENT
	}
	virtual bool DataExchangeReceive(handle client, const string & verb, const DataBlock * data) override
	{
		if (verb == ECF_IPC_VERB_ACTIVATE) {
			CreateNewFile();
			if (main_window) main_window->RequireAttention();
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
	SafePointer<Codec::Image> status_image;
	Codec::InitializeDefaultCodecs();
	{
		window_visibility_counter = modal_counter = 0;
		main_window = 0;
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
	}
	GetWindowSystem()->SetCallback(&main_callback);
	// {
	// 	SafePointer<IIPCClient> client = GetWindowSystem()->CreateIPCClient(ENGINE_VI_APPIDENT, ENGINE_VI_COMPANYIDENT);
	// 	if (client) {
	// 		if (client->SendData(ECF_IPC_VERB_ACTIVATE, 0, 0, 0)) return 0;
	// 	}
	// }
	//if (!GetWindowSystem()->LaunchIPCServer(ENGINE_VI_APPIDENT, ENGINE_VI_COMPANYIDENT)) return 1;
	if (!ServerInitialize()) return 2;
	if (!GetServerName().Length()) {
		if (!ServerSetupCallback::RunSetup()) return 0;
	}
	CreateWindow(interface.Dialog[L"Main"], &panel_callback, Rectangle::Entire());
	
	// TODO: REGISTER CALLBACKS:
	//   NODE STATUS API MESSAGE HANDLERS
	//   TEXT LOGGER API MESSAGE HANDLERS
	//   COMPUTATION API MESSAGE AND EVENT HANDLERS

	status_icon = GetWindowSystem()->CreateStatusBarIcon();
	status_icon->SetCallback(&main_callback);
	status_icon->SetIconColorUsage(StatusBarIconColorUsage::Monochromic);
	status_icon->SetIcon(status_image);
	status_icon->SetEventID(1);
	status_icon->PresentIcon(true);
	status_image.SetReference(0);
	ServerPerformAutoconnect();
	GetWindowSystem()->RunMainLoop();
	status_icon->PresentIcon(false);
	status_icon.SetReference(0);
	ServerShutdown();
	return 0;
}