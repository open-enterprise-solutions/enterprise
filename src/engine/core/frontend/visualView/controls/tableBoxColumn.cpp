#include "tableBox.h"

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CValueTableBoxColumn, IValueControl);

//****************************************************************************

#include "form.h"
#include "metadata/metadata.h"
#include "metadata/singleMetaTypes.h"

OptionList* CValueTableBoxColumn::GetChoiceForm(PropertyOption* property)
{
	OptionList* optList = new OptionList;
	optList->AddOption(_("default"), wxNOT_FOUND);

	IMetadata* metaData = GetMetaData();
	if (metaData != NULL) {
		IMetaObjectRecordDataRef* metaObjectRefValue = NULL;
		if (m_dataSource.isValid()) {

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

CValueTableBoxColumn::CValueTableBoxColumn() : IValueControl(), IAttributeControl()
{
}

wxObject* CValueTableBoxColumn::Create(wxObject* parent, IVisualHost* visualHost)
{
	CDataViewColumnObject* columnObject = new CDataViewColumnObject(this, m_propertyCaption->GetValueAsString(),
		!m_dataSource.isValid() ? m_controlId : GetIdByGuid(m_dataSource), m_propertyWidth->GetValueAsInteger(),
		(wxAlignment)m_propertyAlign->GetValueAsInteger(),
		wxDATAVIEW_COL_REORDERABLE
	);

	columnObject->SetControl(this);

	columnObject->SetTitle(m_propertyCaption->GetValueAsString());
	columnObject->SetWidth(m_propertyWidth->GetValueAsInteger());
	columnObject->SetAlignment((wxAlignment)m_propertyAlign->GetValueAsInteger());

	columnObject->SetBitmap(m_propertyIcon->GetValueAsBitmap());
	columnObject->SetHidden(!m_propertyVisible->GetValueAsBoolean());
	columnObject->SetSortable(m_propertySortable->GetValueAsBoolean());
	columnObject->SetResizeable(m_propertyResizable->GetValueAsBoolean());

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

	for (unsigned int i = 0; i < parentControl->GetChildCount(); i++) {
		CValueTableBoxColumn* child = dynamic_cast<CValueTableBoxColumn*>(parentControl->GetChild(i));
		wxASSERT(child);
		if (m_dataSource.isValid() && m_dataSource == child->m_dataSource) { idx = i; break; }
		else if (!m_dataSource.isValid() && m_controlId == child->m_controlId) { idx = i; break; }
	}

	wxDataViewCtrl* tableCtrl = dynamic_cast<wxDataViewCtrl*>(wxparent);
	wxASSERT(tableCtrl);
	CDataViewColumnObject* columnObject = dynamic_cast<CDataViewColumnObject*>(wxobject);
	wxASSERT(columnObject);

	columnObject->SetControl(this);

	wxString textCaption = m_propertyName->GetValueAsString();
	if (m_dataSource.isValid()) {
		IMetaObject* metaObject = GetMetaSource();
		if (metaObject != NULL) {
			textCaption = metaObject->GetSynonym();
		}
	}

	columnObject->SetTitle(!m_propertyCaption->IsOk() ?
		textCaption : m_propertyCaption->GetValueAsString());
	columnObject->SetWidth(m_propertyWidth->GetValueAsInteger());
	columnObject->SetAlignment((wxAlignment)m_propertyAlign->GetValueAsInteger());

	columnObject->SetBitmap(m_propertyIcon->GetValueAsBitmap());
	columnObject->SetHidden(!m_propertyVisible->GetValueAsBoolean());
	columnObject->SetSortable(m_propertySortable->GetValueAsBoolean());
	columnObject->SetResizeable(m_propertyResizable->GetValueAsBoolean());

	columnObject->SetColModel(!m_dataSource.isValid() ? m_controlId : GetIdByGuid(m_dataSource));

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

#include "metadata/metaObjects/objects/object.h"

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
	return GetIdByGuid(tableBox->m_dataSource) == id;
}

CValue CValueTableBoxColumn::GetControlValue() const
{
	CValueTableBox* tableBox = wxDynamicCast(
		m_parent,
		CValueTableBox
	);
	wxASSERT(tableBox);
	IValueTable::IValueModelReturnLine* retLine = tableBox->m_tableCurrentLine;
	if (retLine != NULL) {
		return retLine->GetValueByMetaID(
			!m_dataSource.isValid() ? m_controlId : GetIdByGuid(m_dataSource)
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
	IValueTable::IValueModelReturnLine* retLine = tableBox->m_tableCurrentLine;
	if (retLine) {
		retLine->SetValueByMetaID(
			!m_dataSource.isValid() ? m_controlId : GetIdByGuid(m_dataSource), vSelected
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
	wxString caption; reader.r_stringZ(caption);
	m_propertyCaption->SetValue(caption);

	m_propertyPasswordMode->SetValue(reader.r_u8());
	m_propertyMultilineMode->SetValue(reader.r_u8());
	m_propertyTexteditMode->SetValue(reader.r_u8());

	m_propertySelectButton->SetValue(reader.r_u8());
	m_propertyListButton->SetValue(reader.r_u8());
	m_propertyClearButton->SetValue(reader.r_u8());

	m_propertyAlign->SetValue(reader.r_s32());
	m_propertyWidth->SetValue(reader.r_s32());
	m_propertyVisible->SetValue(reader.r_u8());
	m_propertyResizable->SetValue(reader.r_u8());
	m_propertySortable->SetValue(reader.r_u8());
	m_propertyReorderable->SetValue(reader.r_u8());

	m_propertyChoiceForm->SetValue(reader.r_s32());

	if (!IAttributeControl::LoadTypeData(reader))
		return false;

	return IValueControl::LoadData(reader);
}

bool CValueTableBoxColumn::SaveData(CMemoryWriter& writer)
{
	writer.w_stringZ(m_propertyCaption->GetValueAsString());

	writer.w_u8(m_propertyPasswordMode->GetValueAsBoolean());
	writer.w_u8(m_propertyMultilineMode->GetValueAsBoolean());
	writer.w_u8(m_propertyTexteditMode->GetValueAsBoolean());

	writer.w_u8(m_propertySelectButton->GetValueAsBoolean());
	writer.w_u8(m_propertyListButton->GetValueAsBoolean());
	writer.w_u8(m_propertyClearButton->GetValueAsBoolean());

	writer.w_s32(m_propertyAlign->GetValueAsInteger());
	writer.w_s32(m_propertyWidth->GetValueAsInteger());
	writer.w_u8(m_propertyVisible->GetValueAsBoolean());
	writer.w_u8(m_propertyResizable->GetValueAsBoolean());
	writer.w_u8(m_propertySortable->GetValueAsBoolean());
	writer.w_u8(m_propertyReorderable->GetValueAsBoolean());

	writer.w_s32(m_propertyChoiceForm->GetValueAsInteger());

	if (!IAttributeControl::SaveTypeData(writer))
		return false;

	return IValueControl::SaveData(writer);
}