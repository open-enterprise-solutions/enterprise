#include "gridBox.h"
#include "backend/serialize/dataBuilder.h"   // ibDataNode (control -> node)
#include "backend/system/value/valueDataComposition.h"   // the source that turns this box into a report
#include "backend/settings/settingsComposer.h"            // the setting marked "restore on open" goes on here
#include "backend/system/systemManager.h"                 // ibValueSystemFunction::Message — the platform's own way to speak
#include "sfrontend/visualView/visualHost.h"              // IsDesignerHost — a form being drawn neither restores nor composes

#include <algorithm>   // the fetched range, clamped
#include <set>         // …and the merged cells it reaches into, written once

//***********************************************************************************
//*                           IMPLEMENT_DYNAMIC_CLASS                               *
//***********************************************************************************

// ⭐⭐ THE MODEL INSTALLS ITSELF HERE. Anything that IS a spreadsheet model — a hand-filled document,
// a composer — is handed in and the box shows the sheet THAT model holds. Nothing else is accepted,
// and the box keeps what it had rather than half-taking a value it cannot show.
//
// 🛑 THE MODEL, never a bare document description: the drill-down parameters and the edit mode live
// on the OBJECT, so installing a description alone leaves every cell bound to a name nothing answers
// to — the sheet looks right and stops opening anything.
bool ibValueGridBox::SetControlValue(const ibValue& varControlVal)
{
	ibValueSpreadsheetModel* model = varControlVal.ConvertToType<ibValueSpreadsheetModel>();
	if (model == nullptr)
		return false;

	m_spreadsheetModel = model;

	// The sheet on show follows the model it was given — the sheet is the model's — and its version
	// moves, so a client drops the cells it holds and fetches the new ones.
	m_shownDocument = model->GetSpreadsheetDocument();
	++m_documentVersion;

	return true;
}

bool ibValueGridBox::GetControlValue(ibValue& pvarControlVal) const
{
	pvarControlVal = m_spreadsheetModel;
	return true;
}

//***********************************************************************************
//*                                 Value Notebook                                  *
//***********************************************************************************

ibValueGridBox::ibValueGridBox() : ibValueWindowComposite(),
m_spreadsheetModel(new ibValueSpreadsheetDocument())   // nothing bound yet — a sheet of its own IS a model
{
	m_shownDocument = m_spreadsheetModel->GetSpreadsheetDocument();

	m_members.Bind(this, &ibValueGridBox::FillControlMembers);
	//set default params
	m_propertyMinSize->SetValue(wxSize(150, 50));
	// ⭐ THE SOURCE TAKES EXACTLY TWO TYPES (Max, 2026-08-19): a **spreadsheet document** — the
	// hand-filled sheet, a printable form — and a **composition** — the report, which builds its own
	// document and brings the two verbs with it. Declared here so the picker offers those and
	// nothing else; the control then behaves by what it was actually given (see ResolveComposition).
	ibTypeDescription allowedSources;
	allowedSources.AppendMetaType(g_valueSpreadsheetCLSID);
	allowedSources.AppendMetaType(g_valueDataCompositionCLSID);
	m_propertySource->SetValue(allowedSources);
}

#include "sfrontend/visualView/ctrl/form.h"

ibValueGridBox::~ibValueGridBox()
{
	*m_aliveToken = false;

	// ⭐ THE BOX IS GOING — TELL THE READ TO STOP. A report composes on a rented background run,
	// and a form closing is not the same moment as the composition dying: the run holds references
	// of its own, so without this it keeps reading against a session nobody is watching, on a
	// connection somebody else is waiting for (Max, 2026-08-19: "sorry, break off, we changed our
	// mind"). CancelFetch raises the same cooperative flag the interpreter obeys and waits the run
	// out — the walk polls it per row, so it stops at the next one.
	//
	// ASKED OF THE MODEL, not of a composition: whatever is bound answers, and a sheet that reads
	// nothing has nothing to stop.
	if (m_spreadsheetModel)
		m_spreadsheetModel->CancelFetch();
}

//**********************************************************************************
//*                                  Life                                          *
//**********************************************************************************

