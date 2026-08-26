#ifndef _GRID_H__
#define _GRID_H__

#include "window.h"
#include "frontend/win/editor/gridEditor/gridEditor.h"
#include "backend/system/value/valueSpreadsheet.h"
#include "backend/propertyManager/property/propertySource.h"   // the document this box shows
#include "backend/compositionDescription.h"                    // ibSettingsDescription — the ACTIVE setting is this control's
#include "frontend/visualView/ctrl/typeControl.h"                // ⚠ the Source property needs its owner to BE a source factory

#include <memory>   // the alive token the compose hop carries back

//********************************************************************************************
//*                                 define commom clsid									     *
//********************************************************************************************

// NAMED WHERE THE CONTROL IS DECLARED, like the tablebox's — a class id belongs to the class, and
// anything asking "is this a gridbox?" names the constant instead of repeating the registration key.
constexpr ibClassID g_controlGridBoxCLSID = control_to_clsid("CT_GRID");

// ---------------------------------------------------------------------------
// ibValueGridBox — the spreadsheet control: a mini-Excel over a DOCUMENT.
//
// ⭐ IT SHOWS A VARIABLE, the way a tablebox shows a table (Max, 2026-08-19). Creating the control
// creates an attribute of type `SpreadsheetDocument` and points the box at it; the box then shows
// what that document holds, and anything else on the form can write into the SAME document — which
// is how a composition's `Compose(Document)` result appears here without the two knowing about each
// other.
//
// With no source named the box falls back to a document of its own — that is what it always was, and
// it keeps a bare gridbox (a scratch sheet, a preview) working with nothing to declare.
// ---------------------------------------------------------------------------
class ibValueGridBox : public ibValueWindowComposite, public ibTypeControlFactory {
	public:

	ibValueGridBox();
	// The compose reports back through CallAfter, which can land after this control is gone.
	virtual ~ibValueGridBox() { *m_aliveToken = false; }

