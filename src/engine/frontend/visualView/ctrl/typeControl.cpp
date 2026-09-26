#include "typeControl.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/objCtor.h"
#include "backend/metaData.h"

////////////////////////////////////////////////////////////////////////////

#include <wx/calctrl.h>
#include <wx/timectrl.h>
#include <wx/popupwin.h>
#include <wx/button.h>
#include <wx/stattext.h>
#include <wx/sizer.h>


#include <map>
#include <functional>

#include "frontend/win/ctrls/dynamicBorder.h"
#include "frontend/visualView/ctrl/frame.h"
#include "frontend/win/dlgs/typeSelector.h"          // the shared type picker — second caller
#include "backend/system/value/valueType.h"          // ibValueTypeDescription / g_valueTypeDescriptionCLSID
#include "backend/choiceLinkResolver.h"               // what narrows this choice — type and conditions
#include "backend/metaCollection/attribute/metaAttributeObject.h"   // the bound attribute holds both
#include "backend/metaCollection/partial/chartOfCharacteristicTypes.h"   // the CONTOUR that narrows the picker
#include "backend/metaCollection/partial/reference/reference.h"          // a reference built on a predefined guid
#include "frontend/win/dlgs/selectPredefined.h"      // the designer's declared-value window — one call, no widgets here

#include "backend/appData.h"                                             // DesignerMode — the two roads part here

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

		// ⭐⭐ WAS A WINDOW ACTUALLY RAISED? Asked of the same fact the type picker asks it of — it shows
		// nothing when fewer than two types are admitted (ShowSelectType), and a cell that admits one
		// has no question to put. Nothing ran an event loop, so nothing destroyed the editor, and the
		// value can be chosen in this very call. It is only the MODAL that forces the call to end, and
		// only a composite cell raises one; every other cell was paying that second click for a window
		// it never saw (Max, 2026-09-25: "why does one have to click the three dots twice in the filter
		// again?").
		const bool asksTheUser = factory->GetTypeDesc().GetClsidCount() > 1;

		const ibClassID clsid = factory->GetDataType();
		const ibMetaData* metaData = factory->GetMetaData();
		if (clsid == 0 || metaData == nullptr || !metaData->IsRegisterCtor(clsid))
			return false;   // the user closed the type choice
		current = metaData->CreateObject(clsid);
		ownerValue->SetControlValue(current);   // the cell now stands on its settled type

		// AND THE CHOICE ENDS HERE WHEN IT WAS ASKED. Settling the type is a MODAL question, and a modal runs an event
		// loop of its own: while it is up the grid finishes editing this cell and destroys the editor
		// control - which is the window handed to us as `parent`. Carrying on in the same call opened
		// the value chooser parented to freed memory, and it died inside wxGetTopLevelParent with a
		// stack pointing at the popup rather than at the modal that invalidated its parent.
		//
		// So the type is settled, said and returned; the value is chosen on the NEXT click, by which
		// time there is a live editor to hang it on. No window pointer outlives a modal here, which is
		// the rule rather than this one repair - the previous line ("keep going, the editor opens now,
		// not on a second click") described a convenience the lifetime does not allow.
		if (asksTheUser)
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
				ibChoiceLinkResolver::FieldOf(factory->GetChoiceHolder(), factory)) {
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

			// ⭐ WHAT NARROWS THIS CHOICE — two things the control supplies and nothing it works out:
			// the FIELD being filled, and WHERE the values its link names are read. The second is the
			// control's own answer — the form's source for a control on a form, the row being edited
			// for a table column — so the list is narrowed by exactly what the person can see beside
			// the field they are filling.
			const ibChoiceHolder holder = factory->GetChoiceHolder();
			const ibChoiceCondition condition = ibChoiceLinkResolver::Resolve(holder,
				ibChoiceLinkResolver::FieldOf(holder, factory));

			// ⭐ WHAT THE FORM IS MADE WITH: which form the author picked, and — as one named part of
			// it — the choice. The condition goes in WHOLE, empty or not: "nothing narrows this" is a
			// condition with nothing in it, and the list asks the same question of both.
			const ibFormRequest request(
				choiceForm != nullptr ? choiceForm->GetName() : wxString(),
				ibCreateRequest(factory->GetSelectMode(), condition));
			return metaObject->ProcessChoice(ownerValue, request);
		}
	}
	return false;
}

