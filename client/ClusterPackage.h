#pragma once

#include <EngineRuntime.h>

namespace Engine
{
	namespace Cluster
	{
		constexpr widechar * PackageAssetWindows = L"windows";
		constexpr widechar * PackageAssetMacOS = L"macosx";
		constexpr widechar * PackageAssetX86 = L"x86";
		constexpr widechar * PackageAssetX64 = L"x64";
		constexpr widechar * PackageAssetARM = L"arm";
		constexpr widechar * PackageAssetARM64 = L"arm64";
		constexpr widechar * PackageAssetSources = L"sources";

		struct PackageAssetDesc
		{
			Array<string> Words;
			string RootFile;
			handle Handle;
		};
		struct PackageFileDesc
		{
			string Path;
			Time DateCreated, DateAltered, DateAccessed;
			handle Handle;
		};

		class Package : public Object
		{
			SafePointer<Storage::Archive> _archive;
			Array<PackageAssetDesc> _assets;
		public:
			Package(Streaming::Stream * stream);
			virtual ~Package(void) override;

			Array<PackageAssetDesc> * EnumerateAssets(void);
			PackageAssetDesc * FindAsset(const string & word_1 = L"", const string & word_2 = L"");

			Array<PackageFileDesc> * EnumerateFiles(handle asset);
			Streaming::Stream * OpenFile(handle file);
		};
		class PackageBuilder : public Object
		{
			struct _file_record
			{
				string file_source, file_name;
				SafePointer<DataBlock> data_source;
				Time date_created, date_altered, date_accessed;
				handle asset;
			};

			int _state;
			SafePointer<Streaming::Stream> _stream;
			Array<PackageAssetDesc> _assets;
			Array<_file_record> _files;
		public:
			PackageBuilder(Streaming::Stream * stream);
			virtual ~PackageBuilder(void) override;

			handle CreateAsset(const string & word_1 = L"", const string & word_2 = L"");
			void SetAssetRootFile(handle asset, const string & path);

			void AddDirectory(handle asset_to, const string & name);
			void AddFile(const string & path, handle asset_to, const string & name);
			void AddFile(const DataBlock * data, Time date_created, Time date_altered, Time date_accessed, handle asset_to, const string & name);

			void Finalize(void);
		};
	}
}