	virtual wxObject* Create(ibFrontendWindow* wxparent, ibVisualHost *visualHost) override;
	virtual void OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost *visualHost, bool firstCreated) override;
	virtual void OnSelected(wxObject* wxobject) override;
	virtual void Update(wxObject* wxobject, ibVisualHost *visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost *visualHost) override;


	// ---- ibTypeControlFactory — what the Source property binds THROUGH ------------------------
	// ⚠ NOT DECORATION. `ibPropertySource` builds its variant only when its owner is a source
	// factory (`CreateVariantData` dynamic_casts to it and returns NULL otherwise) — so a control
	// that declares a Source without this base gets a property with no variant at all, and the
	// first question asked of it asserts. That is exactly how this crashed on the first drop
	// (designer_5084, 2026-08-19): the box was created, its command bar asked whether the source
	// was empty, and the answer went through a variant that was never made.
	virtual ibSourceObject* GetSourceObject() const override;
	virtual ibSourceDescription& GetSourceDesc() const override { return m_propertySource->GetValueAsSourceDesc(); }
	virtual const ibBackendSourceColumn* GetSourceAttributeObject() const override { return m_propertySource->GetSourceAttributeObject(); }
	virtual bool IsSourceMissing() const override { return m_propertySource->IsEmptyProperty(); }
	// The type the binding is restricted to (the two the ctor declares) and the config the names
	// resolve against — both answered the way the tablebox answers them.
	virtual ibTypeDescription& GetTypeDesc() const override { return m_propertySource->GetValueAsTypeDesc(); }
	virtual const ibMetaData* GetMetaData() const override;
	// The attributes this box may be pointed at — by TYPE, because its two types are of different KINDS.
	virtual bool GetSourceList(std::vector<class ibBackendFormAttributeValue*>& out) const override;

	// ⭐ THE SAME TWO ANSWERS A TABLE GIVES. A gridbox whose WHOLE binding is the main attribute IS
	// the form's main view, so the form's command provider resolves to it and its verbs (Compose,
	// Settings) appear on the FORM's toolbar — and it carries no bar of its own, or the same button
	// would sit in two places (Max, 2026-08-20: "exactly like it works with tables").
	//
	// A gridbox bound deeper — `Object.Composer2`, a second grid over another composer — keeps its
	// own bar, because it is a distinct view, not the form's subject.
	virtual bool IsMainSourceBound() const override;
	virtual bool HasCommandBar() const override;
	// The SOURCE, set by the designer (and by whatever generates a report form).
	//
	// ⚠ THE SETTER REFRESHES ITSELF, exactly as the tablebox's does: SetValue writes the property
	// without firing OnPropertyChanged, so a binding set from code would otherwise leave the box
	// showing whatever it held before.
	void SetSource(const ibMetaID& id) { m_propertySource->SetValue(id); RefreshModel(); }
	void SetSource(const std::vector<ibSourceId>& path) { m_propertySource->SetValue(ibSourceDescription(path)); RefreshModel(); }
	ibMetaID GetSource() const { return m_propertySource->GetValueAsSource(); }

	// …and the form's "before run" hook, the third moment — the model has to be in place before the
	// command bar asks what can be done with it.
	virtual bool InitializeControl() override { RefreshModel(); return true; }


	// ⭐ COMMANDS ONLY WHEN THE SOURCE IS A COMPOSITION (Max, 2026-08-19: "if the source is not a
	// composer but a spreadsheet document, rendering the commands is not required"). A box showing a
	// plain document is a sheet — printing and saving belong to the document's own surface; a box
	// showing a COMPOSITION is a report, and a report has two verbs: build it, and configure it.
	virtual ibStandardCommandSet GetStandardCommands(const ibFormID& formType) override;
	virtual void CallAsAction(const ibActionID& lNumAction, class ibBackendValueForm* srcForm) override;
	//support printing
	virtual wxPrintout* CreatePrintout() const;

	// ⭐⭐ THE ONE DOOR THE MODEL ARRIVES THROUGH. A model is HANDED to this control — by the form when
	// it binds its source, by a script assigning `Items.Grid.Value`, by whatever else holds one — and
	// the control shows the sheet THAT model holds. It never goes looking for a model of its own, and
	// there is no second way in for one to arrive (Max, 2026-08-20).
	//
	// SURFACED AS `ThisForm.<name>` ONLY WHEN NOTHING IS BOUND — the tablebox's rule, word for word:
	// a box with a source is reached through the ATTRIBUTE that source names, and publishing both
	// would be two names for one thing that can disagree.
	virtual bool HasValueInControl() const override { return m_propertySource->IsEmptyProperty(); }
	virtual bool SetControlValue(const ibValue& varControlVal = ibValue()) override;
	virtual bool GetControlValue(ibValue& pvarControlVal) const override;

	//methods & attributes
	void FillControlMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

protected:

	// A source change re-takes the model — the one event this control answers.
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue) override;

