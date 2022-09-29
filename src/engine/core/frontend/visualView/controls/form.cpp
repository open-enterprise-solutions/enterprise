////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : frame control
////////////////////////////////////////////////////////////////////////////

#include "form.h"
#include "frontend/visualView/special/enums/valueOrient.h"
#include "metadata/metaObjects/objects/object.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueForm, IValueFrame);

//****************************************************************************
//*                              Frame                                       *
//****************************************************************************

CValueForm::CValueForm() : IValueFrame(), IModuleInfo(),
m_formOwner(NULL), m_sourceObject(NULL), m_metaFormObject(NULL), m_valueFormDocument(NULL),
m_defaultFormType(defaultFormType), m_formModified(false)
{
	//set default params
	m_controlClsid = g_controlFormCLSID;
	m_controlId = defaultFormId;

	//init frame controls
	m_formControls = new CValueFormControl(this);
	m_formControls->IncrRef();

	//init attributes controls
	m_formData = new CValueFormData(this);
	m_formData->IncrRef();
}

CValueForm::CValueForm(IValueFrame* ownerControl, IMetaFormObject* metaForm,
	ISourceDataObject* ownerSrc, const CUniqueKey& formGuid, bool readOnly) : IValueFrame(), IModuleInfo(),
	m_formOwner(NULL), m_sourceObject(NULL), m_metaFormObject(NULL), m_valueFormDocument(NULL),
	m_defaultFormType(defaultFormType), m_formModified(false)
{
	//init default params
	CValueForm::InitializeForm(ownerControl, metaForm, ownerSrc, formGuid, readOnly);

	//set default params
	m_controlClsid = g_controlFormCLSID;
	m_controlId = defaultFormId;

	//init frame controls
	m_formControls = new CValueFormControl(this);
	m_formControls->IncrRef();

	//init attributes controls
	m_formData = new CValueFormData(this);
	m_formData->IncrRef();
}

CValueForm::~CValueForm()
{
	for (auto pair : m_aIdleHandlers) {
		wxTimer* timer = pair.second;
		if (timer->IsRunning()) {
			timer->Stop();
		}
		timer->Unbind(wxEVT_TIMER, &CValueForm::OnIdleHandler, this);
		delete timer;
	}

	if (m_formControls) {
		m_formControls->DecrRef();
	}

	if (m_formData) {
		m_formData->DecrRef();
	}

	for (unsigned int idx = GetChildCount(); idx > 0; idx--) {
		IValueFrame* controlChild =
			dynamic_cast<IValueFrame*>(GetChild(idx - 1));
		ClearRecursive(controlChild);
		if (controlChild) {
			controlChild->DecrRef();
		}
	}

	if (m_sourceObject) {
		m_sourceObject->DecrRef();
	}
}

void CValueForm::Update(wxObject* wxobject, IVisualHost* visualHost)
{
	UpdateForm();
}

void CValueForm::OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost)
{
	//lay out parent window 
	wxWindow* wndParent = visualHost->GetParent();
	if (wndParent) {
		wndParent->Layout();
	}
}

//**********************************************************************************
//*                                   Data		                                   *
//**********************************************************************************

#include "utils/typeconv.h"

bool CValueForm::LoadData(CMemoryReader& reader)
{
	wxString propValue = wxEmptyString;
	reader.r_stringZ(propValue);
	m_propertyCaption->SetValue(propValue);
	m_propertyOrient->SetValue(reader.r_s32());
	reader.r_stringZ(propValue);
	m_propertyFG->SetValue(TypeConv::StringToColour(propValue));
	reader.r_stringZ(propValue);
	m_propertyBG->SetValue(TypeConv::StringToColour(propValue));

	return IValueFrame::LoadData(reader);
}

bool CValueForm::SaveData(CMemoryWriter& writer)
{
	writer.w_stringZ(m_propertyCaption->GetValueAsString());
	writer.w_s32(m_propertyOrient->GetValueAsInteger());

	writer.w_stringZ(
		m_propertyFG->GetValueAsString()
	);
	writer.w_stringZ(
		m_propertyBG->GetValueAsString()
	);

	return IValueFrame::SaveData(writer);
}

//**********************************************************************************
//*                                   Other                                        *
//**********************************************************************************

#include "metadata/metadata.h"

IMetadata* CValueForm::GetMetaData() const
{
	return m_metaFormObject ?
		m_metaFormObject->GetMetadata() :
		NULL;
}

form_identifier_t CValueForm::GetTypeForm() const
{
	return m_metaFormObject ?
		m_metaFormObject->GetTypeForm() :
		m_defaultFormType;
}