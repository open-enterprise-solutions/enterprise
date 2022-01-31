////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaform object
////////////////////////////////////////////////////////////////////////////

#include "metaFormObject.h"
#include "databaseLayer/databaseLayer.h"
#include "frontend/visualView/visualEditorView.h"
#include "metadata/metadata.h"
#include "metadata/objects/baseObject.h"
#include "appData.h"

//***********************************************************************
//*                             IMetaFormObject metadata                *
//***********************************************************************

wxIMPLEMENT_ABSTRACT_CLASS(IMetaFormObject, CMetaModuleObject);

//***********************************************************************
//*                          common value object                        *
//***********************************************************************

CValueForm *IMetaFormObject::LoadControl(const wxString &formData)
{
	wxMemoryBuffer tempBuffer =
		wxBase64Decode(formData);
	CMemoryReader readerData(tempBuffer.GetData(), tempBuffer.GetBufSize());
	u64 clsid = 0;
	CMemoryReader *readerMemory = readerData.open_chunk_iterator(clsid);
	if (!readerMemory)
		return NULL;
	u64 form_id = 0;
	CMemoryReader *readerMetaMemory = readerMemory->open_chunk_iterator(form_id);
	if (!readerMetaMemory)
		return NULL;
	std::shared_ptr <CMemoryReader>readerDataMemory(readerMetaMemory->open_chunk(eDataBlock));
	CValueForm *valueForm = new CValueForm;
	valueForm->SetReadOnly(m_enabled);
	if (!valueForm->LoadControl(this, *readerDataMemory)) {
		wxDELETE(valueForm);
		return NULL;
	}
	std::shared_ptr <CMemoryReader> readerChildMemory(readerMetaMemory->open_chunk(eChildBlock));
	if (readerChildMemory) {
		if (!LoadChildControl(valueForm, *readerChildMemory, valueForm)) {
			wxDELETE(valueForm);
			return NULL;
		}
	}
	return valueForm;
}

bool IMetaFormObject::LoadChildControl(CValueForm *valueForm, CMemoryReader & readerData, IValueFrame *controlParent)
{
	CLASS_ID clsid = 0;
	CMemoryReader *prevReaderMemory = NULL;
	while (!readerData.eof()) {
		CMemoryReader *readerMemory = readerData.open_chunk_iterator(clsid, &*prevReaderMemory);
		if (!readerMemory)
			break;
		u64 form_id = 0;
		CMemoryReader *prevReaderMetaMemory = NULL;
		while (!readerData.eof()) {
			CMemoryReader *readerMetaMemory = readerMemory->open_chunk_iterator(form_id, &*prevReaderMetaMemory);
			if (!readerMetaMemory)
				break;
			wxASSERT(clsid != 0);
			wxString classType = CValue::GetNameObjectFromID(clsid);
			wxASSERT(classType.Length() > 0);
			IValueFrame *newControl = valueForm->NewObject(classType, controlParent, false);
			newControl->SetReadOnly(m_enabled);
			std::shared_ptr <CMemoryReader>readerDataMemory(readerMetaMemory->open_chunk(eDataBlock));			
			if (!newControl->LoadControl(this, *readerDataMemory))
				return false;		
			std::shared_ptr <CMemoryReader> readerChildMemory(readerMetaMemory->open_chunk(eChildBlock));
			if (readerChildMemory) {
				if (!LoadChildControl(valueForm, *readerChildMemory, newControl))
					return false;
			}
			prevReaderMetaMemory = readerMetaMemory;
		}
		prevReaderMemory = readerMemory;
	}
	return true;
}

wxString IMetaFormObject::SaveControl(CValueForm *valueForm)
{
	CMemoryWriter writterData;

	//Save common object
	CMemoryWriter writterMemory;
	CMemoryWriter writterMetaMemory;
	CMemoryWriter writterDataMemory;
	if (!valueForm->SaveControl(this, writterDataMemory)) {
		return wxEmptyString;
	}
	writterMetaMemory.w_chunk(eDataBlock, writterDataMemory.pointer(), writterDataMemory.size());
	CMemoryWriter writterChildMemory;
	if (!SaveChildControl(valueForm, writterChildMemory, valueForm))
		return wxEmptyString;
	writterMetaMemory.w_chunk(eChildBlock, writterChildMemory.pointer(), writterChildMemory.size());
	writterMemory.w_chunk(valueForm->GetControlID(), writterMetaMemory.pointer(), writterMetaMemory.size());
	writterData.w_chunk(valueForm->GetClsid(), writterMemory.pointer(), writterMemory.size());

	return wxBase64Encode(
		writterData.pointer(), writterData.size()
	);
}

