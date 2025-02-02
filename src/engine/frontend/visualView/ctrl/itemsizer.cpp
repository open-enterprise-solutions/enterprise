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

	if (objParent->GetClassName() == wxT("notebookPage")) {
		CPanelPage* objPage = dynamic_cast<CPanelPage*>(objItem);
		wxASSERT(objPage);
		return objPage->GetSizer();
	}
	return objItem;
}

inline wxObject* GetChildFormVisualEditor(IVisualHost* visualEdit, wxObject* wxobject, unsigned int childIndex)
{
	IValueFrame* obj = visualEdit->GetObjectBase(wxobject);
	if (childIndex >= obj->GetChildCount())
		return nullptr;
	return visualEdit->GetWxObject(obj->GetChild(childIndex));
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
	if (nullptr == child) {
		wxLogError(wxT("The SizerItem component has no child - this should not be possible!"));
		return;
	}

	// Get IObject for property access
	CValueSizerItem* obj = wxDynamicCast(visualHost->GetObjectBase(wxobject), CValueSizerItem);

	// Add the child ( window or sizer ) to the sizer
	wxWindow* windowChild = wxDynamicCast(child, wxWindow);
	wxSizer* sizerChild = wxDynamicCast(child, wxSizer);

	if (windowChild != nullptr) {
		sizer->Detach(windowChild);
		sizer->Add(windowChild,
			obj->GetProportion(),
			obj->GetFlagBorder() | obj->GetFlagState(),
			obj->GetBorder());
	}
	else if (sizerChild != nullptr) {
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
	if (nullptr == child) {
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

	if (windowChild != nullptr) {
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
	else if (sizerChild != nullptr) {
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

#include "backend/metaData.h"

IMetaData* CValueSizerItem::GetMetaData() const
{
	IMetaObjectForm* metaFormObject = m_formOwner ?
		m_formOwner->GetFormMetaObject() :
		nullptr;

	//for form buider
	if (metaFormObject == nullptr) {
		ISourceDataObject* srcValue = m_formOwner ?
			m_formOwner->GetSourceObject() :
			nullptr;
		if (srcValue != nullptr) {
			IMetaObjectGenericData* metaValue = srcValue->GetSourceMetaObject();
			wxASSERT(metaValue);
			return metaValue->GetMetaData();
		}
	}

	return metaFormObject ?
		metaFormObject->GetMetaData() :
		nullptr;
}

#include "backend/metaCollection/metaFormObject.h"

form_identifier_t CValueSizerItem::GetTypeForm() const
{
	if (!m_formOwner) {
		wxASSERT(m_formOwner);
		return 0;
	}

	IMetaObjectForm* metaFormObj =
		m_formOwner->GetFormMetaObject();
	wxASSERT(metaFormObj);
	return metaFormObj->GetTypeForm();
}

CProcUnit* CValueSizerItem::GetFormProcUnit() const
{
	if (!m_formOwner) {
		wxASSERT(m_formOwner);
		return nullptr;
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

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

S_CONTROL_TYPE_REGISTER(CValueSizerItem, "sizerItem", "sizer", string_to_clsid("CT_SIZR"));