#include "typeControl.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/objCtor.h"
#include "backend/metaData.h"

////////////////////////////////////////////////////////////////////////////

#include <wx/calctrl.h>
#include <wx/timectrl.h>
#include <wx/popupwin.h>


#include <map>

#include "frontend/win/ctrls/dynamicBorder.h"
#include "frontend/visualView/ctrl/frame.h"
#include "frontend/win/dlgs/typeSelector.h"          // the shared type picker — second caller
#include "backend/system/value/valueType.h"          // ibValueTypeDescription / g_valueTypeDescriptionCLSID
#include "backend/metaCollection/partial/chartOfCharacteristicTypes.h"   // the CONTOUR that narrows the picker
#include "backend/metaCollection/partial/reference/reference.h"          // a reference built on a predefined guid
#include "frontend/win/dlgs/selectPredefined.h"      // the designer's declared-value window — one call, no widgets here

#include "backend/appData.h"                                             // DesignerMode — the two roads part here
#include "backend/system/systemManager.h"                                // Message — "nothing is declared" is an answer

bool ibTypeControlFactory::ChooseValue(ibControlFrame* ownerValue,
	const ibValueMetaObject* choiceForm, wxWindow* parent)
{
	ibTypeControlFactory* factory = dynamic_cast<ibTypeControlFactory*>(ownerValue);
	if (ownerValue == nullptr || factory == nullptr)
		return false;

	ibValue current;
	ownerValue->GetControlValue(current);

	// UNDEFINED = the type is not settled yet. GetDataType answers it — from the
	// metadata by default, asking the user only when the cell admits more than one
	// type, and overridden outright by a cell that already knows (a filter's left
	// side is always a field).
	if (current.GetType() == ibValueTypes::TYPE_EMPTY) {

		// ⭐⭐ …EXCEPT IN THE DESIGNER, WHERE THE TWO QUESTIONS ARE ONE WINDOW. Settling the type
		// through the picker and then choosing a value of it costs two clicks and two modals — and
		// the modal below destroys the editor, which is why the type choice has to end the call. The
		// designer's own window asks BOTH on its two pages, so an empty cell reaches a value in one
		// click ("I want to press the three dots and click the empty reference, or pick a predefined
		// value there" — Max, 2026-08-28).
		//
		// ⚠ THE DECLARATION GOES STRAIGHT IN. Whether anything referenceable is admitted is the
		// WINDOW's question — it answers `false` for a declaration with none, and the road below
		// carries on unchanged. Deciding it here as well would be the same test in two places, and
		// the one that could drift is this one.
		if (appData->DesignerMode()
			&& ibShowPredefinedSelector(ownerValue, factory->GetTypeDesc(), factory->GetMetaData(), parent))
			return true;

		const ibClassID clsid = factory->GetDataType();
		const ibMetaData* metaData = factory->GetMetaData();
		if (clsid == 0 || metaData == nullptr || !metaData->IsRegisterCtor(clsid))
			return false;   // the user closed the type choice
		current = metaData->CreateObject(clsid);
		ownerValue->SetControlValue(current);

		// AND THE CHOICE ENDS HERE. Settling the type is a MODAL question, and a modal runs an event
		// loop of its own: while it is up the grid finishes editing this cell and destroys the editor
		// control - which is the window handed to us as `parent`. Carrying on in the same call opened
		// the value chooser parented to freed memory, and it died inside wxGetTopLevelParent with a
		// stack pointing at the popup rather than at the modal that invalidated its parent.
		//
		// So the type is settled, said and returned; the value is chosen on the NEXT click, by which
		// time there is a live editor to hang it on. No window pointer outlives a modal here, which is
		// the rule rather than this one repair - the previous line ("keep going, the editor opens now,
		// not on a second click") described a convenience the lifetime does not allow.
		return true;
	}

	// THE VALUE OF THAT TYPE: the built-in quick choice first (it knows a boolean,
	// an enumeration, a reference), then the metaobject's own selection form.
	const ibClassID clsid = current.GetClassType();

	// A TYPE DESCRIPTION is edited by the type picker — the same dialog the metadata editor opens,
	// reached here through the ordinary Select button. What may be chosen is NOT decided here: the
	// permitted set comes from the field, which is how a characteristic offers only what its chart
	// declares, and a filter's right side only what its left side admits.
	if (clsid == g_valueTypeDescriptionCLSID) {
		ibValueTypeDescription* typeValue = nullptr;
		if (current.ConvertToValue(typeValue) && typeValue != nullptr) {

			// A CHARACTERISTIC IS ALWAYS THE REFERENCE SHAPE — everything referenceable plus the
			// primitives — NARROWED BY THE CHART THAT OWNS THE FIELD.
			//
			// The filter is the chart's own composition (TypesOfCharacteristics): a characteristic
			// may only ever be one of the things its chart declares. Not the FIELD's type — that one
			// says "this cell holds a type description", which is true and useless as a filter: it
			// intersects the offered list to nothing.
			//
			// The chart is reached the way anything reaches its owner here: the bound attribute
			// knows its parent metaobject. A field with no such owner (a filter cell, a script
			// variable) passes no filter and gets the whole shape, which is the honest answer.
			std::vector<ibClassID> contour;
			if (const ibValueMetaObjectAttributeBase* attr =
				dynamic_cast<const ibValueMetaObjectAttributeBase*>(factory->GetSourceAttributeObject())) {
				if (const ibValueMetaObjectChartOfCharacteristicTypes* chart =
					dynamic_cast<const ibValueMetaObjectChartOfCharacteristicTypes*>(attr->GetParent()))
					contour = chart->GetTypesOfCharacteristics().GetClsidList();
			}

			if (ibShowTypeSelector(parent, ibSelectorDataType::ibSelectorDataType_reference,
				contour, typeValue->m_typeDesc, factory->GetMetaData())) {
				ownerValue->SetControlValue(current);
				return true;
			}
		}
		return false;
	}

	const ibMetaData* metaData = factory->GetMetaData();

	// ⭐⭐ IN THE DESIGNER EVERY REFERENCE WALKS THE ONE FORM — an ENUMERATION included. The quick
	// choice below drops a list of the enum's members under the cell, which is the RUNTIME answer:
	// there it is a value chosen from a small closed set. While a configuration is being written the
	// same question is "which declared value is this", and it is asked the same way for a catalog, a
	// document and an enumeration — one window, one habit (Max, 2026-08-28: "the designer has no
	// separate drop-down for enums").
	//
	// The declaration goes in as it is; whether it admits a reference at all is worked out THERE,
	// from the very structure being handed over. A cell that declares nothing is standing on the type
	// it already holds, so that is what it offers.
	if (appData->DesignerMode()) {
		const ibTypeDescription& declared = factory->GetTypeDesc();
		if (ibShowPredefinedSelector(ownerValue,
				declared.GetClsidList().empty() ? ibTypeDescription(clsid) : declared, metaData, parent))
			return true;
	}

	if (ibTypeControlFactory::QuickChoice(ownerValue, clsid, parent))
		return true;

	const ibCtorMetaValueType* so = metaData != nullptr ? metaData->GetTypeCtor(clsid) : nullptr;
	if (so != nullptr && so->GetMetaTypeCtor() == ibCtorObjectMetaType_Reference) {
		if (const ibValueMetaObject* metaObject = so->GetMetaObject()) {
			// ⭐⭐ TWO MODES, AND THE DESIGNER IS THE OTHER ONE. At run time a reference is chosen from
			// the DATA — the metaobject's own selection form, which is what the line below opens. In the
			// designer there is no data yet, so that form has nothing to show and the button did
			// nothing at all (Max, 2026-08-28: "in the designer such a form has to be made, because
			// right now nothing opens there").
			//
			// What a configuration CAN offer while it is being written is what it DECLARES: its
			// predefined values. So the same "…" opens them instead — the type is already settled by
			// this point, and the choice is which of that type's declared values this is.
			//
			// The type the window offers is what the CELL declares; a cell that declares nothing —
			// a form control bound to one attribute — is already standing on its type, so the value
			// it holds is the whole offer. Asked ABOVE, before the quick choice, so an enumeration
			// takes this road too.
			return metaObject->ProcessChoice(ownerValue,
				choiceForm != nullptr ? choiceForm->GetName() : wxString(), factory->GetSelectMode());
		}
	}
	return false;
}

