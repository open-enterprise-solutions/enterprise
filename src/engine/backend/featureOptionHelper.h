#ifndef __FEATURE_OPTION_HELPER_H__
#define __FEATURE_OPTION_HELPER_H__

#include "backend_core.h"
#include "core/memory/memoryStream.h"

#include <set>

//********************************************************************************************
//*               Feature options — "which switches do I belong to"                       *
//********************************************************************************************
//
// A metaobject can belong to FEATURE OPTIONS: the parts of the system a base may or may not
// use. The object keeps the ids of the options it belongs to, the way it keeps the sections it is
// checked into (interfaceHelper.h) and the compositions it is part of (compositionHelper.h) — so
// "am I available" is asked of the object itself, and a copied object carries its options with it.
//
// Same shape as those two and deliberately not the same set: a section places an object in the
// command interface, a composition gives it a column, an option decides whether it is available at
// all. Three questions, three sets, three chunks on disk.
//
// The ids are the metaIDs of the options. Nothing else is stored: whether an option is on is the
// base's fact, read at run time (featureOptionGate.h), never a mark on the object.

class BACKEND_API ibFeatureOptionObject {
public:

	void SetFeatureOption(const ibMetaID& id, const bool& set = true) {
		if (set) m_featureOptions.emplace(id);
		else m_featureOptions.erase(id);
		DoSetFeatureOption(id, set);
	}

	bool IsInFeatureOption(const ibMetaID& id) const { return m_featureOptions.contains(id); }

	const std::set<ibMetaID>& GetFeatureOptions() const { return m_featureOptions; }

	// MAY THIS BELONG TO A FEATURE OPTION AT ALL? Asked of the object, so the option's editor builds its
	// tree by walking the metadata and MCP refuses what the editor would not offer — a metatype that arrives
	// later answers for itself.
	//
	// No by default: belonging to an option only means something where the interface asks whether a thing
	// is available, and a thing nobody asks about would keep a membership that does nothing.
	virtual bool IsFeatureOptionAllowed() const { return false; }

	virtual ~ibFeatureOptionObject() {}

protected:

	// Hook for owners that need to react — the interface and composition mechanisms have the same one.
	virtual void DoSetFeatureOption(const ibMetaID& id, const bool& set = true) {}

	//load & save feature options in metaobject
	bool LoadFeatureOptions(ibReaderMemory& reader);
	bool SaveFeatureOptions(ibWriterMemory& writer) const;

	std::set<ibMetaID> m_featureOptions;
};

#endif // !__FEATURE_OPTION_HELPER_H__
