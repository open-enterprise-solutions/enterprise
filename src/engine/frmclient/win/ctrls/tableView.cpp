#include "tableView.h"

#include <wx/statbox.h>
#include "gui/univ/ctrl/staticBox.h"

wxTableViewModeDialog::wxTableViewModeDialog(wxWindow* parent, wxWindowID id, ibDataViewViewMode mode, const wxBitmap& picture) :
	ibDialog(parent, id, _("View mode"))
{
	this->SetSizeHints(wxDefaultSize, wxDefaultSize);

	wxBoxSizer* bSizerMain = new wxBoxSizer(wxVERTICAL);

	ibStaticBoxSizer* sbSizerView = new ibStaticBoxSizer(new ibStaticBox(this, wxID_ANY, _("View")), wxVERTICAL);

	m_radioBtnTree = new ibRadioButton(sbSizerView->GetStaticBox(), wxID_ANY, _("Tree"), wxDefaultPosition, wxDefaultSize, 0);
	if (mode == ibDataViewViewMode::ibDataViewTree) m_radioBtnTree->SetValue(true);
	m_radioBtnHierarchy = new ibRadioButton(sbSizerView->GetStaticBox(), wxID_ANY, _("Hierarchy"), wxDefaultPosition, wxDefaultSize, 0);
	if (mode == ibDataViewViewMode::ibDataViewHierarchical) m_radioBtnHierarchy->SetValue(true);
	m_radioBtnList = new ibRadioButton(sbSizerView->GetStaticBox(), wxID_ANY, _("List"), wxDefaultPosition, wxDefaultSize, 0);
	if (mode == ibDataViewViewMode::ibDataViewList) m_radioBtnList->SetValue(true);

	sbSizerView->Add(m_radioBtnTree, 0, wxALL, 5);
	sbSizerView->Add(m_radioBtnHierarchy, 0, wxALL, 5);
	sbSizerView->Add(m_radioBtnList, 0, wxALL, 5);

	bSizerMain->Add(sbSizerView, 1, wxEXPAND, 5);

	m_sdbSizer = new ibStdDialogButtonSizer();
	m_sdbSizerOK = new ibButton(this, wxID_OK);
	m_sdbSizer->AddButton(m_sdbSizerOK);
	m_sdbSizerCancel = new ibButton(this, wxID_CANCEL);
	m_sdbSizer->AddButton(m_sdbSizerCancel);
	m_sdbSizer->Realize();

	bSizerMain->Add(m_sdbSizer, 0, wxEXPAND | wxALL, FromDIP(5));

	this->SetSizer(bSizerMain);
	this->Layout();
	bSizerMain->Fit(this);

	if (picture.IsOk()) {
		wxIcon dlg_icon;
		dlg_icon.CopyFromBitmap(picture);
		ibDialog::SetIcon(dlg_icon);
	}
	ibDialog::Centre(wxBOTH);
}

ibDataViewViewMode wxTableViewModeDialog::GetViewMode() const
{
	if (m_radioBtnTree->GetValue())
		return ibDataViewViewMode::ibDataViewTree;
	else if (m_radioBtnHierarchy->GetValue())
		return ibDataViewViewMode::ibDataViewHierarchical;
	else if (m_radioBtnList->GetValue())
		return ibDataViewViewMode::ibDataViewList;

	return ibDataViewViewMode::ibDataViewList;
}
