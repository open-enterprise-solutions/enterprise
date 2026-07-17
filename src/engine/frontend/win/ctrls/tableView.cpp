#include "tableView.h"
#include "backend/model.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibTableViewCtrl, ibDataViewCtrl);

#include "backend/metadataConfiguration.h"

#include "frontend/win/ctrls/controlTextEditor.h"
#include "frontend/visualView/ctrl/typeControl.h"
#include "frontend/visualView/ctrl/form.h"

#include "backend/objCtor.h"

#include "frontend/win/dlgs/listSettings/listSettings.h"   // ibDialogListSettings — the List-Settings window

bool ibTableViewCtrl::ShowListSettings(ibValueModel* model)
{
	// Open the List-Settings window (Filter / Sort / Group) for ANY model. The dialog edits its own buffer copy
	// of model->GetListSettings() and builds its available filter fields from the model's columns (PATH A),
	// committing onto the model's L5 composer on OK. (This is the single filter/sort/group UI now — the old
	// per-column ShowFilter(ibFilterRow&) dialog is gone.)
	return ibDialogListSettings::ShowListSettingsDialog(model);
}

bool ibTableViewCtrl::ShowViewMode()
{
	class wxTableViewModeDialog : public wxDialog {

	public:

		wxTableViewModeDialog(wxWindow* parent, wxWindowID id, ibDataViewViewMode mode) :
			wxDialog(parent, id, _("View mode"))
		{
			this->SetSizeHints(wxDefaultSize, wxDefaultSize);

			wxBoxSizer* bSizerMain = new wxBoxSizer(wxVERTICAL);

			wxStaticBoxSizer* sbSizerView = new wxStaticBoxSizer(new wxStaticBox(this, wxID_ANY, _("View")), wxVERTICAL);

			m_radioBtnTree = new wxRadioButton(sbSizerView->GetStaticBox(), wxID_ANY, _("Tree"), wxDefaultPosition, wxDefaultSize, 0);
			if (mode == ibDataViewViewMode::ibDataViewTree) m_radioBtnTree->SetValue(true);
			m_radioBtnHierarchy = new wxRadioButton(sbSizerView->GetStaticBox(), wxID_ANY, _("Hierarchy"), wxDefaultPosition, wxDefaultSize, 0);
			if (mode == ibDataViewViewMode::ibDataViewHierarchical) m_radioBtnHierarchy->SetValue(true);
			m_radioBtnList = new wxRadioButton(sbSizerView->GetStaticBox(), wxID_ANY, _("List"), wxDefaultPosition, wxDefaultSize, 0);
			if (mode == ibDataViewViewMode::ibDataViewList) m_radioBtnList->SetValue(true);

			sbSizerView->Add(m_radioBtnTree, 0, wxALL, 5);
			sbSizerView->Add(m_radioBtnHierarchy, 0, wxALL, 5);
			sbSizerView->Add(m_radioBtnList, 0, wxALL, 5);

			bSizerMain->Add(sbSizerView, 1, wxEXPAND, 5);

			m_sdbSizer = new wxStdDialogButtonSizer();
			m_sdbSizerOK = new wxButton(this, wxID_OK);
			m_sdbSizer->AddButton(m_sdbSizerOK);
			m_sdbSizerCancel = new wxButton(this, wxID_CANCEL);
			m_sdbSizer->AddButton(m_sdbSizerCancel);
			m_sdbSizer->Realize();

			bSizerMain->Add(m_sdbSizer, 0, wxEXPAND, 5);

			this->SetSizer(bSizerMain);
			this->Layout();
			bSizerMain->Fit(this);

			wxIcon dlg_icon;
			dlg_icon.CopyFromBitmap(ibBackendPicture::GetPicture(g_picHierarchyCLSID));

			wxDialog::SetIcon(dlg_icon);
			wxDialog::Centre(wxBOTH);
		}

		ibDataViewViewMode GetViewMode() const
		{
			if (m_radioBtnTree->GetValue())
				return ibDataViewViewMode::ibDataViewTree;
			else if (m_radioBtnHierarchy->GetValue())
				return ibDataViewViewMode::ibDataViewHierarchical;
			else if (m_radioBtnList->GetValue())
				return ibDataViewViewMode::ibDataViewList;

			return ibDataViewViewMode::ibDataViewList;
		}

	private:

		wxRadioButton* m_radioBtnTree;
		wxRadioButton* m_radioBtnHierarchy;
		wxRadioButton* m_radioBtnList;
		wxStdDialogButtonSizer* m_sdbSizer;
		wxButton* m_sdbSizerOK;
		wxButton* m_sdbSizerCancel;
	};

	wxTableViewModeDialog* dialog =
		new wxTableViewModeDialog(this, wxID_ANY, GetViewMode());

	bool result = false;
	if (dialog->ShowModal() == wxID_OK) {

		ibTableViewCtrl::SetViewMode(dialog->GetViewMode());
		result = true;
	}
	
	dialog->Destroy();
	return result;
}
