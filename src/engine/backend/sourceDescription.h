#ifndef __SOURCE_DESCRIPTION_H__
#define __SOURCE_DESCRIPTION_H__

#include "backend/backend_core.h"   // ibMetaID, ibClassID (via clsid.h), wxNOT_FOUND
#include <vector>
#include <algorithm>

// Source description — the address of a value inside a data source, stored as a
// single ordered id path. Index 0 is the first hop from the source; the last id
// is the leaf (the value actually read). A one-id path is a plain column; a
// longer path is Ref.Ref.Field navigation. The whole description is what a source
// object is fed instead of a bare metaId — the source walks it against itself.
//
// One hop of a binding path: WHERE to go (id) and the type EXPECTED there (recorded at path-construction).
// The type is g_valueUndefinedCLSID where the source imposes no constraint (the common case), or a concrete
// clsid where a COMPOSITE reference was pinned to one branch in the picker. The walk's gate compares it to
// the live type, so a metadata drift (a reference retyped) reads as a broken binding, not a silent mis-hop.
struct ibSourceHop {
	ibSourceId m_id   = wxNOT_FOUND;
	ibClassID  m_type = g_valueUndefinedCLSID;
	bool operator==(const ibSourceHop& o) const { return m_id == o.m_id && m_type == o.m_type; }
	bool operator!=(const ibSourceHop& o) const { return !(*this == o); }
};

// Mirrors ibMetaDescription (a plain id list with its own *Memory serialiser).
struct ibSourceDescription {
	std::vector<ibSourceHop> m_listSource;
public:
	ibSourceDescription() {}
	ibSourceDescription(const ibSourceId& id) { AppendSource(id); }
	ibSourceDescription(const std::vector<ibSourceId>& array) { AppendSource(array); }
public:

	bool IsOk() const { return GetHopCount() != 0; }

	// True when the path crosses at least one reference (a real dot-walk),
	// as opposed to a plain single-column binding.
	bool IsDotWalk() const { return GetHopCount() > 1; }

	void SetDefaultSource(const ibSourceId& id) {
		ClearSource();
		AppendSource(id);
	}

	void AppendSource(const ibSourceId& id, const ibClassID& type = g_valueUndefinedCLSID) { m_listSource.push_back({ id, type }); }
	void AppendSource(const std::vector<ibSourceId>& array) { for (auto& id : array) AppendSource(id); }

	void ClearSource() { m_listSource.clear(); }

	// The first hop is the entry point from the source; the leaf is the final value.
	ibSourceId GetFirst() const { return m_listSource.empty() ? wxNOT_FOUND : m_listSource.front().m_id; }
	ibSourceId GetLeaf() const { return m_listSource.empty() ? wxNOT_FOUND : m_listSource.back().m_id; }

	ibSourceId GetByIdx(unsigned int idx) const { return idx < m_listSource.size() ? m_listSource[idx].m_id : wxNOT_FOUND; }

	// Number of hops in the path (0 = empty, 1 = plain column, >1 = dot-walk). Callers windowing a path — the
	// tablebox prefix, a walk bound — read this instead of GetPath().size().
	size_t GetHopCount() const { return m_listSource.size(); }

	// The type EXPECTED at hop idx (undefined = accept as is). The gate coerces a pinned composite hop to it.
	ibClassID GetExpectedType(unsigned int idx) const { return idx < m_listSource.size() ? m_listSource[idx].m_type : g_valueUndefinedCLSID; }

	// The full path, BY REF — {id, expected type} per hop. Callers navigate by .m_id and read the recorded
	// .m_type where they must know it (the walk's gate, the drift check). The correspondence travels intact:
	// a hop ALWAYS carries its expected type, never an id stripped of it.
	const std::vector<ibSourceHop>& GetPath() const { return m_listSource; }
};

// A DUMB serializer for a source-description path — the head is a FORM-LOCAL attribute id, each deeper hop a
// source-column id. It writes / reads the ids VERBATIM and knows nothing else: no metadata, no copy awareness.
// What the ids mean and how a paste transforms them is the OWNER's contract (ibPropertySource copy/paste hooks),
// which processes the path before writing / after reading. See the .cpp for the format.
class BACKEND_API ibSourceDescriptionMemory {
public:
	static bool LoadData(class ibReaderMemory& reader, ibSourceDescription& srcDesc);
	static bool SaveData(class ibWriterMemory& writer, ibSourceDescription& srcDesc);

	// node form — a Binary blob of the id path; the byte reader / writer is contained here, not the property.
	static bool ReadNode(const class ibDataValue& value, ibSourceDescription& srcDesc);
	static bool WriteNode(class ibDataValue& value, ibSourceDescription& srcDesc);
};

#endif // !__SOURCE_DESCRIPTION_H__