void ibValueGridBox::OnUpdate(ibDataNode& state, ibVisualHost* host)
{
	ibValueWindowComposite::OnUpdate(state, host);

	state.SetValue(wxT("RowCount"), static_cast<s32>(m_shownDocument->GetNumberRows()));
	state.SetValue(wxT("ColCount"), static_cast<s32>(m_shownDocument->GetNumberCols()));
	state.SetValue(wxT("Version"), m_documentVersion);
	state.SetValue(wxT("Composing"), m_composeRuns > 0);
}

void ibValueGridBox::OnCreated(ibVisualHost* host)
{
	// THE BINDING IS HONOURED HERE TOO — the same moment every other source control takes its value
	// (a checkbox, a textbox, a table all read the bound attribute as they are shown). The form has
	// already handed it over at InitializeControl; taking it again is idempotent.
	RefreshModel();

	// ⚠ NOT FOR THE DESIGNER'S PICTURE OF THE FORM: there the box is a picture of itself — running the report
	// while somebody is drawing the form would read live data into an editor, and slowly; and a person drawing
	// a form is not a reader whose settings these are.
	if (host->IsDesignerHost())
		return;

	// ⭐⭐ THE SETTING MARKED "restore on open" IS PUT ON — HERE, BEFORE THE COMPOSE, because the compose
	// below must read by it. This is the moment a control has been handed its model (Max, 2026-08-26: *"it
	// fires when you assign the model, or on the created event — it happens once anyway"*), and it is the
	// FRONT's job: the back has no idea a form was opened.
	ApplyDefaultSettings();

	// ⭐⭐ COMPOSE ON OPEN — here, as the form opens, and nowhere else. This is the one moment that happens
	// once per opened form: OnUpdate runs again for every frame a client is sent, and a report re-reading
	// the database because a form was drawn again is not a feature.
	//
	// Through the control's own command rather than the model's verb, so it takes exactly the road
	// the button takes: the progress indicator, the background read, the failure message.
	if (m_propertyComposeOnOpen->GetValueAsBoolean())
		CallAsAction(ibSpreadsheetModelCommand_Compose, GetOwnerForm());
}

void ibValueGridBox::OnCleanup(ibVisualHost* host)
{
	// The form closed while a report was being built: the read stops at its next row (the destructor says
	// the same for a box that goes without its form having been shown).
	if (m_spreadsheetModel)
		m_spreadsheetModel->CancelFetch();
}

// ⭐ THE ADDRESS COMES FROM THE BINDING — the leaf of this box's source path, which for a report is
// the COMPOSER's metaID. What a person arranged belongs to what is shown, not to the widget: the
// box can be deleted and drawn again and the shelf is still theirs.
void ibValueGridBox::ApplyDefaultSettings()
{
	const ibGuid objectKey = SettingsObjectKey();
	if (!m_spreadsheetModel || !objectKey.isValid())
		return;   // nothing bound — nothing for a shelf to be about

	switch (ibRestoreDefaultComposerSettings(ibSettingsCategory::Composer, objectKey,
		m_spreadsheetModel->GetModelComposer(), GetMetaData())) {
	case ibDefaultSettingsOutcome::Missing:
		// ⚠ SAID OUT LOUD (Max, 2026-08-26: *"and if it cannot find that setting afterwards, it
		// complains"*). The person marked something to come back on open and it did not: what they
		// then see is the author's settings, which looks exactly like the mark being ignored.
		ibValueSystemFunction::Message(
			_("The settings marked to be restored on open could not be found"),
			ibStatusMessage::ibStatusMessage_Warning);
		break;
	case ibDefaultSettingsOutcome::Restored:
	case ibDefaultSettingsOutcome::None:
		break;   // nothing to say: it worked, or nobody asked for anything
	}
}

//**********************************************************************************
//*                                   Fetch                                        *
//**********************************************************************************

