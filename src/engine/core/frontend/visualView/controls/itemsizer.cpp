#include "sizers.h"
#include "frontend/visualView/pageWindow.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueSizerItem, IValueSizer)

//************************************************************************************
//*                            Support item                                          *
//************************************************************************************

wxObject* GetParentFormVisualEditor(IVisualHost* visualEdit, wxObject* wxobject)
{
	IValueFrame *obj = visualEdit->GetObjectBase(wxobject);
	IValueFrame *objParent = obj->GetParent();

	wxASSERT(objParent);

	wxObject *objItem = visualEdit->GetWxObject(objParent);

	if (objParent->GetClassName() == wxT("page")) {
		CPageWindow *objPage = dynamic_cast<CPageWindow *>(objItem);
		wxASSERT(objPage);
		return objPage->GetSizer();
	}
	else {
		return objItem;
	}
}

wxObject* GetChildFormVisualEditor(IVisualHost* m_visualEdit, wxObject* wxobject, unsigned int childIndex)
{
	IValueFrame * obj = m_visualEdit->GetObjectBase(wxobject);
	if (childIndex >= obj->GetChildCount()) return NULL;
	return m_visualEdit->GetWxObject(obj->GetChild(childIndex));
}

OptionList *CValueSizerItem::GetDefaultOptionBorder(Property *property)
{
	OptionList *m_opt_list = new OptionList();

	m_opt_list->AddOption("left", wxLEFT);
	m_opt_list->AddOption("right", wxRIGHT);
	m_opt_list->AddOption("bottom", wxBOTTOM);
	m_opt_list->AddOption("top", wxTOP);

	return m_opt_list;
}

OptionList *CValueSizerItem::GetDefaultOptionState(Property *property)
{
	OptionList *m_opt_list = new OptionList();

	m_opt_list->AddOption("shrink", wxSHRINK);
	m_opt_list->AddOption("expand", wxEXPAND);
	//m_opt_list->AddOption("shaped", wxSHAPED);

	return m_opt_list;
}

//************************************************************************************
//*                            ValueSizerItem                                        *
//************************************************************************************

CValueSizerItem::CValueSizerItem() : IValueSizer(true),
m_proportion(0), m_flag_border(wxALL), m_border(5), m_flag_state(wxSHRINK)
{
	PropertyContainer *categorySizer = IObjectBase::CreatePropertyContainer("SizerItem");
	categorySizer->AddProperty("border", PropertyType::PT_INT);
	categorySizer->AddProperty("proportion", PropertyType::PT_INT);
	categorySizer->AddProperty("flag_border", PropertyType::PT_BITLIST, &CValueSizerItem::GetDefaultOptionBorder);
	categorySizer->AddProperty("flag_state", PropertyType::PT_OPTION, &CValueSizerItem::GetDefaultOptionState);

	m_category->AddCategory(categorySizer);
}

void CValueSizerItem::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool firstÑreated)
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
	IValueFrame* childObj = visualHost->GetObjectBase(child);

	// Add the spacer
	if (childObj->GetClassName() == _("spacer")) {
		sizer->Add(childObj->GetPropertyAsInteger(_("width")),
			childObj->GetPropertyAsInteger(_("height")),
			obj->m_proportion,
			obj->m_flag_border | obj->m_flag_state,
			obj->m_border
		);
		return;
	}

	// Add the child ( window or sizer ) to the sizer
	wxWindow* windowChild = wxDynamicCast(child, wxWindow);
	wxSizer* sizerChild = wxDynamicCast(child, wxSizer);

	if (windowChild != NULL) {
		sizer->Add(windowChild,
			obj->m_proportion,
			obj->m_flag_border | obj->m_flag_state,
			obj->m_border);
	}
	else if (sizerChild != NULL) {
		sizer->Add(sizerChild,
			obj->m_proportion,
			obj->m_flag_border | obj->m_flag_state,
			obj->m_border);
	}
}

void CValueSizerItem::OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost)
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
	IValueFrame* childObj = visualHost->GetObjectBase(child);

	// Add the spacer
	if (childObj->GetClassName() == _("spacer")) {
		sizer->Detach(sizer->GetItemCount());
	}

	if (childObj->GetClassName() == _("spacer")) {
		sizer->Add(childObj->GetPropertyAsInteger(_("width")),
			childObj->GetPropertyAsInteger(_("height")),
			obj->m_proportion,
			obj->m_flag_border | obj->m_flag_state,
			obj->m_border
		);
		return;
	}

	// Add the child ( window or sizer ) to the sizer
	wxWindow* windowChild = wxDynamicCast(child, wxWindow);
	wxSizer* sizerChild = wxDynamicCast(child, wxSizer);

	if (windowChild != NULL) {
		sizer->Detach(windowChild);
	}
	else if (sizerChild != NULL) {
		sizer->Detach(sizerChild);
	}

	IValueFrame *parentControl = GetParent(); int idx = wxNOT_FOUND;

	for (unsigned int i = 0; i < parentControl->GetChildCount(); i++) {
		IValueFrame *child = parentControl->GetChild(i);
		if (m_controlId == child->GetControlID()) {
			idx = i; break;
		}
	}

	if (windowChild != NULL) {
		if (idx == wxNOT_FOUND) {
			sizer->Add(windowChild,
				obj->m_proportion,
				obj->m_flag_border | obj->m_flag_state,
				obj->m_border);
		}
		else {
			sizer->Insert(idx, windowChild,
				obj->m_proportion,
				obj->m_flag_border | obj->m_flag_state,
				obj->m_border);
		}
	}
	else if (sizerChild != NULL) {
		if (idx == wxNOT_FOUND) {
			sizer->Add(sizerChild,
				obj->m_proportion,
				obj->m_flag_border | obj->m_flag_state,
				obj->m_border);
		}
		else {
			sizer->Insert(idx, sizerChild,
				obj->m_proportion,
				obj->m_flag_border | obj->m_flag_state,
				obj->m_border);
		}
	}
}

//**********************************************************************************
//*                                    Data										   *
//**********************************************************************************

bool CValueSizerItem::LoadData(CMemoryReader &reader)
{
	m_proportion = reader.r_s32();
	m_flag_border = reader.r_s64();
	m_flag_state = reader.r_s64();
	m_border = reader.r_s32();

	return IValueSizer::LoadData(reader);
}

bool CValueSizerItem::SaveData(CMemoryWriter &writer)
{
	writer.w_s32(m_proportion);
	writer.w_s64(m_flag_border);
	writer.w_s64(m_flag_state);
	writer.w_s32(m_border);

	return IValueSizer::SaveData(writer);
}


//**********************************************************************************
//*                                    Property                                    *
//**********************************************************************************

void CValueSizerItem::ReadProperty()
{
	IObjectBase::SetPropertyValue("border", m_border);
	IObjectBase::SetPropertyValue("proportion", m_proportion);
	IObjectBase::SetPropertyValue("flag_border", m_flag_border);
	IObjectBase::SetPropertyValue("flag_state", m_flag_state);
}

void CValueSizerItem::SaveProperty()
{
	IObjectBase::GetPropertyValue("border", m_border);
	IObjectBase::GetPropertyValue("proportion", m_proportion);
	IObjectBase::GetPropertyValue("flag_border", m_flag_border);
	IObjectBase::GetPropertyValue("flag_state", m_flag_state);
}
