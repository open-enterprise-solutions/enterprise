#include "controlAttribute.h"
#include "core/metadata/metaObjects/objects/object.h"
#include "core/metadata/singleClass.h"
#include "core/metadata/metadata.h"
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
		ISourceObject* srcData = m_formData->GetSourceObject();
		if (srcData != NULL) {
			IMetaObjectWrapperData* metaObject = srcData->GetMetaObject();
			if (metaObject != NULL && metaObject->IsAllowed() && id == metaObject->GetMetaID()) {
				ITypeWrapper::SetDefaultMetatype(srcData->GetTypeClass());
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

meta_identifier_t ITypeControl::GetIdByGuid(const Guid& guid) const
{
	if (guid.isValid() && GetSourceObject()) {
		ISourceObject* srcObject = GetSourceObject();
		if (srcObject != NULL) {
			IMetaObjectWrapperData* objMetaValue =
				srcObject->GetMetaObject();
			wxASSERT(objMetaValue);
			IMetaObject* metaObject = objMetaValue ?
				objMetaValue->FindMetaObjectByID(guid) : NULL;
			wxASSERT(metaObject);
			return metaObject ? metaObject->GetMetaID() : wxNOT_FOUND;
		}
	}
	return wxNOT_FOUND;
}

Guid ITypeControl::GetGuidByID(const meta_identifier_t& id) const
{
	if (id != wxNOT_FOUND && GetSourceObject()) {
		ISourceObject* srcObject = GetSourceObject();
		if (srcObject != NULL) {
			IMetaObjectWrapperData* objMetaValue =
				srcObject->GetMetaObject();
			wxASSERT(objMetaValue);
			IMetaObject* metaObject = objMetaValue ?
				objMetaValue->FindMetaObjectByID(id) : NULL;
			wxASSERT(metaObject);
			return metaObject ? metaObject->GetGuid() : wxNullGuid;
		}
	}
	return wxNullGuid;
}

////////////////////////////////////////////////////////////////////////////

bool ITypeControl::LoadTypeData(CMemoryReader& dataReader)
{
	ClearAllMetatype();
	m_dataSource = dataReader.r_stringZ();
	return ITypeAttribute::LoadTypeData(dataReader);
}

bool ITypeControl::SaveTypeData(CMemoryWriter& dataWritter)
{
	dataWritter.w_stringZ(m_dataSource);
	return ITypeAttribute::SaveTypeData(dataWritter);;
}

////////////////////////////////////////////////////////////////////////////

bool ITypeControl::LoadFromVariant(const wxVariant& variant)
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
		SetDefaultMetatype(attrData->GetTypeDescription());
	}
	return true;
}

void ITypeControl::SaveToVariant(wxVariant& variant, IMetadata* metaData) const
{
	wxVariantSourceData* srcData = new wxVariantSourceData(metaData, GetOwnerForm(), GetIdByGuid(m_dataSource));
	wxVariantSourceAttributeData* attrData = srcData->GetAttributeData();
	if (!m_dataSource.isValid()) {
		for (auto clsid : GetClsids()) {
			attrData->SetMetatype(clsid);
		}
		attrData->SetTypeDescription(GetTypeDescription());
	}
	variant = srcData;
}

////////////////////////////////////////////////////////////////////////////

void ITypeControl::DoSetFromMetaId(const meta_identifier_t& id)
{
	IMetadata* metaData = GetMetadata();
	if (metaData != NULL && id != wxNOT_FOUND) {
		ISourceObject* srcData = GetSourceObject();
		if (srcData != NULL) {
			IMetaObjectWrapperData* metaObject = srcData->GetMetaObject();
			if (metaObject != NULL && metaObject->IsAllowed() && id == metaObject->GetMetaID()) {
				ITypeWrapper::SetDefaultMetatype(srcData->GetTypeClass());
				return;
			}
		}
		IMetaAttributeObject* metaAttribute = NULL;
		if (metaData->GetMetaObject(metaAttribute, id)) {
			if (metaAttribute != NULL && metaAttribute->IsAllowed()) {
				ITypeWrapper::SetDefaultMetatype(
					metaAttribute->GetTypeDescription()
				);
				return;
			}
		}
		CMetaTableObject* metaTable = NULL;
		if (metaData->GetMetaObject(metaTable, id)) {
			if (metaTable != NULL && metaTable->IsAllowed()) {
				ITypeWrapper::SetDefaultMetatype(
					metaTable->GetTypeDescription()
				);
				return;
			}
		}

		ITypeWrapper::SetDefaultMetatype(eValueTypes::TYPE_STRING);
	}
}

