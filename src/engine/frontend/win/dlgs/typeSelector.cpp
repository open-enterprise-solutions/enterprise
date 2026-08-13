////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the type picker — one dialog, several callers
////////////////////////////////////////////////////////////////////////////

#include "typeSelector.h"

#include "backend/metaData.h"
#include "backend/objCtor.h"          // ibCtorMetaValueType — the ctor a reference clsid resolves to
#include "backend/compiler/value.h"
#include "frontend/win/ctrls/checktree.h"

#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/imaglist.h>
#include <wx/spinctrl.h>
#include <wx/choice.h>
#include <wx/stattext.h>

#include <map>
#include <algorithm>

namespace {

// One tree node's payload — the ctor it stands for, which is where its clsid comes from.
class ibTypeItemData : public wxTreeItemData {
	const ibCtorAbstractType* m_typeCtor;
public:
	ibTypeItemData(const ibCtorAbstractType* typeCtor) : wxTreeItemData(), m_typeCtor(typeCtor) {}
	ibClassID GetClassType() const { return m_typeCtor->GetClassType(); }
};

// A LEAF — one type offered directly.
void AppendType(ibCheckTree* tc, const wxTreeItemId& parent, const ibCtorAbstractType* so,
	const ibTypeDescription& current, bool allowEdit)
{
	if (so == nullptr)
		return;

	wxImageList* imageList = tc->GetImageList();
	wxASSERT(imageList);

	const int icon = imageList->Add(so->GetClassIcon());
	const wxTreeItemId item = tc->AppendItem(parent, so->GetClassName(), icon, icon, new ibTypeItemData(so));

	const bool held = current.ContainType(so->GetClassType());
	tc->SetItemState(item, held
		? (allowEdit ? ibCheckTree::CHECKED : ibCheckTree::CHECKED_DISABLED)
		: (allowEdit ? ibCheckTree::UNCHECKED : ibCheckTree::UNCHECKED_DISABLED));
	tc->Check(item, held);
}

} // namespace

// WHAT THIS SHAPE OFFERS. Built here, from the registry, so no caller keeps a list of metatypes
// that has to learn about each new one — and so the two callers cannot drift apart.
static std::vector<ibClassID> ibTypesForKind(ibSelectorDataType kind, const ibMetaData* metaData)
{
	std::vector<ibClassID> types;

	const bool anyType = kind == ibSelectorDataType::ibSelectorDataType_any;

	if (anyType)
		types.push_back(ibValue::GetIDByVT(ibValueTypes::TYPE_EMPTY));

	// The primitives. A reference shape carries them too: a characteristic may be a number or a
	// string just as well as a reference to something.
	if (anyType || kind == ibSelectorDataType::ibSelectorDataType_reference) {
		types.push_back(ibValue::GetIDByVT(ibValueTypes::TYPE_BOOLEAN));
		types.push_back(ibValue::GetIDByVT(ibValueTypes::TYPE_NUMBER));
		types.push_back(ibValue::GetIDByVT(ibValueTypes::TYPE_DATE));
		types.push_back(ibValue::GetIDByVT(ibValueTypes::TYPE_STRING));
	}
	else if (kind == ibSelectorDataType::ibSelectorDataType_boolean) {
		types.push_back(ibValue::GetIDByVT(ibValueTypes::TYPE_BOOLEAN));
		types.push_back(ibValue::GetIDByVT(ibValueTypes::TYPE_NUMBER));
	}
	else if (kind == ibSelectorDataType::ibSelectorDataType_resource) {
		types.push_back(ibValue::GetIDByVT(ibValueTypes::TYPE_NUMBER));
	}

	if (anyType)
		types.push_back(ibValue::GetIDByVT(ibValueTypes::TYPE_NULL));

	if (metaData == nullptr)
		return types;

	// EVERYTHING REFERENCEABLE, asked of the registry. A table shape wants the tabular sources
	// instead — those are its references.
	if (anyType || kind == ibSelectorDataType::ibSelectorDataType_reference ||
		kind == ibSelectorDataType::ibSelectorDataType_table) {
		for (auto so : metaData->GetListCtorsByType(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference))
			types.push_back(so->GetClassType());
		for (auto so : metaData->GetListCtorsByType(ibCtorObjectMetaType::ibCtorObjectMetaType_Characteristic))
			types.push_back(so->GetClassType());
	}

	return types;
}

