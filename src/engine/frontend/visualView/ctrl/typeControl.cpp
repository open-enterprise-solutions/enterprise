#include "typeControl.h"
#include "backend/metaCollection/partial/object.h"
#include "backend/objCtor.h"
#include "backend/metaData.h"
#include "form.h"

////////////////////////////////////////////////////////////////////////////

void wxVariantSourceAttributeData::UpdateSourceAttribute()
{
	if (m_metaData != nullptr) {
		std::set<class_identifier_t> clsids;
		for (auto clsid : GetClsids()) {
			if (!m_metaData->IsRegisterCtor(clsid)) {
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
	if (m_metaData != nullptr && id != wxNOT_FOUND) {
		ISourceObject* srcData = m_formData->GetSourceObject();
		if (srcData != nullptr) {
			IMetaObjectGenericData* metaObject = srcData->GetSourceMetaObject();
			if (metaObject != nullptr && metaObject->IsAllowed() && id == metaObject->GetMetaID()) {
				ITypeWrapper::SetDefaultMetatype(srcData->GetSourceClassType());
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

	if (m_metaData != nullptr) {
		IMetaObject* metaObject = m_metaData->GetMetaObject(m_srcId);
		if (metaObject != nullptr &&
			!metaObject->IsAllowed()) {
			return _("<not selected>");
		}
		return metaObject->GetName();
	}

	return _("<not selected>");
}

////////////////////////////////////////////////////////////////////////////

meta_identifier_t ITypeControlAttribute::GetIdByGuid(const Guid& guid) const
{
	if (guid.isValid() && GetSourceObject()) {
		ISourceObject* srcObject = GetSourceObject();
		if (srcObject != nullptr) {
			IMetaObjectGenericData* objMetaValue =
				srcObject->GetSourceMetaObject();
			//wxASSERT(objMetaValue);
			IMetaObject* metaObject = objMetaValue ?
				objMetaValue->FindMetaObjectByID(guid) : nullptr;
			//wxASSERT(metaObject);
			return metaObject ? metaObject->GetMetaID() : wxNOT_FOUND;
		}
	}
	return wxNOT_FOUND;
}

Guid ITypeControlAttribute::GetGuidByID(const meta_identifier_t& id) const
{
	if (id != wxNOT_FOUND && GetSourceObject()) {
		ISourceObject* srcObject = GetSourceObject();
		if (srcObject != nullptr) {
			IMetaObjectGenericData* objMetaValue =
				srcObject->GetSourceMetaObject();
			//wxASSERT(objMetaValue);
			IMetaObject* metaObject = objMetaValue ?
				objMetaValue->FindMetaObjectByID(id) : nullptr;
			//wxASSERT(metaObject);
			return metaObject ? metaObject->GetGuid() : wxNullGuid;
		}
	}
	return wxNullGuid;
}

////////////////////////////////////////////////////////////////////////////

bool ITypeControlAttribute::LoadTypeData(CMemoryReader& dataReader)
{
	ClearAllMetatype();
	m_dataSource = dataReader.r_stringZ();
	return ITypeAttribute::LoadTypeData(dataReader);
}

bool ITypeControlAttribute::SaveTypeData(CMemoryWriter& dataWritter)
{
	dataWritter.w_stringZ(m_dataSource);
	return ITypeAttribute::SaveTypeData(dataWritter);;
}

////////////////////////////////////////////////////////////////////////////

bool ITypeControlAttribute::LoadFromVariant(const wxVariant& variant)
{
	wxVariantSourceData* srcData =
		dynamic_cast<wxVariantSourceData*>(variant.GetData());
	if (srcData == nullptr)
		return false;
	wxVariantSourceAttributeData* attrData = srcData->GetAttributeData();
	if (attrData == nullptr)
		return false;
	m_dataSource = GetGuidByID(srcData->GetSourceId());
	if (!m_dataSource.isValid()) {
		SetDefaultMetatype(attrData->GetTypeDescription());
	}
	return true;
}

void ITypeControlAttribute::SaveToVariant(wxVariant& variant, IMetaData* metaData) const
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

void ITypeControlAttribute::DoSetFromMetaId(const meta_identifier_t& id)
{
	IMetaData* metaData = GetMetaData();
	if (metaData != nullptr && id != wxNOT_FOUND) {
		ISourceObject* srcData = GetSourceObject();
		if (srcData != nullptr) {
			IMetaObjectGenericData* metaObject = srcData->GetSourceMetaObject();
			if (metaObject != nullptr && metaObject->IsAllowed() && id == metaObject->GetMetaID()) {
				ITypeWrapper::SetDefaultMetatype(srcData->GetSourceClassType());
				return;
			}
		}
		IMetaObjectAttribute* metaAttribute = nullptr;
		if (metaData->GetMetaObject(metaAttribute, id)) {
			if (metaAttribute != nullptr && metaAttribute->IsAllowed()) {
				ITypeWrapper::SetDefaultMetatype(
					metaAttribute->GetTypeDescription()
				);
				return;
			}
		}
		CMetaObjectTable* metaTable = nullptr;
		if (metaData->GetMetaObject(metaTable, id)) {
			if (metaTable != nullptr && metaTable->IsAllowed()) {
				ITypeWrapper::SetDefaultMetatype(
					metaTable->GetTypeDescription()
				);
				return;
			}
		}

		ITypeWrapper::SetDefaultMetatype(eValueTypes::TYPE_STRING);
	}
}

eSelectMode ITypeControlAttribute::GetSelectMode() const
{
	IMetaObjectAttribute* srcValue =
		dynamic_cast<IMetaObjectAttribute*>(GetMetaSource());
	if (srcValue != nullptr)
		return srcValue->GetSelectMode();
	return eSelectMode::eSelectMode_Items;
}

class_identifier_t ITypeControlAttribute::GetFirstClsid() const
{
	return ITypeAttribute::GetFirstClsid();
}

std::set<class_identifier_t> ITypeControlAttribute::GetClsids() const
{
	return ITypeAttribute::GetClsids();
}

////////////////////////////////////////////////////////////////////////////

IMetaObject* ITypeControlAttribute::GetMetaSource() const
{
	if (m_dataSource.isValid() && GetSourceObject()) {
		ISourceObject* srcObject = GetSourceObject();
		if (srcObject != nullptr) {
			IMetaObjectGenericData* objMetaValue =
				srcObject->GetSourceMetaObject();
			//wxASSERT(objMetaValue);
			return objMetaValue ? objMetaValue->FindMetaObjectByID(m_dataSource) : nullptr;
		}
	}

	return nullptr;
}

IMetaObject* ITypeControlAttribute::GetMetaObjectById(const class_identifier_t& clsid) const
{
	if (clsid == 0)
		return nullptr;
	IMetaData* metaData = GetMetaData();
	wxASSERT(metaData);
	IMetaValueTypeCtor* singleValue = metaData->GetTypeCtor(clsid);
	if (singleValue != nullptr)
		return singleValue->GetMetaObject();
	return nullptr;
}

void ITypeControlAttribute::SetSourceId(const meta_identifier_t& id)
{
	if (id != wxNOT_FOUND && GetSourceObject()) {
		ISourceObject* srcObject = GetSourceObject();
		if (srcObject != nullptr) {
			IMetaObjectGenericData* objMetaValue =
				srcObject->GetSourceMetaObject();
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

meta_identifier_t ITypeControlAttribute::GetSourceId() const
{
	return GetIdByGuid(m_dataSource);
}

void ITypeControlAttribute::ResetSource()
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

#include "frontend/win/ctrls/dynamicBorder.h"
#include "frontend/visualView/ctrl/frame.h"

bool ITypeControlAttribute::SimpleChoice(IControlFrame* ownerValue, const class_identifier_t& clsid, wxWindow* parent) {

	eValueTypes valType = CValue::GetVTByID(clsid);

	if (valType == eValueTypes::TYPE_NUMBER) {
		return true;
	}
	else if (valType == eValueTypes::TYPE_DATE) {
		class wxPopupDateTimeWindow : public wxPopupTransientWindow {
			wxCalendarCtrl* m_calendar = nullptr;
			wxTimePickerCtrl* m_timePicker = nullptr;
			IControlFrame* m_ownerValue = nullptr;
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

			virtual void Popup(wxWindow* focus = nullptr) override {
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
				if (m_ownerValue != nullptr)
					m_ownerValue->ChoiceProcessing(cDateTime);
				Dismiss();
			}
		};

		if (ownerValue != nullptr) {
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

bool ITypeControlAttribute::QuickChoice(IControlFrame* ownerValue, const class_identifier_t& clsid, wxWindow* parent)
{
	if (!ownerValue->HasQuickChoice())
		return false;

	if (ITypeControlAttribute::SimpleChoice(ownerValue, clsid, parent))
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
				wxListBox(parent, id, pos, size, 0, nullptr, style, validator, name)
			{
			}
		};

		std::map<int, CValue> m_values;

		IControlFrame* m_ownerValue = nullptr;
		wxQuickListBox* m_selListBox = nullptr;

	public:
		wxPopupQuickSelectWindow(IControlFrame* ownerValue, wxWindow* parent, int style = wxBORDER_NONE | wxPU_CONTAINS_CONTROLS | wxWANTS_CHARS) :
			wxPopupTransientWindow(parent, style), m_ownerValue(ownerValue) {

			SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_FRAMEBK));
			wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

			m_selListBox = new wxQuickListBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);

			IDynamicBorder* dynamicBorder = dynamic_cast<IDynamicBorder*>(parent);
			if (dynamicBorder != nullptr) {
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

			m_selListBox->Connect(wxEVT_KEY_DOWN, wxKeyEventHandler(wxPopupQuickSelectWindow::OnKeyDown), nullptr, this);
			m_selListBox->Connect(wxEVT_LEFT_DOWN, wxMouseEventHandler(wxPopupQuickSelectWindow::OnMouseDown), nullptr, this);

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

		virtual void Popup(wxWindow* focus = nullptr) override {
			IDynamicBorder* innerBorder = dynamic_cast<IDynamicBorder*>(m_parent);
			const wxSize& controlSize = (innerBorder != nullptr) ?
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
				if (m_ownerValue != nullptr)
					m_ownerValue->ChoiceProcessing(m_values[m_selListBox->GetSelection()]);
				Dismiss();
			}
			event.Skip();
		}

		void OnMouseDown(wxMouseEvent& event) {
			int selection = m_selListBox->HitTest(event.GetPosition());
			if (selection != wxNOT_FOUND) {
				if (m_ownerValue != nullptr)
					m_ownerValue->ChoiceProcessing(m_values[selection]);
				Dismiss();
			}
			event.Skip();
		}
	};

	if (ownerValue != nullptr) {
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

void ITypeControlAttribute::QuickChoice(IControlFrame* controlValue, CValue& newValue, wxWindow* parent, const wxString& strData)
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
				wxListBox(parent, id, pos, size, 0, nullptr, style, validator, name)
			{
			}
		};

		std::map<int, CValue> m_values;

		IControlFrame* m_controlValue = nullptr;
		wxQuickListBox* m_selListBox = nullptr;

		bool m_selected;

	public:

		wxPopupQuickSelectWindow(IControlFrame* controlValue, wxWindow* parent, int style = wxBORDER_NONE | wxPU_CONTAINS_CONTROLS | wxWANTS_CHARS) :
			wxPopupTransientWindow(parent, style), m_controlValue(controlValue), m_selected(false) {

			SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_FRAMEBK));
			wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

			m_selListBox = new wxQuickListBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);

			IDynamicBorder* dynamicBorder = dynamic_cast<IDynamicBorder*>(parent);
			if (dynamicBorder != nullptr) {
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

			m_selListBox->Connect(wxEVT_KEY_DOWN, wxKeyEventHandler(wxPopupQuickSelectWindow::OnKeyDown), nullptr, this);
			m_selListBox->Connect(wxEVT_LEFT_DOWN, wxMouseEventHandler(wxPopupQuickSelectWindow::OnMouseDown), nullptr, this);

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
				if (m_controlValue != nullptr && answer == wxYES) {
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

		virtual void Popup(wxWindow* focus = nullptr) override {
			IDynamicBorder* innerBorder = dynamic_cast<IDynamicBorder*>(m_parent);
			const wxSize& controlSize = (innerBorder != nullptr) ?
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
				if (m_controlValue != nullptr)
					m_controlValue->ChoiceProcessing(m_values[m_selListBox->GetSelection()]);
				Dismiss();
			}
			event.Skip();
		}

		void OnMouseDown(wxMouseEvent& event) {
			int selection = m_selListBox->HitTest(event.GetPosition());
			if (selection != wxNOT_FOUND) {
				m_selected = true;
				if (m_controlValue != nullptr)
					m_controlValue->ChoiceProcessing(m_values[selection]);
				Dismiss();
			}
			event.Skip();
		}
	};

	if (controlValue != nullptr) {
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

IMetaObjectGenericData* ITypeControlAttribute::GetMetaObject() const
{
	ISourceObject* sourceObject = GetSourceObject();
	return sourceObject != nullptr ?
		sourceObject->GetSourceMetaObject() : nullptr;
}

CValue ITypeControlAttribute::CreateValue() const
{
	return ITypeControlAttribute::CreateValueRef();
}

CValue* ITypeControlAttribute::CreateValueRef() const
{
	if (m_dataSource.isValid() && GetSourceObject()) {
		IMetaObjectGenericData* metaObjectValue = GetMetaObject();
		if (metaObjectValue != nullptr) {
			IMetaObjectAttribute* metaObject = wxDynamicCast(
				metaObjectValue->FindMetaObjectByID(m_dataSource), IMetaObjectAttribute
			);
			if (metaObject != nullptr)
				return metaObject->CreateValueRef();
		}
	}

	return ITypeAttribute::CreateValueRef();
}

class_identifier_t ITypeControlAttribute::GetDataType() const
{
	if (m_dataSource.isValid() && GetSourceObject()) {
		IMetaObjectGenericData* metaObjectValue = GetMetaObject();
		if (metaObjectValue != nullptr) {
			IMetaObjectAttribute* metaObject = wxDynamicCast(
				metaObjectValue->FindMetaObjectByID(m_dataSource), IMetaObjectAttribute
			);
			if (metaObject != nullptr) {
				return ShowSelectType(metaObject->GetMetaData(),
					metaObject->GetTypeDescription()
				);
			}
		}
	}

	return ShowSelectType(GetMetaData(), ITypeAttribute::GetTypeDescription());
}

#include "frontend/win/dlgs/selectData.h"

class_identifier_t ITypeControlAttribute::ShowSelectType(IMetaData* metaData, const typeDescription_t& typeDescription)
{
	if (typeDescription.GetClsidCount() < 2)
		return typeDescription.GetFirstClsid();
	
	CDialogSelectDataType *selectDataType =
		new CDialogSelectDataType(metaData, typeDescription.GetClsids());

	class_identifier_t clsid = 0;	
	if (selectDataType->ShowModal(clsid)) {
		selectDataType->Destroy();
		return clsid;
	}
	selectDataType->Destroy();
	return 0;
}