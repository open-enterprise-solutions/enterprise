#ifndef _DOCVIEW_AUDIT_LOG_H__
#define _DOCVIEW_AUDIT_LOG_H__

// Registration journal — doc/view tab inside the MDI shell.
// Replaces the historical ibDialogAuditLog (modal wxDialog) so the
// admin can keep the journal open alongside form editors. Data side
// (ibLoggerReader) lives on the document; UI side (filter strip +
// AUI toolbar + dataview + tail timer) lives on the view.
//
// Paging contract: model implements Get*Fetch over ibLoggerReader's
// offset+limit SQL surface. Page size = ibAuditLogModel::kPageSize.
// First fetch loads page 0; user scrolls → ibDataViewCtrl calls
// GetNextFetch which appends the next page to the model's
// accumulating row store. Filter change / live refresh → Cleared()
// → control re-fetches from page 0.

// docView.h must come first — it pulls in wx/app.h + wx/docview.h, which
// fully define wxString. backend/logger/loggerReader.h uses wxString in
// its inline helpers; including it before wx is set up triggers
// wxArgNormalizer specialization errors in strvararg.h.
#include "frontend/docView/docView.h"

#include "backend/logger/loggerReader.h"
#include "frontend/win/ctrls/dataview/dataview.h"

#include <memory>
#include <vector>

// Registration journal does not carry a metaobject — it is a standalone
// tool tab. After the ibDocManager collapse it is rebased to the plain
// wx-fork ibDocument/ibView pair instead of ibMetaDocument/ibMetaView,
// registered via a plain ibDocTemplate (no CLSID key) in
// ibDocManager::RegisterDefaultTemplates.

class wxAuiToolBar;
class wxDatePickerCtrl;
class wxChoice;
class wxTextCtrl;
class wxCheckBox;
class wxTimer;
class ibAuditLogModel;

// One per loaded log row — wraps a stable index into the model's
// growing m_loadedRows. Refcounted so ibDataViewItem can pin a row
// past a re-fetch / page eviction without dangling.
class ibAuditLogRowObject : public ibDataViewObject {
public:
	ibAuditLogRowObject(ibAuditLogModel* model, int rowIndex)
		: m_model(model), m_rowIndex(rowIndex) {}

	int GetRowIndex() const { return m_rowIndex; }

	bool IsContainer() const override { return false; }
	bool IsEqualTo(const ibDataViewObject& other) const override;

private:
	ibAuditLogModel* m_model;
	int              m_rowIndex;
};

// Paged journal model. Owns the reader pointer (non-owning) + base
// filter (without offset/limit — those are computed per fetch).
class ibAuditLogModel : public ibDataViewModel {
public:
	// Column ids — match BuildLayout's AppendColumn order. Column 0 is
	// reserved by ibDataViewCtrl fork (passing 0 as model_column produces
	// blank rows), so journal columns start at 1.
	enum Column {
		kColTime    = 1,
		kColLevel   = 2,
		kColUser    = 3,
		kColSource  = 4,
		kColEvent   = 5,
		kColMessage = 6,
		kColRef     = 7,
	};

	// Page size — number of rows fetched per GetFirstFetch / GetNextFetch
	// call. Picked to feel responsive (sub-second on local SQLite) while
	// avoiding many tiny SQL roundtrips for a slow scroll.
	static constexpr int kPageSize = 200;

	ibAuditLogModel() = default;

	void SetReader(ibLoggerReader* reader) { m_reader = reader; }
	void SetFilter(const ibLogFilter& f);

	// Number of rows currently loaded into the model (== rows the user
	// has scrolled to so far + the initial page). Drives the tab title
	// suffix "(N)" so admins see progressive growth.
	std::size_t LoadedRowCount() const { return m_loadedRows.size(); }

	const ibLogRow* RowAt(int index) const;
	const ibLogRow* RowAt(const ibDataViewItem& item) const;

	// ---- ibDataViewModel overrides ----