bool ibTypeControlFactory::SimpleChoice(ibControlFrame* ownerValue, const ibClassID& clsid, wxWindow* parent) {

	ibValueTypes valType = ibValue::GetVTByID(clsid);

	if (valType == ibValueTypes::TYPE_NUMBER) {
		return true;
	}
	else if (valType == ibValueTypes::TYPE_DATE) {
		class wxPopupDateTimeWindow : public wxPopupTransientWindow {
			wxCalendarCtrl* m_calendar = nullptr;
			wxTimePickerCtrl* m_timePicker = nullptr;
			ibControlFrame* m_ownerValue = nullptr;
		public:

			wxPopupDateTimeWindow(ibControlFrame* ownerValue, wxWindow* parent, int style = wxBORDER_NONE | wxPU_CONTAINS_CONTROLS | wxWANTS_CHARS) :
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
				ibValue vSelected; m_ownerValue->GetControlValue(vSelected);
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
				ibValue cDateTime = GetDateTime();
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
	else if (valType == ibValueTypes::TYPE_STRING) {
		return true;
	}

	return false;
}

bool ibTypeControlFactory::QuickChoice(ibControlFrame* ownerValue, const ibClassID& clsid, wxWindow* parent)
{
	if (!ownerValue->HasQuickChoice())
		return false;

	if (ibTypeControlFactory::SimpleChoice(ownerValue, clsid, parent))
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

		std::map<int, ibValue> m_values;

		ibControlFrame* m_ownerValue = nullptr;
		wxQuickListBox* m_selListBox = nullptr;

	public:
		wxPopupQuickSelectWindow(ibControlFrame* ownerValue, wxWindow* parent, int style = wxBORDER_NONE | wxPU_CONTAINS_CONTROLS | wxWANTS_CHARS) :
			wxPopupTransientWindow(parent, style), m_ownerValue(ownerValue) {

			SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_FRAMEBK));
			wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

			m_selListBox = new wxQuickListBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);

			ibControlDynamicBorder* dynamicBorder = dynamic_cast<ibControlDynamicBorder*>(parent);
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
			ibControlDynamicBorder* innerBorder = dynamic_cast<ibControlDynamicBorder*>(m_parent);
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

		void AppendItem(const ibValue& item, bool select = false) {
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
		ibValue cValue; ownerValue->GetControlValue(cValue);
		std::vector<ibValue> listValue;
		if (cValue.FindValue(wxEmptyString, listValue)) {
			wxPopupQuickSelectWindow* popup =
				new wxPopupQuickSelectWindow(ownerValue, parent);
			for (auto selObj : listValue)
				popup->AppendItem(selObj, selObj == cValue);
			popup->Popup();
			return true;
		}
	}
	return false;
}

