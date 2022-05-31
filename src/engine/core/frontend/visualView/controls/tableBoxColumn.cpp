#include "tableBox.h"

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CValueTableBoxColumn, IValueControl);

//****************************************************************************

#include "form.h"
#include "metadata/metadata.h"
#include "metadata/singleMetaTypes.h"

OptionList* CValueTableBoxColumn::GetChoiceForm(Property* property)
{
	OptionList* optList = new OptionList;
	optList->AddOption(_("default"), wxNOT_FOUND);

	IMetadata* metaData = GetMetaData();
	if (metaData != NULL) {
		IMetaObjectRecordDataRef* metaObjectRefValue = NULL;
		if (m_dataSource != wxNOT_FOUND) {

			IMetaObjectWrapperData* metaObjectValue =
				m_formOwner->GetMetaObject();

			if (metaObjectValue != NULL) {
				IMetaObject* metaobject =
					metaObjectValue->FindMetaObjectByID(m_dataSource);

				IMetaAttributeObject* metaAttribute = wxDynamicCast(
					metaobject, IMetaAttributeObject
				);
				if (metaAttribute != NULL) {

					IMetaTypeObjectValueSingle* so = metaData->GetTypeObject(metaAttribute->GetFirstClsid());
					if (so != NULL) {
						metaObjectRefValue = wxDynamicCast(so->GetMetaObject(), IMetaObjectRecordDataRef);
					}
				}
				else
				{
					metaObjectRefValue = wxDynamicCast(
						metaobject, IMetaObjectRecordDataRef);
				}
			}
		}
		else {
			IMetaTypeObjectValueSingle* so = metaData->GetTypeObject(IAttributeControl::GetFirstClsid());
			if (so != NULL) {
				metaObjectRefValue = wxDynamicCast(so->GetMetaObject(), IMetaObjectRecordDataRef);
			}
		}

		if (metaObjectRefValue != NULL) {
			for (auto form : metaObjectRefValue->GetObjectForms()) {
				optList->AddOption(
					form->GetSynonym(),
					form->GetMetaID()
				);
			}
		}
	}

	return optList;
}

ISourceDataObject* CValueTableBoxColumn::GetSourceObject() const
{
	return m_formOwner ? m_formOwner->GetSourceObject()
		: NULL;
}

//***********************************************************************************
//*                            CValueTableBoxColumn                                 *
//***********************************************************************************

CValueTableBoxColumn::CValueTableBoxColumn() : IValueControl(), IAttributeControl(),
m_markup(true),
m_caption("newColumn"),
m_width(wxDVC_DEFAULT_WIDTH),
m_align(wxALIGN_LEFT),
m_selbutton(true), m_listbutton(false), m_clearbutton(true),
m_passwordMode(false), m_multilineMode(false), m_textEditMode(true),
m_choiceForm(wxNOT_FOUND),
m_visible(true), m_resizable(true), m_sortable(false), m_reorderable(true)
{
	PropertyContainer* categoryInfo = IObjectBase::CreatePropertyContainer("Info");
	categoryInfo->AddProperty("name", PropertyType::PT_WXNAME);
	categoryInfo->AddProperty("caption", PropertyType::PT_WXSTRING);

	categoryInfo->AddProperty("password_mode", PropertyType::PT_BOOL);
	categoryInfo->AddProperty("multiline_mode", PropertyType::PT_BOOL);
	categoryInfo->AddProperty("textedit_mode", PropertyType::PT_BOOL);

	m_category->AddCategory(categoryInfo);

	PropertyContainer* categoryData = IObjectBase::CreatePropertyContainer("Data");
	categoryData->AddProperty("source", PropertyType::PT_SOURCE_DATA);
	categoryData->AddProperty("choice_form", PropertyType::PT_OPTION, &CValueTableBoxColumn::GetChoiceForm);
	m_category->AddCategory(categoryData);

	PropertyContainer* categoryButton = IObjectBase::CreatePropertyContainer("Button");

	categoryButton->AddProperty("button_select", PropertyType::PT_BOOL);
	categoryButton->AddProperty("button_list", PropertyType::PT_BOOL);
	categoryButton->AddProperty("button_clear", PropertyType::PT_BOOL);

	//category 
	m_category->AddCategory(categoryButton);

	PropertyContainer* categoryStyle = IObjectBase::CreatePropertyContainer("Style");
	categoryStyle->AddProperty("width", PropertyType::PT_UINT);
	categoryStyle->AddProperty("align", PropertyType::PT_OPTION, &CValueTableBoxColumn::GetAlign);
	categoryStyle->AddProperty("icon", PropertyType::PT_BITMAP);
	categoryStyle->AddProperty("visible", PropertyType::PT_BOOL);
	categoryStyle->AddProperty("resizable", PropertyType::PT_BOOL);
	categoryStyle->AddProperty("sortable", PropertyType::PT_BOOL);
	categoryStyle->AddProperty("reorderable", PropertyType::PT_BOOL);

	m_category->AddCategory(categoryStyle);
}

