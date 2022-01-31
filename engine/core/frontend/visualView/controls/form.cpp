////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : frame control
////////////////////////////////////////////////////////////////////////////

#include "form.h"
#include "frontend/visualView/special/enums/valueOrient.h"
#include "metadata/objects/baseObject.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueForm, IValueFrame);

//****************************************************************************
//*                              Frame                                       *
//****************************************************************************

CValueForm::CValueForm() : IValueFrame(), IModuleInfo(),
m_formOwner(NULL), m_sourceObject(NULL), m_metaFormObject(NULL), m_valueFormDocument(NULL),
m_orient(wxVERTICAL), m_defaultFormType(defaultFormType),
m_formModified(false)
{
	PropertyContainer *categoryFrame = IObjectBase::CreatePropertyContainer("Frame");
	categoryFrame->AddProperty("name", PropertyType::PT_WXNAME, false);
	categoryFrame->AddProperty("caption", PropertyType::PT_WXSTRING);
	categoryFrame->AddProperty("fg", PropertyType::PT_WXCOLOUR);
	categoryFrame->AddProperty("bg", PropertyType::PT_WXCOLOUR);

	m_category->AddCategory(categoryFrame);

	PropertyContainer *categorySizer = IObjectBase::CreatePropertyContainer("Sizer");
	categorySizer->AddProperty("orient", PropertyType::PT_OPTION, &CValueForm::GetOrient);
	m_category->AddCategory(categorySizer);

	//set default params
	m_controlClsid = g_controlFormCLSID;
	m_controlId = defaultFormId;
	m_controlName = GetObjectTypeName();

	//init frame controls
	m_formControls = new CValueFormControl(this);
	m_formControls->IncrRef();

	//init attributes controls
	m_formData = new CValueFormData(this);
	m_formData->IncrRef();
}

CValueForm::CValueForm(IValueFrame *ownerControl, IMetaFormObject *metaForm,
	IDataObjectSource *ownerSrc, const Guid &formGuid, bool readOnly) : IValueFrame(), IModuleInfo(),
	m_valueFormDocument(NULL),
	m_orient(wxVERTICAL), m_defaultFormType(defaultFormType),
	m_formModified(false)
{
	PropertyContainer *categoryFrame = IObjectBase::CreatePropertyContainer("Frame");
	categoryFrame->AddProperty("name", PropertyType::PT_WXNAME, false);
	categoryFrame->AddProperty("caption", PropertyType::PT_WXSTRING);
	categoryFrame->AddProperty("fg", PropertyType::PT_WXCOLOUR);
	categoryFrame->AddProperty("bg", PropertyType::PT_WXCOLOUR);

	m_category->AddCategory(categoryFrame);

	PropertyContainer *categorySizer = IObjectBase::CreatePropertyContainer("Sizer");
	categorySizer->AddProperty("orient", PropertyType::PT_OPTION, &CValueForm::GetOrient);
	m_category->AddCategory(categorySizer);

	//set default params
	m_controlClsid = g_controlFormCLSID;
	m_controlId = defaultFormId;
	m_controlName = GetObjectTypeName();

	//init frame controls
	m_formControls = new CValueFormControl(this);
	m_formControls->IncrRef();

	//init attributes controls
	m_formData = new CValueFormData(this);
	m_formData->IncrRef();

	//init default params
	InitializeForm(ownerControl, metaForm, ownerSrc, formGuid, readOnly);
}

CValueForm::~CValueForm()
{
	for (auto pair : m_aIdleHandlers) {
		wxTimer *timer = pair.second;
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
		IValueFrame *controlChild =
			dynamic_cast<IValueFrame *>(GetChild(idx - 1));
		ClearRecursive(controlChild);
		if (controlChild) {
			controlChild->DecrRef();
		}
	}

	if (m_sourceObject) {
		m_sourceObject->DecrRef();
	}
}

void CValueForm::Update(wxObject* wxobject, IVisualHost *visualHost)
{
	UpdateForm();
}

void CValueForm::OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost)
{
	//lay out parent window 
	wxWindow *wndParent = visualHost->GetParent();
	if (wndParent) {
		wndParent->Layout();
	}
}

void CValueForm::SetPropertyData(Property *property, const CValue &srcValue)
{
	IValueFrame::SetPropertyData(property, srcValue);
}

CValue CValueForm::GetPropertyData(Property *property)
{
	if (property->GetName() == wxT("orient")) {
		return new CValueEnumOrient(m_orient);
	}

	return IValueFrame::GetPropertyData(property);
}

//**********************************************************************************
//*                                   Data		                                   *
//**********************************************************************************

#include "utils/typeconv.h"

bool CValueForm::LoadData(CMemoryReader &reader)
{
	reader.r_stringZ(m_caption);
	m_orient = (wxOrientation)reader.r_s32();

	wxString propValue = wxEmptyString;
	reader.r_stringZ(propValue);
	m_fg = TypeConv::StringToColour(propValue);
	reader.r_stringZ(propValue);
	m_bg = TypeConv::StringToColour(propValue);

	return IValueFrame::LoadData(reader);
}

bool CValueForm::SaveData(CMemoryWriter &writer)
{
	writer.w_stringZ(m_caption);
	writer.w_s32(m_orient);

	writer.w_stringZ(
		TypeConv::ColourToString(m_fg)
	);
	writer.w_stringZ(
		TypeConv::ColourToString(m_bg)
	);

	return IValueFrame::SaveData(writer);
}

//**********************************************************************************
//*                                   Property                                     *
//**********************************************************************************

void CValueForm::ReadProperty()
{
	IObjectBase::SetPropertyValue("name", m_controlName);
	IObjectBase::SetPropertyValue("caption", m_caption);
	IObjectBase::SetPropertyValue("orient", m_orient, true);
	IObjectBase::SetPropertyValue("fg", m_fg);
	IObjectBase::SetPropertyValue("bg", m_bg);
}

void CValueForm::SaveProperty()
{
	IObjectBase::GetPropertyValue("name", m_controlName);
	IObjectBase::GetPropertyValue("caption", m_caption);
	IObjectBase::GetPropertyValue("orient", m_orient, true);
	IObjectBase::GetPropertyValue("fg", m_fg);
	IObjectBase::GetPropertyValue("bg", m_bg);
}

//**********************************************************************************
//*                                   Other                                        *
//**********************************************************************************

#include "metadata/metadata.h"

IMetadata *CValueForm::GetMetaData() const
{
	return m_metaFormObject ?
		m_metaFormObject->GetMetadata() :
		NULL;
}

OptionList *CValueForm::GetTypelist() const
{
	IMetadata *metaData = GetMetaData();

	return metaData ?
		metaData->GetTypelist() :
		NULL;
}

form_identifier_t CValueForm::GetTypeForm() const
{
	return m_metaFormObject ?
		m_metaFormObject->GetTypeForm() :
		m_defaultFormType;
}