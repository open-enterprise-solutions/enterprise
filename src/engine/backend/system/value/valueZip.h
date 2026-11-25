#ifndef __VALUE_ZIP_H__
#define __VALUE_ZIP_H__

// ⭐ A ZIP ARCHIVE FROM A SCRIPT — ZipFileWriter puts files into one, ZipFileReader lists one and takes its files out, a
// ZipFileEntry is one file in it. Over the core's archive (core/zip/zip.h), so what it reads and writes is what a
// workbook, a backup and the help are packed in: entries stored or deflated, names in UTF-8, ZIP64 past four bytes.
// No password and no encryption: an archive that asks for one is refused, not opened half.

#include "backend/compiler/value.h"

#include "core/memory/memoryBuffer.h"

#include <cstdint>
#include <vector>

// WHAT PART OF A FILE'S PATH THE ARCHIVE KEEPS as its name inside. The name alone unless said: files taken from
// several folders then lie side by side; the path below the folder that was added keeps a folder's layout; the whole
// path, its root and drive aside, keeps where each one came from.
enum ibZIPStorePathMode {
	ibZIPStorePathMode_DontStorePath,
	ibZIPStorePathMode_StoreRelativePath,
	ibZIPStorePathMode_StoreFullPath,
};

// WHETHER A FOLDER'S FOLDERS ARE ADDED with it. Not unless said: a folder added is the files it holds.
enum ibZIPSubDirProcessingMode {
	ibZIPSubDirProcessingMode_DontProcess,
	ibZIPSubDirProcessingMode_ProcessRecursively,
};

void ibValueZipFileWriter_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

// FILES PUT INTO AN ARCHIVE — Add names them (a file, a folder, a mask such as `C:\out\*.pdf`) and says what of their
// paths to keep; Write packs them, in the order added, into the file the writer was made with. Nothing is read
// before Write, so a file changed between Add and Write goes in as Write finds it.
class BACKEND_API ibValueZipFileWriter : public ibValueStaticMembers<&ibValueZipFileWriter_BindNames> {
	enum Func {
		enAdd,
		enWrite,
	};
public:

	ibValueZipFileWriter();
	virtual ~ibValueZipFileWriter();

	virtual bool IsEmpty() const override { return false; }

	virtual bool Init() override { return false; }
	virtual bool Init(ibValue** paParams, const long lSizeArray) override;

	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;

private:

	struct Entry {
		ibString m_path;   // the file on disk
		ibString m_name;   // its name in the archive, `/` between folders
	};

	void Add(const ibString& path, ibZIPStorePathMode storePath, ibZIPSubDirProcessingMode subDirs);
	void Write() const;

	ibString           m_fileName;
	std::vector<Entry> m_entries;
};

void ibValueZipFileReader_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

// AN ARCHIVE OPENED TO BE READ — a file, or the bytes of one (BinaryData: an attachment, an HTTP body). Opening reads
// it through once, to list its entries with their sizes; Extract and ExtractAll read it again and write the files.
// What is held between is the listing, never the contents, so an archive of any size is opened in the memory of its
// largest entry.
class BACKEND_API ibValueZipFileReader : public ibValueStaticMembers<&ibValueZipFileReader_BindNames> {
	enum Prop {
		enItems,
	};
	enum Func {
		enExtract,
		enExtractAll,
		enClose,
	};
public:

	ibValueZipFileReader();
	virtual ~ibValueZipFileReader();

	virtual bool IsEmpty() const override { return false; }

	virtual bool Init() override { return false; }
	virtual bool Init(ibValue** paParams, const long lSizeArray) override;

	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) override;

private:

	// One pass over the archive, each entry handed to `take` with its bytes; false from `take` stops the pass.
	template <class Take> void Walk(Take take) const;
	void Extract(const ibString& name, const ibString& folder) const;
	void ExtractAll(const ibString& folder) const;

	ibString       m_fileName;   // the file it was opened from; empty when it was opened from bytes
	ibMemoryBuffer m_data;       // …the bytes it was opened from
	ibValue        m_items;      // the Array of ZipFileEntry, made once when opened
	bool           m_closed = false;
};

void ibValueZipFileEntry_BindNames(ibValue::ibMemberTable& helper, const ibValue* ctx);

// ONE FILE IN AN ARCHIVE, as ZipFileReader.Items lists it: its name inside (`folder/file.txt`) taken apart, and the
// size it unpacks to. Handed out, never made by a script.
class BACKEND_API ibValueZipFileEntry : public ibValueStaticMembers<&ibValueZipFileEntry_BindNames> {
	enum Prop {
		enName,
		enBaseName,
		enExtension,
		enFullName,
		enPath,
		enUncompressedSize,
	};
public:

	ibValueZipFileEntry();
	ibValueZipFileEntry(const ibString& fullName, std::uint64_t size);
	virtual ~ibValueZipFileEntry();

	virtual bool IsEmpty() const override { return m_fullName.IsEmpty(); }

	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override;

	const ibString& GetFullName() const { return m_fullName; }

private:
	ibString      m_fullName;   // as the archive names it, `/` between folders
	std::uint64_t m_size = 0;
};

#pragma region enumeration
#include "backend/compiler/enumUnit.h"

class ibValueEnumZIPStorePathMode : public ibValueEnumeration<ibZIPStorePathMode> {
public:
	ibValueEnumZIPStorePathMode() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibZIPStorePathMode_DontStorePath,     ibT("DontStorePath"),     ibTRANSLATE("Don't store path"));
		AddEnumeration(ibZIPStorePathMode_StoreRelativePath, ibT("StoreRelativePath"), ibTRANSLATE("Store relative path"));
		AddEnumeration(ibZIPStorePathMode_StoreFullPath,     ibT("StoreFullPath"),     ibTRANSLATE("Store full path"));
	}
};

class ibValueEnumZIPSubDirProcessingMode : public ibValueEnumeration<ibZIPSubDirProcessingMode> {
public:
	ibValueEnumZIPSubDirProcessingMode() : ibValueEnumeration() {}

	virtual void CreateEnumeration() {
		AddEnumeration(ibZIPSubDirProcessingMode_DontProcess,        ibT("DontProcess"),        ibTRANSLATE("Don't process"));
		AddEnumeration(ibZIPSubDirProcessingMode_ProcessRecursively, ibT("ProcessRecursively"), ibTRANSLATE("Process recursively"));
	}
};
#pragma endregion

#endif // !__VALUE_ZIP_H__