bool IMetaFormObject::SaveChildControl(CValueForm *valueForm, CMemoryWriter &writterData, IValueFrame *controlParent)
{
	for (unsigned int idx = 0; idx < controlParent->GetChildCount(); idx++) {
		CMemoryWriter writterMemory;
		CMemoryWriter writterMetaMemory;
		CMemoryWriter writterDataMemory;
		IValueFrame *child = controlParent->GetChild(idx);
		wxASSERT(child);
		if (!child->SaveControl(this, writterDataMemory)) {
			return false;
		}
		writterMetaMemory.w_chunk(eDataBlock, writterDataMemory.pointer(), writterDataMemory.size());
		CMemoryWriter writterChildMemory;
		if (!SaveChildControl(valueForm, writterChildMemory, child)) {
			return false;
		}
		writterMetaMemory.w_chunk(eChildBlock, writterChildMemory.pointer(), writterChildMemory.size());
		writterMemory.w_chunk(child->GetControlID(), writterMetaMemory.pointer(), writterMetaMemory.size());
		writterData.w_chunk(child->GetClsid(), writterMemory.pointer(), writterMemory.size());
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////////////////

void IMetaFormObject::SaveFormData(CValueForm *valueForm)
{
	m_formData = SaveControl(valueForm);
}

IMetaFormObject::IMetaFormObject(const wxString &name, const wxString &synonym, const wxString &comment) :
	CMetaModuleObject(name, synonym, comment), m_formData(wxEmptyString), m_firstInitialized(false), m_typeFrom(defaultFormType)
{
	//set default proc
	SetDefaultProcedure("beforeOpen", eContentHelper::eProcedureHelper, { "cancel" });
	SetDefaultProcedure("onOpen", eContentHelper::eProcedureHelper);
	SetDefaultProcedure("beforeClose", eContentHelper::eProcedureHelper, { "cancel" });
	SetDefaultProcedure("onClose", eContentHelper::eProcedureHelper);

	SetDefaultProcedure("onReOpen", eContentHelper::eProcedureHelper);

	SetDefaultProcedure("choiceProcessing", eContentHelper::eProcedureHelper, { { "selectedValue" }, { "choiceSource" } });
}

bool IMetaFormObject::LoadData(CMemoryReader &reader)
{
	reader.r_stringZ(m_formData);
	reader.r_stringZ(m_moduleData);
	return true;
}

bool IMetaFormObject::SaveData(CMemoryWriter &writer)
{
	writer.w_stringZ(m_formData);
	writer.w_stringZ(m_moduleData);
	return true;
}

CValueForm *IMetaFormObject::GenerateForm(IValueFrame *ownerControl,
	IDataObjectSource *ownerSrc, const Guid &guidForm)
{
	CValueForm *valueForm = NULL;

	if (m_formData.IsEmpty()) {
		valueForm = new CValueForm();
		valueForm->InitializeForm(ownerControl, this, ownerSrc, guidForm, m_enabled);
		valueForm->ReadProperty();

		//build form
		if (m_firstInitialized) {
			valueForm->BuildForm(m_typeFrom);
			SaveFormData(valueForm);
			m_firstInitialized = false;
		}
		return valueForm;
	}

	valueForm = LoadControl(m_formData);

	if (valueForm) {
		valueForm->InitializeForm(ownerControl, this, ownerSrc, guidForm, m_enabled);
		valueForm->ReadProperty();
	}

	return valueForm;
}

CValueForm *IMetaFormObject::GenerateFormAndRun(IValueFrame *ownerControl,
	IDataObjectSource *ownerSrc, const Guid &guidForm)
{
	CValueForm *valueForm = NULL;

	IModuleManager *moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);
	if (!moduleManager->FindCompileModule(this, valueForm)) {
		valueForm = GenerateForm(ownerControl, ownerSrc, guidForm);
		if (!valueForm->InitializeFormModule()) {
			wxDELETE(valueForm);
			return NULL;
		}
	}

	return valueForm;
}

wxIMPLEMENT_DYNAMIC_CLASS(CMetaFormObject, IMetaFormObject)

//***********************************************************************
//*                            Metaform                                 *
//***********************************************************************

CMetaFormObject::CMetaFormObject(const wxString &name, const wxString &synonym, const wxString &comment) : IMetaFormObject(name, synonym, comment)
{
	PropertyContainer *categoryForm = IObjectBase::CreatePropertyContainer("FormType");
	categoryForm->AddProperty("form_type", PropertyType::PT_OPTION, &CMetaFormObject::GetFormType);
	m_category->AddCategory(categoryForm);
}

bool CMetaFormObject::LoadData(CMemoryReader &reader)
{
	m_typeFrom = reader.r_u8();
	return IMetaFormObject::LoadData(reader);
}

bool CMetaFormObject::SaveData(CMemoryWriter &writer)
{
	writer.w_u8(m_typeFrom);
	return IMetaFormObject::SaveData(writer);
}

OptionList *CMetaFormObject::GetFormType(Property *)
{
	IMetaObjectValue *metaObjectValue = wxStaticCast(
		GetParent(), IMetaObjectValue
	);

	wxASSERT(metaObjectValue);

	OptionList *optionlist = metaObjectValue->GetFormType();
	optionlist->AddOption(formDefaultName, defaultFormType);

	return optionlist;
}

//***********************************************************************
//*                           read & save property                      *
//***********************************************************************

void CMetaFormObject::ReadProperty()
{
	CMetaModuleObject::ReadProperty();
	IObjectBase::SetPropertyValue("form_type", m_typeFrom);
}

void CMetaFormObject::SaveProperty()
{
	CMetaModuleObject::SaveProperty();
	IObjectBase::GetPropertyValue("form_type", m_typeFrom);
}

//***********************************************************************
//*                             event object                            *
//***********************************************************************

#include "frontend/mainFrame.h"
#include "frontend/formSelector/formSelector.h"

bool CMetaFormObject::OnCreateMetaObject(IMetadata *metaData)
{
	m_firstInitialized = true;

	if (!IMetaFormObject::OnCreateMetaObject(metaData))
		return false;

	IMetaObjectValue *metaObjectValue = wxStaticCast(
		GetParent(), IMetaObjectValue
	);

	wxASSERT(metaObjectValue);
	OptionList *optList = metaObjectValue->GetFormType();

	CSelectTypeForm *selectTypeForm = new CSelectTypeForm(metaObjectValue, this);

	for (auto option : optList->GetOptions()) {
		selectTypeForm->AppendTypeForm(option.m_option, option.m_intVal);
	}

	wxDELETE(optList);

	selectTypeForm->CreateSelector();
	int res = selectTypeForm->ShowModal();
	if (res != wxID_CANCEL) {
		m_typeFrom = res;
	}
	else {
		return false;
	}

	metaObjectValue->OnCreateMetaForm(this);
	return true;
}

bool CMetaFormObject::OnLoadMetaObject(IMetadata *metaData)
{
	return IMetaFormObject::OnLoadMetaObject(metaData);
}

bool CMetaFormObject::OnSaveMetaObject()
{
	return IMetaFormObject::OnSaveMetaObject();
}

bool CMetaFormObject::OnDeleteMetaObject()
{
	IModuleManager *moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	IMetaObjectValue *metaObjectValue = wxStaticCast(
		GetParent(), IMetaObjectValue
	);
	wxASSERT(metaObjectValue);
	metaObjectValue->OnRemoveMetaForm(this);

	return IMetaFormObject::OnDeleteMetaObject();
}

bool CMetaFormObject::OnRunMetaObject(int flags)
{
	if (appData->DesignerMode()) {
		IModuleManager *moduleManager = m_metaData->GetModuleManager();
		wxASSERT(moduleManager);
		IMetaObjectValue *metaObjectValue = wxStaticCast(m_parent, IMetaObjectValue);
		wxASSERT(metaObjectValue);
		return moduleManager->AddCompileModule(this, metaObjectValue->CreateObjectValue(this));
	}

	return IMetaFormObject::OnRunMetaObject(flags);
}

bool CMetaFormObject::OnCloseMetaObject()
{
	IModuleManager *moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!moduleManager->RemoveCompileModule(this))
		return false;

	return IMetaFormObject::OnCloseMetaObject();
}

