#include "tableBox.h"
#include "frontend/visualView/visualEditor.h"  

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(CValueTableBoxColumn, IValueControl);

//****************************************************************************

#include "form.h"
#include "core/metadata/metadata.h"
#include "core/metadata/singleClass.h"

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
			IMetaTypeObjectValueSingle* so = metaData->GetTypeObject(ITypeControl::GetFirstClsid());
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

ISourceObject* CValueTableBoxColumn::GetSourceObject() const
{
	return GetOwner();
}

//***********************************************************************************
//*                            CValueTableBoxColumn                                 *
//***********************************************************************************

CValueTableBoxColumn::CValueTableBoxColumn() :
	IValueControl(), ITypeControl()
{
}

wxObject* CValueTableBoxColumn::Create(wxWindow* wxparent, IVisualHost* visualHost)
{
	wxDataViewColumnObject* columnObject = new wxDataViewColumnObject(this, m_propertyCaption->GetValueAsString(),
		m_dataSource.isValid() ? GetIdByGuid(m_dataSource) : m_controlId, m_propertyWidth->GetValueAsInteger(),
		(wxAlignment)m_propertyAlign->GetValueAsInteger(),
		wxDATAVIEW_COL_REORDERABLE
	);

	columnObject->SetControl(this);
	columnObject->SetControlID(m_controlId);

	columnObject->SetTitle(m_propertyCaption->GetValueAsString());
	columnObject->SetWidth(m_propertyWidth->GetValueAsInteger());
	columnObject->SetAlignment((wxAlignment)m_propertyAlign->GetValueAsInteger());

	columnObject->SetBitmap(m_propertyIcon->GetValueAsBitmap());
	columnObject->SetHidden(!m_propertyVisible->GetValueAsBoolean());
	columnObject->SetSortable(false);
	columnObject->SetResizeable(m_propertyResizable->GetValueAsBoolean());

	columnObject->SetColModel(m_dataSource.isValid() ? GetIdByGuid(m_dataSource) : m_controlId);

	CValueViewRenderer* colRenderer = columnObject->GetRenderer();
	wxASSERT(colRenderer);
	return columnObject;
}

void CValueTableBoxColumn::OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated)
{
	wxDataViewCtrl* tableCtrl = dynamic_cast<wxDataViewCtrl*>(wxparent);
	wxASSERT(tableCtrl);
	wxDataViewColumnObject* columnObject = dynamic_cast<wxDataViewColumnObject*>(wxobject);
	wxASSERT(columnObject);

	wxHeaderCtrl* headerCtrl = tableCtrl->GenericGetHeader();
	if (headerCtrl != NULL)
		headerCtrl->ResetColumnsOrder();
	tableCtrl->AppendColumn(columnObject);
}

#include "appData.h"

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
	wxDataViewColumnObject* columnObject = dynamic_cast<wxDataViewColumnObject*>(wxobject);
	wxASSERT(columnObject);

	columnObject->SetControl(this);

	wxString textCaption = m_propertyName->GetValueAsString();
	if (m_dataSource.isValid()) {
		IMetaObject* metaObject = GetMetaSource();
		if (metaObject != NULL)
			textCaption = metaObject->GetSynonym();
	}

	columnObject->SetTitle(!m_propertyCaption->IsOk() ?
		textCaption : m_propertyCaption->GetValueAsString());
	columnObject->SetWidth(m_propertyWidth->GetValueAsInteger());
	columnObject->SetAlignment((wxAlignment)m_propertyAlign->GetValueAsInteger());

	IValueModel* modelValue = GetOwner()->GetModel();
	sortOrder_t::sortData_t* sort = modelValue != NULL ? modelValue->GetSortByID(m_dataSource.isValid() ? GetIdByGuid(m_dataSource) : m_controlId) : false;

	columnObject->SetBitmap(m_propertyIcon->GetValueAsBitmap());
	columnObject->SetHidden(!m_propertyVisible->GetValueAsBoolean());
	columnObject->SetSortable(sort != NULL && !appData->DesignerMode());
	columnObject->SetResizeable(m_propertyResizable->GetValueAsBoolean());

	if (sort != NULL && sort->m_sortEnable && !sort->m_sortSystem && !appData->DesignerMode())
		columnObject->SetSortOrder(sort->m_sortAscending);

	columnObject->SetColModel(m_dataSource.isValid() ? GetIdByGuid(m_dataSource) : m_controlId);

	wxHeaderCtrl* headerCtrl = tableCtrl->GenericGetHeader();
	if (headerCtrl != NULL) {
		unsigned int model_index = tableCtrl->GetColumnIndex(columnObject);
		unsigned int col_header_index = headerCtrl->GetColumnPos(model_index);
		if (col_header_index != idx && tableCtrl->DeleteColumn(columnObject))
			tableCtrl->InsertColumn(idx, columnObject);
	}
	else {
		unsigned int model_index = tableCtrl->GetColumnIndex(columnObject);
		if (model_index != idx && tableCtrl->DeleteColumn(columnObject))
			tableCtrl->InsertColumn(idx, columnObject);
	}
}