	void GetValue(wxVariant& variant, const ibDataViewItem& item,
	              unsigned int col) const override;
	bool SetValue(const wxVariant&, const ibDataViewItem&,
	              unsigned int) override { return false; }
	ibDataViewItem GetParent(const ibDataViewItem&) const override {
		return ibDataViewItem();
	}

	bool IsListModel() const override { return true; }
	bool IsPagedModel() const override { return true; }
	Features GetFeatures() const override {
		Features f;
		f.flags = Features::DbFetch | Features::Filters;
		return f;
	}

	unsigned int GetFirstFetch(const ibDataViewItem& parent,
	                            const ibDataViewItem& anchor,
	                            int count, ibDataViewItemArray& out) const override;
	unsigned int GetNextFetch(const ibDataViewItem& parent,
	                           const ibDataViewItem& anchor,
	                           int count, ibDataViewItemArray& out) const override;
	unsigned int GetPrevFetch(const ibDataViewItem& parent,
	                           const ibDataViewItem& anchor,
	                           int count, ibDataViewItemArray& out) const override;

private:
	// Internal page loader — appends rows starting at the given offset.
	// Called by GetFirstFetch (after wiping) and GetNextFetch.
	void LoadPage(int offset, int count) const;

	// Mutable: const Get*Fetch overrides populate them lazily.
	mutable std::vector<ibLogRow>                              m_loadedRows;
	mutable std::vector<wxObjectDataPtr<ibAuditLogRowObject>>  m_rowObjects;

	ibLoggerReader* m_reader = nullptr;
	ibLogFilter     m_baseFilter;
};

class FRONTEND_API ibAuditLogDocument : public ibDocument {
public:
	ibAuditLogDocument();

	ibLoggerReader* GetReader() const { return m_reader.get(); }

	// The journal is read-only — never prompts on close, never tracks
	// dirty state.
	bool IsModified() const override { return false; }
	void Modify(bool) override {}

protected:
	bool DoSaveDocument(const wxString&) override { return true; }
	bool DoOpenDocument(const wxString&) override { return true; }

private:
	std::unique_ptr<ibLoggerReader> m_reader;

	wxDECLARE_NO_COPY_CLASS(ibAuditLogDocument);
	wxDECLARE_DYNAMIC_CLASS(ibAuditLogDocument);
};

class FRONTEND_API ibAuditLogView : public ibView {
public:
	ibAuditLogView() : ibView() {}

	bool OnCreate(ibDocument* doc, long flags) override;
	void OnUpdate(ibView* sender, wxObject* hint) override;
	void OnDraw(wxDC* dc) override;
	bool OnClose(bool deleteWindow = true) override;

private:
	void BuildLayout(wxWindow* parent);

	// Re-set filter on the model + bump Cleared so the dataview wipes
	// its window and re-fetches page 0.
	void Reload();
	ibLogFilter ReadFilter() const;

	void OnCommandMenu(wxCommandEvent& event);
	void OnItemActivated(ibDataViewEvent& event);
	void OnTailToggle(wxCommandEvent& event);
	void OnTimer(wxTimerEvent& event);

	// Filter strip
	wxDatePickerCtrl* m_fromDate     = nullptr;
	wxDatePickerCtrl* m_toDate       = nullptr;
	wxChoice*         m_levelChoice  = nullptr;
	wxChoice*         m_sourceChoice = nullptr;
	wxTextCtrl*       m_userText     = nullptr;
	wxTextCtrl*       m_searchText   = nullptr;
	wxCheckBox*       m_tailCheck    = nullptr;

	// AuiToolBar + DataView shell (mirrors ibDialogUserList).
	wxAuiToolBar*    m_toolbarMain  = nullptr;
	ibDataViewCtrl*  m_dataEditor   = nullptr;
	wxObjectDataPtr<ibAuditLogModel> m_model;
	std::shared_ptr<wxTimer>         m_tailTimer;

	// Resolved at OnCreate time; cached for Reload / OnCommandMenu so
	// every callback doesn't have to dynamic_cast through GetDocument().
	ibAuditLogDocument* m_auditDoc = nullptr;

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_DYNAMIC_CLASS(ibAuditLogView);
};

#endif
