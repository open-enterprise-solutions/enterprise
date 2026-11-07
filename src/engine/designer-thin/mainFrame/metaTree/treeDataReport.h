#ifndef _DATAREPORT_WND_H__
#define _DATAREPORT_WND_H__

#include "gui/univ/ctrl/button.h"
#include <wx/choice.h>
#include "gui/univ/ctrl/staticText.h"
#include "gui/univ/ctrl/textCtrl.h"

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

	ibStaticText* m_nameCaption = nullptr;
	ibStaticText* m_synonymCaption = nullptr;
	ibStaticText* m_commentCaption = nullptr;
	ibStaticText* m_defaultForm = nullptr;
	ibStaticText* m_defaultComposer = nullptr;
	ibTextCtrl* m_nameValue = nullptr;
	ibTextCtrl* m_synonymValue = nullptr;
	ibTextCtrl* m_commentValue = nullptr;
	ibChoice* m_defaultFormValue = nullptr;
	ibChoice* m_defaultComposerValue = nullptr;

	ibButton* m_buttonModule = nullptr;
};

#endif