bool ibShowTypeSelector(wxWindow* parent, ibSelectorDataType kind,
	const std::vector<ibClassID>& filter, ibTypeDescription& inOut, const ibMetaData* metaData,
	bool allowEdit, bool single)
{
	// The shape says what is on offer; the filter, when given, narrows it to a declared set. A type
	// the value ALREADY holds survives either way — an editor must not silently drop what it was
	// opened on.
	std::vector<ibClassID> allowed;
	for (const ibClassID& clsid : ibTypesForKind(kind, metaData)) {
		if (filter.empty() || std::find(filter.begin(), filter.end(), clsid) != filter.end())
			allowed.push_back(clsid);
	}
	for (const ibClassID& held : inOut.GetClsidList()) {
		if (std::find(allowed.begin(), allowed.end(), held) == allowed.end())
			allowed.push_back(held);
	}

	// The title names the ACTION and its OBJECT. "Choice type" was a calque — it read as a noun
	// phrase about a kind of choice, which is neither what the window does nor what it is about.
	wxDialog* dlg = new wxDialog(parent, wxID_ANY, _("Select data type"), wxDefaultPosition, wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxCLIP_CHILDREN);

	wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);

	// COMPOSITE OR NOT is the USER's choice, and it stays one — a declaration may legitimately admit
	// several types, and this checkbox is how that is said. `single` is the other question: whether
	// the caller allows the choice at all (a BINDING names exactly one), and where it does not, the
	// checkbox is not offered rather than offered and ignored.
	wxCheckBox* compositeDataType = new wxCheckBox(dlg, wxID_ANY, _("Composite data type"));
	compositeDataType->Enable(allowEdit);
	compositeDataType->Show(!single);

	// Opened on what the value already IS: a description holding several types comes back with the
	// box ticked and the tree in multiple mode.
	const bool composite = !single && inOut.GetClsidCount() > 1;
	compositeDataType->SetValue(composite);

	const int style = composite ? wxCR_MULTIPLE_CHECK : wxCR_SINGLE_CHECK;

	ibCheckTree* tc = new ibCheckTree(dlg, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxTR_HIDE_ROOT | style |
		wxSUNKEN_BORDER | wxTR_TWIST_BUTTONS);

	tc->AssignImageList(new wxImageList(16, 16));
	tc->AddRoot(wxEmptyString);

	compositeDataType->Bind(wxEVT_COMMAND_CHECKBOX_CLICKED, [tc](wxCommandEvent& event) {
		tc->SetWindowStyle(event.IsChecked() ? wxCR_MULTIPLE_CHECK : wxCR_SINGLE_CHECK);
		event.Skip();
	});

	topsizer->Add(compositeDataType, wxSizerFlags(0).Border(wxALL, dlg->FromDIP(5)));
	topsizer->Add(tc, wxSizerFlags(1).Expand().Border(wxALL, dlg->FromDIP(5)));

	// THE QUALIFIERS — length, precision, scale, date fraction. They belong to the TYPE, so they
	// live with the type picker rather than beside it, and they are shown only for the type they
	// mean anything for: a string has a length, a number has precision and scale, a date has its
	// fraction, and a reference has none of them.
	//
	// Edited into a WORKING COPY, not into the caller's description: Cancel must leave what was
	// passed in untouched, and a qualifier changed before pressing Cancel is still a change.
	ibTypeDescription working = inOut;

	wxBoxSizer* stringSizer = new wxBoxSizer(wxHORIZONTAL);
	wxStaticText* stSLength = new wxStaticText(dlg, wxID_ANY, _("Length:"));
	stringSizer->Add(stSLength, 0, wxALL, dlg->FromDIP(5));
	wxSpinCtrl* tcSLength = new wxSpinCtrl(dlg, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
		wxSP_ARROW_KEYS, 0, MAX_LENGTH_STRING);
	tcSLength->SetValue(working.GetLength());
	tcSLength->Enable(allowEdit);
	stringSizer->Add(tcSLength, 0, wxBOTTOM | wxRIGHT, 0);
	tcSLength->Bind(wxEVT_COMMAND_SPINCTRL_UPDATED, [&working](wxSpinEvent& event) {
		working.SetString(event.GetValue());
		event.Skip();
	});
	topsizer->Add(stringSizer, 0, 0, dlg->FromDIP(5));

	wxBoxSizer* numberSizer = new wxBoxSizer(wxHORIZONTAL);
	wxStaticText* stNLength = new wxStaticText(dlg, wxID_ANY, _("Length:"));
	numberSizer->Add(stNLength, 0, wxALL, dlg->FromDIP(5));
	wxSpinCtrl* tcNLength = new wxSpinCtrl(dlg, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
		wxSP_ARROW_KEYS, 0, MAX_PRECISION_NUMBER);
	tcNLength->SetValue(working.GetPrecision());
	tcNLength->Enable(allowEdit);
	numberSizer->Add(tcNLength, 0, wxBOTTOM | wxRIGHT, 0);
	tcNLength->Bind(wxEVT_COMMAND_SPINCTRL_UPDATED, [&working](wxSpinEvent& event) {
		working.SetNumber(event.GetValue(), working.GetScale());
		event.Skip();
	});

	wxStaticText* stNScale = new wxStaticText(dlg, wxID_ANY, _("Scale:"));
	numberSizer->Add(stNScale, 0, wxTOP | wxBOTTOM | wxLEFT, dlg->FromDIP(5));
	wxSpinCtrl* tcNScale = new wxSpinCtrl(dlg, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS);
	tcNScale->SetValue(working.GetScale());
	tcNScale->Enable(allowEdit);
	numberSizer->Add(tcNScale, 0, wxRIGHT | wxLEFT, dlg->FromDIP(5));
	tcNScale->Bind(wxEVT_COMMAND_SPINCTRL_UPDATED, [&working](wxSpinEvent& event) {
		// Scale never exceeds precision — a number cannot keep more decimals than it has digits.
		const unsigned short scale = working.GetPrecision() > event.GetValue()
			? event.GetValue() : working.GetPrecision();
		working.SetNumber(working.GetPrecision(), scale);
		event.Skip();
	});
	topsizer->Add(numberSizer, 0, 0, dlg->FromDIP(5));

	wxBoxSizer* dateSizer = new wxBoxSizer(wxHORIZONTAL);
	wxStaticText* stDFormat = new wxStaticText(dlg, wxID_ANY, _("Date format:"));
	dateSizer->Add(stDFormat, 0, wxALL, dlg->FromDIP(5));
	wxChoice* cDDateFormat = new wxChoice(dlg, wxID_ANY);
	cDDateFormat->Append(_("Date"));
	cDDateFormat->Append(_("Date and time"));
	cDDateFormat->Append(_("Time"));
	cDDateFormat->SetSelection(working.GetDateFraction());
	cDDateFormat->Enable(allowEdit);
	dateSizer->Add(cDDateFormat, 0, wxBOTTOM | wxRIGHT, dlg->FromDIP(5));
	cDDateFormat->Bind(wxEVT_COMMAND_CHOICE_SELECTED, [&working](wxCommandEvent& event) {
		working.SetDate((ibDateFractions)event.GetSelection());
		event.Skip();
	});
	topsizer->Add(dateSizer, 0, 0, dlg->FromDIP(5));

	topsizer->Add(dlg->CreateStdDialogButtonSizer(wxOK | wxCANCEL),
		wxSizerFlags(0).Right().Border(wxBOTTOM | wxRIGHT, dlg->FromDIP(5)));

	// Only the panel for the type standing under the cursor is shown — the same rule the metadata
	// editor followed, kept because it is what makes the dialog readable: three rows of spin
	// controls that mean nothing for a reference are noise, not information.
	auto showQualifiersFor = [=](const ibClassID& clsid) {
		const ibValueTypes vt = ibValue::GetVTByID(clsid);
		for (unsigned int i = 0; i < numberSizer->GetItemCount(); i++) numberSizer->Show(i, vt == ibValueTypes::TYPE_NUMBER);
		for (unsigned int i = 0; i < dateSizer->GetItemCount(); i++)   dateSizer->Show(i, vt == ibValueTypes::TYPE_DATE);
		for (unsigned int i = 0; i < stringSizer->GetItemCount(); i++) stringSizer->Show(i, vt == ibValueTypes::TYPE_STRING);
		topsizer->Layout();
	};

	for (unsigned int i = 0; i < numberSizer->GetItemCount(); i++) numberSizer->Hide(i);
	for (unsigned int i = 0; i < dateSizer->GetItemCount(); i++)   dateSizer->Hide(i);
	for (unsigned int i = 0; i < stringSizer->GetItemCount(); i++) stringSizer->Hide(i);

	tc->Bind(wxEVT_COMMAND_TREE_SEL_CHANGED, [tc, showQualifiersFor](wxTreeEvent& event) {
		const ibTypeItemData* item = dynamic_cast<ibTypeItemData*>(tc->GetItemData(event.GetItem()));
		if (item != nullptr)
			showQualifiersFor(item->GetClassType());
		event.Skip();
	});

	// SORTED INTO CATEGORIES HERE, by the id itself: a reference goes under the group of the kind it
	// points at ("CatalogRef"), a plain type stands at the root. Nobody outside decides this.
	std::map<wxString, wxTreeItemId> groups;

	for (const ibClassID& clsid : allowed) {
		const ibCtorAbstractType* so = metaData != nullptr ? metaData->GetAvailableCtor(clsid) : ibValue::GetAvailableCtor(clsid);
		if (so == nullptr)
			continue;

		wxTreeItemId parentItem = tc->GetRootItem();

		if (IsReference(clsid)) {
			// The group is named after the METATYPE the reference belongs to, so every catalog lands
			// under one heading without anyone passing a list of headings in.
			const ibCtorMetaValueType* metaCtor = metaData != nullptr ? metaData->GetTypeCtor(clsid) : nullptr;
			const ibValueMetaObject* owner = metaCtor != nullptr ? metaCtor->GetMetaObject() : nullptr;
			const wxString groupName = owner != nullptr
				? ibValue::GetNameObjectFromID(owner->GetClassType()) + wxT("Ref")
				: _("References");

			auto it = groups.find(groupName);
			if (it == groups.end()) {
				wxImageList* imageList = tc->GetImageList();
				const int groupIcon = imageList->Add(so->GetClassIcon());
				it = groups.emplace(groupName, tc->AppendItem(tc->GetRootItem(), groupName, groupIcon, groupIcon)).first;
			}
			parentItem = it->second;
		}

		AppendType(tc, parentItem, so, inOut, allowEdit);
	}

	tc->ExpandAll();
	tc->SetDoubleBuffered(true);

	dlg->SetSizer(topsizer);
	topsizer->SetSizeHints(dlg);
	dlg->SetSize(dlg->FromDIP(wxSize(400, 300)));
	dlg->CenterOnParent();

	const int result = dlg->ShowModal();
	if (result == wxID_OK) {
		// The chosen types replace the old set; the qualifiers come from the working copy the spin
		// controls have been editing. Cancel takes neither — which is why nothing above this line
		// ever touched `inOut`.
		inOut.ClearMetaType();

		// COMPOSITE OFF MEANS ONE. The checkbox is not decoration over a multi-select tree: with it
		// clear only one type may be held, so the first ticked is the answer and the rest are not
		// silently appended. `single` says the same thing from the caller's side.
		const bool severalAllowed = compositeDataType->IsShown() && compositeDataType->GetValue();

		wxArrayTreeItemIds ids;
		tc->GetSelections(ids);
		for (const wxTreeItemId& item : ids) {
			if (!item.IsOk())
				continue;
			const ibTypeItemData* picked = dynamic_cast<ibTypeItemData*>(tc->GetItemData(item));
			if (picked != nullptr) {
				inOut.AppendMetaType(picked->GetClassType());
				if (!severalAllowed)
					break;
			}
		}

		inOut.SetTypeData(working.GetTypeData());
	}

	dlg->Destroy();
	return result == wxID_OK;
}
