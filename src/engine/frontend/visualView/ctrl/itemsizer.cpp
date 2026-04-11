#include "sizer.h"
#include "frontend/visualView/pageWindow.h"
#include "form.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibValueSizerItem, ibValueSizer)

//************************************************************************************
//*                            Support item                                          *
//************************************************************************************

inline wxObject* GetParentFormVisualEditor(ibVisualHost* visualEdit, ibValueFrame* object)
{
	ibValueFrame* parent = object->GetParent();
	wxASSERT(parent);

	wxObject* wxparent_object = visualEdit->GetWxObject(parent);
	if (parent->GetClassName() == wxT("NotebookPage")) {
		ibPanelPage* objPage =
			dynamic_cast<ibPanelPage*>(wxparent_object);
		return objPage != nullptr ? objPage->GetSizer() : nullptr;
	}

	return wxparent_object;
}

inline wxObject* GetChildFormVisualEditor(ibVisualHost* visualEdit, wxObject* wxobject, unsigned int childIndex)
{
	ibValueFrame* obj = visualEdit->GetObjectBase(wxobject);
	if (childIndex >= obj->GetChildCount())
		return nullptr;
	return visualEdit->GetWxObject(obj->GetChild(childIndex));
}

//************************************************************************************
//*                            ValueSizerItem                                        *
//************************************************************************************

ibValueSizerItem::ibValueSizerItem() : ibValueFrame()
{
}

void ibValueSizerItem::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated)
{
	ibValueFrame* object = visualHost->GetObjectBase(wxobject);

	// Get parent sizer
	wxSizer* sizer = wxDynamicCast(GetParentFormVisualEditor(visualHost, object), wxSizer);

	// Get child window
	wxObject* child = GetChildFormVisualEditor(visualHost, wxobject, 0);
	if (nullptr == child) {
		wxLogError(wxT("The SizerItem component has no child - this should not be possible!"));
		return;
	}

	// Get IObject for property access
	ibValueSizerItem* obj = wxDynamicCast(object, ibValueSizerItem);

	// Add the child ( window or sizer ) to the sizer
	wxWindow* windowChild = wxDynamicCast(child, wxWindow);
	wxSizer* sizerChild = wxDynamicCast(child, wxSizer);

	if (windowChild != nullptr) {
		sizer->Detach(windowChild);
		sizer->Add(windowChild,
			obj->GetProportion(),
			obj->GetFlagBorder() | obj->GetFlagState(),
			obj->GetBorder());

		windowChild->Layout();
	}
	else if (sizerChild != nullptr) {
		sizer->Detach(sizerChild);
		sizer->Add(sizerChild,
			obj->GetProportion(),
			obj->GetFlagBorder() | obj->GetFlagState(),
			obj->GetBorder());

		sizerChild->Layout();
	}
}

void ibValueSizerItem::OnUpdated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost)
{
	ibValueFrame* object = visualHost->GetObjectBase(wxobject);

	// Get parent sizer
	wxSizer* sizer = wxDynamicCast(GetParentFormVisualEditor(visualHost, object), wxSizer);

	// Get child window
	wxObject* child = GetChildFormVisualEditor(visualHost, wxobject, 0);
	if (nullptr == child) {
		wxLogError(wxT("The SizerItem component has no child - this should not be possible!"));
		return;
	}

	// Get IObject for property access
	ibValueSizerItem* obj = wxDynamicCast(object, ibValueSizerItem);

	// Add the child ( window or sizer ) to the sizer
	wxWindow* windowChild = wxDynamicCast(child, wxWindow);
	wxSizer* sizerChild = wxDynamicCast(child, wxSizer);

	ibValueFrame* parentControl = GetParent(); int idx = wxNOT_FOUND;

	for (unsigned int i = 0; i < parentControl->GetChildCount(); i++) {
		ibValueFrame* child = parentControl->GetChild(i);
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

	const ibValueFrame* parent = object->GetParent();
	if (parent->GetClassName() == wxT("NotebookPage")) {
		wxObject* wxparent_object = visualHost->GetWxObject(parent);
		ibPanelPage* objPage =
			dynamic_cast<ibPanelPage*>(wxparent_object);
		objPage->Layout();
	}
}

#include "backend/metaData.h"

ibMetaData* ibValueSizerItem::GetMetaData() const
{
	const ibValueMetaObjectFormBase* metaFormObject = m_formOwner ?
		m_formOwner->GetFormMetaObject() :
		nullptr;

	//for form buider
	if (metaFormObject == nullptr) {
		ibSourceDataObject* srcValue = m_formOwner ?
			m_formOwner->GetSourceObject() :
			nullptr;
		if (srcValue != nullptr) {
			ibValueMetaObjectGenericData* metaValue = srcValue->GetSourceMetaObject();
			wxASSERT(metaValue);
			return metaValue->GetMetaData();
		}
	}

	return metaFormObject ?
		metaFormObject->GetMetaData() :
		nullptr;
}

#include "backend/metaCollection/metaFormObject.h"

ibFormID ibValueSizerItem::GetTypeForm() const
{
	if (!m_formOwner) {
		wxASSERT(m_formOwner);
		return 0;
	}

	const ibValueMetaObjectFormBase* metaFormObj =
		m_formOwner->GetFormMetaObject();
	wxASSERT(metaFormObj);

	return metaFormObj->GetTypeForm();
}

ibProcUnit* ibValueSizerItem::GetFormProcUnit() const
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

bool ibValueSizerItem::LoadData(ibReaderMemory& reader)
{
	//m_propertyProportion->SetValue(reader.r_s32());
	//m_propertyFlagBorder->SetValue(reader.r_s64());
	//m_propertyFlagState->SetValue(reader.r_s64());
	//m_propertyBorder->SetValue(reader.r_s32());

	m_propertyProportion->LoadData(reader);
	//m_propertyFlagBorder->LoadData(reader);

	m_propertyFlagBorderLeft->LoadData(reader);
	m_propertyFlagBorderRight->LoadData(reader);
	m_propertyFlagBorderTop->LoadData(reader);
	m_propertyFlagBorderBottom->LoadData(reader);

	m_propertyFlagState->LoadData(reader);
	m_propertyBorder->LoadData(reader);

	return ibValueFrame::LoadData(reader);
}

bool ibValueSizerItem::SaveData(ibWriterMemory writer)
{
	//writer.w_s32(m_propertyProportion->GetValueAsInteger());
	//writer.w_s64(m_propertyFlagBorder->GetValueAsInteger());
	//writer.w_s64(m_propertyFlagState->GetValueAsInteger());
	//writer.w_s32(m_propertyBorder->GetValueAsInteger());

	m_propertyProportion->SaveData(writer);
	//m_propertyFlagBorder->SaveData(writer);

	m_propertyFlagBorderLeft->SaveData(writer);
	m_propertyFlagBorderRight->SaveData(writer);
	m_propertyFlagBorderTop->SaveData(writer);
	m_propertyFlagBorderBottom->SaveData(writer);

	m_propertyFlagState->SaveData(writer);
	m_propertyBorder->SaveData(writer);

	return ibValueFrame::SaveData(writer);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

S_CONTROL_TYPE_REGISTER(ibValueSizerItem, "SizerItem", "Sizer", string_to_clsid("CT_SIZR"));