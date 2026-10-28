////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataProcessor - object
////////////////////////////////////////////////////////////////////////////

#include "dataProcessor.h"
#include "backend/metadataDataProcessor.h"   // ibMetaDataDataProcessor::GetManagerModule — whom the object gives its borrow back to
#include "backend/moduleManager/moduleManagerExt.h"

//*********************************************************************************************
//*                                  ObjectCatalogValue                                       *
//*********************************************************************************************

ibValueRecordDataObjectDataProcessor::ibValueRecordDataObjectDataProcessor(const ibValueMetaObjectDataProcessor* metaObject) :
	ibValueRecordDataObjectExt(metaObject)
{
}

ibValueRecordDataObjectDataProcessor::ibValueRecordDataObjectDataProcessor(const ibValueRecordDataObjectDataProcessor& source) :
	ibValueRecordDataObjectExt(source)
{
}

// ShowFormValue / GetFormValue inherited from ibValueRecordDataObject.
// GetCurrentObjectFormID is inline in dataProcessor.h.

// The manager's compile module and proc unit are BORROWED (moduleManagerExt.cpp, ForgetObject says why this matters):
// they go back while the container is still held here, before the bases below run — the base dtor deletes the compile
// module it holds. The container's own dtor, when this object was its last holder, runs after that (the holder base).
ibValueRecordDataObjectExternalDataProcessor::~ibValueRecordDataObjectExternalDataProcessor()
{
	const ibMetaDataDataProcessor* const container = dynamic_cast<const ibMetaDataDataProcessor*>(m_externalMetadata.get());
	if (container != nullptr && container->GetManagerModule() != nullptr)
		container->GetManagerModule()->ForgetObject(this);
}
