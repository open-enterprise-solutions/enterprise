#include "sizers.h"
#include "frontend/visualView/pageWindow.h"
#include "form.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueSizerItem, IValueSizer)

//************************************************************************************
//*                            Support item                                          *
//************************************************************************************

inline wxObject* GetParentFormVisualEditor(IVisualHost* visualEdit, wxObject* wxobject)
{
	IValueFrame* obj = visualEdit->GetObjectBase(wxobject);
	IValueFrame* objParent = obj->GetParent();

	wxASSERT(objParent);

	wxObject* objItem = visualEdit->GetWxObject(objParent);

	if (objParent->GetClassName() == wxT("page")) {
		CPageWindow* objPage = dynamic_cast<CPageWindow*>(objItem);
		wxASSERT(objPage);
		return objPage->GetSizer();
	}
	return objItem;
}

inline wxObject* GetChildFormVisualEditor(IVisualHost* m_visualEdit, wxObject* wxobject, unsigned int childIndex)
{
	IValueFrame* obj = m_visualEdit->GetObjectBase(wxobject);
	if (childIndex >= obj->GetChildCount())
		return NULL;
	return m_visualEdit->GetWxObject(obj->GetChild(childIndex));
}

OptionList* CValueSizerItem::GetDefaultOptionBorder(PropertyBitlist* property)
{
	OptionList* opt_list = new OptionList();

	opt_list->AddOption(_("left"), wxLEFT);
	opt_list->AddOption(_("right"), wxRIGHT);
	opt_list->AddOption(_("bottom"), wxBOTTOM);
	opt_list->AddOption(_("top"), wxTOP);

	return opt_list;
}

OptionList* CValueSizerItem::GetDefaultOptionState(PropertyOption* property)
{
	OptionList* opt_list = new OptionList();

	opt_list->AddOption(_("shrink"), wxSHRINK);
	opt_list->AddOption(_("expand"), wxEXPAND);
	//m_opt_list->AddOption(_("shaped", wxSHAPED);

	return opt_list;
}

//************************************************************************************
//*                            ValueSizerItem                                        *
//************************************************************************************

CValueSizerItem::CValueSizerItem() : IValueFrame()
{
}

void CValueSizerItem::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool firstÑreated)
{
	// Get parent sizer
	wxObject* parent = GetParentFormVisualEditor(visualHost, wxobject);
	wxSizer* sizer = wxDynamicCast(parent, wxSizer);

	// Get child window
	wxObject* child = GetChildFormVisualEditor(visualHost, wxobject, 0);
	if (NULL == child) {
		wxLogError(wxT("The SizerItem component has no child - this should not be possible!"));
		return;
	}

	// Get IObject for property access
	CValueSizerItem* obj = wxDynamicCast(visualHost->GetObjectBase(wxobject), CValueSizerItem);

	// Add the child ( window or sizer ) to the sizer
	wxWindow* windowChild = wxDynamicCast(child, wxWindow);
	wxSizer* sizerChild = wxDynamicCast(child, wxSizer);

	if (windowChild != NULL) {
		sizer->Detach(windowChild);
		sizer->Add(windowChild,
			obj->GetProportion(),
			obj->GetFlagBorder() | obj->GetFlagState(),
			obj->GetBorder());
	}
	else if (sizerChild != NULL) {
		sizer->Detach(sizerChild);
		sizer->Add(sizerChild,
			obj->GetProportion(),
			obj->GetFlagBorder() | obj->GetFlagState(),
			obj->GetBorder());
	}
}

