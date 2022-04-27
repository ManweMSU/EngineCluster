#include "ClusterPackage.h"

namespace Engine
{
	namespace Cluster
	{
		Package::Package(Streaming::Stream * stream) : _assets(0x10)
		{
			_archive = Storage::OpenArchive(stream, Storage::ArchiveMetadataUsage::LoadMetadata);
			if (!_archive) throw InvalidFormatException();
			auto header_file = _archive->FindArchiveFile(L"EIPF", 1);
			if (!header_file) throw InvalidFormatException();
			SafePointer<Streaming::Stream> header_file_stream = _archive->QueryFileStream(header_file);
			if (!header_file_stream) throw InvalidFormatException();
			SafePointer<Storage::Registry> header = Storage::LoadRegistry(header_file_stream);
			if (!header) throw InvalidFormatException();
			for (auto & nn : header->GetSubnodes()) {
				SafePointer<Storage::RegistryNode> n = header->OpenNode(nn);
				PackageAssetDesc desc;
				desc.Words = nn.Split(L'-');
				desc.Handle = handle(n->GetValueInteger(L"ID"));
				desc.RootFile = n->GetValueString(L"EXEC");
				_assets << desc;
			}
		}
		Package::~Package(void) {}
		Array<PackageAssetDesc> * Package::EnumerateAssets(void)
		{
			SafePointer< Array<PackageAssetDesc> > result = new Array<PackageAssetDesc>(0x10);
			for (auto & a : _assets) result->Append(a);
			result->Retain();
			return result;
		}
		PackageAssetDesc * Package::FindAsset(const string & word_1, const string & word_2)
		{
			for (auto & a : _assets) {
				if (word_1.Length() && word_2.Length()) {
					if (a.Words.Length() == 2) {
						if (a.Words[0] == word_1 && a.Words[1] == word_2) return &a;
						else if (a.Words[0] == word_2 && a.Words[1] == word_1) return &a;
					}
				} else if (word_1.Length()) {
					if (a.Words.Length() == 1 && a.Words[0] == word_1) return &a;
				} else throw InvalidArgumentException();
			}
			return 0;
		}
		Array<PackageFileDesc> * Package::EnumerateFiles(handle asset)
		{
			if (!asset) throw InvalidArgumentException();
			SafePointer< Array<PackageFileDesc> > result = new Array<PackageFileDesc>(0x100);
			for (Storage::ArchiveFile file = 1; file <= _archive->GetFileCount(); file++) {
				handle file_asset = handle(_archive->GetFileCustomData(file));
				if (file_asset == asset) {
					PackageFileDesc desc;
					desc.Path = _archive->GetFileName(file);
					if (desc.Path.Length() && desc.Path[desc.Path.Length() - 1] == L'\\') {
						desc.Handle = 0;
						desc.Path = desc.Path.Fragment(0, desc.Path.Length() - 1);
						desc.DateCreated = desc.DateAltered = desc.DateAccessed = 0;
					} else {
						desc.Handle = handle(file);
						desc.DateCreated = _archive->GetFileCreationTime(file);
						desc.DateAltered = _archive->GetFileAlterTime(file);
						desc.DateAccessed = _archive->GetFileAccessTime(file);
					}
					result->Append(desc);
				}
			}
			result->Retain();
			return result;
		}
		Streaming::Stream * Package::OpenFile(handle file)
		{
			uint32 f = intptr(file);
			if (f < 1 || f > _archive->GetFileCount()) throw InvalidArgumentException();
			return _archive->QueryFileStream(f);
		}

