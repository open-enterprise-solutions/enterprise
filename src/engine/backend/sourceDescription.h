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

// Serialises a source-description path as copy-aware GUIDs — NOT metaIds. Each segment's
// metaId is resolved (through metaData) to its metaobject's GetCommonGuid(), which returns
// the COPY-guid during a metadata copy (ibControlCopyGuard stamps every copied object with a
// fresh m_metaCopyGuid). So a binding INSIDE a copied object serialises a reference to the
// COPY, not the original — and on paste it resolves to the copy. A metaId would not remap on
// copy (it points at the original), which is why the binding must go through GUIDs here. Load
// resolves each GUID back to the live metaId for the in-memory walk path.
class BACKEND_API ibSourceDescriptionMemory {
public:
	static bool LoadData(class ibReaderMemory& reader, ibSourceDescription& srcDesc, const class ibMetaData* metaData);
	static bool SaveData(class ibWriterMemory& writer, ibSourceDescription& srcDesc, const class ibMetaData* metaData);

	// node form — kept as a Binary blob (the copy-aware GUID round-trip stays in the
	// byte path); the byte writer is contained here, not in the property.
	static bool ReadNode(const class ibDataValue& value, ibSourceDescription& srcDesc, const class ibMetaData* metaData);
	static bool WriteNode(class ibDataValue& value, ibSourceDescription& srcDesc, const class ibMetaData* metaData);
};

#endif // !__SOURCE_DESCRIPTION_H__
