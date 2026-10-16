#ifndef __METADATA_COLLECTION_H__
#define __METADATA_COLLECTION_H__

#include "backend/system/value/valueMap.h"

// A metadata collection is a structure of metadata objects, plus Find. A structure
// raises when a name is not there; Find answers Undefined, which is what
// Metadata.Documents.Find and an attribute list's Find are for.
class BACKEND_API ibValueMetadataCollection : public ibValueStructure {
public:
	ibValueMetadataCollection();

	virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) override;

protected:
	virtual ibMemberTable* DoGetPMethods() const override;
};

#endif
