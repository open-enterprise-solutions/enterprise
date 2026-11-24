////////////////////////////////////////////////////////////////////////////
//	Description : document journal - metaData
////////////////////////////////////////////////////////////////////////////

#include "documentJournal.h"
#include "documentJournalManager.h"
#include "document.h"

#include "backend/metaData.h"
#include "backend/objCtor.h"
#include "core/serialize/dataBuilder.h"
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
	// After the properties it reads: a journal is never without its reading, a journal of nothing has one too.
	m_queryable = std::make_shared<ibDocumentJournalQueryable>(this);
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

// ⭐ A JOURNAL OF NO DOCUMENT IS NOT SAVED. Its rows are its documents': with none listed it has no rows, no
// column types, and no document for Add to make. Said into the ledger and refused, not thrown, as the accounting
// register says its missing chart (accountingRegisterMetadata.cpp): an exception out of the save would leave the
// application's write open.
bool ibValueMetaObjectDocumentJournal::OnSaveMetaObject(int flags)
{
	if (GetDocuments().empty()) {
		RestructureError(ibString::Format(
			_("%s: at least one document is required - a journal lists documents, and its rows are theirs"), GetName()));
		return false;
	}
	return ibValueMetaObjectGenericData::OnSaveMetaObject(flags);
}

// ⭐ THE MANAGER IS THE JOURNAL'S, AND SO ARE THE SELECTION AND THE SOURCE. A journal has nothing to compile and
// nothing to create, so these are the whole of its run: `DocumentJournals.<Name>` and its Select() for code,
// `DocumentJournal.<Name>` for a query and a list.
bool ibValueMetaObjectDocumentJournal::OnBeforeRunMetaObject(int flags)
{
	registerManager();
	registerSelection();
	return ibValueMetaObjectGenericData::OnBeforeRunMetaObject(flags);
}

// …and the reading is made again here, once every document has run: its columns are typed by theirs.
bool ibValueMetaObjectDocumentJournal::OnAfterRunMetaObject(int flags)
{
	if (!ibValueMetaObjectGenericData::OnAfterRunMetaObject(flags))
		return false;
	m_queryable = std::make_shared<ibDocumentJournalQueryable>(this);
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
	unregisterSelection();
	return ibValueMetaObjectGenericData::OnAfterCloseMetaObject();
}

// Another list of documents, or one of them retyped or deleted, is another reading: typed by them, and reading
// them. Whoever still reads the old one keeps it until done.
bool ibValueMetaObjectDocumentJournal::OnReloadMetaObject()
{
	m_queryable = std::make_shared<ibDocumentJournalQueryable>(this);
	return ibValueMetaObjectGenericData::OnReloadMetaObject();
}

void ibValueMetaObjectDocumentJournal::OnPropertyChanged(ibProperty* property, const ibVariant& oldValue, const ibVariant& newValue)
{
	if (property == m_propertyDocuments)
		OnReloadMetaObject();
	ibValueMetaObjectGenericData::OnPropertyChanged(property, oldValue, newValue);
}

//***********************************************************************
//*                               forms                                 *
//***********************************************************************

void ibValueMetaObjectDocumentJournal::OnCreateFormObject(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == eFormList && m_propertyDefFormList->GetValueAsInteger() == ibNOT_FOUND)
		m_propertyDefFormList->SetValue(metaForm->GetMetaID());
}

void ibValueMetaObjectDocumentJournal::OnRemoveMetaForm(ibValueMetaObjectFormBase* metaForm)
{
	if (metaForm->GetTypeForm() == eFormList && m_propertyDefFormList->GetValueAsInteger() == metaForm->GetMetaID())
		m_propertyDefFormList->SetValue(ibNOT_FOUND);
}

ibValueMetaObjectFormBase* ibValueMetaObjectDocumentJournal::GetDefaultFormByID(const ibFormID& id) const
{
	if (id == eFormList && m_propertyDefFormList->GetValueAsInteger() != ibNOT_FOUND)
		return FindFormObjectByFilter(m_propertyDefFormList->GetValueAsInteger());
	return nullptr;
}

// The list shows the documents by date - what a journal is opened to look at - and reads the journal's
// queryable, which sets each document's access up itself.
ibSourcePtr<ibSourceDataObject> ibValueMetaObjectDocumentJournal::CreateSourceObject(const ibCreateRequest& request, const ibFormID& form_id) const
{
	switch (form_id)
	{
	case eFormList:
		return ibSourcePtr<ibSourceDataObject>(ibCreateList(request, m_queryable, m_queryable->GetDateColumn()));
	}

	return nullptr;
}

ibFormPtr<ibBackendValueForm> ibValueMetaObjectDocumentJournal::GetListForm(const ibFormRequest& request,
	ibBackendControlFrame* ownerControl) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		request,
		ibValueMetaObjectDocumentJournal::eFormList,
		ownerControl, ibCreateList(request.m_create, m_queryable, m_queryable->GetDateColumn())
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