eSelectMode ITypeControl::GetSelectMode() const
{
	IMetaAttributeObject* srcValue =
		dynamic_cast<IMetaAttributeObject*>(GetMetaSource());
	if (srcValue != NULL)
		return srcValue->GetSelectMode();
	return eSelectMode::eSelectMode_Items;
}

CLASS_ID ITypeControl::GetFirstClsid() const
{
	return ITypeAttribute::GetFirstClsid();
}

std::set<CLASS_ID> ITypeControl::GetClsids() const
{
	return ITypeAttribute::GetClsids();
}

////////////////////////////////////////////////////////////////////////////

IMetaObject* ITypeControl::GetMetaSource() const
{
	if (m_dataSource.isValid() && GetSourceObject()) {
		ISourceObject* srcObject = GetSourceObject();
		if (srcObject != NULL) {
			IMetaObjectWrapperData* objMetaValue =
				srcObject->GetMetaObject();
			wxASSERT(objMetaValue);
			return objMetaValue ? objMetaValue->FindMetaObjectByID(m_dataSource) : NULL;
		}
	}

	return NULL;
}

IMetaObject* ITypeControl::GetMetaObjectById(const CLASS_ID& clsid) const
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

void ITypeControl::SetSourceId(const meta_identifier_t& id)
{
	if (id != wxNOT_FOUND && GetSourceObject()) {
		ISourceObject* srcObject = GetSourceObject();
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

meta_identifier_t ITypeControl::GetSourceId() const
{
	return GetIdByGuid(m_dataSource);
}

void ITypeControl::ResetSource()
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

#include "frontend/controls/dynamicBorder.h"
#include "frontend/visualView/controls/frameInterface.h"

bool ITypeControl::SimpleChoice(IControlFrame* ownerValue, const CLASS_ID& clsid, wxWindow* parent) {

	eValueTypes valType = CValue::GetVTByID(clsid);

	if (valType == eValueTypes::TYPE_NUMBER) {
		return true;
	}
	else if (valType == eValueTypes::TYPE_DATE) {
		class wxPopupDateTimeWindow : public wxPopupTransientWindow {
			wxCalendarCtrl* m_calendar = NULL;
			wxTimePickerCtrl* m_timePicker = NULL;
			IControlFrame* m_ownerValue = NULL;
		public:

			wxPopupDateTimeWindow(IControlFrame* ownerValue, wxWindow* parent, int style = wxBORDER_NONE | wxPU_CONTAINS_CONTROLS | wxWANTS_CHARS) :
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
				CValue vSelected; m_ownerValue->GetControlValue(vSelected);
				wxPoint pos = m_parent->GetScreenPosition();
				pos.x += (m_parent->GetSize().x - GetSize().x + 2);
				pos.y += (m_parent->GetSize().y);
				wxPopupTransientWindow::SetPosition(pos);
				const wxDateTime& dateTime = vSelected.GetDateTime();
				if (dateTime.GetYear() > 1600 &&
					dateTime.GetYear() <= 9999) {
					SetDateTime(dateTime);
				}
				wxPopupTransientWindow::Popup(focus);
				m_calendar->SetFocus();
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
				CValue cDateTime = GetDateTime();
				if (m_ownerValue != NULL)
					m_ownerValue->ChoiceProcessing(cDateTime);
				Dismiss();
			}
		};

		if (ownerValue != NULL) {
			wxPopupDateTimeWindow* popup =
				new wxPopupDateTimeWindow(ownerValue, parent);
			popup->Popup();
		}
		return true;
	}
	else if (valType == eValueTypes::TYPE_STRING) {
		return true;
	}

	return false;
}

bool ITypeControl::QuickChoice(IControlFrame* ownerValue, const CLASS_ID& clsid, wxWindow* parent)
{
	if (!ownerValue->HasQuickChoice())
		return false;

	if (ITypeControl::SimpleChoice(ownerValue, clsid, parent))
		return true;

	class wxPopupQuickSelectWindow : public wxPopupTransientWindow {

		class wxQuickListBox : public wxListBox {

		public:

			wxQuickListBox(
				wxWindow* parent,
				wxWindowID  	    id,
				const wxPoint& pos = wxDefaultPosition,
				const wxSize& size = wxDefaultSize,
				long  	            style = wxLB_SINGLE,
				const wxValidator& validator = wxDefaultValidator,
				const wxString& name = wxListBoxNameStr) :
				wxListBox(parent, id, pos, size, 0, NULL, style, validator, name)
			{
			}
		};

		std::map<int, CValue> m_values;

		IControlFrame* m_ownerValue = NULL;
		wxQuickListBox* m_selListBox = NULL;

	public:
		wxPopupQuickSelectWindow(IControlFrame* ownerValue, wxWindow* parent, int style = wxBORDER_NONE | wxPU_CONTAINS_CONTROLS | wxWANTS_CHARS) :
			wxPopupTransientWindow(parent, style), m_ownerValue(ownerValue) {

			SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_FRAMEBK));
			wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

			m_selListBox = new wxQuickListBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);

			IDynamicBorder* dynamicBorder = dynamic_cast<IDynamicBorder*>(parent);
			if (dynamicBorder != NULL) {
				wxWindow* innerControl = dynamicBorder->GetControl();
				wxASSERT(innerControl);
				m_selListBox->SetBackgroundColour(innerControl->GetBackgroundColour());
				m_selListBox->SetForegroundColour(innerControl->GetForegroundColour());
				m_selListBox->SetFont(innerControl->GetFont());
			}
			else {
				m_selListBox->SetBackgroundColour(parent->GetBackgroundColour());
				m_selListBox->SetForegroundColour(parent->GetForegroundColour());
				m_selListBox->SetFont(parent->GetFont());
			}

			m_selListBox->Connect(wxEVT_KEY_DOWN, wxKeyEventHandler(wxPopupQuickSelectWindow::OnKeyDown), NULL, this);
			m_selListBox->Connect(wxEVT_LEFT_DOWN, wxMouseEventHandler(wxPopupQuickSelectWindow::OnMouseDown), NULL, this);

			mainSizer->Add(m_selListBox, wxSizerFlags().Expand());
			wxPopupTransientWindow::SetSizerAndFit(mainSizer);
		}

		virtual void Dismiss() wxOVERRIDE {
			wxPopupTransientWindow::Dismiss();
		}

		// overridden base class virtuals
		virtual bool SetBackgroundColour(const wxColour& colour) {
			if (m_selListBox)
				m_selListBox->SetBackgroundColour(colour);
			return wxPopupTransientWindow::SetBackgroundColour(colour);
		}

		virtual bool SetForegroundColour(const wxColour& colour) {
			if (m_selListBox)
				m_selListBox->SetForegroundColour(colour);
			return wxPopupTransientWindow::SetForegroundColour(colour);
		}

		virtual bool SetFont(const wxFont& font) {
			if (m_selListBox)
				m_selListBox->SetFont(font);
			return wxPopupTransientWindow::SetFont(font);
		}

		virtual void Popup(wxWindow* focus = NULL) override {
			IDynamicBorder* innerBorder = dynamic_cast<IDynamicBorder*>(m_parent);
			const wxSize& controlSize = (innerBorder != NULL) ?
				innerBorder->GetControlSize() : m_parent->GetSize();
			if (m_selListBox->GetCount() > 5)
				wxPopupTransientWindow::SetSize(wxSize(controlSize.x, (m_selListBox->GetCharHeight() * 5) + 4));
			else
				wxPopupTransientWindow::SetSize(wxSize(controlSize.x, (m_selListBox->GetCharHeight() * m_selListBox->GetCount()) + 4));

			wxPoint pos = m_parent->GetScreenPosition();
			pos.x += (m_parent->GetSize().x - GetSize().x);
			pos.y += (m_parent->GetSize().y - 1);
			wxPopupTransientWindow::Layout();
			wxPopupTransientWindow::SetPosition(pos);
			wxPopupTransientWindow::Popup(focus);
			m_selListBox->SetFocus();
		}

		void AppendItem(const CValue& item, bool select = false) {
			int sel = m_selListBox->Append(item.GetString());
			if (select)
				m_selListBox->Select(sel);
			m_values.insert_or_assign(sel, item);
		}

	protected:

		void OnKeyDown(wxKeyEvent& event) {
#if wxUSE_UNICODE
			const wxChar charcode = event.GetUnicodeKey();
#else
			const wxChar charcode = (wxChar)event.GetKeyCode();
#endif
			if (charcode == WXK_RETURN) {
				if (m_ownerValue != NULL)
					m_ownerValue->ChoiceProcessing(m_values[m_selListBox->GetSelection()]);
				Dismiss();
			}
			event.Skip();
		}

		void OnMouseDown(wxMouseEvent& event) {
			int selection = m_selListBox->HitTest(event.GetPosition());
			if (selection != wxNOT_FOUND) {
				if (m_ownerValue != NULL)
					m_ownerValue->ChoiceProcessing(m_values[selection]);
				Dismiss();
			}
			event.Skip();
		}
	};

	if (ownerValue != NULL) {
		CValue cValue; ownerValue->GetControlValue(cValue);
		std::vector<CValue> foundedObjects;
		if (cValue.FindValue(wxEmptyString, foundedObjects)) {
			wxPopupQuickSelectWindow* popup =
				new wxPopupQuickSelectWindow(ownerValue, parent);
			for (auto selObj : foundedObjects)
				popup->AppendItem(selObj, selObj == cValue);
			popup->Popup();
			return true;
		}
	}
	return false;
}

