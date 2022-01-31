#include "widgets.h"
#include "frontend/objinspect/objinspect.h"
#include "metadata/metadata.h"
#include "form.h"

void CValueTextCtrl::OnPropertyCreated()
{
	PropertyContainer *categoryType = m_category->GetCategory(2);
	if (m_source != wxNOT_FOUND) {
		categoryType->HideProperty("type");
		categoryType->HideProperty("precision");
		categoryType->HideProperty("scale");
		categoryType->HideProperty("date_time");
		categoryType->HideProperty("length");
	}
	else {
		switch (m_typeDescription.GetTypeObject())
		{
		case eValueTypes::TYPE_NUMBER:
			categoryType->ShowProperty("precision");
			categoryType->ShowProperty("scale");
			categoryType->HideProperty("date_time");
			categoryType->HideProperty("length");
			break;
		case eValueTypes::TYPE_DATE:
			categoryType->HideProperty("precision");
			categoryType->HideProperty("scale");
			categoryType->ShowProperty("date_time");
			categoryType->HideProperty("length");
			break;
		case eValueTypes::TYPE_STRING:
			categoryType->HideProperty("precision");
			categoryType->HideProperty("scale");
			categoryType->HideProperty("date_time");
			categoryType->ShowProperty("length");
			break;
		default:
			categoryType->HideProperty("precision");
			categoryType->HideProperty("scale");
			categoryType->HideProperty("date_time");
			categoryType->HideProperty("length");
		}

		categoryType->ShowProperty("type");
	}

	IMetadata *metaData = GetMetaData();
	if (metaData) {
		IMetaObjectRefValue* metaObject = NULL;

		if (m_source != wxNOT_FOUND) {
			IMetaObjectValue *metaObjectValue =
				m_formOwner->GetMetaObject();
			if (metaObjectValue) {
				IMetaAttributeObject *metaAttribute = wxDynamicCast(
					metaObjectValue->FindMetaObjectByID(m_source), IMetaAttributeObject
				);
				wxASSERT(metaAttribute);
				metaObject = wxDynamicCast(
					metaData->GetMetaObject(metaAttribute->GetTypeObject()), IMetaObjectRefValue);
			}
		}
		else {
			metaObject = wxDynamicCast(
				metaData->GetMetaObject(m_typeDescription.GetTypeObject()), IMetaObjectRefValue);
		}

		if (metaObject == NULL) {
			categoryType->HideProperty("choice_form");
		}
		else {
			categoryType->ShowProperty("choice_form");
		}
	}
}

bool CValueTextCtrl::OnPropertyChanging(Property *property, const wxString &oldValue)
{
	if (m_typeDescription.GetTypeObject() == eValueTypes::TYPE_NUMBER) {

		int precision = m_typeDescription.GetPrecision();
		if (property->GetName() == wxT("precision")) {
			precision = property->GetValueAsInteger();
		}
		int scale = m_typeDescription.GetScale();
		if (property->GetName() == wxT("scale")) {
			scale = property->GetValueAsInteger();
		}
		if (precision > MAX_PRECISION) {
			return false;
		}
		if (precision == 0 || precision < scale) {
			return false;
		}
	}
	else if (m_typeDescription.GetTypeObject() == eValueTypes::TYPE_STRING) {
		int length = m_typeDescription.GetLength();
		if (property->GetName() == wxT("length")) {
			length = property->GetValueAsInteger();
		}
		if (length > MAX_LENGTH_STRING) {
			return false;
		}
	}

	return IValueWindow::OnPropertyChanging(property, oldValue);
}

void CValueTextCtrl::OnPropertyChanged(Property *property)
{
	PropertyContainer *categoryType = m_category->GetCategory(2);

	if (m_source != wxNOT_FOUND) {
		categoryType->HideProperty("type");
	}
	else {
		categoryType->ShowProperty("type");
	}

	if (property->GetName() == wxT("source") || property->GetName() == wxT("type")) {
		IMetadata *metaData = GetMetaData();
		if (metaData) {
			IMetaObjectRefValue* metaObject = NULL;
			if (m_source != wxNOT_FOUND) {
				IMetaObjectValue *metaObjectValue =
					m_formOwner->GetMetaObject();
				if (metaObjectValue) {
					IMetaAttributeObject *metaAttribute = wxDynamicCast(
						metaObjectValue->FindMetaObjectByID(m_source), IMetaAttributeObject
					);
					wxASSERT(metaAttribute);
					metaObject = wxDynamicCast(
						metaData->GetMetaObject(metaAttribute->GetTypeObject()), IMetaObjectRefValue);

					if (property->GetName() == wxT("source")) {
						m_typeDescription = metaAttribute->GetTypeDescription();
					}
				}
			}
			else {
				metaObject = wxDynamicCast(
					metaData->GetMetaObject(m_typeDescription.GetTypeObject()), IMetaObjectRefValue);
			}

			if (metaObject == NULL) {
				categoryType->HideProperty("choice_form");
			}
			else {
				categoryType->ShowProperty("choice_form");
			}
		}
	}

	if (property->GetName() == wxT("source")
		|| property->GetName() == wxT("type")) {
		objectInspector->RefreshProperty();
	}

	IValueWindow::OnPropertyChanged(property);
}