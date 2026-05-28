#ifndef _DOCVIEW_CONFIG_COMPARE_H__
#define _DOCVIEW_CONFIG_COMPARE_H__

// Configuration Compare/Merge — doc/view tab inside the designer MDI
// shell. Replaces the historical ibDialogConfigCompare (modal wxDialog)
// so multiple comparisons can stay open at once (e.g. compare-with-DB
// AND compare-with-file side by side). Document holds the walker
// output + the two non-owning root pointers + optional save and
// applied callbacks. View builds the toolbar + tree dataview +
// filter / direction / Apply bottom strip.

// docView.h must come first — pulls in wx/app.h + wx/docview.h so
// wxString is fully defined before any header that uses it inline
// (backend/clsid.h, configCompareModel.h).
#include "frontend/docView/docView.h"

#include "backend/clsid.h"
#include "designer/mainFrame/configCompare/configCompareModel.h"

#include <wx/object.h>

#include <functional>

const ibClassID g_toolConfigCompareCLSID = string_to_clsid("TL_CCMP");

class wxAuiToolBar;
class wxButton;
class wxChoice;
class ibDataViewCtrl;
class ibDataViewEvent;

class ibConfigCompareDocument : public ibDocument {
public:
	ibConfigCompareDocument();

	// Caller wires the diff context BEFORE OnCreate runs (the view's
	// BuildLayout reads roots/labels/model from here). Both roots are
	// non-owning — caller keeps the source configurations alive for
	// the document's lifetime.
	void Configure(class ibValueMetaObject* leftRoot,
	               class ibValueMetaObject* rightRoot,
	               const wxString& leftLabel,
	               const wxString& rightLabel,
	               std::function<bool()> rightSaveCallback = {},
	               std::function<void()> appliedCallback = {});

	class ibValueMetaObject* GetLeftRoot()  const { return m_leftRoot;  }
	class ibValueMetaObject* GetRightRoot() const { return m_rightRoot; }
	const wxString& GetLeftLabel()  const { return m_leftLabel;  }
	const wxString& GetRightLabel() const { return m_rightLabel; }

	ibDataViewMetaDiffModel* GetModel() const { return m_model.get(); }
	const std::function<bool()>& GetRightSaveCallback() const { return m_rightSaveCallback; }
	const std::function<void()>& GetAppliedCallback()   const { return m_appliedCallback; }

	bool IsModified() const override { return false; }
	void Modify(bool) override {}

protected:
	bool DoSaveDocument(const wxString&) override { return true; }
	bool DoOpenDocument(const wxString&) override { return true; }

private:
	class ibValueMetaObject* m_leftRoot  = nullptr;
	class ibValueMetaObject* m_rightRoot = nullptr;
	wxString m_leftLabel;
	wxString m_rightLabel;

	wxObjectDataPtr<ibDataViewMetaDiffModel> m_model;
	std::function<bool()> m_rightSaveCallback;
	std::function<void()> m_appliedCallback;

	wxDECLARE_NO_COPY_CLASS(ibConfigCompareDocument);
	wxDECLARE_DYNAMIC_CLASS(ibConfigCompareDocument);
};

class ibConfigCompareView : public ibView {
public:
	ibConfigCompareView() : ibView() {}

	bool OnCreate(ibDocument* doc, long flags) override;
	void OnUpdate(ibView* sender, wxObject* hint) override;
	void OnDraw(wxDC* dc) override;
	bool OnClose(bool deleteWindow = true) override;

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

	// Direction-parametrized merge primitives. pull = right → left,
	// !pull = left → right.
	void ApplyAdd(const ibMetaDiffRecord& rec, bool pull);
	void ApplyDelete(const ibMetaDiffRecord& rec, bool pull);
	void ApplyReplace(const ibMetaDiffRecord& rec, bool pull);

	bool HasOnlyInAncestor(int recordIndex) const;
	class ibValueMetaObject* FindTargetParent(
		const ibMetaDiffRecord& rec, bool pull) const;

	void ExpandAllRecursive(const ibDataViewItem& parent);
	void CollapseAllRecursive(const ibDataViewItem& parent);

	wxAuiToolBar*  m_toolbar         = nullptr;
	ibDataViewCtrl* m_dataView       = nullptr;
	wxChoice*      m_filterChoice    = nullptr;
	wxChoice*      m_directionChoice = nullptr;
	wxButton*      m_buttonApply     = nullptr;

	// Resolved at OnCreate; cached for every callback.
	ibConfigCompareDocument* m_compareDoc = nullptr;

	wxDECLARE_DYNAMIC_CLASS(ibConfigCompareView);
};

#endif
