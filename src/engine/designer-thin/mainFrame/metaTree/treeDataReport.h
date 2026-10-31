#ifndef _DATAREPORT_WND_H__
#define _DATAREPORT_WND_H__

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include "treeConfiguration.h"

// THE EXTERNAL REPORT'S TREE — the desktop's ibDataReportTree (designer/mainFrame/metaTree), in its tab: the data
// processor's (treeDataProcessor.h) and the report's other declaration beside its default form — which composer is the
// main one. The rows are its tab's View (frmserver ibDataReportTree); the header is its object's properties' rows.
class ibDataReportTree : public ibMetaTreeBase {
public:

	ibDataReportTree(wxWindow* parent, long long tab);

	// The rows, and the header from the root's.
	virtual void Draw(const ibProtocolNode& root) override;

private:

	// events:
	void OnEditCaptionName(wxCommandEvent& event);
	void OnEditCaptionSynonym(wxCommandEvent& event);
	void OnEditCaptionComment(wxCommandEvent& event);

	void OnChoiceDefForm(wxCommandEvent& event);
	void OnChoiceDefComposer(wxCommandEvent& event);

	void OnButtonModuleClicked(wxCommandEvent& event);

	wxStaticText* m_nameCaption = nullptr;
	wxStaticText* m_synonymCaption = nullptr;
	wxStaticText* m_commentCaption = nullptr;
	wxStaticText* m_defaultForm = nullptr;
	wxStaticText* m_defaultComposer = nullptr;
	wxTextCtrl* m_nameValue = nullptr;
	wxTextCtrl* m_synonymValue = nullptr;
	wxTextCtrl* m_commentValue = nullptr;
	wxChoice* m_defaultFormValue = nullptr;
	wxChoice* m_defaultComposerValue = nullptr;

	wxButton* m_buttonModule = nullptr;
};

#endif