void ITypeControl::QuickChoice(IControlFrame* controlValue, CValue& newValue, wxWindow* parent, const wxString& strData)
{
	class wxPopupQuickSelectWindow : public wxPopupTransientWindow {

		class wxQuickListBox : public wxListBox {

		public:

			wxQuickListBox(
				wxWindow* parent,
				wxWindowID  	    id,
				const wxPoint& pos = wxDefaultPosition,
				const wxSize& size = wxDefaultSize,
				long  	            style = wxLB_SINGLE,
				const wxValidator& validator = wxDefaultValidator,
				const wxString& name = wxListBoxNameStr) :
				wxListBox(parent, id, pos, size, 0, NULL, style, validator, name)
			{
			}
		};

		std::map<int, CValue> m_values;

		IControlFrame* m_controlValue = NULL;
		wxQuickListBox* m_selListBox = NULL;

		bool m_selected;

	public:

		wxPopupQuickSelectWindow(IControlFrame* controlValue, wxWindow* parent, int style = wxBORDER_NONE | wxPU_CONTAINS_CONTROLS | wxWANTS_CHARS) :
			wxPopupTransientWindow(parent, style), m_controlValue(controlValue), m_selected(false) {

			SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_FRAMEBK));
			wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

			m_selListBox = new wxQuickListBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);

			IDynamicBorder* dynamicBorder = dynamic_cast<IDynamicBorder*>(parent);
			if (dynamicBorder != NULL) {
				wxWindow* innerControl = dynamicBorder->GetControl();
				wxASSERT(innerControl);
				m_selListBox->SetBackgroundColour(innerControl->GetBackgroundColour());
				m_selListBox->SetForegroundColour(innerControl->GetForegroundColour());
				m_selListBox->SetFont(innerControl->GetFont());
			}
			else {
				m_selListBox->SetBackgroundColour(parent->GetBackgroundColour());
				m_selListBox->SetForegroundColour(parent->GetForegroundColour());
				m_selListBox->SetFont(parent->GetFont());
			}

			m_selListBox->Connect(wxEVT_KEY_DOWN, wxKeyEventHandler(wxPopupQuickSelectWindow::OnKeyDown), NULL, this);
			m_selListBox->Connect(wxEVT_LEFT_DOWN, wxMouseEventHandler(wxPopupQuickSelectWindow::OnMouseDown), NULL, this);

			mainSizer->Add(m_selListBox, wxSizerFlags().Expand());
			wxPopupTransientWindow::SetSizerAndFit(mainSizer);
		}

		virtual void Dismiss() wxOVERRIDE {
			if (!m_selected) {
				wxPopupTransientWindow::Dismiss();
				int answer = wxMessageBox(
					_("Incorrect data entered into field. Do you want to cancel?"),
					_("Enterprise"),
					wxYES_NO | wxCENTRE | wxICON_QUESTION, m_parent
				);
				if (m_controlValue != NULL && answer == wxYES) {
					CValue retValue;
					if (m_controlValue->GetControlValue(retValue))
						m_controlValue->ChoiceProcessing(retValue);
				}
				else {
					wxPopupTransientWindow::Show();
					m_selListBox->SetFocus();
				}
			}
			else {
				wxPopupTransientWindow::Dismiss();
			}
		}

		// overridden base class virtuals
		virtual bool SetBackgroundColour(const wxColour& colour) {
			if (m_selListBox)
				m_selListBox->SetBackgroundColour(colour);
			return wxPopupTransientWindow::SetBackgroundColour(colour);
		}

		virtual bool SetForegroundColour(const wxColour& colour) {
			if (m_selListBox)
				m_selListBox->SetForegroundColour(colour);
			return wxPopupTransientWindow::SetForegroundColour(colour);
		}

		virtual bool SetFont(const wxFont& font) {
			if (m_selListBox)
				m_selListBox->SetFont(font);
			return wxPopupTransientWindow::SetFont(font);
		}

		virtual void Popup(wxWindow* focus = NULL) override {
			IDynamicBorder* innerBorder = dynamic_cast<IDynamicBorder*>(m_parent);
			const wxSize& controlSize = (innerBorder != NULL) ?
				innerBorder->GetControlSize() : m_parent->GetSize();
			if (m_selListBox->GetCount() > 5)
				wxPopupTransientWindow::SetSize(wxSize(controlSize.x, (m_selListBox->GetCharHeight() * 5) + 4));
			else
				wxPopupTransientWindow::SetSize(wxSize(controlSize.x, (m_selListBox->GetCharHeight() * m_selListBox->GetCount()) + 4));

			wxPoint pos = m_parent->GetScreenPosition();
			pos.x += (m_parent->GetSize().x - GetSize().x);
			pos.y += (m_parent->GetSize().y - 1);
			wxPopupTransientWindow::Layout();
			wxPopupTransientWindow::SetPosition(pos);
			wxPopupTransientWindow::Popup(focus);
			m_selListBox->SetFocus();
		}

		void AppendItem(const CValue& item, bool select = false) {
			int sel = m_selListBox->Append(item.GetString());
			if (select)
				m_selListBox->Select(sel);
			m_values.insert_or_assign(sel, item);
		}

	protected:

		void OnKeyDown(wxKeyEvent& event) {
#if wxUSE_UNICODE
			const wxChar charcode = event.GetUnicodeKey();
#else
			const wxChar charcode = (wxChar)event.GetKeyCode();
#endif
			if (charcode == WXK_RETURN) {
				m_selected = true;
				if (m_controlValue != NULL)
					m_controlValue->ChoiceProcessing(m_values[m_selListBox->GetSelection()]);
				Dismiss();
			}
			event.Skip();
		}

		void OnMouseDown(wxMouseEvent& event) {
			int selection = m_selListBox->HitTest(event.GetPosition());
			if (selection != wxNOT_FOUND) {
				m_selected = true;
				if (m_controlValue != NULL)
					m_controlValue->ChoiceProcessing(m_values[selection]);
				Dismiss();
			}
			event.Skip();
		}
	};

	if (controlValue != NULL) {
		if (strData.Length() > 0) {
			std::vector<CValue> foundedObjects;
			if (newValue.FindValue(strData, foundedObjects)) {
				size_t count = foundedObjects.size();
				if (count > 1) {
					wxPopupQuickSelectWindow* popup =
						new wxPopupQuickSelectWindow(controlValue, parent);
					for (auto selObj : foundedObjects)
						popup->AppendItem(selObj, selObj == newValue);
					popup->Popup();
				}
				else if (count == 1) {
					controlValue->ChoiceProcessing(foundedObjects.at(0));
				}
			}
		}
		else {
			controlValue->ChoiceProcessing(newValue);
		}
	}
}

