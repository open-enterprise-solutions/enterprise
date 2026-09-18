#include "sequence.h"
#include "sequenceManager.h"

#include "backend/metaData.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/system/value/valueDynamicList.h"   // ibCreateList — the list form rides the universal dynamic list

//***********************************************************************
//*                              metaData                               *
//***********************************************************************

ibValueMetaObjectSequence::ibValueMetaObjectSequence() : ibValueMetaObjectRegisterData()
{
	// The set's module opens with the handlers a set is written through, as a register's does.
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("BeforeWrite"), ibContentHelper::eProcedureHelper, { wxT("Cancel"), wxT("Replacing") });
	(*m_propertyObjectModule)->SetDefaultProcedure(wxT("OnWrite"), ibContentHelper::eProcedureHelper, { wxT("Cancel"), wxT("Replacing") });
}

ibValueMetaObjectSequence::~ibValueMetaObjectSequence()
{
}

// ⭐⭐ WHAT THIS METATYPE OWNS, IT DRIVES ITSELF through all four events — its two modules, the moment
// it publishes, and the identity of the borders table. The base drives what the base declares and no
// more, which is not a gap to work around but the rule: a kind's own children are its own business.
//
// 🛑 Leaving the modules out cost a crash rather than a missing menu entry: a record set asks its
// register for the module to compile against, and a module that was never created is a null
// metaobject — `ibValueMetaObject::GetFileName` on nothing, at the moment a document that registers
// in the sequence is loaded (dump, 2026-09-18).
bool ibValueMetaObjectSequence::OnCreateMetaObject(ibMetaData* metaData, int flags)
{
	return ibValueMetaObjectRegisterData::OnCreateMetaObject(metaData, flags)
		&& (*m_propertyAttributePointInTime)->OnCreateMetaObject(metaData, flags)
		&& m_borders->OnCreateMetaObject(metaData, flags)
		&& (*m_propertyManagerModule)->OnCreateMetaObject(metaData, flags)
		&& (*m_propertyObjectModule)->OnCreateMetaObject(metaData, flags);
}

bool ibValueMetaObjectSequence::OnLoadMetaObject(ibMetaData* metaData)
{
	if (!(*m_propertyManagerModule)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyObjectModule)->OnLoadMetaObject(metaData)) return false;
	if (!(*m_propertyAttributePointInTime)->OnLoadMetaObject(metaData)) return false;
	if (!m_borders->OnLoadMetaObject(metaData)) return false;

	return ibValueMetaObjectRegisterData::OnLoadMetaObject(metaData);
}

bool ibValueMetaObjectSequence::OnSaveMetaObject(int flags)
{
	if (!(*m_propertyManagerModule)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyObjectModule)->OnSaveMetaObject(flags)) return false;
	if (!(*m_propertyAttributePointInTime)->OnSaveMetaObject(flags)) return false;
	if (!m_borders->OnSaveMetaObject(flags)) return false;

	return ibValueMetaObjectRegisterData::OnSaveMetaObject(flags);
}

bool ibValueMetaObjectSequence::OnDeleteMetaObject()
{
	if (!(*m_propertyManagerModule)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyObjectModule)->OnDeleteMetaObject()) return false;
	if (!(*m_propertyAttributePointInTime)->OnDeleteMetaObject()) return false;
	if (!m_borders->OnDeleteMetaObject()) return false;

	return ibValueMetaObjectRegisterData::OnDeleteMetaObject();
}

// ⭐ THE BORDERS ARE A TABLE OF THE SEQUENCE'S OWN COLUMNS: the dimensions that make the key, and the
// registration the key has got to — its recorder and its date. Read as `Sequence.<Name>.Borders`.
bool ibValueMetaObjectSequence::OnAfterRunMetaObject(int flags)
{
	if (!ibValueMetaObjectRegisterData::OnAfterRunMetaObject(flags))
		return false;
	// …and the registrations answer through THIS kind's surface, which vends the moment beside the
	// stored columns. The base has just registered its own under the same (namespace, name); a source
	// is kept in a map by that pair, so registering here — after it — is what puts ours in its place.
	m_metaData->RegisterSource(&m_ownQueryable);
	if (HasBorders())
		m_metaData->RegisterSource(&m_bordersSource);
	return true;
}

