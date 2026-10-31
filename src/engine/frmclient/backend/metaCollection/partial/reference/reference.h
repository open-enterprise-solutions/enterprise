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
class ibValue;

class FRMCLIENT_API ibValueReferenceDataObject {
public:

	// The metaobjects these types point at — none for a type that is no reference (the engine's).
	static std::vector<ibMetaID> ConvertToMetaIds(const std::vector<ibClassID>& clsids, const ibMetaData* metaData);
	// …and the empty reference of one, as the server wrote it and reads it — `CatalogRef.Goods.EmptyRef` in the designer;
	// Undefined for a type it said nothing of (the engine's managers' EmptyRef).
	static ibValue EmptyRef(const ibClassID& clsid);

	// …as the server answered for them: children Type (a class id as text), Targets and Value.
	static void ReadReferences(const ibProtocolNode& references);

private:

	static std::map<ibClassID, std::vector<ibMetaID>>& Targets();
	static std::map<ibClassID, ibDataNode>& EmptyRefs();
};

#endif
