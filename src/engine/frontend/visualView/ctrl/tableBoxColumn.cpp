#include "tableBox.h"
#include "tableBoxColumnRenderer.h"

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************

wxIMPLEMENT_DYNAMIC_CLASS(ibValueModelTableBoxColumn, ibValueControl);

//****************************************************************************
#include "backend/metaData.h"
#include "backend/objCtor.h"

bool ibValueModelTableBoxColumn::GetChoiceForm(ibPropertyList* property)
{
	const ibMetaData* metaData = GetMetaData();
	if (metaData != nullptr) {
		ibValueMetaObjectRecordDataRef* metaObjectRefValue = nullptr;
		if (!m_propertySource->IsEmptyProperty()) {

			ibValueMetaObjectGenericData* metaObjectValue =
				m_formOwner->GetMetaObject();

			if (metaObjectValue != nullptr) {
				ibValueMetaObject* metaobject =
					metaObjectValue->FindAnyObjectByFilter(m_propertySource->GetValueAsSource());

				ibValueMetaObjectAttributeBase* attribute = wxDynamicCast(
					metaobject, ibValueMetaObjectAttributeBase
				);
				if (attribute != nullptr) {

					const ibCtorMetaValueType* so = metaData->GetTypeCtor(attribute->GetFirstClsid());
					if (so != nullptr) {
						metaObjectRefValue = wxDynamicCast(so->GetMetaObject(), ibValueMetaObjectRecordDataRef);
					}
				}
				else
				{
					metaObjectRefValue = wxDynamicCast(
						metaobject, ibValueMetaObjectRecordDataRef);
				}
			}
		}
		else {
			const ibCtorMetaValueType* so = metaData->GetTypeCtor(ibTypeControlFactory::GetFirstClsid());
			if (so != nullptr) {
				metaObjectRefValue = wxDynamicCast(so->GetMetaObject(), ibValueMetaObjectRecordDataRef);
			}
		}

		if (metaObjectRefValue != nullptr) {
			for (auto form : metaObjectRefValue->GetFormArrayObject()) {
				property->AppendItem(
					form->GetSynonym(),
					form->GetMetaID(),
					form->GetIcon(),
					form
				);
			}
		}
	}

	return true;
}

//***********************************************************************************
//*                            ibValueModelTableBoxColumn                                 *
//***********************************************************************************

ibValueModelTableBoxColumn::ibValueModelTableBoxColumn() :
	ibValueControl(), ibTypeControlFactory(), m_model_id(wxNOT_FOUND)
{
}

ibMetaData* ibValueModelTableBoxColumn::GetMetaData() const
{
	return m_formOwner ?
		m_formOwner->GetMetaData() : nullptr;
}

wxString ibValueModelTableBoxColumn::GetControlTitle() const
{
	if (!m_propertyTitle->IsEmptyProperty()) {
		return m_propertyTitle->GetValueAsTranslateString();
	}
	else if (!m_propertySource->IsEmptyProperty()) {
		const ibValueMetaObject* metaObject = m_propertySource->GetSourceAttributeObject();
		wxASSERT(metaObject);
		return metaObject->GetSynonym();
	}

	return m_propertyName->GetValueAsString();
}

wxObject* ibValueModelTableBoxColumn::Create(wxWindow* wxparent, ibVisualHost* visualHost)
{
	ibDataViewColumnObject* dataViewColumn = new ibDataViewColumnObject(this, wxT(""),
		wxNOT_FOUND, wxDVC_DEFAULT_WIDTH, wxALIGN_CENTER, wxDATAVIEW_COL_REORDERABLE);

	dataViewColumn->SetControl(this);
	return dataViewColumn;
}

void ibValueModelTableBoxColumn::OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated)
{
	ibDataViewCtrl* dataViewCtrl = dynamic_cast<ibDataViewCtrl*>(wxparent);
	wxASSERT(dataViewCtrl);
	ibDataViewColumnObject* dataViewColumn = dynamic_cast<ibDataViewColumnObject*>(wxobject);
	wxASSERT(dataViewColumn);

	dataViewCtrl->AppendColumn(dataViewColumn);
	GetOwner()->SetCalculateColumnPos();
}

#include "backend/appData.h"

