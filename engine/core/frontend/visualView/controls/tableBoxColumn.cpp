#include "tableBox.h"

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CValueTableBoxColumn, IValueControl);

//****************************************************************************

#include "form.h"
#include "metadata/metadata.h"

OptionList *CValueTableBoxColumn::GetChoiceForm(Property *property)
{
	OptionList *optList = new OptionList;
	optList->AddOption(_("default"), wxNOT_FOUND);

	IMetadata *metaData = GetMetaData();
	if (metaData) {
		
		IMetaObjectRefValue* metaObjectRefValue = NULL;

		if (m_colSource != wxNOT_FOUND) {
			
			IMetaObjectValue *metaObjectValue =
				m_formOwner->GetMetaObject();
			
			if (metaObjectValue) {
				IMetaObject *metaobject =
					metaObjectValue->FindMetaObjectByID(m_colSource);

				IMetaAttributeObject *metaAttribute = wxDynamicCast(
					metaobject, IMetaAttributeObject
				);
				if (metaAttribute) {
					metaObjectValue = wxDynamicCast(
						metaData->GetMetaObject(metaAttribute->GetTypeObject()), IMetaObjectRefValue);
				}
				else
				{
					metaObjectValue = wxDynamicCast(
						metaobject, IMetaObjectRefValue);
				}
			}
		}
		else {
			metaObjectRefValue = wxDynamicCast(
				metaData->GetMetaObject(m_typeDescription.GetTypeObject()), IMetaObjectRefValue);
		}

		if (metaObjectRefValue) {
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

//***********************************************************************************
//*                            CValueTableBoxColumn                                 *
//***********************************************************************************

CValueTableBoxColumn::CValueTableBoxColumn() : IValueControl(), IAttributeInfo(eValueTypes::TYPE_STRING),
m_markup(true),
m_caption("newColumn"),
m_width(wxDVC_DEFAULT_WIDTH),
m_align(wxALIGN_LEFT),
m_selbutton(true), m_listbutton(false), m_clearbutton(false),
m_passwordMode(false), m_multilineMode(false), m_textEditMode(true),
m_colSource(wxNOT_FOUND), m_choiceForm(wxNOT_FOUND), 
m_visible(true), m_resizable(true), m_sortable(false), m_reorderable(true)
{
	PropertyContainer *categoryInfo = IObjectBase::CreatePropertyContainer("Info");
	categoryInfo->AddProperty("name", PropertyType::PT_WXNAME);
	categoryInfo->AddProperty("caption", PropertyType::PT_WXSTRING);


	categoryInfo->AddProperty("password_mode", PropertyType::PT_BOOL);
	categoryInfo->AddProperty("multiline_mode", PropertyType::PT_BOOL);
	categoryInfo->AddProperty("textedit_mode", PropertyType::PT_BOOL);

	m_category->AddCategory(categoryInfo);

	PropertyContainer *categoryData = IObjectBase::CreatePropertyContainer("Data");
	categoryData->AddProperty("source", PropertyType::PT_SOURCE);

	categoryData->AddProperty("type", PropertyType::PT_TYPE_SELECT);
	categoryData->AddProperty("precision", PropertyType::PT_UINT);
	categoryData->AddProperty("scale", PropertyType::PT_UINT);
	categoryData->AddProperty("date_time", PropertyType::PT_OPTION, &CValueTableBoxColumn::GetDateTimeFormat);
	categoryData->AddProperty("length", PropertyType::PT_UINT);

	categoryData->AddProperty("choice_form", PropertyType::PT_OPTION, &CValueTableBoxColumn::GetChoiceForm);
	m_category->AddCategory(categoryData);

	PropertyContainer *categoryButton = IObjectBase::CreatePropertyContainer("Button");

	categoryButton->AddProperty("button_select", PropertyType::PT_BOOL);
	categoryButton->AddProperty("button_list", PropertyType::PT_BOOL);
	categoryButton->AddProperty("button_clear", PropertyType::PT_BOOL);

	//category 
	m_category->AddCategory(categoryButton);

	PropertyContainer *categoryStyle = IObjectBase::CreatePropertyContainer("Style");
	categoryStyle->AddProperty("width", PropertyType::PT_UINT);
	categoryStyle->AddProperty("align", PropertyType::PT_OPTION, &CValueTableBoxColumn::GetAlign);
	categoryStyle->AddProperty("icon", PropertyType::PT_BITMAP);
	categoryStyle->AddProperty("visible", PropertyType::PT_BOOL);
	categoryStyle->AddProperty("resizable", PropertyType::PT_BOOL);
	categoryStyle->AddProperty("sortable", PropertyType::PT_BOOL);
	categoryStyle->AddProperty("reorderable", PropertyType::PT_BOOL);

	m_category->AddCategory(categoryStyle);
}

wxObject* CValueTableBoxColumn::Create(wxObject* parent, IVisualHost *visualHost)
{
	CDataViewColumnObject *columnObject = new CDataViewColumnObject(this, m_caption,
		m_colSource == wxNOT_FOUND ? m_controlId : m_colSource, m_width,
		(wxAlignment)m_align,
		wxDATAVIEW_COL_REORDERABLE
	);

	columnObject->SetTitle(m_caption);
	columnObject->SetWidth(m_width);
	columnObject->SetAlignment((wxAlignment)m_align);

	columnObject->SetBitmap(m_icon);
	columnObject->SetHidden(!m_visible);
	columnObject->SetSortable(m_sortable);
	columnObject->SetResizeable(m_resizable);

	columnObject->SetControlID(m_controlId);

	CValueViewRenderer *colRenderer = columnObject->GetRenderer();
	wxASSERT(colRenderer);

	return columnObject;
}

void CValueTableBoxColumn::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost, bool first—reated)
{
	wxDataViewCtrl *m_tableCtrl = dynamic_cast<wxDataViewCtrl *>(wxparent);
	wxASSERT(m_tableCtrl);
	CDataViewColumnObject *columnObject = dynamic_cast<CDataViewColumnObject *>(wxobject);
	wxASSERT(columnObject);
	m_tableCtrl->AppendColumn(columnObject);
}

void CValueTableBoxColumn::OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost *visualHost)
{
	IValueFrame *m_parentControl = GetParent(); int idx = wxNOT_FOUND;

	for (unsigned int i = 0; i < m_parentControl->GetChildCount(); i++)
	{
		CValueTableBoxColumn *child = dynamic_cast<CValueTableBoxColumn *>(m_parentControl->GetChild(i));
		wxASSERT(child);
		if (m_colSource != wxNOT_FOUND && m_colSource == child->m_colSource) { idx = i; break; }
		else if (m_colSource == wxNOT_FOUND && m_controlId == child->m_controlId) { idx = i; break; }
	}

	wxDataViewCtrl *m_tableCtrl = dynamic_cast<wxDataViewCtrl *>(wxparent);
	wxASSERT(m_tableCtrl);
	CDataViewColumnObject *columnObject = dynamic_cast<CDataViewColumnObject *>(wxobject);
	wxASSERT(columnObject);

	columnObject->SetTitle(m_caption);
	columnObject->SetWidth(m_width);
	columnObject->SetAlignment(m_align);

	columnObject->SetBitmap(m_icon);
	columnObject->SetHidden(!m_visible);
	columnObject->SetSortable(m_sortable);
	columnObject->SetResizeable(m_resizable);

	columnObject->SetColModel(m_colSource == wxNOT_FOUND ? m_controlId : m_colSource);

	CValueViewRenderer *colRenderer = columnObject->GetRenderer();
	wxASSERT(colRenderer);

	m_tableCtrl->DeleteColumn(columnObject);
	m_tableCtrl->InsertColumn(idx, columnObject);
}

void CValueTableBoxColumn::Cleanup(wxObject* obj, IVisualHost *visualHost)
{
	wxDataViewCtrl *m_tableCtrl = dynamic_cast<wxDataViewCtrl *>(visualHost->GetWxObject(GetParent()));
	wxASSERT(m_tableCtrl);
	CDataViewColumnObject *columnObject = dynamic_cast<CDataViewColumnObject *>(obj);
	wxASSERT(columnObject);
	columnObject->SetHidden(true);
	m_tableCtrl->DeleteColumn(columnObject);
}

bool CValueTableBoxColumn::CanDeleteControl() const
{
	return m_parent->GetChildCount() > 1;
}

#include "metadata/objects/baseObject.h"

bool CValueTableBoxColumn::FilterSource(const CSourceExplorer &src, meta_identifier_t id)
{
	CValueTableBox *tableBox = wxDynamicCast(
		GetParent(),
		CValueTableBox
	);

	return tableBox->m_dataSource == id;
}

//***********************************************************************************
//*                                  Data											*
//***********************************************************************************

bool CValueTableBoxColumn::LoadData(CMemoryReader &reader)
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

	m_colSource = reader.r_s32();
	m_choiceForm = reader.r_s32();

	reader.r(&m_typeDescription, sizeof(typeDescription_t));

	return IValueControl::LoadData(reader);
}

