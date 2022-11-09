#include "attributeControl.h"
#include "metadata/metaObjects/objects/object.h"
#include "metadata/singleMetaTypes.h"
#include "metadata/metadata.h"
#include "form.h"

////////////////////////////////////////////////////////////////////////////

void wxVariantSourceAttributeData::UpdateSourceAttribute()
{
	if (m_metaData != NULL) {
		std::set<CLASS_ID> clsids;
		for (auto clsid : GetClsids()) {
			if (!m_metaData->IsRegisterObject(clsid)) {
				clsids.insert(clsid);
			}
		}
		for (auto clsid : clsids) {
			ClearMetatype(clsid);
		}
	}
}

void wxVariantSourceAttributeData::DoSetFromMetaId(const meta_identifier_t& id)
{
	if (m_metaData != NULL && id != wxNOT_FOUND) {
		ISourceDataObject* srcData = m_formData->GetSourceObject();
		if (srcData != NULL) {
			IMetaObjectWrapperData* metaObject = srcData->GetMetaObject();
			if (metaObject != NULL && metaObject->IsAllowed() && id == metaObject->GetMetaID()) {
				IAttributeWrapper::SetDefaultMetatype(srcData->GetClassType());
				return;
			}
		}
	}

	wxVariantAttributeData::DoSetFromMetaId(id);
}

wxString wxVariantSourceData::MakeString() const
{
	if (m_srcId == wxNOT_FOUND) {
		return _("<not selected>");
	}

	if (m_metaData != NULL) {
		IMetaObject* metaObject = m_metaData->GetMetaObject(m_srcId);
		if (metaObject != NULL &&
			!metaObject->IsAllowed()) {
			return _("<not selected>");
		}
		return metaObject->GetName();
	}

	return _("<not selected>");
}

////////////////////////////////////////////////////////////////////////////

meta_identifier_t IAttributeControl::GetIdByGuid(const Guid& guid) const
{
	if (guid.isValid() && GetSourceObject()) {
		ISourceDataObject* srcObject = GetSourceObject();
		if (srcObject != NULL) {
			IMetaObjectWrapperData* objMetaValue =
				srcObject->GetMetaObject();
			wxASSERT(objMetaValue);
			IMetaObject* metaObject = objMetaValue->FindMetaObjectByID(guid);
			wxASSERT(objMetaValue);
			return metaObject->GetMetaID();
		}
	}
	return wxNOT_FOUND;
}

Guid IAttributeControl::GetGuidByID(const meta_identifier_t& id) const
{
	if (id != wxNOT_FOUND && GetSourceObject()) {
		ISourceDataObject* srcObject = GetSourceObject();
		if (srcObject != NULL) {
			IMetaObjectWrapperData* objMetaValue =
				srcObject->GetMetaObject();
			wxASSERT(objMetaValue);
			IMetaObject* metaObject = objMetaValue->FindMetaObjectByID(id);
			wxASSERT(objMetaValue);
			return metaObject->GetGuid();
		}
	}
	return wxNullGuid;
}

////////////////////////////////////////////////////////////////////////////

bool IAttributeControl::LoadTypeData(CMemoryReader& dataReader)
{
	ClearAllMetatype();
	m_dataSource = dataReader.r_stringZ();
	return IAttributeInfo::LoadTypeData(dataReader);
}

bool IAttributeControl::SaveTypeData(CMemoryWriter& dataWritter)
{
	dataWritter.w_stringZ(m_dataSource);
	return IAttributeInfo::SaveTypeData(dataWritter);;
}

////////////////////////////////////////////////////////////////////////////

bool IAttributeControl::LoadFromVariant(const wxVariant& variant)
{
	wxVariantSourceData* srcData =
		dynamic_cast<wxVariantSourceData*>(variant.GetData());
	if (srcData == NULL)
		return false;
	wxVariantSourceAttributeData* attrData = srcData->GetAttributeData();
	if (attrData == NULL)
		return false;
	m_dataSource = GetGuidByID(srcData->GetSourceId());
	if (!m_dataSource.isValid()) {
		SetDefaultMetatype(
			attrData->GetClsids(), attrData->GetDescription()
		);
	}
	return true;
}

void IAttributeControl::SaveToVariant(wxVariant& variant, IMetadata* metaData) const
{
	wxVariantSourceData* srcData = new wxVariantSourceData(metaData, GetOwnerForm(), GetIdByGuid(m_dataSource));
	wxVariantSourceAttributeData* attrData = srcData->GetAttributeData();
	if (!m_dataSource.isValid()) {
		for (auto clsid : GetClsids()) {
			attrData->SetMetatype(clsid);
		}
		attrData->SetDescription(GetDescription());
	}
	variant = srcData;
}

////////////////////////////////////////////////////////////////////////////