//***********************************************************************
//*                           CommonFormObject metadata                 *
//***********************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CMetaCommonFormObject, IMetaFormObject)

CMetaCommonFormObject::CMetaCommonFormObject(const wxString &name, const wxString &synonym, const wxString &comment) : IMetaFormObject(name, synonym, comment) { m_typeFrom = defaultFormType; }

//***********************************************************************
//*                             event object                            *
//***********************************************************************

bool CMetaCommonFormObject::OnCreateMetaObject(IMetadata *metaData)
{
	m_firstInitialized = true;
	return CMetaModuleObject::OnCreateMetaObject(metaData);
}

bool CMetaCommonFormObject::OnRunMetaObject(int flags)
{
	if (appData->DesignerMode()) {
		IModuleManager *moduleManager = m_metaData->GetModuleManager();
		wxASSERT(moduleManager);
		if (moduleManager->AddCompileModule(this, GenerateFormAndRun())) {
			return CMetaModuleObject::OnRunMetaObject(flags);
		}
		return false;
	}
	return CMetaModuleObject::OnRunMetaObject(flags);
}

bool CMetaCommonFormObject::OnCloseMetaObject()
{
	if (appData->DesignerMode()) {
		IModuleManager *moduleManager = m_metaData->GetModuleManager();
		wxASSERT(moduleManager);
		if (moduleManager->RemoveCompileModule(this)) {
			return CMetaModuleObject::OnCloseMetaObject();
		}
		return false;
	}
	return CMetaModuleObject::OnCloseMetaObject();
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_REGISTER(CMetaFormObject, "metaForm", g_metaFormCLSID);
METADATA_REGISTER(CMetaCommonFormObject, "metaCommonForm", g_metaCommonFormCLSID);