wxObject* CValueTableBoxColumn::Create(wxObject* parent, IVisualHost* visualHost)
{
	CDataViewColumnObject* columnObject = new CDataViewColumnObject(this, m_caption,
		m_dataSource == wxNOT_FOUND ? m_controlId : m_dataSource, m_width,
		(wxAlignment)m_align,
		wxDATAVIEW_COL_REORDERABLE
	);

	columnObject->SetControl(this);

	columnObject->SetTitle(m_caption);
	columnObject->SetWidth(m_width);
	columnObject->SetAlignment((wxAlignment)m_align);

	columnObject->SetBitmap(m_icon);
	columnObject->SetHidden(!m_visible);
	columnObject->SetSortable(m_sortable);
	columnObject->SetResizeable(m_resizable);

	columnObject->SetControlID(m_controlId);

	CValueViewRenderer* colRenderer = columnObject->GetRenderer();
	wxASSERT(colRenderer);

	return columnObject;
}

void CValueTableBoxColumn::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated)
{
	wxDataViewCtrl* tableCtrl = dynamic_cast<wxDataViewCtrl*>(wxparent);
	wxASSERT(tableCtrl);
	CDataViewColumnObject* columnObject = dynamic_cast<CDataViewColumnObject*>(wxobject);
	wxASSERT(columnObject);
	tableCtrl->AppendColumn(columnObject);
}

void CValueTableBoxColumn::OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost)
{
	IValueFrame* parentControl = GetParent(); int idx = wxNOT_FOUND;

	for (unsigned int i = 0; i < parentControl->GetChildCount(); i++)
	{
		CValueTableBoxColumn* child = dynamic_cast<CValueTableBoxColumn*>(parentControl->GetChild(i));
		wxASSERT(child);
		if (m_dataSource != wxNOT_FOUND && m_dataSource == child->m_dataSource) { idx = i; break; }
		else if (m_dataSource == wxNOT_FOUND && m_controlId == child->m_controlId) { idx = i; break; }
	}

	wxDataViewCtrl* tableCtrl = dynamic_cast<wxDataViewCtrl*>(wxparent);
	wxASSERT(tableCtrl);
	CDataViewColumnObject* columnObject = dynamic_cast<CDataViewColumnObject*>(wxobject);
	wxASSERT(columnObject);

	columnObject->SetControl(this);

	columnObject->SetTitle(m_caption);
	columnObject->SetWidth(m_width);
	columnObject->SetAlignment(m_align);

	columnObject->SetBitmap(m_icon);
	columnObject->SetHidden(!m_visible);
	columnObject->SetSortable(m_sortable);
	columnObject->SetResizeable(m_resizable);

	columnObject->SetColModel(m_dataSource == wxNOT_FOUND ? m_controlId : m_dataSource);

	CValueViewRenderer* colRenderer = columnObject->GetRenderer();
	wxASSERT(colRenderer);

	tableCtrl->DeleteColumn(columnObject);
	tableCtrl->InsertColumn(idx, columnObject);
}

void CValueTableBoxColumn::Cleanup(wxObject* obj, IVisualHost* visualHost)
{
	wxDataViewCtrl* tableCtrl = dynamic_cast<wxDataViewCtrl*>(visualHost->GetWxObject(GetParent()));
	wxASSERT(tableCtrl);
	CDataViewColumnObject* columnObject = dynamic_cast<CDataViewColumnObject*>(obj);
	wxASSERT(columnObject);
	columnObject->SetHidden(true);
	tableCtrl->DeleteColumn(columnObject);
}

bool CValueTableBoxColumn::CanDeleteControl() const
{
	return m_parent->GetChildCount() > 1;
}

#include "metadata/metaObjects/objects/baseObject.h"

//*******************************************************************
//*							 Control value	                        *
//*******************************************************************

bool CValueTableBoxColumn::FilterSource(const CSourceExplorer& src, const meta_identifier_t& id)
{
	CValueTableBox* tableBox = wxDynamicCast(
		m_parent,
		CValueTableBox
	);
	wxASSERT(tableBox);
	return tableBox->m_dataSource == id;
}

CValue CValueTableBoxColumn::GetControlValue() const
{
	CValueTableBox* tableBox = wxDynamicCast(
		m_parent,
		CValueTableBox
	);
	wxASSERT(tableBox);
	IValueTable::IValueTableReturnLine* retLine = tableBox->m_tableCurrentLine;
	if (retLine) {
		return retLine->GetValueByMetaID(
			m_dataSource == wxNOT_FOUND ? m_controlId : m_dataSource
		);
	}
	return CValue();
}

#include "frontend/controls/textEditor.h"