void IAttributeControl::DoSetFromMetaId(const meta_identifier_t& id)
{
	IMetadata* metaData = GetMetadata();
	if (metaData != NULL && id != wxNOT_FOUND) {

		ISourceDataObject* srcData = GetSourceObject();
		if (srcData != NULL) {
			IMetaObjectWrapperData* metaObject = srcData->GetMetaObject();
			if (metaObject != NULL && metaObject->IsAllowed() && id == metaObject->GetMetaID()) {
				IAttributeWrapper::SetDefaultMetatype(srcData->GetClassType());
				return;
			}
		}

		IMetaAttributeObject* metaAttribute = NULL; 
		if (metaData->GetMetaObject(metaAttribute, id)) {
			if (metaAttribute != NULL && metaAttribute->IsAllowed()) {
				IAttributeWrapper::SetDefaultMetatype(
					metaAttribute->GetClsids(), metaAttribute->GetDescription()
				);
				return;
			}
		}

		CMetaTableObject* metaTable = NULL;
		if (metaData->GetMetaObject(metaTable, id)) {
			if (metaTable != NULL && metaTable->IsAllowed()) {
				IAttributeWrapper::SetDefaultMetatype(
					metaTable->GetClsidTable()
				);
				return;
			}
		}

		IAttributeWrapper::SetDefaultMetatype(eValueTypes::TYPE_STRING);
	}
}

CLASS_ID IAttributeControl::GetFirstClsid() const
{
	return IAttributeInfo::GetFirstClsid();
}

std::set<CLASS_ID> IAttributeControl::GetClsids() const
{
	return IAttributeInfo::GetClsids();
}

////////////////////////////////////////////////////////////////////////////

IMetaObject* IAttributeControl::GetMetaSource() const
{
	if (m_dataSource.isValid() && GetSourceObject()) {
		ISourceDataObject* srcObject = GetSourceObject();
		if (srcObject) {
			IMetaObjectWrapperData* objMetaValue =
				srcObject->GetMetaObject();
			wxASSERT(objMetaValue);
			return objMetaValue->FindMetaObjectByID(m_dataSource);
		}
	}

	return NULL;
}

IMetaObject* IAttributeControl::GetMetaObjectById(const CLASS_ID& clsid) const
{
	if (clsid == 0)
		return NULL;
	IMetadata* metaData = GetMetadata();
	wxASSERT(metaData);
	IMetaTypeObjectValueSingle* singleValue = metaData->GetTypeObject(clsid);
	if (singleValue != NULL)
		return singleValue->GetMetaObject();
	return NULL;
}

void IAttributeControl::SetSourceId(const meta_identifier_t& id)
{
	if (id != wxNOT_FOUND && GetSourceObject()) {
		ISourceDataObject* srcObject = GetSourceObject();
		if (srcObject != NULL) {
			IMetaObjectWrapperData* objMetaValue =
				srcObject->GetMetaObject();
			wxASSERT(objMetaValue);
			IMetaObject* metaObject = objMetaValue->FindMetaObjectByID(id);
			wxASSERT(objMetaValue);
			m_dataSource = metaObject->GetGuid();
		}
	}
	else {
		m_dataSource.reset();
	}

	DoSetFromMetaId(id);
}

meta_identifier_t IAttributeControl::GetSourceId() const
{
	return GetIdByGuid(m_dataSource);
}

void IAttributeControl::ResetSource()
{
	if (m_dataSource.isValid()) {
		wxASSERT(GetClsidCount() > 0);
		m_dataSource.reset();
	}
}

//////////////////////////////////////////////////////////////////////

#include <wx/calctrl.h>
#include <wx/timectrl.h>
#include <wx/popupwin.h>

#include "frontend/visualView/controls/baseFrame.h"

