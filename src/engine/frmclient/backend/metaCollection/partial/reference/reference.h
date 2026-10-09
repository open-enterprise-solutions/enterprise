#ifndef _FRMCLIENT_REFERENCE_H__
#define _FRMCLIENT_REFERENCE_H__

// A REFERENCE — the engine's ibValueReferenceDataObject (backend/metaCollection/partial/reference/reference.h) as far as
// a settings window's field tree asks it: which metaobjects a reference type points at, the question that gives a
// field its [+]. A reference's value and its fields are the configuration's — the server's; what it answers for the
// types a window shows is kept here (ReadReferences), and asked as the engine is asked.

#include <map>
#include <vector>

#include "frmclient/backend/backend_core.h"
#include "protocol/protocolNode.h"

class ibMetaData;

class FRMCLIENT_API ibValueReferenceDataObject {
public:

	// The metaobjects these types point at — none for a type that is no reference (the engine's).
	static std::vector<ibMetaID> ConvertToMetaIds(const std::vector<ibClassID>& clsids, const ibMetaData* metaData);

	// …as the server answered for them: children Type (a class id as text) and Targets.
	static void ReadReferences(const ibProtocolNode& references);

private:

	static std::map<ibClassID, std::vector<ibMetaID>>& Targets();
};

#endif
