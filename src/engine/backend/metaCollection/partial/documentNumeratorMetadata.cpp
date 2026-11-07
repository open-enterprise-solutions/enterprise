////////////////////////////////////////////////////////////////////////////
//	Description : document numerator - one number series for several document kinds
////////////////////////////////////////////////////////////////////////////

#include "documentNumerator.h"
#include "document.h"
#include "backend/metaData.h"
#include "backend/backend_exception.h"
#include "core/serialize/dataBuilder.h"

ibValueMetaObjectDocumentNumerator::ibValueMetaObjectDocumentNumerator(const ibString& name, const ibString& synonym, const ibString& comment) :
	ibValueMetaObject(name, synonym, comment)
{
}

std::vector<ibValueMetaObjectDocument*> ibValueMetaObjectDocumentNumerator::GetMembers() const
{
	std::vector<ibValueMetaObjectDocument*> members;
	if (m_metaData == nullptr)
		return members;
	for (ibValueMetaObjectDocument* document : m_metaData->GetAnyArrayObject<ibValueMetaObjectDocument>(g_metaDocumentCLSID)) {
		if (document->GetNumerator() == this)
			members.push_back(document);
	}
	return members;
}

void ibValueMetaObjectDocumentNumerator::RefuseDelete() const
{
	const std::vector<ibValueMetaObjectDocument*> members = GetMembers();
	if (!members.empty()) {
		ibBackendCoreException::Error(_("The document numerator '%s' cannot be deleted: the document '%s' numbers from it. Clear the document's Numerator property first."),
			GetName(), members.front()->GetName());
	}
}

//***********************************************************************
//*                           Events                                    *
//***********************************************************************

// THE NUMBER ATTRIBUTE IS THE NUMERATOR'S OWN CHILD — it takes every phase the numerator does, as a
// document's Number does from its document (ibValueMetaObjectRecordDataRecorderRef), or its id would be
// a different number in every session.

bool ibValueMetaObjectDocumentNumerator::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	if (!ibValueMetaObject::OnCreateMetaObject(metaData, flags))
		return false;
	return (*m_propertyNumber)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectDocumentNumerator::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyNumber)->OnLoadMetaObject(metaData))
		return false;
	return ibValueMetaObject::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectDocumentNumerator::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyNumber)->OnSaveMetaObject(flags))
		return false;
	return ibValueMetaObject::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectDocumentNumerator::OnDeleteMetaObject()
{
	if (!(*m_propertyNumber)->OnDeleteMetaObject())
		return false;
	return ibValueMetaObject::OnDeleteMetaObject();
}

bool ibValueMetaObjectDocumentNumerator::OnBeforeRunMetaObject(int flags)
{
	if (!(*m_propertyNumber)->OnBeforeRunMetaObject(flags))
		return false;
	return ibValueMetaObject::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectDocumentNumerator::OnAfterRunMetaObject(int flags)
{
	if (!(*m_propertyNumber)->OnAfterRunMetaObject(flags))
		return false;
	return ibValueMetaObject::OnAfterRunMetaObject(flags);
}

bool ibValueMetaObjectDocumentNumerator::OnBeforeCloseMetaObject()
{
	if (!(*m_propertyNumber)->OnBeforeCloseMetaObject())
		return false;
	return ibValueMetaObject::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectDocumentNumerator::OnAfterCloseMetaObject()
{
	if (!(*m_propertyNumber)->OnAfterCloseMetaObject())
		return false;
	return ibValueMetaObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                           Data                                      *
//***********************************************************************

bool ibValueMetaObjectDocumentNumerator::ReadData(const ibDataNode& node)
{
	m_propertyNumber->SetNodeValue(node.GetProperty(m_propertyNumber->GetName()));
	m_propertyNumberPeriodicity->SetNodeValue(node.GetProperty(m_propertyNumberPeriodicity->GetName()));
	m_propertyCheckUnique->SetNodeValue(node.GetProperty(m_propertyCheckUnique->GetName()));
	return true;
}

bool ibValueMetaObjectDocumentNumerator::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyNumber->GetName(), m_propertyNumber->GetNodeValue());
	node.SetProperty(m_propertyNumberPeriodicity->GetName(), m_propertyNumberPeriodicity->GetNodeValue());
	node.SetProperty(m_propertyCheckUnique->GetName(), m_propertyCheckUnique->GetNodeValue());
	return true;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectDocumentNumerator, "DocumentNumerator", g_metaDocumentNumeratorCLSID);