void CValueTableBoxColumn::SetControlValue(CValue& vSelected)
{
	CValueTableBox* tableBox = wxDynamicCast(
		m_parent,
		CValueTableBox
	);
	wxASSERT(tableBox);
	IValueTable::IValueTableReturnLine* retLine = tableBox->m_tableCurrentLine;
	if (retLine) {
		retLine->SetValueByMetaID(
			m_dataSource == wxNOT_FOUND ? m_controlId : m_dataSource, vSelected
		);
	}

	CDataViewColumnObject* columnObject =
		dynamic_cast<CDataViewColumnObject*>(GetWxObject());
	if (columnObject) {
		CValueViewRenderer* renderer = columnObject->GetRenderer();
		wxASSERT(renderer);
		CTextCtrl* textEditor = dynamic_cast<CTextCtrl*>(renderer->GetEditorCtrl());
		if (textEditor) {
			textEditor->SetTextValue(vSelected.GetString());
		}
		else {
			wxVariant valVariant = wxEmptyString;
			renderer->FinishSelecting(valVariant);
		}
	}
}

//***********************************************************************************
//*                                  Data											*
//***********************************************************************************

bool CValueTableBoxColumn::LoadData(CMemoryReader& reader)
{
	reader.r_stringZ(m_caption);

	m_passwordMode = reader.r_u8();
	m_multilineMode = reader.r_u8();
	m_textEditMode = reader.r_u8();

	m_selbutton = reader.r_u8();
	m_listbutton = reader.r_u8();
	m_clearbutton = reader.r_u8();

	m_align = (wxAlignment)reader.r_s32();
	m_width = reader.r_s32();
	m_visible = reader.r_u8();
	m_resizable = reader.r_u8();
	m_sortable = reader.r_u8();
	m_reorderable = reader.r_u8();

	m_choiceForm = reader.r_s32();

	if (!IAttributeControl::LoadData(reader))
		return false;

	return IValueControl::LoadData(reader);
}

bool CValueTableBoxColumn::SaveData(CMemoryWriter& writer)
{
	writer.w_stringZ(m_caption);

	writer.w_u8(m_passwordMode);
	writer.w_u8(m_multilineMode);
	writer.w_u8(m_textEditMode);

	writer.w_u8(m_selbutton);
	writer.w_u8(m_listbutton);
	writer.w_u8(m_clearbutton);

	writer.w_s32(m_align);
	writer.w_s32(m_width);
	writer.w_u8(m_visible);
	writer.w_u8(m_resizable);
	writer.w_u8(m_sortable);
	writer.w_u8(m_reorderable);

	writer.w_s32(m_choiceForm);

	if (!IAttributeControl::SaveData(writer))
		return false;

	return IValueControl::SaveData(writer);
}


//***********************************************************************************
//*                                  Property                                       *
//***********************************************************************************

void CValueTableBoxColumn::ReadProperty()
{
	IObjectBase::SetPropertyValue("name", m_controlName);
	IObjectBase::SetPropertyValue("caption", m_caption);

	IObjectBase::SetPropertyValue("password_mode", m_passwordMode);
	IObjectBase::SetPropertyValue("multiline_mode", m_multilineMode);
	IObjectBase::SetPropertyValue("textedit_mode", m_textEditMode);

	IObjectBase::SetPropertyValue("button_select", m_selbutton);
	IObjectBase::SetPropertyValue("button_list", m_listbutton);
	IObjectBase::SetPropertyValue("button_clear", m_clearbutton);

	IObjectBase::SetPropertyValue("align", m_align, true);
	IObjectBase::SetPropertyValue("icon", m_icon);
	IObjectBase::SetPropertyValue("width", m_width);
	IObjectBase::SetPropertyValue("visible", m_visible);
	IObjectBase::SetPropertyValue("resizable", m_resizable);
	IObjectBase::SetPropertyValue("sortable", m_sortable);
	IObjectBase::SetPropertyValue("reorderable", m_reorderable);

	IObjectBase::SetPropertyValue("choice_form", m_choiceForm);

	SaveToVariant(
		GetPropertyAsVariant("source"), GetMetaData()
	);
}

void CValueTableBoxColumn::SaveProperty()
{
	IObjectBase::GetPropertyValue("name", m_controlName);
	IObjectBase::GetPropertyValue("caption", m_caption);

	IObjectBase::GetPropertyValue("password_mode", m_passwordMode);
	IObjectBase::GetPropertyValue("multiline_mode", m_multilineMode);
	IObjectBase::GetPropertyValue("textedit_mode", m_textEditMode);

	IObjectBase::GetPropertyValue("button_select", m_selbutton);
	IObjectBase::GetPropertyValue("button_list", m_listbutton);
	IObjectBase::GetPropertyValue("button_clear", m_clearbutton);

	IObjectBase::GetPropertyValue("align", m_align, true);
	IObjectBase::GetPropertyValue("icon", m_icon);
	IObjectBase::GetPropertyValue("width", m_width);
	IObjectBase::GetPropertyValue("visible", m_visible);
	IObjectBase::GetPropertyValue("resizable", m_resizable);
	IObjectBase::GetPropertyValue("sortable", m_sortable);
	IObjectBase::GetPropertyValue("reorderable", m_reorderable);

	IObjectBase::GetPropertyValue("choice_form", m_choiceForm);

	LoadFromVariant(
		GetPropertyAsVariant("source")
	);
}