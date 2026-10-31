#ifndef _DATAPROCESSOR_WND_H__
#define _DATAPROCESSOR_WND_H__

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

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

	wxStaticText* m_nameCaption = nullptr;
	wxStaticText* m_synonymCaption = nullptr;
	wxStaticText* m_commentCaption = nullptr;
	wxStaticText* m_defaultForm = nullptr;
	wxTextCtrl* m_nameValue = nullptr;
	wxTextCtrl* m_synonymValue = nullptr;
	wxTextCtrl* m_commentValue = nullptr;
	wxChoice* m_defaultFormValue = nullptr;

	wxButton* m_buttonModule = nullptr;
};

#endif
