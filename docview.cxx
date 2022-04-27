#include "docview.h"

using namespace Engine;
using namespace Engine::Windows;
using namespace Engine::Graphics;
using namespace Engine::UI;
using namespace Engine::Streaming;
using namespace Engine::Storage;

IWindow * help_window = 0;

string ProcessPlaintext(const string & input, int & pos)
{
	if (input[pos] == L'{') pos++;
	int sp = pos;
	while (input[pos] && input[pos] != L'}') pos++;
	int len = pos - sp;
	if (input[pos]) pos++;
	return input.Fragment(sp, len);
}
void ProcessBlock(DynamicString & output, const string & input, int & pos)
{
	if (input[pos] == L'{') pos++;
	while (input[pos] && input[pos] != L'}') {
		if (input[pos] == L'\\') {
			pos++;
			if (input[pos] == L'\\') {
				output << input[pos];
				pos++;
			} else {
				int s = pos;
				while (L'a' <= input[pos] && input[pos] <= L'z') pos++;
				auto word = input.Fragment(s, pos - s);
				if (word == L"n") {
					output << L"\n";
				} else if (word == L"t") {
					output << L"\t";
				} else if (word == L"h") {
					output << L"\33h001E";
					ProcessBlock(output, input, pos);
					output << L"\33e";
				} else if (word == L"hh") {
					output << L"\33h003C";
					ProcessBlock(output, input, pos);
					output << L"\33e";
				} else if (word == L"b") {
					output << L"\33b";
					ProcessBlock(output, input, pos);
					output << L"\33e";
				} else if (word == L"i") {
					output << L"\33i";
					ProcessBlock(output, input, pos);
					output << L"\33e";
				} else if (word == L"code") {
					output << L"\33f01";
					ProcessBlock(output, input, pos);
					output << L"\33e";
				} else if (word == L"link") {
					output << L"\33l";
					output << ProcessPlaintext(input, pos);
					output << L"\33";
					ProcessBlock(output, input, pos);
					output << L"\33e";
				} else if (word == L"img") {
					output << L"\33p00140014@";
					output << ProcessPlaintext(input, pos);
					output << L"\33e";
				}
			}
		} else if (input[pos] > L' ') {
			output << input[pos];
			pos++;
		} else if (input[pos] == L' ' || input[pos] == L'\n') {
			output << L' ';
			pos++;
			while (input[pos] == L' ' || input[pos] == L'\n') pos++;
		} else pos++;
	}
	if (input[pos]) pos++;
}
string ProcessDocumentationCode(const string & code)
{
	DynamicString output;
	output << L"\33n" << Graphics::SystemSerifFont << L"\33e";
	output << L"\33n" << Graphics::SystemMonoSerifFont << L"\33e";
	output << L"\33f00\33c********\33h0014";
	int pos = 0;
	ProcessBlock(output, code, pos);
	output << L"\33e\33e\33e";
	return output.ToString();
}

class HelpWindowCallback : public IEventCallback, public Controls::RichEdit::IRichEditHook
{
	IWindow * _window;
	InterfaceTemplate & _interface;
	SafePointer<Archive> _documents;
	Array<string> _history;
	int _current;
	Controls::RichEdit * _edit;
	void _load_page(const string & name)
	{
		FindControl(_window, 1)->Enable(_current > 0);
		FindControl(_window, 2)->Enable(_current < _history.Length() - 1);
		FindControl(_window, 3)->Enable(name != L"main");
		FindControl(_window, 4)->Enable(name != L"index");
		if (_documents) {
			auto file = _documents->FindArchiveFile(name);
			if (file) {
				SafePointer<Stream> stream = _documents->QueryFileStream(file);
				SafePointer<TextReader> reader = new TextReader(stream, Encoding::UTF8);
				auto code = reader->ReadAll();
				code = ProcessDocumentationCode(code);
				_edit->SetAttributedText(code);
			}
		}
	}
	void _move_to(const string & name)
	{
		while (_history.Length() > _current + 1) _history.RemoveLast();
		_history << name;
		_current++;
		if (_history.Length() > 0x200) {
			_history.RemoveFirst();
			_current--;
		}
		_load_page(name);
	}
public:
	HelpWindowCallback(InterfaceTemplate & interface) : _interface(interface), _history(0x20), _current(-1) {}
	virtual void Created(IWindow * window) override
	{
		_window = window;
		try {
			SafePointer<Stream> original = new FileStream(IO::Path::GetDirectory(IO::GetExecutablePath()) + L"/ecfsrvr.ehlp", AccessRead, OpenExisting);
			SafePointer<Archive> primary = OpenArchive(original);
			if (!primary) throw Exception();
			auto file = primary->FindArchiveFile(Assembly::CurrentLocale);
			if (!file) throw Exception();
			SafePointer<Stream> stream = primary->QueryFileStream(file);
			if (!stream) throw Exception();
			_documents = OpenArchive(stream);
		} catch (...) {}
		RegisterWindow(window);
		_edit = FindControl(window, 101)->As<Controls::RichEdit>();
		_edit->SetHook(this);
		_move_to(L"main");
	}
	virtual void Destroyed(IWindow * window) override
	{
		UnregisterWindow(window);
		delete this;
		help_window = 0;
	}
	virtual void HandleControlEvent(IWindow * window, int id, ControlEvent event, Control * control) override
	{
		if (id == 1) {
			if (_current > 0) {
				_current--;
				_load_page(_history[_current]);
			}
		} else if (id == 2) {
			if (_current < _history.Length() - 1) {
				_current++;
				_load_page(_history[_current]);
			}
		} else if (id == 3) _move_to(L"main");
		else if (id == 4) _move_to(L"index");
	}
	virtual void WindowClose(IWindow * window) override
	{
		HideWindow(window);
		window->Destroy();
	}
	virtual void LinkPressed(const string & resource, Controls::RichEdit * sender) override { _move_to(resource); }
	virtual Graphics::IBitmap * GetImageByName(const string & resource, Controls::RichEdit * sender) override
	{
		auto ref = _interface.Texture[resource];
		if (ref) ref->Retain();
		return ref;
	}
};

void RunDocumentationWindow(Engine::UI::InterfaceTemplate & interface)
{
	if (help_window) {
		help_window->Activate();
	} else {
		SafePointer<IScreen> screen = GetDefaultScreen();
		auto callback = new HelpWindowCallback(interface);
		help_window = CreateWindow(interface.Dialog[L"Help"], callback, Rectangle::Entire());
		auto position = Box(Rectangle(Coordinate(0, 20.0, 0.0), Coordinate(0, 20.0, 0.0), Coordinate(0, -20.0, 0.4), Coordinate(0, -20.0, 1.)), screen->GetUserRectangle());
		help_window->SetPosition(position);
		DisplayWindow(help_window);
	}
}
void CloseDocumentationWindow(void) { if (help_window) help_window->GetCallback()->WindowClose(help_window); }