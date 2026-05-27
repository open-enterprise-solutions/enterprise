#include "predefinedEditor.h"

#include "frontend/visualView/ctrl/frame.h"
#include "frontend/win/theme/luna_toolbarart.h"

enum
{
	model_name,
	model_code,
	model_description
};

void ibDialogPredefinedEditor::ibDataViewPredefinedTreeStore::GetValue(wxVariant& variant,
	const ibDataViewItem& item, unsigned int col) const
{
	if (item.GetID() == (void*)1)
	{
		if (col == model_name)
			variant = _("Predefined items");

		return;
	}

	ibPredefinedValueObject* predefined =
		static_cast<ibPredefinedValueObject*>(item.GetID());

	if (predefined != nullptr) {
		if (col == model_name)
			variant = predefined->GetPredefinedName();
		else if (col == model_code)
			variant = predefined->GetPredefinedCode();
		else if (col == model_description)
			variant = predefined->GetPredefinedDescription();
	}
}

bool ibDialogPredefinedEditor::ibDataViewPredefinedTreeStore::IsContainer(const ibDataViewItem& item) const
{
	if (item.GetID() == (void*)1)
		return true;

	ibPredefinedValueObject* predefined =
		static_cast<ibPredefinedValueObject*>(item.GetID());

	if (predefined != nullptr)
		return predefined->IsPredefinedFolder();

	return true;
}

unsigned int ibDialogPredefinedEditor::ibDataViewPredefinedTreeStore::GetFirstFetch(
	const ibDataViewItem& parent,
	const ibDataViewItem& /*anchor*/, int /*count*/,
	ibDataViewItemArray& out) const
{
	// Root-level request: return every predefined whose parent is null.
	// Folder-level request: return predefineds whose GetPredefinedParent()
	// matches the requested folder. Prior code returned the flat list for
	// every call, which forced the tree to render everything at the root and
	// never displayed folder nesting even though the backend stored it.
	const ibPredefinedValueObject* parentObj = parent.IsOk()
		? static_cast<const ibPredefinedValueObject*>(parent.GetID())
		: nullptr;

	for (const auto& object : m_valueMetaObjectHierarchy->GetPredefinedValueArray()) {
		const ibPredefinedValueObject* const itemParent = object->GetPredefinedParent().get();
		if (itemParent == parentObj)
			out.Add(ibDataViewItem(object.get()));
	}

	return out.Count();
}

/////////////////////////////////////////////////////////////////////////////////////

void ibDialogPredefinedEditor::CreateDialogView()
{
	wxDialog::SetSizeHints(wxDefaultSize, wxDefaultSize);

	wxBoxSizer* sizerList = new wxBoxSizer(wxVERTICAL);

	m_toolbarTableEditor = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_TEXT);
	m_toolbarTableEditor->SetArtProvider(new wxAuiLunaToolBarArt());

	m_toolAdd = m_toolbarTableEditor->AddTool(wxID_TOOL_ADD, _("Add"), ibBackendPicture::GetPicture(g_picAddCLSID), wxNullBitmap, wxITEM_NORMAL, wxEmptyString, wxEmptyString, nullptr);
	m_toolCopy = m_toolbarTableEditor->AddTool(wxID_TOOL_COPY, _("Copy"), ibBackendPicture::GetPicture(g_picCopyCLSID), wxNullBitmap, wxITEM_NORMAL, wxEmptyString, wxEmptyString, nullptr);
	m_toolEdit = m_toolbarTableEditor->AddTool(wxID_TOOL_EDIT, _("Edit"), ibBackendPicture::GetPicture(g_picEditCLSID), wxNullBitmap, wxITEM_NORMAL, wxEmptyString, wxEmptyString, nullptr);
	m_toolDelete = m_toolbarTableEditor->AddTool(wxID_TOOL_DELETE, _("Delete"), ibBackendPicture::GetPicture(g_picDeleteCLSID), wxNullBitmap, wxITEM_NORMAL, wxEmptyString, wxEmptyString, nullptr);

	m_toolbarTableEditor->EnableTool(wxID_TOOL_ADD, m_tableModelStore->IsEnabled(ibDataViewItem(nullptr), model_name));
	m_toolbarTableEditor->EnableTool(wxID_TOOL_COPY, m_tableModelStore->IsEnabled(ibDataViewItem(nullptr), model_name));
	m_toolbarTableEditor->EnableTool(wxID_TOOL_EDIT, m_tableModelStore->IsEnabled(ibDataViewItem(nullptr), model_name));
	m_toolbarTableEditor->EnableTool(wxID_TOOL_DELETE, m_tableModelStore->IsEnabled(ibDataViewItem(nullptr), model_name));

	m_toolbarTableEditor->SetForegroundColour(wxDefaultStypeFGColour);
	m_toolbarTableEditor->SetBackgroundColour(wxDefaultStypeBGColour);

	m_toolbarTableEditor->Realize();
	m_toolbarTableEditor->Connect(wxEVT_MENU, wxCommandEventHandler(ibDialogPredefinedEditor::OnCommandMenu), nullptr, this);

	sizerList->Add(m_toolbarTableEditor, 0, wxALL | wxEXPAND, 0);

	ibDataViewColumn* columnName = new ibDataViewColumn(_("Name"), new ibDataViewTextRenderer(wxT("string"), wxDATAVIEW_CELL_INERT), model_name, FromDIP(125), wxALIGN_LEFT,
		wxDATAVIEW_COL_SORTABLE);

	ibDataViewColumn* columnCode = new ibDataViewColumn(_("Code"), new ibDataViewTextRenderer(wxT("string"), wxDATAVIEW_CELL_INERT), model_code, FromDIP(100), wxALIGN_LEFT,
		wxDATAVIEW_COL_SORTABLE);

	ibDataViewColumn* columnDescription = new ibDataViewColumn(_("Description"), new ibDataViewTextRenderer(wxT("string"), wxDATAVIEW_CELL_INERT), model_description, FromDIP(175), wxALIGN_LEFT,
		wxDATAVIEW_COL_SORTABLE);

	m_tableEditor = new ibDataViewCtrl(this, wxID_ANY);

	m_tableEditor->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, &ibDialogPredefinedEditor::OnItemActivated, this);
	m_tableEditor->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &ibDialogPredefinedEditor::OnContextMenu, this);

	m_tableEditor->Connect(wxEVT_MENU, wxCommandEventHandler(ibDialogPredefinedEditor::OnCommandMenu), nullptr, this);

	m_tableEditor->AppendColumn(columnName);
	m_tableEditor->AppendColumn(columnCode);
	m_tableEditor->AppendColumn(columnDescription);

	m_tableEditor->SetForegroundColour(wxDefaultStypeFGColour);
	//m_tableEditor->SetBackgroundColour(wxDefaultStypeBGColour);

	m_tableEditor->AssociateModel(m_tableModelStore);

	sizerList->Add(m_tableEditor, 1, wxALL | wxEXPAND, FromDIP(5));

	wxDialog::SetSizer(sizerList);
	wxDialog::Layout();
	wxDialog::Centre(wxBOTH);

	wxIcon dlg_icon;
	dlg_icon.CopyFromBitmap(ibBackendPicture::GetPicture(g_metaAttributeCLSID));

	wxDialog::SetIcon(dlg_icon);
	wxDialog::SetFocus();
}

