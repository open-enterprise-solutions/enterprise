////////////////////////////////////////////////////////////////////////////
//	Gridbox — WHAT IT SHOWS: the bound source, and what resolves out of it
////////////////////////////////////////////////////////////////////////////

#include "gridBox.h"
#include "frontend/visualView/ctrl/form.h"
#include "backend/system/value/valueDataComposition.h"   // the source that turns this box into a report
#include "backend/spreadsheetModel.h"   // ibValueSpreadsheetModel — the ONE question this control is moving to
#include "backend/metaCollection/partial/dataReport.h"   // ⏳ interim: until the composer derives the model
#include <algorithm>                                     // std::find — the picker takes each attribute once

const ibMetaData* ibValueGridBox::GetMetaData() const
{
	return GetOwnerForm() != nullptr ? GetOwnerForm()->GetMetaData() : nullptr;
}

// WHICH ATTRIBUTES THIS BOX MAY BE POINTED AT — filtered by TYPE, not by "kind".
//
// ⭐ The form sorts its attributes into KINDS (scalar / table / table column) and every other control
// picks one kind. This box cannot: its two types fall on either side of that line — a spreadsheet
// document is a plain attribute, a composition is a table. Asking by kind therefore gives one of the
// two and hides the other, which is exactly the paradox Max hit (2026-08-19): "the TABLE's picker
// sees the composer, and the gridbox's sees nothing at all".
//
// So both kinds are collected and the answer is filtered by the types this control declared it takes
// (see the constructor). One statement of what a gridbox shows, in one place.
bool ibValueGridBox::GetSourceList(std::vector<ibBackendFormAttributeValue*>& out) const
{
	const ibValueForm* form = GetOwnerForm();
	if (form == nullptr)
		return false;

	std::vector<ibBackendFormAttributeValue*> candidates;
	form->GetSourceList(ibSourceDataType::ibSourceDataType_attribute, candidates);
	form->GetSourceList(ibSourceDataType::ibSourceDataType_table, candidates);

	for (ibBackendFormAttributeValue* holder : candidates) {
		if (holder == nullptr)
			continue;
		// An attribute can arrive under both kinds — take it once.
		if (std::find(out.begin(), out.end(), holder) != out.end())
			continue;
		const ibTypeDescription& declared = holder->GetTypeDesc();
		if (declared.ContainType(g_valueSpreadsheetCLSID) || declared.ContainType(g_valueDataCompositionCLSID)) {
			out.push_back(holder);
			continue;
		}

		// ⭐ …AND ANYTHING THAT IS A SPREADSHEET MODEL COUNTS, UNCONDITIONALLY (Max, 2026-08-20:
		// "let the report be a source with no condition — a report always has a gridbox until
		// somebody deliberately removes it"). The control asks the MODEL, never "is this a report":
		// a report, a composition and a plain document all answer it, and the fourth thing that
		// shows a sheet will answer it too without this line changing.
		// ⭐ …AND AN ATTRIBUTE THAT CONTAINS ONE COUNTS — a report declares its composers INSIDE
		// itself, so the form's only attribute is the report object, whose own type is not a
		// composition. The picker then offers the attribute and the tree under it, and what is
		// actually picked is the COMPOSER (`Object.Composer1`): a report is not a source, its
		// composer is (Max, 2026-08-20).
		const ibSourceDataObject* source = holder->GetSourceValue();
		const ibSourceExplorer* explorer = source != nullptr ? source->GetSourceExplorer() : nullptr;
		if (explorer == nullptr)
			continue;
		for (unsigned int idx = 0; idx < explorer->GetHelperCount(); ++idx) {
			const ibSourceExplorer* node = explorer->GetHelper(idx);
			if (node != nullptr && node->ContainType(g_valueDataCompositionCLSID)) {
				out.push_back(holder);
				break;
			}
		}
	}
	return !out.empty();
}
ibSourceObject* ibValueGridBox::GetSourceObject() const
{
	return GetOwnerForm() != nullptr ? GetOwnerForm()->GetSourceObject() : nullptr;
}

