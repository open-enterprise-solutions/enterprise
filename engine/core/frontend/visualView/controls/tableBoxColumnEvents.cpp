#include "tableBox.h"
#include "metadata/metadata.h"
#include "metadata/singleMetaTypes.h"
#include "form.h"

CLASS_ID CValueTableBoxColumn::GetTypeClsid()
{
	CLASS_ID clsid = 0;

	if (m_colSource != wxNOT_FOUND) {
		IMetaAttributeObject *metaObject = NULL;
		if (m_formOwner) {
			IMetaObjectValue *metaObjectValue = m_formOwner->GetMetaObject();
			if (metaObjectValue) {
				metaObject = wxDynamicCast(
					metaObjectValue->FindMetaObjectByID(m_colSource), IMetaAttributeObject
				);
			}
			if (metaObject != NULL) {
				clsid = metaObject->GetClassTypeObject();
			}
		}
	}
	else {
		IMetadata *metaData = GetMetaData();
		if (m_typeDescription.GetTypeObject() >= defaultMetaID) {
			IMetaObjectValue *metaObject = wxDynamicCast(
				metaData->GetMetaObject(m_typeDescription.GetTypeObject()), IMetaObjectValue
			);
			wxASSERT(metaObject);

			IMetaTypeObjectValueSingle *clsFactory =
				metaObject->GetTypeObject(eMetaObjectType::enReference);
			wxASSERT(clsFactory);
			clsid = clsFactory->GetTypeID();
		}
		else {
			clsid = CValue::GetIDByVT((const eValueTypes)m_typeDescription.GetTypeObject());
		}
	}

	return clsid;
}

bool CValueTableBoxColumn::TextProcessing(CValue &selValue,  const wxString &stringData)
{
	CLASS_ID clsid = GetTypeClsid();
	if (clsid > 0) {
		IMetadata *metaData = GetMetaData();
		wxString className =
			metaData->GetNameObjectFromID(clsid);
		CValue newValue = metaData->CreateObject(className);
		if (stringData.Length() > 0) {
			std::vector<CValue> foundedObjects;
			if (newValue.FindValue(stringData, foundedObjects)) {
				selValue = foundedObjects.at(0);
			}
		}
		else {
			selValue = newValue;
		}
		return true;
	}
	return false;
}

void CValueTableBoxColumn::OnSelectButtonPressed(wxCommandEvent &event)
{
	CValue selfControl = this; CValue standartProcessing = true;
	IValueControl::CallEvent("startChoice", selfControl, standartProcessing);
	if (!standartProcessing.GetBoolean()) {
		return;
	}

	if (m_colSource != wxNOT_FOUND) {
		IMetaObject *metaObject = NULL;
		if (m_formOwner) {
			IMetaObjectValue *metaObjectValue = m_formOwner->GetMetaObject();
			if (metaObjectValue) {
				metaObject = metaObjectValue->FindMetaObjectByID(m_colSource);
			}
		}
		if (metaObject) {
			metaObject->ProcessChoice(this, m_choiceForm);
		}
	}
	else {
		IMetadata *metaData = GetMetaData();
		if (metaData) {
			IMetaObject* metaObject = metaData->GetMetaObject(m_typeDescription.GetTypeObject());
			if (metaObject) {
				metaObject->ProcessChoice(this, m_choiceForm);
			}
		}
	}
}

void CValueTableBoxColumn::OnListButtonPressed(wxCommandEvent &event)
{
	CValue selfControl = this; CValue standartProcessing = true;
	IValueControl::CallEvent("startListChoice", selfControl, standartProcessing);
	if (!standartProcessing.GetBoolean()) {
		return;
	}

}

void CValueTableBoxColumn::OnClearButtonPressed(wxCommandEvent &event)
{
	CValue selfControl = this; CValue standartProcessing = true;
	IValueControl::CallEvent("clearing", selfControl, standartProcessing);
	if (!standartProcessing.GetBoolean()) {
		return;
	}

	CValueTableBox *tableBox = wxDynamicCast(
		GetParent(),
		CValueTableBox
	);

	wxASSERT(tableBox);

	if (tableBox->m_tableCurrentLine) {
		if (m_colSource != wxNOT_FOUND) {
			tableBox->m_tableCurrentLine->SetValueByMetaID(m_colSource, CValue());
		}
	}

	CDataViewColumnObject *columnObject =
		dynamic_cast<CDataViewColumnObject *>(GetWxObject());
	if (columnObject) {
		CValueViewRenderer *renderer = columnObject->GetRenderer();
		wxASSERT(renderer);
		wxVariant valVariant = wxEmptyString;
		renderer->FinishSelecting(valVariant);
	}
}

#include "frontend/controls/textCtrl.h"

void CValueTableBoxColumn::OnTextEnter(wxCommandEvent &event)
{
	wxTextCtrl *textCtrl = wxDynamicCast(
		event.GetEventObject(), wxTextCtrl
	);

	CValue selValue;
	if (TextProcessing(selValue, textCtrl->GetValue())) {
		textCtrl->SetValue(selValue.GetString());
	}

	event.Skip();
}

void CValueTableBoxColumn::OnKillFocus(wxFocusEvent &event)
{
	wxTextCtrl *textCtrl = wxDynamicCast(
		event.GetEventObject(), wxTextCtrl
	);

	CValue selValue;
	if (TextProcessing(selValue, textCtrl->GetValue())) {
		textCtrl->SetValue(selValue.GetString());
	}

	event.Skip();
}

void CValueTableBoxColumn::ChoiceProcessing(CValue &vSelected)
{
	CValueTableBox *tableBox = wxDynamicCast(
		GetParent(),
		CValueTableBox
	);

	wxASSERT(tableBox);

	if (tableBox->m_tableCurrentLine) {
		if (m_colSource != wxNOT_FOUND) {
			tableBox->m_tableCurrentLine->SetValueByMetaID(m_colSource, vSelected);
		}
	}

	CDataViewColumnObject *columnObject =
		dynamic_cast<CDataViewColumnObject *>(GetWxObject());
	if (columnObject) {
		CValueViewRenderer *renderer = columnObject->GetRenderer();
		wxASSERT(renderer);
		wxVariant valVariant = vSelected.GetString();
		renderer->FinishSelecting(valVariant);
	}
}