void ibTypeControlFactory::QuickChoice(ibControlFrame* controlValue, ibValue& newValue, wxWindow* parent, const wxString& strData)
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

		std::map<int, ibValue> m_values;

		ibControlFrame* m_controlValue = nullptr;
		wxQuickListBox* m_selListBox = nullptr;

		bool m_selected;

	public:

		wxPopupQuickSelectWindow(ibControlFrame* controlValue, wxWindow* parent, int style = wxBORDER_NONE | wxPU_CONTAINS_CONTROLS | wxWANTS_CHARS) :
			wxPopupTransientWindow(parent, style), m_controlValue(controlValue), m_selected(false) {

			SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_FRAMEBK));
			wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

			m_selListBox = new wxQuickListBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);

			ibControlDynamicBorder* dynamicBorder = dynamic_cast<ibControlDynamicBorder*>(parent);
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
					wxTheApp->GetAppName(),
					wxYES_NO | wxCENTRE | wxICON_QUESTION, m_parent
				);
				if (m_controlValue != nullptr && answer == wxYES) {
					ibValue retValue;
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
			ibControlDynamicBorder* innerBorder = dynamic_cast<ibControlDynamicBorder*>(m_parent);
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

		void AppendItem(const ibValue& item, bool select = false) {
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
			std::vector<ibValue> listValue;
			if (newValue.FindValue(strData, listValue)) {
				size_t count = listValue.size();
				if (count > 1) {
					wxPopupQuickSelectWindow* popup =
						new wxPopupQuickSelectWindow(controlValue, parent);
					for (auto selObj : listValue)
						popup->AppendItem(selObj, selObj == newValue);
					popup->Popup();
				}
				else if (count == 1) {
					controlValue->ChoiceProcessing(listValue.at(0));
				}
			}
		}
		else {
			controlValue->ChoiceProcessing(newValue);
		}
	}
}

/////////////////////////////////////////////////////////////////////

ibSelectMode ibTypeControlFactory::GetSelectMode() const
{
	// Select mode is a metaobject-attribute concern — a single reference to a HIERARCHICAL catalog
	// carries Items / Folders / FoldersAndItems. Resolve the bound leaf and, WHEN it is a metadata
	// attribute, read its mode; a plain column (a dynamic list's queryable column) has none →
	// default to item selection.
	const ibValueMetaObjectAttributeBase* attr =
		dynamic_cast<const ibValueMetaObjectAttributeBase*>(GetSourceAttributeObject());
	if (attr != nullptr) return attr->GetSelectMode();
	return ibSelectMode::ibSelectMode_Items;
}

ibValue ibTypeControlFactory::CreateValue() const
{
	return ibTypeControlFactory::CreateValueRef();
}

ibValue* ibTypeControlFactory::CreateValueRef() const
{
	// Value creation is the FACTORY's job — it knows its bound Type (GetTypeDesc); delegating to
	// the source attribute was a duplicate of exactly this.
	return ibBackendTypeSourceFactory::CreateValueRef();
}

ibClassID ibTypeControlFactory::GetDataType() const
{
	// Type + metadata come from the factory itself — its bound source property already reflects
	// the resolved field's Type.
	return ShowSelectType(GetMetaData(), GetTypeValueDesc());
}

#include "frontend/win/dlgs/selectData.h"

ibClassID ibTypeControlFactory::ShowSelectType(const ibMetaData* metaData, const ibTypeDescription& typeDescription)
{
	if (typeDescription.GetClsidCount() < 2) return typeDescription.GetFirstClsid();
	
	ibDialogSelectDataType *selectDataType = new ibDialogSelectDataType(metaData, typeDescription.GetClsidList());

	ibClassID clsid = 0;	
	if (selectDataType->ShowModal(clsid)) {
		selectDataType->Destroy();
		return clsid;
	}
	selectDataType->Destroy();
	return 0;
}
