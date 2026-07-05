#ifndef __SOURCE_DESCRIPTION_H__
#define __SOURCE_DESCRIPTION_H__

#include "backend/backend_core.h"   // ibMetaID, wxNOT_FOUND
#include <vector>
#include <algorithm>

// Source description — the address of a value inside a data source, stored as a
// single ordered id path. Index 0 is the first hop from the source; the last id
// is the leaf (the value actually read). A one-id path is a plain column; a
// longer path is Ref.Ref.Field navigation. The whole description is what a source
// object is fed instead of a bare metaId — the source walks it against itself.
//
// Mirrors ibMetaDescription (a plain id list with its own *Memory serialiser).
struct ibSourceDescription {
	std::vector<ibSourceId> m_listSource;
public:
	ibSourceDescription() {}
	ibSourceDescription(const ibSourceId& id) : m_listSource({ id }) {}
	ibSourceDescription(const std::vector<ibSourceId>& array) : m_listSource(array) {}
public:

	bool IsOk() const { return m_listSource.size() > 0; }

	// True when the path crosses at least one reference (a real dot-walk),
	// as opposed to a plain single-column binding.
	bool IsDotWalk() const { return m_listSource.size() > 1; }

	void SetDefaultSource(const ibSourceId& id) {
		ClearSource();
		AppendSource(id);
	}

	void AppendSource(const ibSourceId& id) { m_listSource.emplace_back(id); }
	void AppendSource(const std::vector<ibSourceId>& array) { for (auto& id : array) m_listSource.emplace_back(id); }

	void ClearSource() { m_listSource.clear(); }

	// The first hop is the entry point from the source; the leaf is the final value.
	ibSourceId GetFirst() const { return m_listSource.empty() ? wxNOT_FOUND : m_listSource.front(); }
	ibSourceId GetLeaf() const { return m_listSource.empty() ? wxNOT_FOUND : m_listSource.back(); }

	ibSourceId GetByIdx(unsigned int idx) const {
		if (idx >= m_listSource.size())
			return wxNOT_FOUND;
		return m_listSource[idx];
	}

	const std::vector<ibSourceId>& GetPath() const { return m_listSource; }
};

// Serialises a source-description path as RAW ids — the head is a FORM-LOCAL attribute id, each deeper hop a
// source-column id. Resolution is METADATA-AGNOSTIC: the source explorer WALKS the path (FindById per hop,
// each hop's value yielding the next explorer), so the serializer never touches metadata — no guid, no
// metaData param. Load takes the ids verbatim; the explorer resolves them. A legacy guid-tail blob (old
// metadata-coupled writer) recovers only its head — re-save rewrites it raw. See the .cpp for the format.
class BACKEND_API ibSourceDescriptionMemory {
public:
	static bool LoadData(class ibReaderMemory& reader, ibSourceDescription& srcDesc);
	static bool SaveData(class ibWriterMemory& writer, ibSourceDescription& srcDesc);

	// node form — a Binary blob of the raw id path; the byte reader / writer is contained here, not the property.
	static bool ReadNode(const class ibDataValue& value, ibSourceDescription& srcDesc);
	static bool WriteNode(class ibDataValue& value, ibSourceDescription& srcDesc);
};

#endif // !__SOURCE_DESCRIPTION_H__
