#include <EngineRuntime.h>

#include "../../client/ClusterPackage.h"

using namespace Engine;
using namespace Engine::IO;
using namespace Engine::IO::ConsoleControl;
using namespace Engine::Streaming;
using namespace Engine::Cluster;

enum class ProjectMode { Unknown, Sources, Binaries };

struct AssetDesc {
	string Path, Words, RootFile;
};
struct {
	bool silent = false;
	string package_file;
	string input_project;
	ProjectMode input_project_mode = ProjectMode::Unknown;
	Array<AssetDesc> assets = Array<AssetDesc>(0x10);
} state;

Console console;

bool ParseCommandLine(void)
{
	SafePointer< Array<string> > args = GetCommandLine();
	for (int i = 1; i < args->Length(); i++) {
		auto & a = args->ElementAt(i);
		if (a[0] == L':' || a[0] == L'-') {
			for (int j = 1; j < a.Length(); j++) {
				if (a[j] == L'p' && i < args->Length() - 1) {
					if (state.input_project.Length()) return false;
					i++;
					state.input_project = args->ElementAt(i);
				} else if (a[j] == L'x') {
					if (state.input_project_mode != ProjectMode::Unknown) return false;
					state.input_project_mode = ProjectMode::Binaries;
				} else if (a[j] == L's') {
					if (state.input_project_mode != ProjectMode::Unknown) return false;
					state.input_project_mode = ProjectMode::Sources;
				} else if (a[j] == L'S') {
					state.silent = true;
				} else if (a[j] == L'd' && i < args->Length() - 3) {
					AssetDesc asset;
					asset.Path = args->ElementAt(i + 1);
					asset.Words = args->ElementAt(i + 2);
					asset.RootFile = args->ElementAt(i + 3);
					state.assets << asset;
					i += 3;
				} else return false;
			}
		} else {
			if (state.package_file.Length()) return false;
			state.package_file = a;
		}
	}
	if (state.input_project.Length() && state.input_project_mode == ProjectMode::Unknown) return false;
	return true;
}

string LocateRootFile(const string & path_at, const string & proj_file, const string & words)
{
	string system;
	auto wl = words.Split(L'-');
	for (auto & w : wl) {
		if (w == PackageAssetWindows || w == PackageAssetMacOS) system = w;
	}
	if (system == PackageAssetWindows) {
		SafePointer< Array<string> > files = Search::GetFiles(path_at + L"/*.exe;*.dll");
		if (files->Length() == 1) return files->FirstElement();
		for (auto & f : *files) {
			if (Path::GetFileNameWithoutExtension(f) == Path::GetFileNameWithoutExtension(proj_file)) return f;
		}
		return L"";
	} else if (system == PackageAssetMacOS) {
		SafePointer< Array<string> > files = Search::GetFiles(path_at + L"/*");
		if (files->Length() == 1 && files->FirstElement()[0] != L'.') return files->FirstElement();
		for (auto & f : *files) {
			if (Path::GetFileNameWithoutExtension(f) == Path::GetFileNameWithoutExtension(proj_file) &&
				(Path::GetExtension(f) == L"" || Path::GetExtension(f) == L"dylib")) return f;
		}
		SafePointer< Array<string> > dirs = Search::GetDirectories(path_at + L"/*");
		for (auto & d : *dirs) if (Path::GetExtension(d) == L"app") {
			SafePointer< Array<string> > files2 = Search::GetFiles(path_at + L"/" + d + L"/Contents/MacOS/*");
			for (auto & f : *files2) {
				if (Path::GetFileName(f) == Path::GetFileNameWithoutExtension(proj_file)) return d + L"\\Contents\\MacOS\\" + f;
			}
		}
		return L"";
	} else return L"";
}
bool AddDirectory(PackageBuilder * package, handle asset, const string & path, const string & prefix)
{
	SafePointer< Array<string> > files = Search::GetFiles(path + L"/*");
	SafePointer< Array<string> > dirs = Search::GetDirectories(path + L"/*");
	for (auto & f : *files) {
		if (f[0] == L'.' || f[0] == L'_') continue;
		if (!state.silent) {
			console << L"Adding a file \"" << TextColor(ConsoleColor::Cyan) << f << TextColor(ConsoleColor::Default) << L"\"" << LineFeed();
		}
		package->AddFile(path + L"/" + f, asset, prefix + f);
	}
	for (auto & d : *dirs) {
		if (d[0] == L'.' || d[0] == L'_') continue;
		if (!state.silent) {
			console << L"Adding a directory \"" << TextColor(ConsoleColor::Cyan) << d << TextColor(ConsoleColor::Default) << L"\"" << LineFeed();
		}
		package->AddDirectory(asset, prefix + d);
		if (!AddDirectory(package, asset, path + L"/" + d, prefix + d + L"\\")) return false;
	}
	return true;
}
bool PackageFromDirectory(PackageBuilder * package, const AssetDesc & desc)
{
	if (!state.silent) {
		console << L"Adding an asset \"" << TextColor(ConsoleColor::Cyan) << desc.Words << TextColor(ConsoleColor::Default) << L"\"" << LineFeed();
	}
	handle asset = 0;
	try {
		auto words = desc.Words.Split(L'-');
		if (words.Length() == 1) {
			asset = package->CreateAsset(words[0]);
		} else if (words.Length() == 2) {
			asset = package->CreateAsset(words[0], words[1]);
		} else {
			if (!state.silent) {
				console << TextColor(ConsoleColor::Red) << L"The number of words is wrong (1 or 2 is assumed)." << TextColor(ConsoleColor::Default) << LineFeed();
			}
			return false;
		}
		package->SetAssetRootFile(asset, desc.RootFile);
	} catch (...) {
		if (!state.silent) {
			console << TextColor(ConsoleColor::Red) << L"Failed to configure an asset." << TextColor(ConsoleColor::Default) << LineFeed();
		}
		return false;
	}
	return AddDirectory(package, asset, desc.Path, L"");
}
bool PackageFromProject(PackageBuilder * package, const string & project, ProjectMode mode)
{
	auto full_project = ExpandPath(project);
	if (mode == ProjectMode::Sources) {
		AssetDesc desc;
		desc.Path = Path::GetDirectory(full_project);
		desc.RootFile = Path::GetFileName(full_project);
		desc.Words = PackageAssetSources;
		return PackageFromDirectory(package, desc);
	} else if (mode == ProjectMode::Binaries) {
		auto build = Path::GetDirectory(full_project) + L"/_build/";
		SafePointer< Array<string> > variants = Search::GetDirectories(build + L"*");
		for (auto & v : *variants) {
			auto words = v.Split(L'_');
			bool release = true;
			DynamicString words_cat;
			for (int i = 0; i < words.Length(); i++) {
				if (words[i] == L"debug") release = false;
				if (words[i] == L"debug" || words[i] == L"release") {
					words.Remove(i);
					break;
				}
				if (words_cat.Length()) words_cat << L"-";
				words_cat << words[i];
			}
			if (!release) continue;
			auto root = ExpandPath(build + v);
			AssetDesc desc;
			desc.Path = root;
			desc.Words = words_cat.ToString();
			desc.RootFile = LocateRootFile(desc.Path, full_project, desc.Words);
			if (!PackageFromDirectory(package, desc)) return false;
		}
		return true;
	} else return false;
}