namespace {

// ONE CELL AS A CLIENT DRAWS IT — what it shows, and only what it sets of its own (the header says what
// each field means). Read off the cell's description rather than through the sheet's getters: those answer
// an unset colour with the desktop's window colour, which is a question for whoever draws, not for a server.
void ibWriteFetchedCell(ibDataNode& response, const ibBackendSpreadsheetObject& document,
	const ibSpreadsheetCellDescription& cell)
{
	// The text the sheet shows — a caption in the document's language, a parameter by its value, a template
	// filled in: the one door the grid and the printout read a cell through.
	const wxString text = document.ComputeStringValueFromParameters(cell.m_value, cell.m_fillSetType);

	int spanRows = 1, spanCols = 1;
	cell.GetSize(&spanRows, &spanCols);
	const bool merged = spanRows > 1 || spanCols > 1;

	// Nothing to show — no text, no span, no fill of its own: not written, absent is empty.
	if (text.IsEmpty() && !merged && !cell.m_backgroundColour.IsOk())
		return;

	ibDataNode& node = response.AddChild(0, 0);
	node.SetValue(wxT("Row"), static_cast<s32>(cell.m_row + 1));
	node.SetValue(wxT("Col"), static_cast<s32>(cell.m_col + 1));
	node.SetValue(wxT("Text"), text);
	if (merged) {
		node.SetValue(wxT("RowSpan"), static_cast<s32>(spanRows));
		node.SetValue(wxT("ColSpan"), static_cast<s32>(spanCols));
	}
	if (cell.m_font.IsOk() && cell.m_font.GetWeight() >= wxFONTWEIGHT_BOLD)
		node.SetValue(wxT("Bold"), true);
	if (cell.m_alignHorz & wxALIGN_RIGHT)
		node.SetValue(wxT("Align"), wxString(wxT("Right")));
	else if (cell.m_alignHorz & wxALIGN_CENTER_HORIZONTAL)
		node.SetValue(wxT("Align"), wxString(wxT("Center")));
	if (cell.m_backgroundColour.IsOk())
		node.SetValue(wxT("BackgroundColour"), cell.m_backgroundColour.GetAsString(wxC2S_HTML_SYNTAX));
	if (cell.m_textColour.IsOk())
		node.SetValue(wxT("TextColour"), cell.m_textColour.GetAsString(wxC2S_HTML_SYNTAX));
}

}   // namespace

bool ibValueGridBox::Fetch(const ibDataNode& request, ibDataNode& response)
{
	if (!m_shownDocument)
		return false;

	const ibBackendSpreadsheetObject& document = *m_shownDocument;
	const ibSpreadsheetDescription& desc = document.GetSpreadsheetDesc();

	// THE RANGE ASKED, from 1 and inclusive, clamped to the sheet…
	const int top = std::max<int>(request.GetValue<s32>(wxT("Top")), 1);
	const int left = std::max<int>(request.GetValue<s32>(wxT("Left")), 1);
	int bottom = std::min<int>(request.GetValue<s32>(wxT("Bottom")), desc.GetNumberRows());
	int right = std::min<int>(request.GetValue<s32>(wxT("Right")), desc.GetNumberCols());

	// …and to what one answer carries: a range wider than that is cut at the right, a taller one at the bottom.
	if (right - left + 1 > s_fetchCellLimit)
		right = left + s_fetchCellLimit - 1;
	if (right >= left && bottom - top + 1 > s_fetchCellLimit / (right - left + 1))
		bottom = top + s_fetchCellLimit / (right - left + 1) - 1;

	// What was served, and of which sheet: the client files the cells under the version they were read at.
	response.SetValue(wxT("Version"), m_documentVersion);
	response.SetValue(wxT("Top"), static_cast<s32>(top));
	response.SetValue(wxT("Left"), static_cast<s32>(left));
	response.SetValue(wxT("Bottom"), static_cast<s32>(bottom));
	response.SetValue(wxT("Right"), static_cast<s32>(right));
	if (bottom < top || right < left)
		return true;   // the range lies outside the sheet — nothing in it

	// THE CELLS. The sheet counts from 0.
	std::set<std::pair<int, int>> reached;   // merged cells cornered outside the range, written once
	for (int row = top - 1; row < bottom; row++) {
		for (int col = left - 1; col < right; col++) {
			const ibSpreadsheetCellDescription* cell = desc.GetCell(row, col);
			if (cell == nullptr)
				continue;

			int spanRows = 1, spanCols = 1;
			if (cell->GetSize(&spanRows, &spanCols) >= 0) {
				ibWriteFetchedCell(response, document, *cell);
				continue;
			}

			// A cell under a merge says nothing of its own: it holds the way back to the cell that does (a
			// negative offset). That one is written where the loop meets it — unless its corner lies above
			// or left of the range, and then here, once.
			const int ownerRow = row + spanRows, ownerCol = col + spanCols;
			if ((ownerRow < top - 1 || ownerCol < left - 1) && reached.emplace(ownerRow, ownerCol).second) {
				if (const ibSpreadsheetCellDescription* owner = desc.GetCell(ownerRow, ownerCol))
					ibWriteFetchedCell(response, document, *owner);
			}
		}
	}

	// THE SIZES OF THE RANGE — every column, and the rows with a height of their own (see the header).
	ibDataNode& cols = response.Child(wxT("Cols"));
	for (int col = left - 1; col < right; col++) {
		ibDataNode& size = cols.AddChild(0, 0);
		size.SetValue(wxT("Index"), static_cast<s32>(col + 1));
		size.SetValue(wxT("Size"), static_cast<s32>(desc.GetColSize(col)));
	}

	ibDataNode& rows = response.Child(wxT("Rows"));
	for (int row = top - 1; row < bottom; row++) {
		if (!desc.HasRowSize(row))
			continue;   // automatic height — whoever shows the sheet works it out
		ibDataNode& size = rows.AddChild(0, 0);
		size.SetValue(wxT("Index"), static_cast<s32>(row + 1));
		size.SetValue(wxT("Size"), static_cast<s32>(desc.GetRowSize(row)));
	}

	return true;
}