// THE TABLE'S TWO ANSWERS, WORD FOR WORD (ibValueModelTableBox). This box IS the form's main view
// when its WHOLE binding is the main attribute — a single hop. Bound deeper (`Object.Composer2`, a
// second grid over another composer) it is a distinct view and keeps everything of its own.
bool ibValueGridBox::IsMainSourceBound() const
{
	const ibSourceDescription& desc = m_propertySource->GetValueAsSourceDesc();
	if (desc.GetHopCount() == 0 || desc.GetHopCount() > 2)
		return false;

	ibBackendFormAttributeValue* holder = FindSourceHolder(desc.GetFirst());
	if (holder == nullptr || !holder->IsMain())
		return false;

	// A single hop IS the main attribute. Two hops are main when the second names the source's OWN
	// main node — a report's DEFAULT composer. That is asked of the SOURCE (IsMainSourceNode), so
	// this control needs no idea which kinds have such a node; a box bound to any OTHER composer is
	// a distinct view and keeps its own bar.
	if (desc.GetHopCount() == 1)
		return true;

	const ibSourceDataObject* source = holder->GetSourceValue();
	return source != nullptr && source->IsMainSourceNode(desc.GetLeaf());
}

// …and therefore no bar of its own: the form's toolbar already serves these verbs, because the
// form's command provider resolves to this very view. Two bars would put Compose in two places.
bool ibValueGridBox::HasCommandBar() const
{
	if (IsMainSourceBound())
		return false;
	return ibValueFrame::HasCommandBar();
}

//****************************************************************************
//*   The model — built from the source, asked everything                    *
//****************************************************************************

// The settings behind the box. The composer LIVES IN THE MODEL — it IS one — so this is the model
// when the model happens to be a composer, and nothing when it is a plain sheet. The ONE place that
// tells the two apart.
// ⭐⭐ TAKE THE MODEL THE BINDING NAMES — the tablebox's mechanism, ported (its RefreshModel →
// CreateModel pair, [tableBox.cpp:344]). A control is never TOLD its source changed: it goes and
// reads it, at the three moments the binding can differ from what is held —
//
//   * the form is built      (InitializeControl, the "before run" hook every control gets);
//   * the window is created  (Create — the fresh window has to be handed a sheet);
//   * the Source is set      (the designer's property edit, or SetSource from code: SetValue fires
//                             no event, which is why the setter calls this itself).
//
// One function, not the sibling's two: there is no table to re-create here, so the second name would
// only forward to the first.
void ibValueGridBox::RefreshModel()
{
	const ibValueForm* form = GetOwnerForm();

	if (form != nullptr && !m_propertySource->IsEmptyProperty()) {
		// The form walks its own attributes — this control neither knows nor stores the path's
		// meaning. What comes back IS a model (a report's composer, a document attribute), and the
		// ONE door takes it from here.
		ibValue bound;
		if (form->GetValueByAttributePath(m_propertySource->GetValueAsSourceDesc(), bound) &&
			SetControlValue(bound))
			return;
	}

	// NOTHING BOUND — or a binding that resolves to something this box cannot show. A gridbox falls
	// back to a sheet OF ITS OWN, which is what keeps a bare box a usable scratch sheet, and is the
	// twin of the table re-creating its own value-table when the source no longer matches.
	//
	// 🛑 A DOCUMENT ALREADY HELD IS KEPT. Its content is the DESIGNER's — typed into the box and
	// serialised with it (ReadData reads into the model's sheet), so replacing it on every refresh
	// would quietly wipe a template every time a property was touched. Only a COMPOSER left over
	// from a previous binding is dropped, and its sheet was never anybody's input.
	ibValueSpreadsheetModel* held = m_spreadsheetModel;
	if (dynamic_cast<ibValueSpreadsheetDocument*>(held) == nullptr)
		SetControlValue(ibValuePtr<ibValueSpreadsheetDocument>(new ibValueSpreadsheetDocument()));
}

ibValueDataComposition* ibValueGridBox::ResolveComposition() const
{
	// The typed view the pointer already gives — no cast of its own. (A C-style cast stood here and
	// read like a const_cast in disguise, which is exactly what it must not be: ibValuePtr hands out
	// a non-const T* by design, so nothing has to be cast away.)
	ibValueSpreadsheetModel* model = m_spreadsheetModel;
	return dynamic_cast<ibValueDataComposition*>(model);
}