int Main(void)
{
	try {
		if (!ParseCommandLine()) {
			console << TextColor(ConsoleColor::Red) << L"Invalid command line." << TextColor(ConsoleColor::Default) << LineFeed();
			return 2;
		}
		if (state.package_file.Length()) {
			SafePointer<PackageBuilder> package;
			try {
				SafePointer<Stream> stream = new FileStream(state.package_file, AccessReadWrite, CreateAlways);
				package = new PackageBuilder(stream);
			} catch (...) {
				if (!state.silent) {
					console << TextColor(ConsoleColor::Red) << L"Failed to create the package file." << TextColor(ConsoleColor::Default) << LineFeed();
				}
				return 3;
			}
			if (state.input_project.Length()) {
				if (!PackageFromProject(package, state.input_project, state.input_project_mode)) return 4;
			}
			for (auto & asset : state.assets) {
				if (!PackageFromDirectory(package, asset)) return 5;
			}
			try {
				if (!state.silent) {
					console << L"Finalizing the package...";
				}
				package->Finalize();
				package.SetReference(0);
			} catch (...) {
				if (!state.silent) {
					console << TextColor(ConsoleColor::Red) << L"Failed" << TextColor(ConsoleColor::Default) << LineFeed();
					console << TextColor(ConsoleColor::Red) << L"Failed to finalize the package." << TextColor(ConsoleColor::Default) << LineFeed();
				}
				return 3;
			}
			if (!state.silent) {
				console << TextColor(ConsoleColor::Green) << L"Succeed" << TextColor(ConsoleColor::Default) << LineFeed();
			}
		} else {
			console << ENGINE_VI_APPNAME << LineFeed();
			console << L"Command line syntax:" << LineFeed();
			console << L"  ecfxpack <package.eipf> [-S] [-p <project.ertproj> [-x | -s]]" << LineFeed();
			console << L"  ecfxpack <package.eipf> [-S] [-d <directory> <words> <root file>]" << LineFeed();
			console << L"Where:" << LineFeed();
			console << L"  package.eipf    - a package file to create," << LineFeed();
			console << L"  -S              - creates a package in silent mode," << LineFeed();
			console << L"  -p              - uses project file as base for an assets," << LineFeed();
			console << L"  project.ertproj - Engine Runtime project file," << LineFeed();
			console << L"  -x              - creates a package with executables already built," << LineFeed();
			console << L"  -s              - creates a package with sources found," << LineFeed();
			console << L"  -d              - uses a directory as base for an asset," << LineFeed();
			console << L"  words           - words assigned to this asset, known words:" << LineFeed();
			console << L"    windows       - Microsoft Windows OS," << LineFeed();
			console << L"    macosx        - Apple Mac OS," << LineFeed();
			console << L"    x86           - Intel x86 architecture," << LineFeed();
			console << L"    x64           - Intel x86-64 architecture," << LineFeed();
			console << L"    arm           - ARM architecture," << LineFeed();
			console << L"    arm64         - ARM64 architecture," << LineFeed();
			console << L"    sources       - source files," << LineFeed();
			console << L"  root file       - a file within the package to be used as entry point." << LineFeed() << LineFeed();
		}
	} catch (...) {
		if (!state.silent) {
			console << TextColor(ConsoleColor::Red) << L"Failed with an exception." << TextColor(ConsoleColor::Default) << LineFeed();
		}
		return 1;
	}
	return 0;
}