//**********************************************************************************
//*                                   Data										   *
//**********************************************************************************

bool ibValueGridBox::ReadData(const ibDataNode& node)
{
	// ⭐ THE SOURCE IS PART OF THE CONTROL (Max, 2026-08-19: "you forgot the serialisation that
	// stores the source"). Everything else about a gridbox survived a save and the binding did not,
	// so a form reopened with the box pointing at nothing — and the report it was built for looked
	// like it had lost its composition.
	m_propertySource->SetNodeValue(node.GetProperty(m_propertySource->GetName()));

	// The sheet a designer typed into travels with the control — read into the sheet the MODEL holds,
	// which with no source bound is the box's own document.
	if (m_spreadsheetModel) {
		ibSpreadsheetDescriptionMemory::ReadNode(node.GetProperty(wxT("Spreadsheet")),
			m_spreadsheetModel->GetSpreadsheetDocument()->GetSpreadsheetDesc());
	}
	return ibValueWindowComposite::ReadData(node);
}
bool ibValueGridBox::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertySource->GetName(), m_propertySource->GetNodeValue());

	if (m_spreadsheetModel) {
		ibDataValue spreadsheet;
		ibSpreadsheetDescriptionMemory::WriteNode(spreadsheet,
			m_spreadsheetModel->GetSpreadsheetDocument()->GetSpreadsheetDesc());
		node.SetProperty(wxT("Spreadsheet"), spreadsheet);
	}
	return ibValueWindowComposite::WriteData(node);
}

//***********************************************************************************
enum prop {
	eGridValue,
};

void ibValueGridBox::FillControlMembers(ibMemberTable& helper) const
{
	helper.AppendProp(wxT("Value"), eGridValue, eControl);
}

bool ibValueGridBox::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
	if (lPropAlias == eControl) {
		const long lPropData = m_members.GetPropData(lPropNum);
		if (lPropData == eGridValue)
			return SetControlValue(varPropVal);   // ONE door: a script assigns what the form binds
	}

	return ibValueFrame::SetPropVal(lPropNum, varPropVal);
}

bool ibValueGridBox::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
	if (lPropAlias == eControl) {
		const long lPropData = m_members.GetPropData(lPropNum);
		if (lPropData == eGridValue)
			return GetControlValue(pvarPropVal);
	}
	return ibValueFrame::GetPropVal(lPropNum, pvarPropVal);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueGridBox, "Gridbox", "Container", g_controlGridBoxCLSID);
