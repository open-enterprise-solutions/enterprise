#include "metadataCollection.h"
#include "backend/backend_exception.h"

namespace {

constexpr long kFind = 3; // after Count, Property, Get on a read-only container

void BindMetadataCollectionNames(ibValue::ibMemberTable& helper, const ibValue*)
{
	ibValueContainer::BindContainerNames(helper, true);
	helper.AppendFunc(wxT("Find"), 1, wxT("Find(name)"));
}

}

ibValueMetadataCollection::ibValueMetadataCollection()
	: ibValueStructure(true)
{
}

ibValue::ibMemberTable* ibValueMetadataCollection::DoGetPMethods() const
{
	return ibMemberTable::Shared<&BindMetadataCollectionNames>();
}

bool ibValueMetadataCollection::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue,
	ibValue** paParams, const long lSizeArray)
{
	if (lMethodNum != kFind)
		return ibValueStructure::CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);

	if (lSizeArray < 1 || paParams == nullptr)
		ibBackendCoreException::Error(_("Find: the name to look for is not given"));

	ibValue found;
	Property(*paParams[0], found);
	pvarRetValue = found;
	return true;
}
