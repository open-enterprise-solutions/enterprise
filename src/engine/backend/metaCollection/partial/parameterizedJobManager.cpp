////////////////////////////////////////////////////////////////////////////
//	Description : parameterized scheduled job — manager
////////////////////////////////////////////////////////////////////////////

#include "parameterizedJobManager.h"

#include "backend/appData.h"
#include "backend/metaData.h"
#include "backend/query/dataQueryBuilder.h"   // L3 door — FindBy* via WhereLike
#include "commonObject.h"
#include "reference/reference.h"
#include "selector/objectSelector.h"
#include "backend/system/value/valueGuid.h"

const ibValueMetaObjectCommonModule* ibValueManagerDataObjectJob::GetManagerModule() const
{
	return m_metaObject->GetManagerModule();
}

ibValueReferenceDataObject* ibValueManagerDataObjectJob::EmptyRef() const
{
	return ibValueReferenceDataObject::Create(m_metaObject);
}

namespace {

// The same shared FindBy* the catalog manager uses: locate the first row whose `attribute LIKE
// pattern` and return its reference, through the L3 door. Empty reference on no match.
ibValueReferenceDataObject* FindByAttributeLike(const ibValueMetaObjectRecordDataMutableRef* meta,
	ibValueMetaObjectAttributePredefined* attr, const ibValue& cParam)
{
	if (attr == nullptr || cParam.IsEmpty())
		return ibValueReferenceDataObject::Create(meta);
	try {
		ibDataQueryBuilder q;
		q.From(meta->GetQueryable()).WhereLike(attr, attr->AdjustValue(cParam));
		ibReadPageRequest page;
		page.m_count = 1;
		ibDataQueryResult sel = q.Execute(page);
		if (sel.Next()) {
			const ibGuid foundedGuid = sel.GetValue(meta->GetQueryable()->GetIdentitySort().back().m_col).GetString();
			if (foundedGuid.isValid())
				return ibValueReferenceDataObject::Create(meta, foundedGuid);
		}
	}
	catch (...) { /* fall through to an empty reference */ }
	return ibValueReferenceDataObject::Create(meta);
}

} // namespace

ibValueReferenceDataObject* ibValueManagerDataObjectJob::FindByCode(const ibValue& cParam) const
{
	if (appData->DesignerMode())
		return ibValueReferenceDataObject::Create(m_metaObject);
	return FindByAttributeLike(m_metaObject, m_metaObject->GetDataCode(), cParam);
}

ibValueReferenceDataObject* ibValueManagerDataObjectJob::FindByDescription(const ibValue& cParam) const
{
	if (appData->DesignerMode())
		return ibValueReferenceDataObject::Create(m_metaObject);
	return FindByAttributeLike(m_metaObject, m_metaObject->GetDataDescription(), cParam);
}

enum Func {
	eCreateElement = 0,
	eCreateGroup,
	eSelect,
	eFindByCode,
	eFindByDescription,
	eExecute,
	eGetForm,
	eGetListForm,
	eGetSelectForm,
	eGetTemplate,
	eEmptyRef
};

void ibValueManagerDataObjectJob::FillManagerMethods(ibMemberTable& helper) const
{
	// Order is load-bearing — CallAsFunc switches on the method index (eCreateElement = 0 …).
	helper.AppendFunc(wxT("CreateElement"), wxT("CreateElement()"));
	helper.AppendFunc(wxT("CreateGroup"), wxT("CreateGroup()"));
	helper.AppendFunc(wxT("Select"), wxT("Select()"));
	helper.AppendFunc(wxT("FindByCode"), 1, wxT("FindByCode(code : string)"));
	helper.AppendFunc(wxT("FindByDescription"), 1, wxT("FindByDescription(descr : string)"));
	// RUN ONE ROW by reference, ignoring its schedule — the script-side twin of the list command,
	// and the same single entry the tick uses. Without it a job could only be exercised by waiting
	// out its interval, which makes "is the job wrong or is the manager wrong?" unanswerable.
	helper.AppendFunc(wxT("Execute"), 1, wxT("Execute(job : reference)"));
	helper.AppendFunc(wxT("GetForm"), 3, wxT("GetForm(name : string, owner : any, id : guid)"));
	helper.AppendFunc(wxT("GetListForm"), 3, wxT("GetListForm(name : string, owner : any, id : guid)"));
	helper.AppendFunc(wxT("GetSelectForm"), 3, wxT("GetSelectForm(name : string, owner : any, id : guid)"));
	helper.AppendFunc(wxT("GetTemplate"), 1, wxT("GetTemplate(name : string)"));
	helper.AppendFunc(wxT("EmptyRef"), wxT("EmptyRef()"));
}

bool ibValueManagerDataObjectJob::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case eCreateElement:
		pvarRetValue = m_metaObject->CreateObjectValue(ibObjectMode::OBJECT_ITEM);
		return true;
	case eCreateGroup:
		pvarRetValue = m_metaObject->CreateObjectValue(ibObjectMode::OBJECT_FOLDER);
		return true;
	case eSelect:
		pvarRetValue = new ibValueSelectorRecordDataObject(m_metaObject);
		return true;
	case eFindByCode:
		pvarRetValue = FindByCode(*paParams[0]);
		return true;
	case eFindByDescription:
		pvarRetValue = FindByDescription(*paParams[0]);
		return true;
	case eExecute: {
		// The argument is a reference to one of THIS job's rows — the one value that crosses a
		// session boundary, and the only thing a job is ever started with.
		ibValueReferenceDataObject* reference = lSizeArray > 0 ? paParams[0]->ConvertToType<ibValueReferenceDataObject>() : nullptr;
		if (reference == nullptr)
			ibBackendCoreException::Error(_("a job reference is required"));
		if (!m_metaObject->AccessRight_Execute())
			ibBackendCoreException::Error(_("insufficient rights to execute the job"));
		pvarRetValue = m_metaObject->RunJobByReference(reference->GetGuid());
		return true;
	}
	case eGetForm: {
		ibValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr;
		pvarRetValue = m_metaObject->GetGenericForm(lSizeArray > 0 ? paParams[0]->GetString() : wxString(wxEmptyString),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr,
			guidVal ? ((ibGuid)*guidVal) : ibGuid());
		return true;
	}
	case eGetListForm: {
		ibValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr;
		pvarRetValue = m_metaObject->GetListForm(lSizeArray > 0 ? paParams[0]->GetString() : wxString(wxEmptyString),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr,
			guidVal ? ((ibGuid)*guidVal) : ibGuid());
		return true;
	}
	case eGetSelectForm: {
		ibValueGuid* guidVal = lSizeArray > 2 ? paParams[2]->ConvertToType<ibValueGuid>() : nullptr;
		pvarRetValue = m_metaObject->GetSelectForm(lSizeArray > 0 ? paParams[0]->GetString() : wxString(wxEmptyString),
			lSizeArray > 1 ? paParams[1]->ConvertToType<ibBackendControlFrame>() : nullptr,
			guidVal ? ((ibGuid)*guidVal) : ibGuid());
		return true;
	}
	case eGetTemplate:
		pvarRetValue = m_metaObject->GetTemplate(paParams[0]->GetString());
		return true;
	case eEmptyRef:
		pvarRetValue = EmptyRef();
		return true;
	}

	return ibValueManagerDataObject::CallAsFunc(lMethodNum, pvarRetValue, paParams, lSizeArray);
}
