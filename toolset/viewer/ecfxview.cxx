#include <EngineRuntime.h>

#include "../../client/ClusterPackage.h"

using namespace Engine;
using namespace Engine::IO;
using namespace Engine::IO::ConsoleControl;
using namespace Engine::Streaming;
using namespace Engine::Cluster;

struct {
	bool silent = false;
	bool overwrite = false;
	string package_file, extract_words, extract_path;
} state;

Console console;

bool ParseCommandLine(void)
{
	SafePointer< Array<string> > args = GetCommandLine();
	for (int i = 1; i < args->Length(); i++) {
		auto & a = args->ElementAt(i);
		if (a[0] == L':' || a[0] == L'-') {
			for (int j = 1; j < a.Length(); j++) {
				if (a[j] == L'S') {
					state.silent = true;
				} else if (a[j] == L'o') {
					state.overwrite = true;
				} else if (a[j] == L'x' && i < args->Length() - 2) {
					state.extract_words = args->ElementAt(i + 1);
					state.extract_path = args->ElementAt(i + 2);
					i += 2;
				} else return false;
			}
		} else {
			if (state.package_file.Length()) return false;
			state.package_file = a;
		}
	}
	return true;
}

int Main(void)
{
	try {
		if (!ParseCommandLine()) {
			console << TextColor(ConsoleColor::Red) << L"Invalid command line." << TextColor(ConsoleColor::Default) << LineFeed();
			return 2;
		}
		if (state.package_file.Length()) {
			SafePointer<Package> package;
			try {
				SafePointer<Stream> stream = new FileStream(state.package_file, AccessRead, OpenExisting);
				package = new Package(stream);
			} catch (...) {
				if (!state.silent) {
					console << TextColor(ConsoleColor::Red) << L"Failed to open the package file." << TextColor(ConsoleColor::Default) << LineFeed();
				}
				return 3;
			}
			if (state.extract_words.Length() && state.extract_path.Length()) {
				auto words = state.extract_words.Split(L'-');
				PackageAssetDesc * asset = 0;
				if (words.Length() == 1) {
					asset = package->FindAsset(words[0]);
				} else if (words.Length() == 2) {
					asset = package->FindAsset(words[0], words[1]);
				}
				if (!asset) {
					if (!state.silent) {
						console << TextColor(ConsoleColor::Red) << L"The package with words specified not found." << TextColor(ConsoleColor::Default) << LineFeed();
					}
					return 4;
				}
				SafePointer< Array<PackageFileDesc> > files = package->EnumerateFiles(asset->Handle);
				state.extract_path = ExpandPath(state.extract_path);
				CreateDirectoryTree(state.extract_path);
				for (auto & f : *files) {
					if (f.Handle) {
						if (!state.silent) {
							console << L"Extracting a file \"" << TextColor(ConsoleColor::Cyan) << f.Path << TextColor(ConsoleColor::Default) << L"\"...";
						}
						try {
							auto mode = state.overwrite ? CreateAlways : CreateNew;
							FileStream to(state.extract_path + L"/" + f.Path, AccessReadWrite, mode);
							SafePointer<Streaming::Stream> from = package->OpenFile(f.Handle);
							from->CopyTo(&to);
							DateTime::SetFileCreationTime(to.Handle(), f.DateCreated);
							DateTime::SetFileAlterTime(to.Handle(), f.DateAltered);
							DateTime::SetFileAccessTime(to.Handle(), f.DateAccessed);
							if (!state.silent) {
								console << TextColor(ConsoleColor::Green) << L"Succeed" << TextColor(ConsoleColor::Default) << LineFeed();
							}
						} catch (FileAccessException & e) {
							if (e.code == Error::FileExists) {
								if (!state.silent) {
									console << TextColor(ConsoleColor::Yellow) << L"Skipped" << TextColor(ConsoleColor::Default) << LineFeed();
								}
							} else {
								if (!state.silent) {
									console << TextColor(ConsoleColor::Red) << L"Failed" << TextColor(ConsoleColor::Default) << LineFeed();
								}
							}
						} catch (...) {
							if (!state.silent) {
								console << TextColor(ConsoleColor::Red) << L"Failed" << TextColor(ConsoleColor::Default) << LineFeed();
							}
						}
					} else {
						if (!state.silent) {
							console << L"Creating a directory \"" << TextColor(ConsoleColor::Cyan) << f.Path << TextColor(ConsoleColor::Default) << L"\"...";
						}
						CreateDirectoryTree(state.extract_path + L"/" + f.Path);
						if (!state.silent) {
							console << TextColor(ConsoleColor::Green) << L"Succeed" << TextColor(ConsoleColor::Default) << LineFeed();
						}
					}
				}
				if (!state.silent) {
					console << TextColor(ConsoleColor::Green) << L"Succeed" << TextColor(ConsoleColor::Default) << LineFeed();
				}
			} else {
				SafePointer< Array<PackageAssetDesc> > assets = package->EnumerateAssets();
				for (auto & a : *assets) {
					DynamicString words;
					for (auto & w : a.Words) {
						if (words.Length()) words << L'-';
						words << w;
					}
					console << L"Asset \"" << TextColor(ConsoleColor::Cyan) << words.ToString() << TextColor(ConsoleColor::Default) <<
						L"\", root file: \"" << TextColor(ConsoleColor::Cyan) << a.RootFile << TextColor(ConsoleColor::Default) << L"\"" << LineFeed();
				}
			}
		} else {
			console << ENGINE_VI_APPNAME << LineFeed();
			console << L"Command line syntax:" << LineFeed();
			console << L"  ecfxview <package.eipf> [-S] [-x <words> <path-to>] [-o]" << LineFeed();
			console << L"Where:" << LineFeed();
			console << L"  package.eipf - a package file to view," << LineFeed();
			console << L"  -S           - works in silent mode," << LineFeed();
			console << L"  -x           - extracts some asset from the package," << LineFeed();
			console << L"  -o           - allows files to be overwritten," << LineFeed();
			console << L"  words        - words of an asset to extract," << LineFeed();
			console << L"  path-to      - a path to place file extracted." << LineFeed() << LineFeed();
		}
	} catch (...) {
		if (!state.silent) {
			console << TextColor(ConsoleColor::Red) << L"Failed with an exception." << TextColor(ConsoleColor::Default) << LineFeed();
		}
		return 1;
	}
	return 0;
}