void ibValueModelTableBoxColumn::OnUpdated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost)
{
	ibDataViewCtrl* dataViewCtrl = dynamic_cast<ibDataViewCtrl*>(wxparent);
	wxASSERT(dataViewCtrl);
	ibDataViewColumnObject* dataViewColumn = dynamic_cast<ibDataViewColumnObject*>(wxobject);
	wxASSERT(dataViewColumn);

	const unsigned int order_position = GetParentPosition();

	if (m_propertyRepresentation->GetValueAsEnum() == ibRepresentation::ibRepresentation_Auto) {
		dataViewColumn->SetTitle(GetControlTitle());
		dataViewColumn->SetBitmap(m_propertyHeaderPicture->GetValueAsBitmap());
		dataViewColumn->SetFooterTitle(m_propertyFooterText->GetValueAsTranslateString());
		dataViewColumn->SetFooterBitmap(m_propertyFooterPicture->GetValueAsBitmap());
	}
	else if (m_propertyRepresentation->GetValueAsEnum() == ibRepresentation::ibRepresentation_PictureAndText) {
		dataViewColumn->SetTitle(GetControlTitle());
		dataViewColumn->SetBitmap(m_propertyHeaderPicture->GetValueAsBitmap());
		dataViewColumn->SetFooterTitle(m_propertyFooterText->GetValueAsTranslateString());
		dataViewColumn->SetFooterBitmap(m_propertyFooterPicture->GetValueAsBitmap());
	}
	else if (m_propertyRepresentation->GetValueAsEnum() == ibRepresentation::ibRepresentation_Picture) {
		dataViewColumn->SetTitle(wxEmptyString);
		dataViewColumn->SetBitmap(m_propertyHeaderPicture->GetValueAsBitmap());
		dataViewColumn->SetFooterTitle(wxEmptyString);
		dataViewColumn->SetFooterBitmap(m_propertyFooterPicture->GetValueAsBitmap());
	}
	else if (m_propertyRepresentation->GetValueAsEnum() == ibRepresentation::ibRepresentation_Text) {
		dataViewColumn->SetTitle(GetControlTitle());
		dataViewColumn->SetBitmap(wxNullBitmap);
		dataViewColumn->SetFooterTitle(m_propertyFooterText->GetValueAsTranslateString());
		dataViewColumn->SetFooterBitmap(wxNullBitmap);
	}

	dataViewColumn->SetWidth(m_propertyWidth->GetValueAsUInteger());
	dataViewColumn->SetAlignment(m_propertyHeaderAlign->GetValueAsEnum());
	dataViewColumn->SetFooterAlignment(m_propertyFooterAlign->GetValueAsEnum());

	const ibFormID source_column = GetModelColumn();

	ibValueModel* modelValue = GetOwner()->GetModel();
	ibSortOrder::ibSortData* sort = modelValue != nullptr ? modelValue->GetSortByID(source_column) : nullptr;

	dataViewColumn->SetHidden(!m_propertyVisible->GetValueAsBoolean());
	dataViewColumn->SetSortable(sort != nullptr && !appData->DesignerMode());
	dataViewColumn->SetResizeable(m_propertyResizable->GetValueAsBoolean());

	if (sort != nullptr && sort->m_sortEnable && !sort->m_sortSystem && !appData->DesignerMode())
		dataViewColumn->SetSortOrder(sort->m_sortAscending);

	dataViewColumn->SetColumnModel(source_column);
}

void ibValueModelTableBoxColumn::Cleanup(wxObject* obj, ibVisualHost* visualHost)
{
	ibDataViewCtrl* dataViewCtrl = dynamic_cast<ibDataViewCtrl*>(visualHost->GetWxObject(GetOwner()));
	wxASSERT(dataViewCtrl);
	ibDataViewColumnObject* dataViewColumn = dynamic_cast<ibDataViewColumnObject*>(obj);
	wxASSERT(dataViewColumn);

	dataViewCtrl->DeleteColumn(dataViewColumn);
	GetOwner()->SetCalculateColumnPos();
}

bool ibValueModelTableBoxColumn::CanDeleteControl() const
{
	return m_parent->GetChildCount() > 1;
}

#include "backend/metaCollection/partial/commonObject.h"

//*******************************************************************
//*							 Control value	                        *
//*******************************************************************

bool ibValueModelTableBoxColumn::FilterSource(const ibSourceExplorer& src, const ibMetaID& id) const
{
	return id == GetOwner()->GetSource();
}

#include "frontend/win/ctrls/controlTextEditor.h"