bool CValueTableBoxColumn::SaveData(CMemoryWriter &writer)
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

	writer.w_s32(m_colSource);
	writer.w_s32(m_choiceForm);

	writer.w(&m_typeDescription, sizeof(typeDescription_t));

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

	IObjectBase::SetPropertyValue("source", m_colSource);
	IObjectBase::SetPropertyValue("choice_form", m_choiceForm);

	IObjectBase::SetPropertyValue("type", m_typeDescription.GetTypeObject());

	switch (m_typeDescription.GetTypeObject())
	{
	case eValueTypes::TYPE_NUMBER:
		IObjectBase::SetPropertyValue("precision", m_typeDescription.GetPrecision(), true);
		IObjectBase::SetPropertyValue("scale", m_typeDescription.GetScale(), true);
		break;
	case eValueTypes::TYPE_DATE:
		IObjectBase::SetPropertyValue("date_time", m_typeDescription.GetDateTime(), true);
		break;
	case eValueTypes::TYPE_STRING:
		IObjectBase::SetPropertyValue("length", m_typeDescription.GetLength(), true);
		break;
	}
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

	IObjectBase::GetPropertyValue("source", m_colSource);
	IObjectBase::GetPropertyValue("choice_form", m_choiceForm);

	meta_identifier_t metaType = 0;
	if (IObjectBase::GetPropertyValue("type", metaType)) {

		if (metaType != m_typeDescription.GetTypeObject()) {
			m_typeDescription.SetDefaultMetatype(metaType);
			switch (m_typeDescription.GetTypeObject())
			{
			case eValueTypes::TYPE_NUMBER:
				IObjectBase::SetPropertyValue("precision", m_typeDescription.GetPrecision(), true);
				IObjectBase::SetPropertyValue("scale", m_typeDescription.GetScale(), true);
				break;
			case eValueTypes::TYPE_DATE:
				IObjectBase::SetPropertyValue("date_time", m_typeDescription.GetDateTime(), true);
				break;
			case eValueTypes::TYPE_STRING:
				IObjectBase::SetPropertyValue("length", m_typeDescription.GetLength(), true);
				break;
			}
		}

		switch (m_typeDescription.GetTypeObject())
		{
		case eValueTypes::TYPE_NUMBER:
		{
			unsigned char precision = 10, scale = 0;
			IObjectBase::GetPropertyValue("precision", precision, true);
			IObjectBase::GetPropertyValue("scale", scale, true);
			m_typeDescription.SetNumber(precision, scale);
			break;
		}
		case eValueTypes::TYPE_DATE:
		{
			eDateFractions dateTime = eDateFractions::eDateTime;
			IObjectBase::GetPropertyValue("date_time", dateTime, true);
			m_typeDescription.SetDate(dateTime);
			break;
		}
		case eValueTypes::TYPE_STRING:
		{
			unsigned short length = 0;
			IObjectBase::GetPropertyValue("length", length, true);
			m_typeDescription.SetString(length);
			break;
		}
		}
	}
}