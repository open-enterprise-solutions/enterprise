#ifndef _FRMCLIENT_DOCVIEW_AUDIT_LOG_H__
#define _FRMCLIENT_DOCVIEW_AUDIT_LOG_H__

// Registration journal — the old desktop's doc/view tab, in a tab the
// server opened (the tab's Kind AuditLog). The journal is the server's: the reader fetches it by the tab, a row's
// object is opened there.
//
// Paging contract: model implements Get*Fetch over ibLoggerReader's
// offset+limit surface. Page size = ibAuditLogModel::kPageSize.
// First fetch loads page 0; user scrolls → ibDataViewCtrl calls
// GetNextFetch which appends the next page to the model's
// accumulating row store. Filter change / live refresh → Cleared()
// → control re-fetches from page 0.

#include "frmclient/docView/tabDocument.h"

#include "frmclient/backend/logger/loggerReader.h"
#include "frmclient/win/ctrls/dataview/dataview.h"

#include <memory>
#include <vector>

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
	// call. Picked to feel responsive while avoiding many tiny roundtrips
	// for a slow scroll.
	static constexpr int kPageSize = 200;

	ibAuditLogModel() = default;

	void SetReader(ibLoggerReader* reader) { m_reader = reader; }
	void SetFilter(const ibLogFilter& f);

	// Number of rows currently loaded into the model (== rows the user
	// has scrolled to so far + the initial page).
	std::size_t LoadedRowCount() const { return m_loadedRows.size(); }

	const ibLogRow* RowAt(int index) const;
	const ibLogRow* RowAt(const ibDataViewItem& item) const;

	// ---- ibDataViewModel overrides ----

	void GetValue(ibVariant& variant, const ibDataViewItem& item,
	              unsigned int col) const override;
	bool SetValue(const ibVariant&, const ibDataViewItem&,
	              unsigned int) override { return false; }
	ibDataViewItem GetParent(const ibDataViewItem&) const override {
		return ibDataViewItem();
	}

	bool IsListModel() const override { return true; }
	bool IsPagedModel() const override { return true; }
	Features GetFeatures() const override {
		Features f;
		f.flags = Features::Filters;   // keyed DB paging is the default (HasKeyedRows() == true); fetch is uniform
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

// A TAB THE SERVER OPENED (ibTabDocument) — title, close and the rest are the tab's — with the desktop's reader on it,
// over the tab, and a view of its own; made by the window for a tab of its Kind.
class FRMCLIENT_API ibAuditLogDocument : public ibTabDocument {
public:
	explicit ibAuditLogDocument(long long tabId);

	ibLoggerReader* GetReader() const { return m_reader.get(); }

protected:
	virtual ibFrontendView* DoCreateView() override;

private:
	std::unique_ptr<ibLoggerReader> m_reader;
};

class FRMCLIENT_API ibAuditLogView : public ibFrontendView {
public:
	ibAuditLogView() : ibFrontendView() {}

	virtual bool OnCreate(ibFrontendDocument* doc, long flags) override;
	virtual void OnUpdate(ibFrontendView* sender, wxObject* hint) override;
	virtual void OnDraw(wxDC* dc) override;
	virtual bool OnClose(bool deleteWindow = true) override;

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
};

#endif