bool ibValueModelTableBoxColumn::SetControlValue(const ibValue& varControlVal)
{
	ibValueModelTableBase::ibValueModelReturnLine* currentLine = GetCurrentLine();
	if (currentLine != nullptr) {
		currentLine->SetValueByMetaID(
			GetModelColumn(), varControlVal
		);
	}

	ibDataViewColumnObject* dataViewColumn =
		dynamic_cast<ibDataViewColumnObject*>(GetWxObject());

	if (dataViewColumn != nullptr) {
		ibDataViewValueRenderer* renderer = dataViewColumn->GetRenderer();
		wxASSERT(renderer);
		ibControlTextEditor* textEditor = dynamic_cast<ibControlTextEditor*>(renderer->GetEditorCtrl());
		if (textEditor != nullptr) {
			textEditor->SetValue(varControlVal.GetString());
			textEditor->SetInsertionPointEnd();
		}
		else {
			renderer->FinishSelecting();
		}
	}

	m_formOwner->RefreshForm();
	return true;
}

bool ibValueModelTableBoxColumn::GetControlValue(ibValue& pvarControlVal) const
{
	ibValueModelTableBase::ibValueModelReturnLine* currentLine = GetCurrentLine();
	if (currentLine != nullptr) {
		return currentLine->GetValueByMetaID(
			GetModelColumn(), pvarControlVal
		);
	}

	return false;
}

//***********************************************************************************
//*                                  Data											*
//***********************************************************************************

bool ibValueModelTableBoxColumn::LoadData(ibReaderMemory& reader)
{
	m_propertyTitle->LoadData(reader);
	m_propertyRepresentation->LoadData(reader);
	m_propertyFooterText->LoadData(reader);

	m_propertyHeaderPicture->LoadData(reader);
	m_propertyFooterPicture->LoadData(reader);

	m_propertyPasswordMode->LoadData(reader);
	m_propertyMultilineMode->LoadData(reader);
	m_propertyTexteditMode->LoadData(reader);

	m_propertySelectButton->LoadData(reader);
	m_propertyOpenButton->LoadData(reader);
	m_propertyClearButton->LoadData(reader);

	m_propertyHeaderAlign->LoadData(reader);
	m_propertyFooterAlign->LoadData(reader);

	m_propertyWidth->LoadData(reader);
	m_propertyVisible->LoadData(reader);
	m_propertyResizable->LoadData(reader);
	//m_propertySortable->LoadData(reader);
	m_propertyReorderable->LoadData(reader);

	m_propertyChoiceForm->LoadData(reader);

	if (!m_propertySource->LoadData(reader))
		return false;

	//events
	m_eventOnChange->LoadData(reader);
	m_eventStartChoice->LoadData(reader);
	m_eventStartListChoice->LoadData(reader);
	m_eventClearing->LoadData(reader);
	m_eventOpening->LoadData(reader);
	m_eventChoiceProcessing->LoadData(reader);
	return ibValueControl::LoadData(reader);
}

bool ibValueModelTableBoxColumn::SaveData(ibWriterMemory writer)
{
	m_propertyTitle->SaveData(writer);
	m_propertyRepresentation->SaveData(writer);
	m_propertyFooterText->SaveData(writer);

	m_propertyHeaderPicture->SaveData(writer);
	m_propertyFooterPicture->SaveData(writer);

	m_propertyPasswordMode->SaveData(writer);
	m_propertyMultilineMode->SaveData(writer);
	m_propertyTexteditMode->SaveData(writer);

	m_propertySelectButton->SaveData(writer);
	m_propertyOpenButton->SaveData(writer);
	m_propertyClearButton->SaveData(writer);

	m_propertyHeaderAlign->SaveData(writer);
	m_propertyFooterAlign->SaveData(writer);

	m_propertyWidth->SaveData(writer);
	m_propertyVisible->SaveData(writer);
	m_propertyResizable->SaveData(writer);
	//m_propertySortable->SaveData(writer);
	m_propertyReorderable->SaveData(writer);

	m_propertyChoiceForm->SaveData(writer);

	if (!m_propertySource->SaveData(writer))
		return false;

	//events
	m_eventOnChange->SaveData(writer);
	m_eventStartChoice->SaveData(writer);
	m_eventStartListChoice->SaveData(writer);
	m_eventClearing->SaveData(writer);
	m_eventOpening->SaveData(writer);
	m_eventChoiceProcessing->SaveData(writer);
	return ibValueControl::SaveData(writer);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

S_CONTROL_TYPE_REGISTER(ibValueModelTableBoxColumn, "TableboxColumn", "TableboxColumn", g_controlTableBoxColumnCLSID);