bool ibValueMetaObjectSequence::OnBeforeCloseMetaObject()
{
	m_metaData->UnregisterSource(&m_bordersSource);   // mirror of the run's RegisterSource
	m_metaData->UnregisterSource(&m_ownQueryable);
	return ibValueMetaObjectRegisterData::OnBeforeCloseMetaObject();
}

// No "default list form" property yet: a sequence opens the form generated from its own fields. The
// property is a line of its own when somebody wants to pick a form (registers declare one each).
ibValueMetaObjectFormBase* ibValueMetaObjectSequence::GetDefaultFormByID(const ibFormID& /*id*/) const
{
	return nullptr;
}

// The list shows the REGISTRATIONS, ordered by the moment they carry — which is what a person opens
// a sequence to look at: what is registered, for which key, and how far it has got.
ibSourceDataObject* ibValueMetaObjectSequence::CreateSourceObject(const ibValueMetaObjectFormBase* metaObject) const
{
	switch (metaObject->GetTypeForm())
	{
	case eFormList:
		return ibCreateList(GetQueryable(), GetRegisterPeriod()->GetQueryColumn());
	}

	return nullptr;
}

// The list form of the registrations, built the way every register's list is.
ibBackendValueForm* ibValueMetaObjectSequence::GetListForm(const wxString& strFormName,
	ibBackendControlFrame* ownerControl, const ibUniqueKey& formGuid) const
{
	return ibValueMetaObjectGenericData::CreateAndBuildForm(
		strFormName,
		ibValueMetaObjectSequence::eFormList,
		ownerControl, ibCreateList(GetQueryable(), GetRegisterPeriod()->GetQueryColumn()),
		formGuid
	);
}

ibValueManagerDataObject* ibValueMetaObjectSequence::CreateManagerDataObjectValue() const
{
	return new ibValueManagerDataObjectSequence(this);
}

ibValueRecordSetObject* ibValueMetaObjectSequence::CreateRecordSetObjectRegValue(const ibUniqueKeyPair& uniqueKey) const
{
	return new ibValueRecordSetObjectSequence(this, uniqueKey);
}

// ⭐⭐ AND WHAT THIS METATYPE OWNS, IT ALSO WRITES AND READS. The borders' holder is written for its
// IDENTITY alone — the id is what the differ matches a table by, and what names the table on the next
// run. Left out, the holder was numbered at creation and forgotten on the way to the database: the
// table was created once and then belonged to nobody, and `Sequence.<Name>.Borders` could not be
// queried after a restart, because nothing said there was a border table at all (2026-09-18).
bool ibValueMetaObjectSequence::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyAutomaticBorder->GetName(), m_propertyAutomaticBorder->GetNodeValue());
	node.SetProperty(m_propertyAttributePointInTime->GetName(), m_propertyAttributePointInTime->GetNodeValue());
	m_borders->SaveNode(node.Child(m_borders->GetName()));

	node.SetProperty(m_propertyObjectModule->GetName(), m_propertyObjectModule->GetNodeValue());
	node.SetProperty(m_propertyManagerModule->GetName(), m_propertyManagerModule->GetNodeValue());

	return ibValueMetaObjectRegisterData::WriteData(node);
}

bool ibValueMetaObjectSequence::ReadData(const ibDataNode& node)
{
	m_propertyAutomaticBorder->SetNodeValue(node.GetProperty(m_propertyAutomaticBorder->GetName()));
	m_propertyAttributePointInTime->SetNodeValue(node.GetProperty(m_propertyAttributePointInTime->GetName()));
	if (const ibDataNode* saved = node.FindChild(m_borders->GetName()))
		m_borders->LoadNode(*saved);

	m_propertyObjectModule->SetNodeValue(node.GetProperty(m_propertyObjectModule->GetName()));
	m_propertyManagerModule->SetNodeValue(node.GetProperty(m_propertyManagerModule->GetName()));

	return ibValueMetaObjectRegisterData::ReadData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectSequence, "Sequence", g_metaSequenceCLSID);
