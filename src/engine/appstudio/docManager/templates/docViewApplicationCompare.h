#ifndef _DOCVIEW_APPLICATION_COMPARE_H__
#define _DOCVIEW_APPLICATION_COMPARE_H__

// THE DESKTOP'S (designer/docManager/templates/docViewApplicationCompare.h) WHOLE — a document of the client's, in a tab of
// its own (no Kind the server raises it with). Records come from schema ApplicationCompare Build (the server's Walk); Merge
// is schema ApplicationCompare command Merge (CopyMetaObject / Remove / Paste on the server). The menu opens it
// (ibFrontendMainFrameDesigner::OnApplication).
//
// Application Compare/Merge — doc/view tab inside the designer shell.
// Document holds the walker output (wire records) + compare Mode/Files for
// Apply + optional applied callback. View builds the toolbar + tree dataview +
// filter / direction / Apply bottom strip.

#include "frmclient/docView/docView.h"

#include "core/clsid.h"
#include "appstudio/mainFrame/applicationCompare/applicationCompareModel.h"
#include "protocol/protocol.h"
#include "protocol/protocolDesigner.h"   // ibProtocol::ibSchema::ibApplicationCompare

#include <wx/object.h>

#include <functional>
#include <vector>
#include "deskgui/univ/ctrl/button.h"
#include "deskgui/univ/ctrl/choice.h"

constexpr ibClassID g_toolApplicationCompareCLSID = make_clsid("TL_CCMP", ibClassKind_None);   // tool/doc id — not a registered type

class ibAuiToolBar;
class ibButton;
class ibChoice;
class ibDataViewCtrl;
class ibDataViewEvent;

class ibApplicationCompareDocument : public ibFrontendDocument {
public:
	ibApplicationCompareDocument();

	// Caller wires the diff context BEFORE OnCreate runs. Records are the
	// server's Walk (schema ApplicationCompare Build). Mode/Files identify the
	// same sides for Merge; localRightPath is where a Push downloads the
	// right-side file (empty — no Push / Database compare).
	void Configure(std::vector<ibMetaDiffRecord> records,
	               const wxString& leftLabel,
	               const wxString& rightLabel,
	               ibProtocol::ibSchema::ibApplicationCompare::ibMode mode,
	               const wxString& fileId = wxString(),
	               const wxString& file2Id = wxString(),
	               const wxString& localRightPath = wxString(),
	               std::function<void()> appliedCallback = {});

	const wxString& GetLeftLabel()  const { return m_leftLabel;  }
	const wxString& GetRightLabel() const { return m_rightLabel; }

	ibDataViewMetaDiffModel* GetModel() const { return m_model.get(); }
	ibProtocol::ibSchema::ibApplicationCompare::ibMode GetMode() const { return m_mode; }
	const wxString& GetFileId() const { return m_fileId; }
	const wxString& GetFile2Id() const { return m_file2Id; }
	const wxString& GetLocalRightPath() const { return m_localRightPath; }
	bool CanPush() const { return !m_localRightPath.IsEmpty(); }
	const std::function<void()>& GetAppliedCallback() const { return m_appliedCallback; }

	virtual bool IsModified() const override { return false; }
	virtual void Modify(bool) override {}

protected:
	virtual bool DoSaveDocument(const wxString&) override { return true; }
	virtual bool DoOpenDocument(const wxString&) override { return true; }

	virtual ibFrontendView* DoCreateView() override;

private:
	wxString m_leftLabel;
	wxString m_rightLabel;
	ibProtocol::ibSchema::ibApplicationCompare::ibMode m_mode =
		ibProtocol::ibSchema::ibApplicationCompare::ibMode::File;
	wxString m_fileId;
	wxString m_file2Id;
	wxString m_localRightPath;

	wxObjectDataPtr<ibDataViewMetaDiffModel> m_model;
	std::function<void()> m_appliedCallback;

	wxDECLARE_NO_COPY_CLASS(ibApplicationCompareDocument);
};

class ibApplicationCompareView : public ibFrontendView {
public:
	ibApplicationCompareView() : ibFrontendView() {}

	virtual bool OnCreate(ibFrontendDocument* doc, long flags) override;
	virtual void OnUpdate(ibFrontendView* sender, wxObject* hint) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

private:

	enum {
		wxID_TOOL_SELECT_ALL = wxID_HIGHEST + 1,
		wxID_TOOL_CLEAR,
		wxID_TOOL_EXPAND_ALL,
		wxID_TOOL_COLLAPSE_ALL,
		wxID_TOOL_APPLY_MERGE,
	};

	void BuildLayout(wxWindow* parent);
	void RefreshDirectionChoice();
	void RefreshApplyButtonState();

	void OnSelectAll(wxCommandEvent& event);
	void OnClearSelection(wxCommandEvent& event);
	void OnExpandAll(wxCommandEvent& event);
	void OnCollapseAll(wxCommandEvent& event);
	void OnApplyMerge(wxCommandEvent& event);
	void OnFilterChanged(wxCommandEvent& event);
	void OnDirectionChanged(wxCommandEvent& event);
	void OnItemCollapsing(ibDataViewEvent& event);

	bool HasOnlyInAncestor(int recordIndex) const;
	ibMetaID FindTargetParent(
		const ibMetaDiffRecord& rec, bool pull) const;

	void ExpandAllRecursive(const ibDataViewItem& parent);
	void CollapseAllRecursive(const ibDataViewItem& parent);

	ibAuiToolBar*  m_toolbar         = nullptr;
	ibDataViewCtrl* m_dataView       = nullptr;
	ibChoice*      m_filterChoice    = nullptr;
	ibChoice*      m_directionChoice = nullptr;
	ibButton*      m_buttonApply     = nullptr;

	ibApplicationCompareDocument* m_compareDoc = nullptr;
};

#endif
