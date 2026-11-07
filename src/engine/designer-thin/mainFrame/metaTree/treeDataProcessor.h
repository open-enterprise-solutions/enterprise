#ifndef _DATAPROCESSOR_WND_H__
#define _DATAPROCESSOR_WND_H__

#include "deskgui/univ/ctrl/button.h"
#include <wx/choice.h>
#include "deskgui/univ/ctrl/staticText.h"
#include "deskgui/univ/ctrl/textCtrl.h"

#include "treeConfiguration.h"

// THE EXTERNAL DATA PROCESSOR'S TREE — the desktop's ibDataProcessorTree (designer/mainFrame/metaTree), in its tab: the
// header — name, synonym, comment, default form — the rows, and the module's button. The rows are its tab's View
// (frmserver ibDataProcessorTree); the header is its object's properties' rows (the root's Properties, as the inspector's),
// an edit given back through the inspector's door (schema Properties, Save by its id and the tab).
class ibDataProcessorTree : public ibMetaTreeBase {
public:

	ibDataProcessorTree(wxWindow* parent, long long tab);

	// The rows, and the header from the root's.
	virtual void Draw(const ibProtocolNode& root) override;

private:

	// events:
	void OnEditCaptionName(wxCommandEvent& event);
	void OnEditCaptionSynonym(wxCommandEvent& event);
	void OnEditCaptionComment(wxCommandEvent& event);

	void OnChoiceDefForm(wxCommandEvent& event);

	void OnButtonModuleClicked(wxCommandEvent& event);

	ibStaticText* m_nameCaption = nullptr;
	ibStaticText* m_synonymCaption = nullptr;
	ibStaticText* m_commentCaption = nullptr;
	ibStaticText* m_defaultForm = nullptr;
	ibTextCtrl* m_nameValue = nullptr;
	ibTextCtrl* m_synonymValue = nullptr;
	ibTextCtrl* m_commentValue = nullptr;
	ibChoice* m_defaultFormValue = nullptr;

	ibButton* m_buttonModule = nullptr;
};

#endif