bool IAttributeControl::SelectSimpleValue(const CLASS_ID& clsid, wxWindow* parent) const {
	eValueTypes valType = CValue::GetVTByID(clsid);
	if (valType == eValueTypes::TYPE_DATE) {
		class wxPopupDateTimeWindow : public wxPopupTransientWindow {
			IValueFrame* m_ownerValue = NULL;
			wxCalendarCtrl* m_calendar = NULL;
			wxTimePickerCtrl* m_timePicker = NULL;
		public:

			wxPopupDateTimeWindow(IValueFrame* ownerValue, wxWindow* parent, int style = wxBORDER_NONE) :
				wxPopupTransientWindow(parent, style), m_ownerValue(ownerValue) {

				SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_FRAMEBK));

				wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
				wxBoxSizer* subSizer = new wxBoxSizer(wxHORIZONTAL);

				m_calendar = new wxCalendarCtrl(this, wxID_ANY, wxDefaultDateTime,
					wxDefaultPosition, wxDefaultSize, wxNO_BORDER);
				mainSizer->Add(m_calendar, wxSizerFlags().Expand().Border(wxALL, FromDIP(2)));

				m_timePicker = new wxTimePickerCtrl(this, wxID_ANY);
				subSizer->Add(m_timePicker, wxSizerFlags(3).Expand().Border(wxALL, FromDIP(2)));

				wxButton* OKButton = new wxButton(this, wxID_OK);
				OKButton->Bind(wxEVT_COMMAND_BUTTON_CLICKED, &wxPopupDateTimeWindow::OnOKButtonClicked, this);
				subSizer->Add(OKButton, wxSizerFlags(3).Expand().Border(wxALL, FromDIP(2)));

				mainSizer->Add(subSizer, wxSizerFlags().Expand());
				wxPopupTransientWindow::SetSizerAndFit(mainSizer);
			}

			// overridden base class virtuals
			virtual bool SetBackgroundColour(const wxColour& colour) {
				if (m_calendar)
					m_calendar->SetBackgroundColour(colour);
				if (m_timePicker)
					m_timePicker->SetBackgroundColour(colour);
				return wxPopupTransientWindow::SetBackgroundColour(colour);
			}

			virtual bool SetForegroundColour(const wxColour& colour) {
				if (m_calendar)
					m_calendar->SetForegroundColour(colour);
				if (m_timePicker)
					m_timePicker->SetForegroundColour(colour);
				return wxPopupTransientWindow::SetForegroundColour(colour);
			}

			virtual bool SetFont(const wxFont& font) {
				if (m_calendar)
					m_calendar->SetFont(font);
				if (m_timePicker)
					m_timePicker->SetFont(font);
				return wxPopupTransientWindow::SetFont(font);
			}

			virtual void Popup(wxWindow* focus = NULL) override {
				const CValue& vSelected = m_ownerValue->GetControlValue();
				wxPoint pos = m_parent->GetScreenPosition();
				pos.x += (m_parent->GetSize().x - GetSize().x + 2);
				pos.y += m_parent->GetSize().y;
				SetPosition(pos);
				const wxDateTime& dateTime = vSelected.ToDateTime();
				if (dateTime.GetYear() > 1600 &&
					dateTime.GetYear() <= 9999) {
					SetDateTime(dateTime);
				}
				wxPopupTransientWindow::Popup(focus);
			}

		private:

			wxDateTime GetDateTime() const {
				wxDateTime dateOnly, timeOnly;
				dateOnly = m_calendar->GetDate();
				wxCHECK(dateOnly.IsValid(), wxInvalidDateTime);
				timeOnly = m_timePicker->GetValue();
				wxCHECK(timeOnly.IsValid(), wxInvalidDateTime);
				return wxDateTime(dateOnly.GetDay(), dateOnly.GetMonth(), dateOnly.GetYear(),
					timeOnly.GetHour(), timeOnly.GetMinute(), timeOnly.GetSecond());
			}

			void SetDateTime(const wxDateTime& dateTime) {
				m_calendar->SetDate(dateTime);
				m_timePicker->SetValue(dateTime);
			}

			void OnOKButtonClicked(wxCommandEvent&) {
				if (m_ownerValue != NULL)
					m_ownerValue->ChoiceProcessing(CValue(GetDateTime()));
				Dismiss();
			}
		};

		IValueFrame* ownerValue = dynamic_cast<IValueFrame*>(
			const_cast<IAttributeControl*>(this));
		if (ownerValue != NULL) {
			wxPopupDateTimeWindow* popup =
				new wxPopupDateTimeWindow(ownerValue, parent);
			popup->Popup();
		}
		return true;
	}

	return false;
}

//////////////////////////////////////////////////////////////////////

IMetaObjectWrapperData* IAttributeControl::GetMetaObject() const
{
	ISourceDataObject* sourceObject = GetSourceObject();
	return sourceObject ?
		sourceObject->GetMetaObject() : NULL;
}

CValue IAttributeControl::CreateValue() const
{
	return IAttributeControl::CreateValueRef();
}


CValue *IAttributeControl::CreateValueRef() const
{
	if (m_dataSource.isValid() && GetSourceObject()) {
		IMetaObjectWrapperData* metaObjectValue =
			IAttributeControl::GetMetaObject();
		IMetaAttributeObject* metaObject = NULL;
		if (metaObjectValue != NULL) {
			metaObject = wxDynamicCast(
				metaObjectValue->FindMetaObjectByID(m_dataSource), IMetaAttributeObject
			);
		}
		if (metaObject != NULL) {
			return metaObject->CreateValueRef();
		}
	}

	return IAttributeInfo::CreateValueRef();
}

CLASS_ID IAttributeControl::GetDataType() const
{
	if (m_dataSource.isValid() && GetSourceObject()) {
		IMetaObjectWrapperData* metaObjectValue =
			IAttributeControl::GetMetaObject();
		IMetaAttributeObject* metaObject = NULL;
		if (metaObjectValue != NULL) {
			metaObject = wxDynamicCast(
				metaObjectValue->FindMetaObjectByID(m_dataSource), IMetaAttributeObject
			);
		}
		if (metaObject != NULL) {
			return metaObject->GetDataType();
		}
	}

	return IAttributeInfo::GetDataType();
}