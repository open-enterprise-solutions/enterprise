#include "tableView.h"
#include "backend/tabularModel.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibTableViewCtrl, ibDataViewCtrl);

#include "backend/metadataConfiguration.h"

#include "frontend/win/ctrls/controlTextEditor.h"
#include "frontend/visualView/ctrl/typeControl.h"
#include "frontend/visualView/ctrl/form.h"

#include "backend/objCtor.h"

// (ShowListSettings REMOVED. A control forwarding to a window put the settings road inside a widget,
//  so "how are a model's settings opened" had two answers. There is one door and it is asked for by
//  name: ibDialogListSettings::ShowUserSettings.)

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