private:

	// TAKE THE MODEL THE BINDING NAMES — the tablebox's mechanism (RefreshModel → CreateModel), as
	// one function. Called at the three moments a binding can differ from what is held: the form is
	// built, the window is created, the Source is set.
	void RefreshModel();

	// ⭐ WHERE THIS REPORT'S SAVED SETTINGS LIVE — the guid of the COMPOSER this box is bound to,
	// taken from the leaf of its binding (Max, 2026-08-26: *"the unique identifier of the attribute
	// it is bound to, from the source"*). Not the source object's guid: that is the guid of the
	// TABLE the report reads, so two different reports over one table would share one shelf. And not
	// the control's: the settings belong to what is shown, so the box may be redrawn without losing
	// them. Empty when nothing is bound, and then there is no shelf.
	ibGuid SettingsObjectKey() const;

	// The settings behind the box — a composer HAS them, a plain sheet has none. The one place that
	// knows which of the two is which.
	class ibValueDataComposition* ResolveComposition() const;

	// ⭐ ONE REFERENCE, AND IT IS THE MODEL. The model holds the backend sheet and hands it back
	// (GetSpreadsheetDocument), so there is no second place a document could live and no way for the
	// box to end up showing one sheet while a run filled another. With nothing bound it is a
	// spreadsheet document of its own — which is what keeps a bare gridbox a usable sheet.
	//
	// SET BY ASSIGNMENT ONLY (SetPropVal): the model installs ITSELF here, so this control never
	// walks a path to go looking for one — and therefore never needs to be written to from a const
	// reader.
	ibValuePtr<class ibValueSpreadsheetModel> m_spreadsheetModel;

	// (NO SETTING KEPT HERE. The ACTIVE one lives on the model, in its composer; opening the settings
	//  takes a COPY of it, the window edits the copy, and OK assigns it back — Max, 2026-08-24.)

	// ⭐ "IS THIS CONTROL STILL THERE" — read by the delivery hop on the UI thread. The same token
	// the paged dataview carries (ibDataViewCtrl::m_aliveToken), and for the same reason: a compose
	// runs off-thread and reports back through CallAfter, which can land after the form has closed.
	// A wxWeakRef would WRITE into the control as it dies, racing the copy the worker holds; this is
	// only ever copied by the worker and only ever read on the thread the destructor runs on.
	std::shared_ptr<bool> m_aliveToken = std::make_shared<bool>(true);

	ibPropertyCategory* m_categoryData = ibPropertyObject::CreatePropertyCategory(wxT("Data"), _("Data"));
	ibPropertySource*   m_propertySource = ibPropertyObject::CreateProperty<ibPropertySource>(m_categoryData, wxT("Source"), _("Source"));

	// ⭐⭐ COMPOSE ON OPEN — the report builds itself as the control is created, with no press of
	// Compose (Max, 2026-08-26). A report normally waits because the reader sets its parameters
	// first and a read fired before that reads the wrong thing; but a report put on a DESKTOP, or
	// opened already answered, has nothing left to be asked, and the button is then a step that
	// means nothing.
	//
	// ⚠ A PROPERTY OF THE CONTROL, not of the schema. What to read is the composition's business;
	// WHEN to read it on screen is this window's, and the two were nearly merged — putting it in the
	// description would have leaked a form's behaviour into the data layer (Max caught it).
	//
	// ⚠ AND IT FIRES AT CREATION, ONCE. Not on every Update: a re-render, a property edit, a resize
	// all pass through there, and a report that re-read itself on each of them would be reading the
	// database because a window moved.
	ibPropertyBoolean*  m_propertyComposeOnOpen = ibPropertyObject::CreateProperty<ibPropertyBoolean>(
		m_categoryData, wxT("ComposeOnOpen"), _("Compose on open"), wxT(""), false);

	// ⭐⭐ DETAIL PROCESSING — the first event this control has ever had, and it is named after the
	// QUESTION rather than after the gesture that asks it (Max, 2026-08-26: "there is no such event
	// as left-click and right-click; you simply call it detail processing").
	//
	// A printed sheet is not a picture: every figure was COMPOSED from something, and clicking one
	// is how a person asks what from. The cell already carries its value — the composer stamps
	// `SetParameter` + `SetCellDetailsParameter` on every heading, cell and total — and the click
	// ended in `OpenCellDetailsParameter` → `ibValue::ShowValue`. What was missing was the FORK: a
	// report saying what a click means in it. So this is not a new mechanism; it is the question
	// asked one step before the answer that already existed.
	//
	// ⚠ THE MOUSE BUTTON IS NOT PART OF IT. The RIGHT button belongs to copy / paste and cannot be
	// taken over (Max), so detail lives on the LEFT one — and the day it moves elsewhere, a handler
	// written against "detail processing" still means what it meant.
	//
	// ⚠ `StandardProcessing` cleared takes the default away entirely: no menu, no value opened. The
	// row and the column travel because a handler doing its own thing must know WHICH cell was hit.
	ibPropertyCategory* m_categoryEvent = ibPropertyObject::CreatePropertyCategory(wxT("Event"), _("Event"));
	ibEventControl*     m_eventOnDetailProcessing = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent,
		wxT("OnDetailProcessing"), _("On detail processing"), _("When a cell is clicked — before its value is opened or detailed"),
		wxArrayString{ wxT("Control"), wxT("Row"), wxT("Column"), wxT("StandardProcessing") });

	// The grid's own click, forwarded to the event above. Bound where the window is made, the way
	// the tablebox binds its dataview events.
	void OnCellLeftClick(class ibGridEvent& event);

	// …and what the menu's second entry does: a NEW report over a copy of this one's schema, opened
	// as its own form. See the body — a form cannot be copied, a composition can.
	void ShowCellDetail(int row, int col);
};

#endif
