////////////////////////////////////////////////////////////////////////////
//	Description : parameterized scheduled job — save & load metaData
////////////////////////////////////////////////////////////////////////////

#include "parameterizedJob.h"

#include "backend/serialize/dataBuilder.h"

//***********************************************************************
//*                       Save & load metaData                          *
//***********************************************************************

bool ibValueMetaObjectParameterizedJob::WriteData(ibDataNode& node) const
{
	// The job's own attributes are SERIALISED, like the catalog's Owner — an attribute that is not
	// written comes back with a fresh metaID on the next load, and a column is addressed by that
	// id: the stored Schedule / LastRun of every row would then belong to no requisite at all.
	node.SetProperty(m_propertyAttributeActive->GetName(), m_propertyAttributeActive->GetNodeValue());
	node.SetProperty(m_propertyAttributeSchedule->GetName(), m_propertyAttributeSchedule->GetNodeValue());
	node.SetProperty(m_propertyAttributeLastRun->GetName(), m_propertyAttributeLastRun->GetNodeValue());
	node.SetProperty(m_propertyAttributeNextRun->GetName(), m_propertyAttributeNextRun->GetNodeValue());

	node.SetProperty(m_propertyObjectModule->GetName(), m_propertyObjectModule->GetNodeValue());
	node.SetProperty(m_propertyManagerModule->GetName(), m_propertyManagerModule->GetNodeValue());

	node.SetValue(m_propertyDefFormObject->GetName(), GetGuidByID(m_propertyDefFormObject->GetValueAsInteger()).str());
	node.SetValue(m_propertyDefFormFolder->GetName(), GetGuidByID(m_propertyDefFormFolder->GetValueAsInteger()).str());
	node.SetValue(m_propertyDefFormList->GetName(), GetGuidByID(m_propertyDefFormList->GetValueAsInteger()).str());
	node.SetValue(m_propertyDefFormSelect->GetName(), GetGuidByID(m_propertyDefFormSelect->GetValueAsInteger()).str());
	node.SetValue(m_propertyDefFormFolderSelect->GetName(), GetGuidByID(m_propertyDefFormFolderSelect->GetValueAsInteger()).str());

	// Both sides in the same commit — a property declared and never serialised is "the one failure
	// mode that looks like the feature working" (SplitTotals, docs/scheduled-jobs.md § 12).
	node.SetProperty(m_propertyUse->GetName(), m_propertyUse->GetNodeValue());
	node.SetProperty(m_propertySchedule->GetName(), m_propertySchedule->GetNodeValue());
	node.SetProperty(m_propertyRetryCount->GetName(), m_propertyRetryCount->GetNodeValue());
	node.SetProperty(m_propertyRetryInterval->GetName(), m_propertyRetryInterval->GetNodeValue());

	return ibValueMetaObjectRecordDataHierarchyMutableRef::WriteData(node);
}

bool ibValueMetaObjectParameterizedJob::ReadData(const ibDataNode& node)
{
	m_propertyAttributeActive->ReadNodeValue(node.GetProperty(m_propertyAttributeActive->GetName()));
	m_propertyAttributeSchedule->ReadNodeValue(node.GetProperty(m_propertyAttributeSchedule->GetName()));
	m_propertyAttributeLastRun->ReadNodeValue(node.GetProperty(m_propertyAttributeLastRun->GetName()));
	m_propertyAttributeNextRun->ReadNodeValue(node.GetProperty(m_propertyAttributeNextRun->GetName()));

	m_propertyObjectModule->ReadNodeValue(node.GetProperty(m_propertyObjectModule->GetName()));
	m_propertyManagerModule->ReadNodeValue(node.GetProperty(m_propertyManagerModule->GetName()));

	m_propertyDefFormObject->SetValue(GetIdByGuid(node.GetValue<wxString>(m_propertyDefFormObject->GetName())));
	m_propertyDefFormFolder->SetValue(GetIdByGuid(node.GetValue<wxString>(m_propertyDefFormFolder->GetName())));
	m_propertyDefFormList->SetValue(GetIdByGuid(node.GetValue<wxString>(m_propertyDefFormList->GetName())));
	m_propertyDefFormSelect->SetValue(GetIdByGuid(node.GetValue<wxString>(m_propertyDefFormSelect->GetName())));
	m_propertyDefFormFolderSelect->SetValue(GetIdByGuid(node.GetValue<wxString>(m_propertyDefFormFolderSelect->GetName())));

	m_propertyUse->ReadNodeValue(node.GetProperty(m_propertyUse->GetName()));
	m_propertySchedule->ReadNodeValue(node.GetProperty(m_propertySchedule->GetName()));
	m_propertyRetryCount->ReadNodeValue(node.GetProperty(m_propertyRetryCount->GetName()));
	m_propertyRetryInterval->ReadNodeValue(node.GetProperty(m_propertyRetryInterval->GetName()));

	return ibValueMetaObjectRecordDataHierarchyMutableRef::ReadData(node);
}