/////////////////////////////////////////////////////////////////////

IMetaObjectWrapperData* ITypeControl::GetMetaObject() const
{
	ISourceObject* sourceObject = GetSourceObject();
	return sourceObject != NULL ?
		sourceObject->GetMetaObject() : NULL;
}

CValue ITypeControl::CreateValue() const
{
	return ITypeControl::CreateValueRef();
}

CValue* ITypeControl::CreateValueRef() const
{
	if (m_dataSource.isValid() && GetSourceObject()) {
		IMetaObjectWrapperData* metaObjectValue = GetMetaObject();
		if (metaObjectValue != NULL) {
			IMetaAttributeObject* metaObject = wxDynamicCast(
				metaObjectValue->FindMetaObjectByID(m_dataSource), IMetaAttributeObject
			);
			if (metaObject != NULL)
				return metaObject->CreateValueRef();
		}
	}

	return ITypeAttribute::CreateValueRef();
}

CLASS_ID ITypeControl::GetDataType() const
{
	if (m_dataSource.isValid() && GetSourceObject()) {
		IMetaObjectWrapperData* metaObjectValue = GetMetaObject();
		if (metaObjectValue != NULL) {
			IMetaAttributeObject* metaObject = wxDynamicCast(
				metaObjectValue->FindMetaObjectByID(m_dataSource), IMetaAttributeObject
			);
			if (metaObject != NULL)
				return metaObject->GetDataType();
		}
	}

	return ITypeAttribute::GetDataType();
}