bool ibTypeControlFactory::SimpleChoice(ibControlFrame* ownerValue, const ibClassID& clsid, wxWindow* parent) {

	ibValueTypes valType = ibValue::GetVTByID(clsid);

	if (valType == ibValueTypes::TYPE_NUMBER) {
		// A NUMBER IS WORKED OUT MORE OFTEN THAN IT IS LOOKED UP, so its "..." opens a pocket calculator the way
		// a date's opens a calendar: the field's value on the display, a figure keyed in or calculated, and OK
		// puts the result into the field. It counts in ibNumber, the exact decimal the field holds, so 0.1 + 0.2
		// is 0.3; left to right like any pocket calculator ("12 + 3 *" finishes 12 + 3 first).
		class wxPopupCalculatorWindow : public wxPopupTransientWindow {
			enum ibCalcOp { ibCalcOp_None, ibCalcOp_Add, ibCalcOp_Subtract, ibCalcOp_Multiply, ibCalcOp_Divide };
			// What the display stands for: the last result, the figure being typed, or a division by zero.
			enum ibCalcState { ibCalcState_Result, ibCalcState_Typing, ibCalcState_Error };

			wxStaticText* m_operation = nullptr;
			wxStaticText* m_display = nullptr;
			ibControlFrame* m_ownerValue = nullptr;

			wxString m_entry;            // the figure being typed, exactly as typed ("0.", "12.50")
			ibNumber m_accumulator;      // the left side of the open operation, or the last result
			ibCalcOp m_pending = ibCalcOp_None;
			ibCalcState m_state = ibCalcState_Result;
		public:

			wxPopupCalculatorWindow(ibControlFrame* ownerValue, wxWindow* parent, int style = wxBORDER_NONE | wxPU_CONTAINS_CONTROLS | wxWANTS_CHARS) :
				wxPopupTransientWindow(parent, style), m_ownerValue(ownerValue) {

				SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_FRAMEBK));

				wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
				const int gap = FromDIP(3);

				// The open operation, small, above the figure it is waiting for.
				m_operation = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
					wxALIGN_RIGHT | wxST_NO_AUTORESIZE);
				m_operation->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
				mainSizer->Add(m_operation, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, gap * 2));

				m_display = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
					wxALIGN_RIGHT | wxST_NO_AUTORESIZE);
				wxFont big = m_display->GetFont();
				big.SetPointSize(big.GetPointSize() + 6);
				m_display->SetFont(big);
				mainSizer->Add(m_display, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, gap * 2));

				const wxSize keySize(FromDIP(48), FromDIP(34));
				wxFlexGridSizer* keySizer = new wxFlexGridSizer(4, gap, gap);
				const auto key = [&](const wxString& label, std::function<void()> action) {
					wxButton* button = new wxButton(this, wxID_ANY, label, wxDefaultPosition, keySize);
					button->Bind(wxEVT_BUTTON, [this, action](wxCommandEvent&) { action(); ShowState(); });
					keySizer->Add(button, wxSizerFlags().Expand());
				};
				key(wxT("C"), [this] { Reset(ibNumber(0)); });
				key(wxT("⌫"), [this] { Backspace(); });
				key(wxT("±"), [this] { Negate(); });
				key(OperatorSign(ibCalcOp_Divide), [this] { Operator(ibCalcOp_Divide); });
				for (int row = 2; row >= 0; --row) {
					for (int col = 0; col < 3; ++col) {
						const int digit = row * 3 + col + 1;
						key(wxString::Format(wxT("%d"), digit), [this, digit] { Digit(digit); });
					}
					const ibCalcOp op = row == 2 ? ibCalcOp_Multiply : row == 1 ? ibCalcOp_Subtract : ibCalcOp_Add;
					key(OperatorSign(op), [this, op] { Operator(op); });
				}
				key(wxT("0"), [this] { Digit(0); });
				key(wxT("."), [this] { Point(); });
				key(wxT("="), [this] { Equals(); });

				wxButton* OKButton = new wxButton(this, wxID_OK, wxEmptyString, wxDefaultPosition, keySize);
				OKButton->Bind(wxEVT_COMMAND_BUTTON_CLICKED, &wxPopupCalculatorWindow::OnOKButtonClicked, this);
				keySizer->Add(OKButton, wxSizerFlags().Expand());

				mainSizer->Add(keySizer, wxSizerFlags().Expand().Border(wxALL, gap * 2));
				wxPopupTransientWindow::SetSizerAndFit(mainSizer);

				// Keys typed on the keyboard work as well as the ones clicked - a sum is keyed in far more
				// often than it is clicked in.
				Bind(wxEVT_CHAR_HOOK, &wxPopupCalculatorWindow::OnCharHook, this);
				for (wxWindow* child : GetChildren())
					child->Bind(wxEVT_CHAR_HOOK, &wxPopupCalculatorWindow::OnCharHook, this);
			}

			virtual void Popup(wxWindow* focus = nullptr) override {
				ibValue vSelected; m_ownerValue->GetControlValue(vSelected);
				wxPoint pos = m_parent->GetScreenPosition();
				pos.x += (m_parent->GetSize().x - GetSize().x + 2);
				pos.y += (m_parent->GetSize().y);
				wxPopupTransientWindow::SetPosition(pos);
				Reset(vSelected.GetType() == ibValueTypes::TYPE_NUMBER ? vSelected.GetNumber() : ibNumber(0));
				ShowState();
				wxPopupTransientWindow::Popup(focus);
				SetFocus();
			}

		private:

			// Start from a value - the one in the field. The next digit replaces it, as on any calculator
			// that shows a result, while an operator carries on from it.
			void Reset(const ibNumber& initial) {
				m_entry.clear();
				m_accumulator = initial;
				m_pending = ibCalcOp_None;
				m_state = ibCalcState_Result;
			}

			void BeginEntry() {
				m_entry.clear();
				m_state = ibCalcState_Typing;
			}

			void Digit(int digit) {
				if (m_state == ibCalcState_Error)
					Reset(ibNumber(0));
				if (m_state != ibCalcState_Typing)
					BeginEntry();
				// A leading zero is not kept ("007" is 7), and typing has a length: the field it ends up in
				// is a number of a declared size, not an unbounded string of digits.
				if (m_entry == wxT("0"))
					m_entry.clear();
				else if (m_entry == wxT("-0"))
					m_entry = wxT("-");
				int digits = 0;
				for (const wxUniChar c : m_entry)
					if (c >= wxT('0') && c <= wxT('9'))
						++digits;
				if (digits < 20)
					m_entry << digit;
			}

			void Point() {
				if (m_state == ibCalcState_Error)
					Reset(ibNumber(0));
				if (m_state != ibCalcState_Typing)
					BeginEntry();
				if (m_entry.Find(wxT('.')) != wxNOT_FOUND)
					return;
				if (m_entry.empty() || m_entry == wxT("-"))
					m_entry += wxT("0");
				m_entry += wxT('.');
			}

			void Backspace() {
				if (m_state == ibCalcState_Error)
					Reset(ibNumber(0));
				if (m_state != ibCalcState_Typing)
					return;   // a result is not typed text; there is nothing to take a character off
				if (!m_entry.empty())
					m_entry.RemoveLast();
				if (m_entry == wxT("-"))
					m_entry.clear();
			}

			void Negate() {
				if (m_state == ibCalcState_Typing) {
					if (m_entry.StartsWith(wxT("-")))
						m_entry.Remove(0, 1);
					else
						m_entry.Prepend(wxT("-"));
				}
				else if (m_state == ibCalcState_Result) {
					m_accumulator = -m_accumulator;
				}
			}

			// Two operators in a row ("12 + *") replace one another, they do not combine.
			void Operator(ibCalcOp op) {
				if (m_state == ibCalcState_Typing)
					TakeEntry();
				if (m_state != ibCalcState_Error)
					m_pending = op;
			}

			void Equals() {
				if (m_state == ibCalcState_Typing)
					TakeEntry();
				m_pending = ibCalcOp_None;
			}

			// The typed figure is taken: the right side of the open operation, or the value itself.
			void TakeEntry() {
				wxString text = m_entry;
				if (text.EndsWith(wxT(".")))
					text.RemoveLast();   // "12." is 12
				ibNumber right(0);
				if (!text.empty() && text != wxT("-"))
					right.FromString(text);

				m_entry.clear();
				m_state = ibCalcState_Result;

				switch (m_pending) {
				case ibCalcOp_None: m_accumulator = right; return;
				case ibCalcOp_Add: m_accumulator += right; break;
				case ibCalcOp_Subtract: m_accumulator -= right; break;
				case ibCalcOp_Multiply: m_accumulator *= right; break;
				case ibCalcOp_Divide:
					if (right.IsZero()) {
						m_state = ibCalcState_Error;
						return;
					}
					m_accumulator /= right;
					break;
				}
				// A quotient has no end in general (1 / 3): it is cut at ten places, and what was cut is
				// still exact in every place a person can read.
				m_accumulator = m_accumulator.Round(10);
			}

			static wxString OperatorSign(ibCalcOp op) {
				switch (op) {
				case ibCalcOp_Add: return wxT("+");
				case ibCalcOp_Subtract: return wxT("−");
				case ibCalcOp_Multiply: return wxT("×");
				case ibCalcOp_Divide: return wxT("÷");
				default: return wxT(" ");
				}
			}

			void ShowState() {
				wxString text;
				if (m_state == ibCalcState_Error) {
					text = _("Error");
				}
				else if (m_state == ibCalcState_Typing) {
					text = m_entry.empty() ? wxString(wxT("0")) : m_entry;
				}
				else {
					// The shortest exact spelling: no trailing zeros after the point, no bare point.
					text = m_accumulator.ToString();
					if (text.Find(wxT('.')) != wxNOT_FOUND) {
						while (text.EndsWith(wxT("0")))
							text.RemoveLast();
						if (text.EndsWith(wxT(".")))
							text.RemoveLast();
					}
					if (text.empty() || text == wxT("-0"))
						text = wxT("0");
				}
				m_display->SetLabel(text);
				m_operation->SetLabel(OperatorSign(m_pending));
			}

			void OnOKButtonClicked(wxCommandEvent&) {
				Equals();
				if (m_state == ibCalcState_Error) {
					ShowState();   // 12 / 0 is not a value to put into the field
					return;
				}
				ibValue cNumber(m_accumulator);
				if (m_ownerValue != nullptr)
					m_ownerValue->ChoiceProcessing(cNumber);
				Dismiss();
			}

			void OnCharHook(wxKeyEvent& event) {
				const int code = event.GetKeyCode();
				const wxUniChar ch = event.GetUnicodeKey();

				if (code == WXK_ESCAPE) {
					Dismiss();
					return;
				}
				// Enter finishes an open operation first, and only then takes the result.
				if (code == WXK_RETURN || code == WXK_NUMPAD_ENTER) {
					if (m_pending != ibCalcOp_None) {
						Equals();
						ShowState();
					}
					else {
						wxCommandEvent clicked(wxEVT_COMMAND_BUTTON_CLICKED, wxID_OK);
						OnOKButtonClicked(clicked);
					}
					return;
				}

				if (code == WXK_BACK) Backspace();
				else if (code == WXK_DELETE) Reset(ibNumber(0));
				else if (ch >= wxT('0') && ch <= wxT('9')) Digit(static_cast<int>(ch.GetValue() - wxT('0')));
				else if (ch == wxT('.') || ch == wxT(',')) Point();
				else if (ch == wxT('+')) Operator(ibCalcOp_Add);
				else if (ch == wxT('-')) Operator(ibCalcOp_Subtract);
				else if (ch == wxT('*')) Operator(ibCalcOp_Multiply);
				else if (ch == wxT('/')) Operator(ibCalcOp_Divide);
				else if (ch == wxT('=')) Equals();
				else { event.Skip(); return; }
				ShowState();
			}
		};

		if (ownerValue != nullptr) {
			wxPopupCalculatorWindow* popup =
				new wxPopupCalculatorWindow(ownerValue, parent);
			popup->Popup();
		}
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
		ibChoiceLinkResolver::FieldOf(GetChoiceHolder(), this);
	if (attr != nullptr) return attr->GetSelectMode();
	return ibSelectMode::ibSelectMode_Items;
}

ibValue ibTypeControlFactory::CreateValue() const
{
	// Value creation is the FACTORY's job — it knows its bound Type (GetTypeDesc); delegating to
	// the source attribute was a duplicate of exactly this.
	return ibBackendTypeSourceFactory::CreateValue();
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
