////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaform object
////////////////////////////////////////////////////////////////////////////

#include "metaFormObject.h"
#include <3rdparty/databaseLayer/databaseLayer.h>
#include "frontend/visualView/visualHost.h"
#include "core/metadata/metadata.h"
#include "core/metadata/metaObjects/objects/object.h"
#include "appData.h"

//***********************************************************************
//*                             IMetaFormObject metadata                *
//***********************************************************************

wxIMPLEMENT_ABSTRACT_CLASS(IMetaFormObject, CMetaModuleObject);

//***********************************************************************
//*                          common value object                        *
//***********************************************************************

CValueForm* IMetaFormObject::LoadControl(const wxMemoryBuffer& formData) const
{
	CMemoryReader readerData(formData);
	u64 clsid = 0;
	std::shared_ptr<CMemoryReader>readerMemory(readerData.open_chunk_iterator(clsid));
	if (!readerMemory)
		return NULL;
	u64 form_id = 0;
	std::shared_ptr<CMemoryReader>readerMetaMemory(readerMemory->open_chunk_iterator(form_id));
	if (!readerMetaMemory)
		return NULL;
	std::shared_ptr <CMemoryReader>readerDataMemory(readerMetaMemory->open_chunk(eDataBlock));
	CValueForm* valueForm = new CValueForm;
	valueForm->SetReadOnly(m_propEnabled);
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

bool IMetaFormObject::LoadChildControl(CValueForm* valueForm, CMemoryReader& readerData, IValueFrame* controlParent) const
{
	CLASS_ID clsid = 0;
	CMemoryReader* prevReaderMemory = NULL;
	while (!readerData.eof()) {
		CMemoryReader* readerMemory = readerData.open_chunk_iterator(clsid, &*prevReaderMemory);
		if (!readerMemory)
			break;
		u64 form_id = 0;
		CMemoryReader* prevReaderMetaMemory = NULL;
		while (!readerData.eof()) {
			CMemoryReader* readerMetaMemory = readerMemory->open_chunk_iterator(form_id, &*prevReaderMetaMemory);
			if (!readerMetaMemory)
				break;
			wxASSERT(clsid != 0);
			wxString classType = CValue::GetNameObjectFromID(clsid);
			wxASSERT(classType.Length() > 0);
			IValueFrame* newControl = valueForm->NewObject(classType, controlParent, false);
			newControl->SetReadOnly(m_propEnabled);
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

wxMemoryBuffer IMetaFormObject::SaveControl(CValueForm* valueForm) const
{
	CMemoryWriter writterData;

	//Save common object
	CMemoryWriter writterMemory;
	CMemoryWriter writterMetaMemory;
	CMemoryWriter writterDataMemory;
	if (!valueForm->SaveControl(this, writterDataMemory)) {
		return wxMemoryBuffer();
	}
	writterMetaMemory.w_chunk(eDataBlock, writterDataMemory.pointer(), writterDataMemory.size());
	CMemoryWriter writterChildMemory;
	if (!SaveChildControl(valueForm, writterChildMemory, valueForm))
		return wxMemoryBuffer();
	writterMetaMemory.w_chunk(eChildBlock, writterChildMemory.pointer(), writterChildMemory.size());
	writterMemory.w_chunk(valueForm->GetControlID(), writterMetaMemory.pointer(), writterMetaMemory.size());
	writterData.w_chunk(valueForm->GetClsid(), writterMemory.pointer(), writterMemory.size());

	return writterData.buffer();
}

bool IMetaFormObject::SaveChildControl(CValueForm* valueForm, CMemoryWriter& writterData, IValueFrame* controlParent) const
{
	for (unsigned int idx = 0; idx < controlParent->GetChildCount(); idx++) {
		CMemoryWriter writterMemory;
		CMemoryWriter writterMetaMemory;
		CMemoryWriter writterDataMemory;
		IValueFrame* child = controlParent->GetChild(idx);
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

void IMetaFormObject::SaveFormData(CValueForm* valueForm)
{
	m_formData = SaveControl(valueForm);
}

IMetaFormObject::IMetaFormObject(const wxString& name, const wxString& synonym, const wxString& comment) :
	CMetaModuleObject(name, synonym, comment), m_formData(), m_firstInitialized(false)
{
	//set default proc
	SetDefaultProcedure("beforeOpen", eContentHelper::eProcedureHelper, { "cancel" });
	SetDefaultProcedure("onOpen", eContentHelper::eProcedureHelper);
	SetDefaultProcedure("beforeClose", eContentHelper::eProcedureHelper, { "cancel" });
	SetDefaultProcedure("onClose", eContentHelper::eProcedureHelper);

	SetDefaultProcedure("onReOpen", eContentHelper::eProcedureHelper);
	SetDefaultProcedure("choiceProcessing", eContentHelper::eProcedureHelper, { { "selectedValue" }, { "choiceSource" } });
	SetDefaultProcedure("refreshDisplay", eContentHelper::eProcedureHelper);
}

#define chunkForm 0x023456543

bool IMetaFormObject::LoadData(CMemoryReader& reader)
{
	reader.r_chunk(m_metaId + chunkForm, m_formData);
	reader.r_stringZ(m_moduleData);
	return true;
}

bool IMetaFormObject::SaveData(CMemoryWriter& writer)
{
	writer.w_chunk(m_metaId + chunkForm, m_formData);
	writer.w_stringZ(m_moduleData);
	return true;
}

CValueForm* IMetaFormObject::GenerateForm(IControlFrame* ownerControl,
	ISourceDataObject* ownerSrc, const CUniqueKey& guidForm)
{
	if (m_formData.IsEmpty()) {
		CValueForm* valueForm = new CValueForm(ownerControl, this, ownerSrc, guidForm, m_propEnabled);
		//build form
		if (m_firstInitialized) {
			valueForm->BuildForm(GetTypeForm());
			SaveFormData(valueForm);
			m_firstInitialized = false;
		}
		return valueForm;
	}

	CValueForm *valueForm = LoadControl(m_formData);

	if (valueForm != NULL) {
		valueForm->InitializeForm(ownerControl, this, ownerSrc, guidForm, m_propEnabled);
	}

	return valueForm;
}

CValueForm* IMetaFormObject::GenerateFormAndRun(IControlFrame* ownerControl,
	ISourceDataObject* ownerSrc, const CUniqueKey& guidForm)
{
	CValueForm* valueForm = NULL;

	IModuleManager* moduleManager = m_metaData->GetModuleManager();
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

CMetaFormObject::CMetaFormObject(const wxString& name, const wxString& synonym, const wxString& comment) : IMetaFormObject(name, synonym, comment)
{
}

bool CMetaFormObject::LoadData(CMemoryReader& reader)
{
	m_properyFormType->SetValue(reader.r_s32());
	return IMetaFormObject::LoadData(reader);
}

bool CMetaFormObject::SaveData(CMemoryWriter& writer)
{
	writer.w_s32(m_properyFormType->GetValueAsInteger());
	return IMetaFormObject::SaveData(writer);
}

OptionList* CMetaFormObject::GetFormType(PropertyOption*)
{
	IMetaObjectWrapperData* metaObject = wxStaticCast(
		GetParent(), IMetaObjectWrapperData
	);

	wxASSERT(metaObject);

	OptionList* optionlist = metaObject->GetFormType();
	optionlist->AddOption(formDefaultName, defaultFormType);

	return optionlist;
}

//***********************************************************************
//*                             event object                            *
//***********************************************************************

#include "frontend/mainFrame.h"
#include "frontend/formSelector/formSelector.h"

bool CMetaFormObject::OnCreateMetaObject(IMetadata* metaData)
{
	m_firstInitialized = true;

	if (!IMetaFormObject::OnCreateMetaObject(metaData))
		return false;

	IMetaObjectWrapperData* metaObject = wxStaticCast(
		GetParent(), IMetaObjectWrapperData
	);

	wxASSERT(metaObject);
	OptionList* optList = metaObject->GetFormType();

	CSelectTypeForm* selectTypeForm = new CSelectTypeForm(metaObject, this);
	for (auto option : optList->GetOptions()) {
		selectTypeForm->AppendTypeForm(option.m_name, option.m_label, option.m_intVal);
	}
	wxDELETE(optList);

	selectTypeForm->CreateSelector();
	form_identifier_t res = selectTypeForm->ShowModal();
	if (res != wxID_CANCEL) {
		m_properyFormType->SetValue(res);
	}
	else {
		return false;
	}

	metaObject->OnCreateMetaForm(this);

	if (metaData != NULL) {
		IMetadataWrapperTree* metaTree = metaData->GetMetaTree();
		if (metaTree != NULL) {
			metaTree->UpdateChoiceSelection();
		}
	}

	return true;
}

bool CMetaFormObject::OnLoadMetaObject(IMetadata* metaData)
{
	return IMetaFormObject::OnLoadMetaObject(metaData);
}

bool CMetaFormObject::OnSaveMetaObject()
{
	return IMetaFormObject::OnSaveMetaObject();
}

bool CMetaFormObject::OnDeleteMetaObject()
{
	IMetaObjectWrapperData* metaObject = wxStaticCast(m_parent, IMetaObjectWrapperData);
	wxASSERT(metaObject);
	metaObject->OnRemoveMetaForm(this);

	return IMetaFormObject::OnDeleteMetaObject();
}

bool CMetaFormObject::OnBeforeRunMetaObject(int flags)
{
	return IMetaFormObject::OnBeforeRunMetaObject(flags);
}

bool CMetaFormObject::OnAfterRunMetaObject(int flags)
{
	if (appData->DesignerMode()) {

		IModuleManager* moduleManager = m_metaData->GetModuleManager();
		wxASSERT(moduleManager);

		IMetaObjectWrapperData* metaObject = wxStaticCast(m_parent, IMetaObjectWrapperData);
		wxASSERT(metaObject);

		return moduleManager->AddCompileModule(this, metaObject->CreateObjectForm(this));
	}

	return IMetaFormObject::OnAfterRunMetaObject(flags);
}

bool CMetaFormObject::OnBeforeCloseMetaObject()
{
	IModuleManager* moduleManager = m_metaData->GetModuleManager();
	wxASSERT(moduleManager);

	if (!moduleManager->RemoveCompileModule(this))
		return false;

	return IMetaFormObject::OnBeforeCloseMetaObject();
}

bool CMetaFormObject::OnAfterCloseMetaObject()
{
	return IMetaFormObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                           CommonFormObject metadata                 *
//***********************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CMetaCommonFormObject, IMetaFormObject)

CMetaCommonFormObject::CMetaCommonFormObject(const wxString& name, const wxString& synonym, const wxString& comment) : IMetaFormObject(name, synonym, comment) {}

//***********************************************************************
//*                             event object                            *
//***********************************************************************

bool CMetaCommonFormObject::OnCreateMetaObject(IMetadata* metaData)
{
	m_firstInitialized = true;
	return CMetaModuleObject::OnCreateMetaObject(metaData);
}

bool CMetaCommonFormObject::OnBeforeRunMetaObject(int flags)
{
	if (appData->DesignerMode()) {
		IModuleManager* moduleManager = m_metaData->GetModuleManager();
		wxASSERT(moduleManager);
		if (moduleManager->AddCompileModule(this, GenerateFormAndRun())) {
			return CMetaModuleObject::OnBeforeRunMetaObject(flags);
		}
		return false;
	}
	return CMetaModuleObject::OnBeforeRunMetaObject(flags);
}

bool CMetaCommonFormObject::OnAfterCloseMetaObject()
{
	if (appData->DesignerMode()) {
		IModuleManager* moduleManager = m_metaData->GetModuleManager();
		wxASSERT(moduleManager);
		if (moduleManager->RemoveCompileModule(this)) {
			return CMetaModuleObject::OnAfterCloseMetaObject();
		}
		return false;
	}
	return CMetaModuleObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_REGISTER(CMetaFormObject, "baseForm", g_metaFormCLSID);
METADATA_REGISTER(CMetaCommonFormObject, "commonForm", g_metaCommonFormCLSID);