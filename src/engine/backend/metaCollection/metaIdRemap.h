#ifndef __META_ID_REMAP_H__
#define __META_ID_REMAP_H__

#include "core/core.h"    // ibMetaID
#include "core/clsid.h"   // ibClassID, IsMetaValue / clsid_metaID / make_clsid_dynamic — a dynamic class id is built on a metaID

#include <unordered_map>
#include <unordered_set>

// ⭐ THE IDS A SUBTREE WAS RENUMBERED BY — old metaID → new metaID (#578).
//
// A data processor or report read from a file INTO a configuration keeps none of the file's ids: they would collide
// with the configuration's own. ibValueMetaObject::RenumberSubtree gives every object of the subtree, its root and its
// predefined children included, a fresh id and answers with this map. The objects are then renumbered, but whatever
// HOLDS an id of them — a button's command, a field's source, a form attribute's type, all of them inside a form's
// data — still holds the file's. ibValueMetaObject::RemapSubtree hands this map to every property of the subtree
// (ibBackendProperty::RemapIds), and each holder rewrites what it holds, as it alone knows which of its ids are
// metaIDs and which are its form's own.
//
// An id that is not in the map is not this subtree's and is left as it is: a host's catalog a field is typed by, a
// form attribute's id, a standard action.
class ibMetaIdRemap {
public:

	void Add(ibMetaID from, ibMetaID to) { m_ids.emplace(from, to); }

	bool IsEmpty() const { return m_ids.empty(); }
	size_t GetCount() const { return m_ids.size(); }

	// Was this id renumbered? A holder that has to tell an object's id from an id of its own asks before it maps.
	bool Has(ibMetaID id) const { return m_ids.find(id) != m_ids.end(); }

	// The id the object has now — or the same id, when it is not one of the subtree's.
	ibMetaID Map(ibMetaID id) const {
		const auto found = m_ids.find(id);
		return found != m_ids.end() ? found->second : id;
	}

	// A class id built ON a metaID (a reference, an object, a tabular section… of a subtree object) names the object
	// by its new id, with its kind and metaclass kept. A static class id, or one of an object outside the subtree,
	// comes back as it is.
	ibClassID MapClsid(ibClassID clsid) const {
		if (!IsMetaValue(clsid))
			return clsid;
		const ibMetaID metaId = static_cast<ibMetaID>(clsid_metaID(clsid));
		const auto found = m_ids.find(metaId);
		if (found == m_ids.end())
			return clsid;
		return make_clsid_dynamic(static_cast<ibClassID>(found->second), clsid_kind(clsid), clsid_metaclass(clsid));
	}

	// A holder is remapped once: the first visit answers true, every later one false (ibPropertyObject::RemapIds).
	// Applying the map twice is not harmless — a new id can be the old id of another object of the subtree.
	bool Visit(const void* holder) const { return m_visited.insert(holder).second; }

	// The same map with nothing visited — for a walk over objects that live only for it (a form built to be remapped
	// and destroyed: the next one may be allocated where it was).
	ibMetaIdRemap Unvisited() const { ibMetaIdRemap copy; copy.m_ids = m_ids; return copy; }

private:
	std::unordered_map<ibMetaID, ibMetaID> m_ids;
	mutable std::unordered_set<const void*> m_visited;
};

#endif // !__META_ID_REMAP_H__
