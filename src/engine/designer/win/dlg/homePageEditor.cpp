////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : home page workspace editor (designer)
////////////////////////////////////////////////////////////////////////////

#include "homePageEditor.h"

#include <wx/treectrl.h>
#include <wx/imaglist.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/artprov.h>
#include <wx/numdlg.h>
#include <wx/msgdlg.h>

#include "backend/metaData.h"
#include "backend/backend_picture.h"                 // ibBackendPicture::GetPicture — the toolbar art
#include "backend/metaCollection/metaFormObject.h"
#include "frontend/win/theme/luna_toolbarart.h"

#include <map>

enum {
	model_form = 0,
	model_height,
};

//********************************************************************************************
//*                                    workspace editor                                      *
//********************************************************************************************

ibDialogHomePageEditor::ibDialogHomePageEditor(wxWindow* parent, ibValueMetaObjectConfiguration* metaObject)
	: wxDialog(parent, wxID_ANY, _("Home page workspace"),
		wxDefaultPosition, wxSize(760, 480), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	m_metaObject(metaObject)
{
	if (m_metaObject != nullptr)
		m_description = m_metaObject->GetHomePage();

	CreateDialogView();

	FillColumn(eHomePageColumn_Left);
	FillColumn(eHomePageColumn_Right);
}

wxWindow* ibDialogHomePageEditor::CreateColumnPane(wxWindow* parent, ibHomePageColumn column, const wxString& caption)
{
	wxPanel* pane = new wxPanel(parent, wxID_ANY);
	wxBoxSizer* paneSizer = new wxBoxSizer(wxVERTICAL);
	pane->SetSizer(paneSizer);

	wxStaticText* paneCaption = new wxStaticText(pane, wxID_ANY, caption);
	paneSizer->Add(paneCaption, 0, wxALL, FromDIP(3));

	wxAuiToolBar* toolbar = new wxAuiToolBar(pane, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_LAYOUT);
	toolbar->SetArtProvider(new wxAuiLunaToolBarArt());

	toolbar->AddTool(wxID_TOOL_ADD, _("Add"), ibBackendPicture::GetPicture(g_picAddCLSID));
	toolbar->AddTool(wxID_TOOL_EDIT, _("Height"), ibBackendPicture::GetPicture(g_picEditCLSID));
	toolbar->AddTool(wxID_TOOL_DELETE, _("Delete"), ibBackendPicture::GetPicture(g_picDeleteCLSID));
	toolbar->AddSeparator();
	toolbar->AddTool(wxID_TOOL_UP, _("Up"), wxArtProvider::GetBitmapBundle(wxART_GO_UP, wxART_TOOLBAR, FromDIP(wxSize(16, 16))));
	toolbar->AddTool(wxID_TOOL_DOWN, _("Down"), wxArtProvider::GetBitmapBundle(wxART_GO_DOWN, wxART_TOOLBAR, FromDIP(wxSize(16, 16))));
	toolbar->AddSeparator();
	// The move-across tools mirror the two arrows between the panes in the reference UI: an
	// item is moved to the OTHER column, never copied.
	toolbar->AddTool(column == eHomePageColumn_Left ? wxID_TOOL_TO_RIGHT : wxID_TOOL_TO_LEFT,
		column == eHomePageColumn_Left ? _("Move to right column") : _("Move to left column"),
		wxArtProvider::GetBitmapBundle(column == eHomePageColumn_Left ? wxART_GO_FORWARD : wxART_GO_BACK,
			wxART_TOOLBAR, FromDIP(wxSize(16, 16))));

	toolbar->Realize();
	toolbar->Bind(wxEVT_MENU, &ibDialogHomePageEditor::OnCommandMenu, this);

	paneSizer->Add(toolbar, 0, wxEXPAND);

	wxListCtrl* listCtrl = new wxListCtrl(pane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxLC_REPORT | wxLC_SINGLE_SEL);

	// Every row carries the FORM metaobject's own icon — the same glyph the metadata tree
	// shows. Without it the workspace reads as an anonymous list of strings; with it, a
	// glance says "these are forms". The image list is owned by the control.
	listCtrl->AssignImageList(new wxImageList(16, 16), wxIMAGE_LIST_SMALL);

	// The check state IS the item's visibility — the same column the reference UI shows.
	listCtrl->EnableCheckBoxes(true);
	listCtrl->AppendColumn(_("Form"), wxLIST_FORMAT_LEFT, FromDIP(240));
	listCtrl->AppendColumn(_("Height"), wxLIST_FORMAT_RIGHT, FromDIP(70));

	listCtrl->Bind(wxEVT_LIST_ITEM_ACTIVATED, &ibDialogHomePageEditor::OnItemActivated, this);
	listCtrl->Bind(wxEVT_LIST_ITEM_CHECKED, &ibDialogHomePageEditor::OnItemChecked, this);
	listCtrl->Bind(wxEVT_LIST_ITEM_UNCHECKED, &ibDialogHomePageEditor::OnItemChecked, this);

	paneSizer->Add(listCtrl, 1, wxEXPAND | wxTOP, FromDIP(3));

	m_pane[column] = pane;
	m_toolbar[column] = toolbar;
	m_listCtrl[column] = listCtrl;
	m_paneCaption[column] = paneCaption;

	return pane;
}

void ibDialogHomePageEditor::CreateDialogView()
{
	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer* columnsSizer = new wxBoxSizer(wxHORIZONTAL);
	columnsSizer->Add(CreateColumnPane(this, eHomePageColumn_Left, _("Left column:")), 1, wxEXPAND | wxALL, FromDIP(4));
	columnsSizer->Add(CreateColumnPane(this, eHomePageColumn_Right, _("Right column:")), 1, wxEXPAND | wxALL, FromDIP(4));
	mainSizer->Add(columnsSizer, 1, wxEXPAND);

	wxBoxSizer* templateSizer = new wxBoxSizer(wxHORIZONTAL);
	templateSizer->Add(new wxStaticText(this, wxID_ANY, _("Home page template:")), 0,
		wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(4));

	m_templateChoice = new wxChoice(this, wxID_ANY);
	// Order matches ibHomePageTemplate — the selection index IS the template.
	m_templateChoice->Append(_("One column"));
	m_templateChoice->Append(_("Two columns of equal width"));
	m_templateChoice->Append(_("Two columns, wide left (2:1)"));
	m_templateChoice->Append(_("Two columns, wide right (1:2)"));
	m_templateChoice->SetSelection((int)m_description.GetTemplate());
	m_templateChoice->Bind(wxEVT_CHOICE, &ibDialogHomePageEditor::OnTemplateChanged, this);
	templateSizer->Add(m_templateChoice, 1, wxALL, FromDIP(4));

	mainSizer->Add(templateSizer, 0, wxEXPAND);

	wxStdDialogButtonSizer* buttonSizer = new wxStdDialogButtonSizer();
	m_buttonOK = new wxButton(this, wxID_OK);
	m_buttonOK->Bind(wxEVT_BUTTON, &ibDialogHomePageEditor::OnCommandOK, this);
	buttonSizer->AddButton(m_buttonOK);
	wxButton* buttonCancel = new wxButton(this, wxID_CANCEL);
	buttonSizer->AddButton(buttonCancel);
	buttonSizer->Realize();

	mainSizer->Add(buttonSizer, 0, wxEXPAND | wxALL, FromDIP(5));

	// A locked configuration (another user's exclusive edit) shows the workspace but commits
	// nothing — the same read-only rule every other metadata editor obeys.
	if (m_metaObject != nullptr && !m_metaObject->IsEditable()) {
		m_buttonOK->Enable(false);
		for (int idx = eHomePageColumn_Left; idx < eHomePageColumn_Count; idx++)
			m_toolbar[idx]->Enable(false);
		m_templateChoice->Enable(false);
	}

	SetSizer(mainSizer);

	// The stored template decides how many panes are up when the dialog opens. No folding
	// here — the description is only ever folded by an explicit switch, not by opening.
	ApplyTemplateLayout(/*foldColumns*/ false);

	Layout();
	Centre(wxBOTH);

	SetIcon(ibBackendPicture::GetPictureAsIcon(g_picHomePageCLSID));
}

const ibValueMetaObjectFormBase* ibDialogHomePageEditor::FindItemForm(const ibHomePageItem& item) const
{
	const ibMetaData* const metaData = m_metaObject != nullptr ? m_metaObject->GetMetaData() : nullptr;
	if (metaData == nullptr)
		return nullptr;

	// Resolved LIVE on every read — a form deleted since it was attached reads as missing
	// instead of quietly disappearing from the list.
	return metaData->FindAnyObjectByFilter<ibValueMetaObjectFormBase>(item.m_formId,
		{ g_metaCommonFormCLSID, g_metaFormCLSID }, true);
}

void ibDialogHomePageEditor::FillColumn(ibHomePageColumn column, long selectIndex)
{
	wxListCtrl* const listCtrl = m_listCtrl[column];
	if (listCtrl == nullptr)
		return;

	listCtrl->DeleteAllItems();

	wxImageList* const imageList = listCtrl->GetImageList(wxIMAGE_LIST_SMALL);
	if (imageList != nullptr)
		imageList->RemoveAll();

	const std::vector<ibHomePageItem>& items = m_description.GetColumn(column);

	for (size_t idx = 0; idx < items.size(); idx++) {
		const ibHomePageItem& item = items[idx];
		const ibValueMetaObjectFormBase* const metaForm = FindItemForm(item);

		// Icon per row: the form's own, or the workspace glyph for one that is gone.
		int imageIndex = -1;
		if (imageList != nullptr) {
			const wxIcon rowIcon = metaForm != nullptr ?
				metaForm->GetIcon() : ibBackendPicture::GetPictureAsIcon(g_picHomePageCLSID);
			if (rowIcon.IsOk())
				imageIndex = imageList->Add(rowIcon);
		}

		const long row = listCtrl->InsertItem((long)idx,
			metaForm != nullptr ? metaForm->GetFullName() : _("<not found>"), imageIndex);
		// 0 means "share equally" — say so rather than showing a bare zero.
		listCtrl->SetItem(row, model_height, item.m_height != 0 ?
			wxString::Format(wxT("%u"), item.m_height) : wxString(_("same")));
		listCtrl->CheckItem(row, item.m_visible);
	}

	if (selectIndex != wxNOT_FOUND && selectIndex < listCtrl->GetItemCount())
		listCtrl->SetItemState(selectIndex, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
}

ibHomePageColumn ibDialogHomePageEditor::ColumnOfList(const wxObject* listCtrl) const
{
	return listCtrl == m_listCtrl[eHomePageColumn_Right] ? eHomePageColumn_Right : eHomePageColumn_Left;
}

ibHomePageColumn ibDialogHomePageEditor::ColumnOfEvent(const wxCommandEvent& event) const
{
	// The two toolbars share one handler; the event object says which one fired. The
	// move-across ids are unambiguous on their own.
	if (event.GetId() == wxID_TOOL_TO_RIGHT) return eHomePageColumn_Left;
	if (event.GetId() == wxID_TOOL_TO_LEFT)  return eHomePageColumn_Right;
	return event.GetEventObject() == m_toolbar[eHomePageColumn_Right] ?
		eHomePageColumn_Right : eHomePageColumn_Left;
}

long ibDialogHomePageEditor::GetSelection(ibHomePageColumn column) const
{
	wxListCtrl* const listCtrl = m_listCtrl[column];
	return listCtrl != nullptr ?
		listCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) : wxNOT_FOUND;
}

void ibDialogHomePageEditor::OnCommandMenu(wxCommandEvent& event)
{
	const ibHomePageColumn column = ColumnOfEvent(event);
	const long selection = GetSelection(column);

	if (event.GetId() == wxID_TOOL_ADD) {

		ibDialogHomePageFormSelect dlg(this, m_metaObject != nullptr ? m_metaObject->GetMetaData() : nullptr);
		if (dlg.ShowModal() == wxID_OK && dlg.GetSelectedForm() != wxNOT_FOUND) {
			// The same form MAY be attached twice: the runtime builds a form value per open
			// (no compile cache outside the designer), so two cells are two independent forms
			// — the same thing two tabs of one list would be.
			m_description.AppendItem(column, ibHomePageItem(dlg.GetSelectedForm()));
			FillColumn(column, (long)m_description.GetColumn(column).size() - 1);
		}
	}
	else if (event.GetId() == wxID_TOOL_EDIT) {

		if (selection == wxNOT_FOUND)
			return;

		ibHomePageItem& item = m_description.GetColumn(column)[selection];
		// The height is a share of the column, not pixels — 0 keeps the item equal to the
		// other unset ones.
		const long height = wxGetNumberFromUser(
			_("Height share of the column (0 - same as the others)"),
			_("Height"), _("Home page workspace"), (long)item.m_height, 0, 100, this);

		if (height >= 0) {
			item.m_height = (unsigned int)height;
			FillColumn(column, selection);
		}
	}
	else if (event.GetId() == wxID_TOOL_DELETE) {

		if (selection != wxNOT_FOUND && m_description.RemoveItem(column, (unsigned int)selection))
			FillColumn(column, selection > 0 ? selection - 1 : wxNOT_FOUND);
	}
	else if (event.GetId() == wxID_TOOL_UP || event.GetId() == wxID_TOOL_DOWN) {

		const int offset = event.GetId() == wxID_TOOL_UP ? -1 : 1;
		if (selection != wxNOT_FOUND && m_description.MoveItem(column, (unsigned int)selection, offset))
			FillColumn(column, selection + offset);
	}
	else if (event.GetId() == wxID_TOOL_TO_RIGHT || event.GetId() == wxID_TOOL_TO_LEFT) {

		if (selection == wxNOT_FOUND)
			return;

		const ibHomePageColumn target = column == eHomePageColumn_Left ?
			eHomePageColumn_Right : eHomePageColumn_Left;

		const ibHomePageItem moved = m_description.GetColumn(column)[selection];
		m_description.RemoveItem(column, (unsigned int)selection);
		m_description.AppendItem(target, moved);

		FillColumn(column);
		FillColumn(target, (long)m_description.GetColumn(target).size() - 1);
	}
}

void ibDialogHomePageEditor::OnItemActivated(wxListEvent& event)
{
	// Double-click edits the height — the only per-item value besides visibility.
	wxCommandEvent editEvent(wxEVT_MENU, wxID_TOOL_EDIT);
	editEvent.SetEventObject(m_toolbar[ColumnOfList(event.GetEventObject())]);
	OnCommandMenu(editEvent);
}

void ibDialogHomePageEditor::OnItemChecked(wxListEvent& event)
{
	const ibHomePageColumn column = ColumnOfList(event.GetEventObject());
	std::vector<ibHomePageItem>& items = m_description.GetColumn(column);

	const long index = event.GetIndex();
	if (index >= 0 && index < (long)items.size())
		items[index].m_visible = (event.GetEventType() == wxEVT_LIST_ITEM_CHECKED);
}

void ibDialogHomePageEditor::ApplyTemplateLayout(bool foldColumns)
{
	const bool twoColumns = m_description.IsTwoColumns();

	if (!twoColumns && foldColumns) {
		// One column now — move what the right pane held to the END of the left one. The user
		// SEES the single order that will render instead of items surviving in a pane that is
		// no longer on screen.
		for (const ibHomePageItem& item : m_description.GetColumn(eHomePageColumn_Right))
			m_description.AppendItem(eHomePageColumn_Left, item);
		m_description.GetColumn(eHomePageColumn_Right).clear();
		FillColumn(eHomePageColumn_Left);
		FillColumn(eHomePageColumn_Right);
	}

	// A one-column workspace has ONE list, and it is not "the left one" any more.
	m_paneCaption[eHomePageColumn_Left]->SetLabel(twoColumns ? _("Left column:") : _("Forms:"));
	m_pane[eHomePageColumn_Right]->Show(twoColumns);

	// The move-across tool has nowhere to move to while there is one column.
	m_toolbar[eHomePageColumn_Left]->EnableTool(wxID_TOOL_TO_RIGHT, twoColumns);
	m_toolbar[eHomePageColumn_Left]->Refresh();

	Layout();
}

void ibDialogHomePageEditor::OnTemplateChanged(wxCommandEvent& event)
{
	m_description.SetTemplate((ibHomePageTemplate)m_templateChoice->GetSelection());
	ApplyTemplateLayout(/*foldColumns*/ true);

	event.Skip();
}

void ibDialogHomePageEditor::OnCommandOK(wxCommandEvent& event)
{
	if (m_metaObject != nullptr && m_metaObject->IsEditable()) {

		// The template is already on the working copy (OnTemplateChanged set it, and the
		// column layout followed it) — only the commit is left.
		if (m_metaObject->GetHomePage() != m_description) {
			m_metaObject->SetHomePage(m_description);
			if (ibMetaData* metaData = m_metaObject->GetMetaData())
				metaData->Modify(true);
		}
	}

	event.Skip();
}

//********************************************************************************************
//*                                     form picker                                          *
//********************************************************************************************

ibDialogHomePageFormSelect::ibDialogHomePageFormSelect(wxWindow* parent, const ibMetaData* metaData)
	: wxDialog(parent, wxID_ANY, _("Select form"),
		wxDefaultPosition, wxSize(420, 480), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	m_metaData(metaData)
{
	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	m_treeCtrl = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_LINES_AT_ROOT | wxTR_HIDE_ROOT);

	// Same rule as the workspace list: every row shows the metaobject's own icon, so a form
	// reads as a form and its owner as its own kind. Owned by the control.
	m_treeCtrl->AssignImageList(new wxImageList(16, 16));

	m_treeCtrl->Bind(wxEVT_TREE_SEL_CHANGED, &ibDialogHomePageFormSelect::OnSelectionChanged, this);
	m_treeCtrl->Bind(wxEVT_TREE_ITEM_ACTIVATED, &ibDialogHomePageFormSelect::OnItemActivated, this);

	mainSizer->Add(m_treeCtrl, 1, wxEXPAND | wxALL, FromDIP(5));

	wxStdDialogButtonSizer* buttonSizer = new wxStdDialogButtonSizer();
	m_buttonOK = new wxButton(this, wxID_OK);
	m_buttonOK->Enable(false);   // nothing selected yet — a group row is not an answer
	buttonSizer->AddButton(m_buttonOK);
	buttonSizer->AddButton(new wxButton(this, wxID_CANCEL));
	buttonSizer->Realize();

	mainSizer->Add(buttonSizer, 0, wxEXPAND | wxALL, FromDIP(5));

	SetSizer(mainSizer);
	Layout();
	Centre(wxBOTH);

	FillTree();
}

namespace {

// A tree row that stands for one form metaobject; a group row carries none.
class ibHomePageFormItemData : public wxTreeItemData {
public:
	explicit ibHomePageFormItemData(const ibMetaID& formId) : m_formId(formId) {}
	ibMetaID GetFormId() const { return m_formId; }
private:
	ibMetaID m_formId;
};

} // namespace

int ibDialogHomePageFormSelect::AppendIcon(const wxIcon& icon)
{
	wxImageList* const imageList = m_treeCtrl->GetImageList();
	if (imageList == nullptr || !icon.IsOk())
		return -1;
	return imageList->Add(icon);
}

void ibDialogHomePageFormSelect::FillTree()
{
	const wxTreeItemId root = m_treeCtrl->AddRoot(wxT("root"));

	if (m_metaData == nullptr)
		return;

	// Every form the configuration owns, grouped under its owner — a common form has none, so
	// it files under its own group. Grouping is by owner identity, so the tree follows the
	// metadata tree without duplicating its walk.
	std::map<const ibValueMetaObject*, wxTreeItemId> groups;
	wxTreeItemId commonGroup;

	for (ibValueMetaObjectFormBase* metaForm :
		m_metaData->GetAnyArrayObject<ibValueMetaObjectFormBase>({ g_metaCommonFormCLSID, g_metaFormCLSID }, true)) {

		if (metaForm == nullptr || !metaForm->IsAllowed())
			continue;

		wxTreeItemId parentNode;

		if (metaForm->GetClassType() == g_metaCommonFormCLSID) {
			if (!commonGroup.IsOk())
				commonGroup = m_treeCtrl->AppendItem(root, _("Common forms"),
					AppendIcon(metaForm->GetIcon()), AppendIcon(metaForm->GetIcon()));
			parentNode = commonGroup;
		}
		else {
			const ibValueMetaObject* const owner = metaForm->GetParent();
			if (owner == nullptr)
				continue;
			auto founded = groups.find(owner);
			if (founded == groups.end()) {
				const int ownerIcon = AppendIcon(owner->GetIcon());
				founded = groups.emplace(owner,
					m_treeCtrl->AppendItem(root, owner->GetFullName(), ownerIcon, ownerIcon)).first;
			}
			parentNode = founded->second;
		}

		const int formIcon = AppendIcon(metaForm->GetIcon());
		m_treeCtrl->AppendItem(parentNode, metaForm->GetName(),
			formIcon, formIcon, new ibHomePageFormItemData(metaForm->GetMetaID()));
	}

	m_treeCtrl->ExpandAll();
}

void ibDialogHomePageFormSelect::OnSelectionChanged(wxTreeEvent& event)
{
	m_selectedForm = wxNOT_FOUND;

	const wxTreeItemId selection = event.GetItem();
	if (selection.IsOk()) {
		if (const ibHomePageFormItemData* data =
			dynamic_cast<ibHomePageFormItemData*>(m_treeCtrl->GetItemData(selection)))
			m_selectedForm = data->GetFormId();
	}

	m_buttonOK->Enable(m_selectedForm != wxNOT_FOUND);

	event.Skip();
}

void ibDialogHomePageFormSelect::OnItemActivated(wxTreeEvent& event)
{
	if (m_selectedForm != wxNOT_FOUND)
		EndModal(wxID_OK);

	event.Skip();
}