void CValueSizerItem::OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost)
{
	// Get parent sizer
	wxObject* parent = GetParentFormVisualEditor(visualHost, wxobject);
	wxSizer* sizer = wxDynamicCast(parent, wxSizer);

	// Get child window
	wxObject* child = GetChildFormVisualEditor(visualHost, wxobject, 0);
	if (NULL == child) {
		wxLogError(wxT("The SizerItem component has no child - this should not be possible!"));
		return;
	}

	// Get IObject for property access
	CValueSizerItem* obj = wxDynamicCast(visualHost->GetObjectBase(wxobject), CValueSizerItem);

	// Add the child ( window or sizer ) to the sizer
	wxWindow* windowChild = wxDynamicCast(child, wxWindow);
	wxSizer* sizerChild = wxDynamicCast(child, wxSizer);

	IValueFrame* parentControl = GetParent(); int idx = wxNOT_FOUND;

	for (unsigned int i = 0; i < parentControl->GetChildCount(); i++) {
		IValueFrame* child = parentControl->GetChild(i);
		if (m_controlId == child->GetControlID()) {
			idx = i; 
			break;
		}
	}

	if (windowChild != NULL) {
		if (idx == wxNOT_FOUND) {
			sizer->Detach(windowChild);
			sizer->Add(windowChild,
				obj->GetProportion(),
				obj->GetFlagBorder() | obj->GetFlagState(),
				obj->GetBorder());
		}
		else {
			sizer->Detach(windowChild);
			sizer->Insert(idx, windowChild,
				obj->GetProportion(),
				obj->GetFlagBorder() | obj->GetFlagState(),
				obj->GetBorder());
		}
	}
	else if (sizerChild != NULL) {
		if (idx == wxNOT_FOUND) {
			sizer->Detach(sizerChild);
			sizer->Add(sizerChild,
				obj->GetProportion(),
				obj->GetFlagBorder() | obj->GetFlagState(),
				obj->GetBorder());
		}
		else {
			sizer->Detach(sizerChild);
			sizer->Insert(idx, sizerChild,
				obj->GetProportion(),
				obj->GetFlagBorder() | obj->GetFlagState(),
				obj->GetBorder());
		}
	}
}

#include "metadata/metadata.h"

IMetadata* CValueSizerItem::GetMetaData() const
{
	IMetaFormObject* metaFormObject = m_formOwner ?
		m_formOwner->GetFormMetaObject() :
		NULL;

	//for form buider
	if (metaFormObject == NULL) {
		ISourceDataObject* srcValue = m_formOwner ?
			m_formOwner->GetSourceObject() :
			NULL;
		if (srcValue != NULL) {
			IMetaObjectWrapperData* metaValue = srcValue->GetMetaObject();
			wxASSERT(metaValue);
			return metaValue->GetMetadata();
		}
	}

	return metaFormObject ?
		metaFormObject->GetMetadata() :
		NULL;
}

#include "metadata/metaObjects/metaFormObject.h"

form_identifier_t CValueSizerItem::GetTypeForm() const
{
	if (!m_formOwner) {
		wxASSERT(m_formOwner);
		return 0;
	}

	IMetaFormObject* metaFormObj =
		m_formOwner->GetFormMetaObject();
	wxASSERT(metaFormObj);
	return metaFormObj->GetTypeForm();
}

CProcUnit* CValueSizerItem::GetFormProcUnit() const
{
	if (!m_formOwner) {
		wxASSERT(m_formOwner);
		return NULL;
	}

	return m_formOwner->GetProcUnit();
}

//**********************************************************************************
//*                                    Data										   *
//**********************************************************************************

bool CValueSizerItem::LoadData(CMemoryReader& reader)
{
	m_propertyProportion->SetValue(reader.r_s32());
	m_propertyFlagBorder->SetValue(reader.r_s64());
	m_propertyFlagState->SetValue(reader.r_s64());
	m_propertyBorder->SetValue(reader.r_s32());

	return IValueFrame::LoadData(reader);
}

bool CValueSizerItem::SaveData(CMemoryWriter& writer)
{
	writer.w_s32(m_propertyProportion->GetValueAsInteger());
	writer.w_s64(m_propertyFlagBorder->GetValueAsInteger());
	writer.w_s64(m_propertyFlagState->GetValueAsInteger());
	writer.w_s32(m_propertyBorder->GetValueAsInteger());

	return IValueFrame::SaveData(writer);
}
