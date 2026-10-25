#ifndef __SERIALIZE_FILE_PROVIDER_H__
#define __SERIALIZE_FILE_PROVIDER_H__

#include "core/serialize/dataBuilder.h"

// A lossless directory representation. The memory overloads carry the root manifest;
// referenced nodes and assets live under directory. The directory overloads publish
// and read configuration.json, replacing only a dedicated export directory.
class CORE_API ibFileProvider : public ibFormatProvider {
public:
	explicit ibFileProvider(const wxString& directory) : m_directory(directory) {}

	virtual bool Write(const ibDataNode& root, ibWriterMemory& writer) const override;
	virtual bool Read(ibReaderMemory& reader, ibDataNode& root) const override;

	bool Write(const ibDataNode& root) const;
	bool Read(ibDataNode& root) const;

private:
	wxString m_directory;
};

#endif // __SERIALIZE_FILE_PROVIDER_H__