void CValueTableBoxColumn::Cleanup(wxObject* obj, IVisualHost* visualHost)
{
	wxDataViewCtrl* tableCtrl = dynamic_cast<wxDataViewCtrl*>(visualHost->GetWxObject(GetOwner()));
	wxASSERT(tableCtrl);
	wxDataViewColumnObject* columnObject = dynamic_cast<wxDataViewColumnObject*>(obj);
	wxASSERT(columnObject);
	wxHeaderCtrl* headerCtrl = tableCtrl->GenericGetHeader();
	if (headerCtrl != NULL) {
		if (headerCtrl->GetColumnCount() == 1 && !columnObject->IsShown())
			columnObject->SetHidden(false);
		headerCtrl->ResetColumnsOrder();
	}
	tableCtrl->DeleteColumn(columnObject);
}

bool CValueTableBoxColumn::CanDeleteControl() const
{
	return m_parent->GetChildCount() > 1;
}

#include "core/metadata/metaObjects/objects/object.h"

//*******************************************************************
//*							 Control value	                        *
//*******************************************************************

bool CValueTableBoxColumn::FilterSource(const CSourceExplorer& src, const meta_identifier_t& id)
{
	return GetIdByGuid(GetOwner()->m_dataSource) == id;
}

#include "frontend/visualView/dvc/dvc.h"
#include "frontend/controls/textEditor.h"

bool CValueTableBoxColumn::SetControlValue(const CValue& varControlVal)
{
	IValueTable::IValueModelReturnLine* retLine = GetReturnLine();
	if (retLine != NULL) {
		retLine->SetValueByMetaID(
			m_dataSource.isValid() ? GetIdByGuid(m_dataSource) : m_controlId, varControlVal
		);
	}
	wxDataViewColumnObject* columnObject =
		dynamic_cast<wxDataViewColumnObject*>(GetWxObject());
	if (columnObject != NULL) {
		CValueViewRenderer* renderer = columnObject->GetRenderer();
		wxASSERT(renderer);
		wxTextContainerCtrl* textEditor = dynamic_cast<wxTextContainerCtrl*>(renderer->GetEditorCtrl());
		if (textEditor != NULL) {
			textEditor->SetTextValue(varControlVal.GetString());
			textEditor->SetInsertionPointEnd();
			textEditor->SetFocus();
		}
		else {
			renderer->FinishSelecting();
		}
	}

	return true;
}

bool CValueTableBoxColumn::GetControlValue(CValue& pvarControlVal) const
{
	IValueTable::IValueModelReturnLine* retLine = GetReturnLine();
	if (retLine != NULL) {
		return retLine->GetValueByMetaID(
			m_dataSource.isValid() ? GetIdByGuid(m_dataSource) : m_controlId, pvarControlVal
		);
	}
	return false;
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
	m_propertyOpenButton->SetValue(reader.r_u8());
	m_propertyClearButton->SetValue(reader.r_u8());

	m_propertyAlign->SetValue(reader.r_s32());
	m_propertyWidth->SetValue(reader.r_s32());
	m_propertyVisible->SetValue(reader.r_u8());
	m_propertyResizable->SetValue(reader.r_u8());
	//m_propertySortable->SetValue(reader.r_u8());
	m_propertyReorderable->SetValue(reader.r_u8());

	m_propertyChoiceForm->SetValue(reader.r_s32());

	if (!ITypeControl::LoadTypeData(reader))
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
	writer.w_u8(m_propertyOpenButton->GetValueAsBoolean());
	writer.w_u8(m_propertyClearButton->GetValueAsBoolean());

	writer.w_s32(m_propertyAlign->GetValueAsInteger());
	writer.w_s32(m_propertyWidth->GetValueAsInteger());
	writer.w_u8(m_propertyVisible->GetValueAsBoolean());
	writer.w_u8(m_propertyResizable->GetValueAsBoolean());
	//writer.w_u8(m_propertySortable->GetValueAsBoolean());
	writer.w_u8(m_propertyReorderable->GetValueAsBoolean());

	writer.w_s32(m_propertyChoiceForm->GetValueAsInteger());

	if (!ITypeControl::SaveTypeData(writer))
		return false;

	return IValueControl::SaveData(writer);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

S_CONTROL_VALUE_REGISTER(CValueTableBoxColumn, "tableboxColumn", "tableboxColumn", g_controlTableBoxColumnCLSID);