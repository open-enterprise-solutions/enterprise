////////////////////////////////////////////////////////////////////////////
//	Description : document journal - metaData
////////////////////////////////////////////////////////////////////////////

#include "documentJournal.h"
#include "documentJournalManager.h"
#include "document.h"

#include "backend/metaData.h"
#include "backend/objCtor.h"
#include "core/serialize/dataBuilder.h"
#include "backend/system/value/valueArray.h"
#include "backend/system/value/valueDynamicList.h"   // ibCreateList — the list form rides the universal dynamic list

//***********************************************************************
//*                    the documents a journal lists                    *
//***********************************************************************

ibPropertyChoiceMode ibPropertyJournalDocuments::GetValueList(ibPropertyChoiceList& list)
{
	return CreateValueList(list, ibPropertyChoiceMode::Mult, { g_metaDocumentCLSID });
}

//***********************************************************************
//*                              metaData                               *
//***********************************************************************

ibValueMetaObjectDocumentJournal::ibValueMetaObjectDocumentJournal() : ibValueMetaObjectGenericData()
{
}

ibValueMetaObjectDocumentJournal::~ibValueMetaObjectDocumentJournal()
{
}

std::vector<const ibValueMetaObjectDocument*> ibValueMetaObjectDocumentJournal::GetDocuments() const
{
	std::vector<const ibValueMetaObjectDocument*> documents;
	if (m_metaData == nullptr)
		return documents;

	const ibMetaDescription& listed = GetRegisteredDocuments();
	for (unsigned int idx = 0; idx < listed.GetTypeCount(); idx++) {
		const ibValueMetaObject* found = m_metaData->FindAnyObjectByFilter(listed.GetByIdx(idx));
		const ibValueMetaObjectDocument* document = nullptr;
		if (found != nullptr && !found->IsDeleted() && found->ConvertToValue(document) && document != nullptr)
			documents.push_back(document);
	}
	return documents;
}

// The references, in the order the journal is read in: by date. The bounds are the date's, inclusive at
// both ends, as a list's period is.
ibValue ibValueMetaObjectDocumentJournal::Select(const ibValue& from, const ibValue& to) const
{
	std::vector<ibValue> refs;
	const ibDocumentJournalQueryable* rows = GetQueryable();

	if (!GetDocuments().empty()) {
		ibDataQueryBuilder door;
		door.From(rows);
		door.Select(rows->GetRefColumn(), ibT("Ref"));
		if (!from.IsEmpty())
			door.WhereCompare(rows->GetDateColumn(), ibQueryFilterOp::GreaterEqual, from);
		if (!to.IsEmpty())
			door.WhereCompare(rows->GetDateColumn(), ibQueryFilterOp::LessEqual, to);
		door.OrderBy(rows->GetDateColumn(), true);

		ibDataQueryResult result = door.Execute(ibReadPageRequest());
		while (result.Next())
			refs.push_back(result.GetValue(rows->GetRefColumn()));
	}

	return new ibValueArray(refs);
}

// ⭐ THE MANAGER IS THE JOURNAL'S, AND SO IS THE SOURCE. A journal has nothing to compile and nothing to
// create, so these two are the whole of its run: `DocumentJournals.<Name>` for code, `DocumentJournal.<Name>`
// for a query and a list.
bool ibValueMetaObjectDocumentJournal::OnBeforeRunMetaObject(int flags)
{
	registerManager();
	return ibValueMetaObjectGenericData::OnBeforeRunMetaObject(flags);
}

bool ibValueMetaObjectDocumentJournal::OnAfterRunMetaObject(int flags)
{
	if (!ibValueMetaObjectGenericData::OnAfterRunMetaObject(flags))
		return false;
	m_metaData->RegisterSource(&m_source);
	return true;
}

bool ibValueMetaObjectDocumentJournal::OnBeforeCloseMetaObject()
{
	m_metaData->UnregisterSource(&m_source);   // mirror of the run's RegisterSource
	return ibValueMetaObjectGenericData::OnBeforeCloseMetaObject();
}

bool ibValueMetaObjectDocumentJournal::OnAfterCloseMetaObject()
{
	unregisterManager();
	return ibValueMetaObjectGenericData::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                               forms                                 *
//***********************************************************************

void ibValueMetaObjectDocumentJournal::OnCreateFormObject(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == eFormList && m_propertyDefFormList->GetValueAsInteger() == wxNOT_FOUND)
		m_propertyDefFormList->SetValue(metaForm->GetMetaID());
}

void ibValueMetaObjectDocumentJournal::OnRemoveMetaForm(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == eFormList && m_propertyDefFormList->GetValueAsInteger() == metaForm->GetMetaID())
		m_propertyDefFormList->SetValue(wxNOT_FOUND);
}

ibValueMetaObjectFormBase* ibValueMetaObjectDocumentJournal::GetDefaultFormByID(const ibFormID& id) const
{
	if (id == eFormList && m_propertyDefFormList->GetValueAsInteger() != wxNOT_FOUND)
		return FindFormObjectByFilter(m_propertyDefFormList->GetValueAsInteger());
	return nullptr;
}

// The list shows the documents by date - what a journal is opened to look at.
ibSourcePtr<ibSourceDataObject> ibValueMetaObjectDocumentJournal::CreateSourceObject(const ibCreateRequest& request, const ibFormID& form_id) const
{
	switch (form_id)
	{
	case eFormList:
		return ibSourcePtr<ibSourceDataObject>(ibCreateList(request, GetQueryable(), GetQueryable()->GetDateColumn()));
	}

	return nullptr;
}

ibFormPtr<ibBackendValueForm> ibValueMetaObjectDocumentJournal::GetListForm(const ibFormRequest& request,
	ibBackendControlFrame* ownerControl) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		request,
		ibValueMetaObjectDocumentJournal::eFormList,
		ownerControl, ibCreateList(request.m_create, GetQueryable(), GetQueryable()->GetDateColumn())
	);
}

ibValuePtr<ibValueManagerDataObject> ibValueMetaObjectDocumentJournal::CreateManagerDataObjectValue() const
{
	return ibValuePtr<ibValueManagerDataObject>(new ibValueManagerDataObjectDocumentJournal(this));
}

//***********************************************************************
//*                          read & write                               *
//***********************************************************************

bool ibValueMetaObjectDocumentJournal::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyDocuments->GetName(), m_propertyDocuments->GetNodeValue());
	node.SetValue(m_propertyDefFormList->GetName(), GetGuidByID(m_propertyDefFormList->GetValueAsInteger()).str());
	return ibValueMetaObjectGenericData::WriteData(node);
}

bool ibValueMetaObjectDocumentJournal::ReadData(const ibDataNode& node)
{
	m_propertyDocuments->SetNodeValue(node.GetProperty(m_propertyDocuments->GetName()));
	m_propertyDefFormList->SetValue(GetIdByGuid(node.GetValue<ibString>(m_propertyDefFormList->GetName())));
	return ibValueMetaObjectGenericData::ReadData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectDocumentJournal, "DocumentJournal", g_metaDocumentJournalCLSID);
