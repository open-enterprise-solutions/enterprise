#ifndef __COMPOSITION_HELPER_H__
#define __COMPOSITION_HELPER_H__

#include "backend_core.h"
#include "backend/fileSystem/fs.h"

#include <set>

//********************************************************************************************
//*                        Composition — "which sets am I part of"                           *
//********************************************************************************************
//
// A metaobject can be checked INTO things: a common attribute's composition today, and
// whatever else needs the same shape tomorrow. This is that mechanism, and it is its own
// on purpose.
//
// It is deliberately NOT ibInterfaceObject (interfaceHelper.h). That one belongs to
// SECTIONS — it is the old name for them, and it carries their vocabulary
// (ibInterfaceCommandSection, the panel areas a checked item renders in). Putting a
// different kind of membership into the same set would leave one container holding two
// meanings, with nothing able to tell them apart: a walk over "what am I checked into"
// could no longer answer "as what". Same shape, different question, so — its own set,
// its own chunk on disk, its own name.
//
// The ids in here are the metaIDs of the things this object is part of. Nothing else is
// stored: what the membership PRODUCES (a column, a menu entry, …) belongs to whoever
// owns the set, not here.

class BACKEND_API ibCompositionObject {
public:

	void SetComposition(const ibMetaID& id, const bool& set = true) {
		if (set) m_compositions.emplace(id);
		else m_compositions.erase(id);
		DoSetComposition(id, set);
	}

	bool IsInComposition(const ibMetaID& id) const { return m_compositions.find(id) != m_compositions.end(); }

	const std::set<ibMetaID>& GetCompositions() const { return m_compositions; }

	// MAY THIS OBJECT BE PART OF A COMPOSITION AT ALL? The question belongs to the
	// mechanism, so whoever offers a composition asks HERE and never keeps a list of
	// which metatypes qualify — a metatype that arrives later answers for itself.
	//
	// No by default, and each family opts in: an object that is not stored has nowhere to
	// put what membership produces, and one whose composition has not been thought through
	// should not silently acquire it.
	virtual bool IsCompositionAllowed() const { return false; }

	virtual ~ibCompositionObject() {}

protected:

	// Hook for owners that need to react — the interface mechanism has the same one, and
	// for the same reason: the flag alone is rarely the whole change.
	virtual void DoSetComposition(const ibMetaID& id, const bool& set = true) {}

	//load & save composition in metaobject
	bool LoadComposition(ibReaderMemory& reader);
	bool SaveComposition(ibWriterMemory& writer) const;

	std::set<ibMetaID> m_compositions;
};

#endif // !__COMPOSITION_HELPER_H__
