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
	std::vector<ibMetaID> m_listSource;
public:
	ibSourceDescription() {}
	ibSourceDescription(const ibMetaID& id) : m_listSource({ id }) {}
	ibSourceDescription(const std::vector<ibMetaID>& array) : m_listSource(array) {}
public:

	bool IsOk() const { return m_listSource.size() > 0; }

	// True when the path crosses at least one reference (a real dot-walk),
	// as opposed to a plain single-column binding.
	bool IsDotWalk() const { return m_listSource.size() > 1; }

	void SetDefaultSource(const ibMetaID& id) {
		ClearSource();
		AppendSource(id);
	}

	void SetDefaultSource(const ibSourceDescription& srcDesc) {
		ClearSource();
		AppendSource(srcDesc.m_listSource);
	}

	void AppendSource(const ibMetaID& id) { m_listSource.emplace_back(id); }
	void AppendSource(const std::vector<ibMetaID>& array) { for (auto& id : array) m_listSource.emplace_back(id); }

	void ClearSource() { m_listSource.clear(); }
	bool ContainSource(const ibMetaID& id) const {
		auto iterator = std::find(m_listSource.begin(), m_listSource.end(), id);
		return iterator != m_listSource.end();
	}

	// The first hop is the entry point from the source; the leaf is the final value.
	ibMetaID GetFirst() const { return m_listSource.empty() ? wxNOT_FOUND : m_listSource.front(); }
	ibMetaID GetLeaf() const { return m_listSource.empty() ? wxNOT_FOUND : m_listSource.back(); }

	ibMetaID GetByIdx(unsigned int idx) const {
		if (idx >= m_listSource.size())
			return wxNOT_FOUND;
		return m_listSource[idx];
	}

	const std::vector<ibMetaID>& GetPath() const { return m_listSource; }

	unsigned int GetSourceCount() const { return m_listSource.size(); }
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
};

#endif // !__SOURCE_DESCRIPTION_H__