void ibDialogPredefinedEditor::OnContextMenu(ibDataViewEvent& event)
{
	wxMenu menu;

	for (unsigned int idx = 0; idx < m_toolbarTableEditor->GetToolCount(); idx++) {
		wxAuiToolBarItem* tool = m_toolbarTableEditor->FindToolByIndex(idx);
		if (tool) {
			wxMenuItem* item = menu.Append(tool->GetId(), tool->GetLabel());
			item->Enable(m_tableModelStore->IsEnabled(ibDataViewItem(nullptr), model_name));
			item->SetBitmap(tool->GetBitmapBundle());
		}
	}

	m_tableEditor->PopupMenu(&menu);
}

void ibDialogPredefinedEditor::OnCommandMenu(wxCommandEvent& event)
{
	const ibDataViewItem& selection =
		m_tableEditor->GetSelection();

	int result = wxID_CANCEL;

	if (event.GetId() == wxID_TOOL_ADD) {
		ibDialogPredefinedItem dlg(this);
		result = dlg.ShowModal();
	}
	else if (event.GetId() == wxID_TOOL_COPY) {
		if (selection.IsOk()) {

			ibPredefinedValueObject* predefined =
				static_cast<ibPredefinedValueObject*>(selection.GetID());

			ibDialogPredefinedItem dlg(this,
				wxNewUniqueGuid,
				predefined->GetPredefinedParent(),
				predefined->IsPredefinedFolder(),
				predefined->GetPredefinedName(),
				predefined->GetPredefinedCode(),
				predefined->GetPredefinedDescription());

			dlg.SetReadOnly(!m_tableModelStore->IsEnabled(selection, model_name));
			result = dlg.ShowModal();
		}
	}
	else if (event.GetId() == wxID_TOOL_EDIT) {
		if (selection.IsOk()) {

			ibPredefinedValueObject* predefined =
				static_cast<ibPredefinedValueObject*>(selection.GetID());

			ibDialogPredefinedItem dlg(this,
				predefined->GetPredefinedGuid(),
				predefined->GetPredefinedParent(),
				predefined->IsPredefinedFolder(),
				predefined->GetPredefinedName(),
				predefined->GetPredefinedCode(),
				predefined->GetPredefinedDescription());

			dlg.SetReadOnly(!m_tableModelStore->IsEnabled(selection, model_name));
			result = dlg.ShowModal();
		}
	}
	else if (event.GetId() == wxID_TOOL_DELETE) {

		if (selection.IsOk()) {
			ibPredefinedValueObject* predefined =
				static_cast<ibPredefinedValueObject*>(selection.GetID());

			DeletePredefinedValue(predefined->GetPredefinedGuid());
			result = wxID_OK;
		}
	}

	if (result == wxID_OK) m_tableModelStore->Cleared();

	event.Skip();
}

void ibDialogPredefinedEditor::OnItemActivated(ibDataViewEvent& event)
{
	const ibDataViewItem& selection =
		m_tableEditor->GetSelection();

	ibPredefinedValueObject* predefined =
		static_cast<ibPredefinedValueObject*>(selection.GetID());

	ibDialogPredefinedItem dlg(this,
		predefined->GetPredefinedGuid(),
		predefined->GetPredefinedParent(),
		predefined->IsPredefinedFolder(),
		predefined->GetPredefinedName(),
		predefined->GetPredefinedCode(),
		predefined->GetPredefinedDescription());

	dlg.SetReadOnly(!m_tableModelStore->IsEnabled(selection, model_name));

	if (dlg.ShowModal() == wxID_OK)
		m_tableModelStore->Cleared();

	event.Skip();
}