		PackageBuilder::PackageBuilder(Streaming::Stream * stream) : _state(0), _assets(0x10), _files(0x40) { _stream.SetRetain(stream); }
		PackageBuilder::~PackageBuilder(void) {}
		handle PackageBuilder::CreateAsset(const string & word_1, const string & word_2)
		{
			PackageAssetDesc asset;
			if (word_1.Length()) asset.Words << word_1;
			if (word_2.Length()) asset.Words << word_2;
			uint32 mx = 0;
			for (auto & a : _assets) {
				uint32 ah = intptr(a.Handle);
				if (ah > mx) mx = ah;
			}
			asset.Handle = handle(mx + 1);
			_assets << asset;
			return asset.Handle;
		}
		void PackageBuilder::SetAssetRootFile(handle asset, const string & path)
		{
			for (auto & a : _assets) if (a.Handle == asset) {
				a.RootFile = path;
				return;
			}
			throw InvalidArgumentException();
		}
		void PackageBuilder::AddDirectory(handle asset_to, const string & name)
		{
			bool found = false;
			for (auto & a : _assets) if (a.Handle == asset_to) { found = true; break; }
			if (!found) throw InvalidArgumentException();
			_file_record file;
			file.asset = asset_to;
			file.file_name = name;
			file.date_accessed = file.date_altered = file.date_created = 0;
			for (int i = 0; i < _files.Length(); i++) {
				if (_files[i].file_source.Length() || _files[i].data_source) {
					_files.Insert(file, i);
					return;
				}
			}
			_files.Append(file);
		}
		void PackageBuilder::AddFile(const string & path, handle asset_to, const string & name)
		{
			bool found = false;
			for (auto & a : _assets) if (a.Handle == asset_to) { found = true; break; }
			if (!found) throw InvalidArgumentException();
			_file_record file;
			file.file_source = IO::ExpandPath(path);
			file.asset = asset_to;
			file.date_accessed = file.date_altered = file.date_created = 0;
			file.file_name = name;
			_files.Append(file);
		}
		void PackageBuilder::AddFile(const DataBlock * data, Time date_created, Time date_altered, Time date_accessed, handle asset_to, const string & name)
		{
			if (!data) throw InvalidArgumentException();
			bool found = false;
			for (auto & a : _assets) if (a.Handle == asset_to) { found = true; break; }
			if (!found) throw InvalidArgumentException();
			_file_record file;
			file.data_source = new DataBlock(0x100);
			file.data_source->SetLength(data->Length());
			MemoryCopy(file.data_source->GetBuffer(), data->GetBuffer(), data->Length());
			file.asset = asset_to;
			file.date_accessed = date_accessed;
			file.date_altered = date_altered;
			file.date_created = date_created;
			file.file_name = name;
			_files.Append(file);
		}
		void PackageBuilder::Finalize(void)
		{
			if (_state) throw InvalidStateException();
			_stream->SetLength(0);
			_stream->Seek(0, Streaming::Begin);
			SafePointer<ThreadPool> pool = new ThreadPool;
			SafePointer<Storage::NewArchive> arc = Storage::CreateArchive(_stream, _files.Length() + 1,
				Storage::NewArchiveFlags::UseFormat64 | Storage::NewArchiveFlags::CreateMetadata);
			SafePointer<Storage::Registry> manifest = Storage::CreateRegistry();
			for (auto & a : _assets) {
				int32 asset_id = intptr(a.Handle);
				DynamicString asset_words;
				for (auto & w : a.Words) {
					if (asset_words.Length()) asset_words << L"-";
					asset_words << w;
				}
				auto asset_words_s = asset_words.ToString();
				manifest->CreateNode(asset_words_s);
				SafePointer<Storage::RegistryNode> asset_node = manifest->OpenNode(asset_words_s);
				asset_node->CreateValue(L"ID", Storage::RegistryValueType::Integer);
				asset_node->SetValue(L"ID", asset_id);
				asset_node->CreateValue(L"EXEC", Storage::RegistryValueType::String);
				asset_node->SetValue(L"EXEC", a.RootFile);
			}
			Storage::MethodChain chain = Storage::MethodChain(Storage::CompressionMethod::LempelZivWelch, Storage::CompressionMethod::Huffman);
			{
				Streaming::MemoryStream stream(0x1000);
				manifest->Save(&stream);
				stream.Seek(0, Streaming::Begin);
				arc->SetFileData(1, &stream, chain, Storage::CompressionQuality::ExtraVariative, pool);
				arc->SetFileID(1, 1);
				arc->SetFileType(1, L"EIPF");
				manifest.SetReference(0);
			}
			for (int i = 0; i < _files.Length(); i++) {
				auto & file = _files[i];
				Storage::ArchiveFile file_id = i + 2;
				uint32 custom = intptr(file.asset);
				arc->SetFileCustom(file_id, custom);
				if (file.file_source.Length()) {
					arc->SetFileName(file_id, file.file_name);
					SafePointer<Streaming::FileStream> source = new Streaming::FileStream(file.file_source, Streaming::AccessRead, Streaming::OpenExisting);
					arc->SetFileData(file_id, source, chain, Storage::CompressionQuality::ExtraVariative, pool);
					arc->SetFileCreationTime(file_id, IO::DateTime::GetFileCreationTime(source->Handle()));
					arc->SetFileAccessTime(file_id, IO::DateTime::GetFileAccessTime(source->Handle()));
					arc->SetFileAlterTime(file_id, IO::DateTime::GetFileAlterTime(source->Handle()));
				} else if (file.data_source) {
					arc->SetFileName(file_id, file.file_name);
					Streaming::MemoryStream stream(0x1000);
					stream.WriteArray(file.data_source);
					stream.Seek(0, Streaming::Begin);
					arc->SetFileData(file_id, &stream, chain, Storage::CompressionQuality::ExtraVariative, pool);
					arc->SetFileCreationTime(file_id, file.date_created);
					arc->SetFileAccessTime(file_id, file.date_accessed);
					arc->SetFileAlterTime(file_id, file.date_altered);
				} else {
					arc->SetFileName(file_id, file.file_name + L"\\");
				}
			}
			arc->Finalize();
			_state = 1;
		}
	}
}