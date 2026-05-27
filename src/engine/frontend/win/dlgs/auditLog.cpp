#include "auditLog.h"

#include "backend/appData.h"
#include "backend/logger/logger.h"
#include "backend/picturePredefined.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/partial/reference/reference.h"

#include "frontend/win/theme/luna_toolbarart.h"
#include "frontend/win/dlgs/userItem.h"
#include "frontend/visualView/ctrl/frame.h"

#include <wx/datetime.h>
#include <wx/stattext.h>
#include <wx/itemattr.h>
#include <wx/log.h>
#include <wx/variant.h>

// -----------------------------------------------------------------
// Column indices — model methods key off these. Match the order in
// BuildLayout()'s AppendColumn calls. Start at 1 to mirror userList
// (column 0 is reserved by the ibDataViewCtrl fork; passing 0 as
// model_column produces an empty TableBox even though the model
// holds rows — symptom: title shows "(N)" but body is blank).
// -----------------------------------------------------------------
namespace {
constexpr unsigned int colTime    = 1;
constexpr unsigned int colLevel   = 2;
constexpr unsigned int colUser    = 3;
constexpr unsigned int colSource  = 4;
constexpr unsigned int colEvent   = 5;
constexpr unsigned int colMessage = 6;
constexpr unsigned int colRef     = 7;

const wxChar* LevelTag(int lvl)
{
    switch (lvl) {
    case 0: return wxT("INFO");
    case 1: return wxT("WARN");
    case 2: return wxT("ERROR");
    case 3: return wxT("AUDIT");
    }
    return wxT("?");
}

// Source dropdown values mirror the Audit-source taxonomy enforced by
// producers. Empty entry = "any". Order: most relevant for admin first.
const wxChar* const kSources[] = {
    wxT(""), wxT("auth"), wxT("session"), wxT("record"),
    wxT("document"), wxT("metadata"), nullptr
};

struct LevelOption { const wxChar* label; int minLevel; };
const LevelOption kLevels[] = {
    { wxT("All"),       -1 },
    { wxT("Info+"),      0 },
    { wxT("Warn+"),      1 },
    { wxT("Error+"),     2 },
    { wxT("Audit only"), 3 },
};

enum {
    wxID_AUDIT_TOOL_APPLY = wxID_HIGHEST + 1,
    wxID_AUDIT_TOOL_CLEAR,
    wxID_AUDIT_TOOL_REFRESH,
    wxID_AUDIT_TOOL_OPEN_REF,
};
}   // namespace

// -----------------------------------------------------------------
// ibAuditLogModel — mirrors ibUserListModel: PrepareData fetches via
// ibLoggerReader and Reset()s the index. Ctor pre-populates so the
// dataview sees the right row count at AssociateModel time; later
// refresh calls go through PrepareData(reader, filter) again.
// -----------------------------------------------------------------
class ibAuditLogModel : public ibDataViewIndexListModel {
    wxDECLARE_NO_COPY_CLASS(ibAuditLogModel);
public:
    ibAuditLogModel(ibLoggerReader* reader, const ibLogFilter& filter) {
        PrepareData(reader, filter);
    }

    void PrepareData(ibLoggerReader* reader, const ibLogFilter& filter) {
        m_rows.clear();
        if (reader != nullptr) m_rows = reader->Query(filter);
        Reset(static_cast<unsigned int>(m_rows.size()));
    }

    std::size_t RowCount() const { return m_rows.size(); }

    const ibLogRow* RowAt(const ibDataViewItem& item) const {
        const unsigned int r = GetRow(item);
        return r < m_rows.size() ? &m_rows[r] : nullptr;
    }

    unsigned int GetCount() const override { return static_cast<unsigned int>(m_rows.size()); }

    void GetValueByRow(wxVariant& val, unsigned row, unsigned col) const override {
        if (row >= m_rows.size()) return;
        const ibLogRow& r = m_rows[row];
        switch (col) {
        case colTime: {
            const wxDateTime t((wxLongLong)r.ts_ms);
            val = t.Format(wxT("%Y-%m-%d %H:%M:%S"));
            break;
        }
        case colLevel:   val = wxString(LevelTag(r.level)); break;
        case colUser:    val = r.user_name; break;
        case colSource:  val = r.source;    break;
        case colEvent:   val = r.event_type; break;
        case colMessage: val = r.message;   break;
        case colRef:     val = r.ref_guid.IsEmpty()
                              ? wxString()
                              : r.ref_guid.Left(8) + wxT("…");
                         break;
        }
    }

    bool SetValueByRow(const wxVariant&, unsigned, unsigned) override { return false; }

private:
    std::vector<ibLogRow> m_rows;
};

// -----------------------------------------------------------------
// ibDialogAuditLog
// -----------------------------------------------------------------
ibDialogAuditLog::ibDialogAuditLog(wxWindow* parent, wxWindowID id,
                                   const wxString& title, const wxPoint& pos,
                                   const wxSize& size, long style)
    : wxDialog(parent, id, title, pos, size, style)
    , m_tailTimer(new wxTimer)
{
    SetSizeHints(wxSize(640, 320), wxDefaultSize);
    this->SetBackgroundColour(wxColour(184, 201, 212));   // #B8C9D4 powder-blue dialog

    if (appData && appData->GetLogger())
        m_reader = std::make_unique<ibLoggerReader>(appData->GetLogger()->GetLogDir());

    BuildLayout();
    Reload();

    m_tailTimer->Bind(wxEVT_TIMER, &ibDialogAuditLog::OnTimer, this);

    wxIcon dlg_icon;
    dlg_icon.CopyFromBitmap(ibBackendPicture::GetPicture(g_picUserActiveCLSID));
    SetIcon(dlg_icon);
    SetFocus();

    Layout();
    Centre(wxBOTH);
}

ibDialogAuditLog::~ibDialogAuditLog()
{
    if (m_tailTimer && m_tailTimer->IsRunning()) m_tailTimer->Stop();
}

void ibDialogAuditLog::BuildLayout()
{
    wxBoxSizer* main = new wxBoxSizer(wxVERTICAL);

    // ----- Filter strip -----
    wxBoxSizer* filt = new wxBoxSizer(wxHORIZONTAL);

    filt->Add(new wxStaticText(this, wxID_ANY, _("From:")),
              0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(3));
    wxDateTime defaultFrom = wxDateTime::Today();
    defaultFrom.Subtract(wxDateSpan::Days(7));
    m_fromDate = new wxDatePickerCtrl(this, wxID_ANY, defaultFrom,
        wxDefaultPosition, wxDefaultSize, wxDP_DROPDOWN | wxDP_SHOWCENTURY);
    filt->Add(m_fromDate, 0, wxALL, FromDIP(3));

    filt->Add(new wxStaticText(this, wxID_ANY, _("To:")),
              0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(3));
    m_toDate = new wxDatePickerCtrl(this, wxID_ANY, wxDateTime::Today(),
        wxDefaultPosition, wxDefaultSize, wxDP_DROPDOWN | wxDP_SHOWCENTURY);
    filt->Add(m_toDate, 0, wxALL, FromDIP(3));

    filt->Add(new wxStaticText(this, wxID_ANY, _("Level:")),
              0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(3));
    m_levelChoice = new wxChoice(this, wxID_ANY);
    for (const auto& l : kLevels) m_levelChoice->Append(l.label);
    m_levelChoice->SetSelection(4);                              // Audit only
    filt->Add(m_levelChoice, 0, wxALL, FromDIP(3));

    filt->Add(new wxStaticText(this, wxID_ANY, _("Source:")),
              0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(3));
    m_sourceChoice = new wxChoice(this, wxID_ANY);
    for (int i = 0; kSources[i] != nullptr; ++i) {
        m_sourceChoice->Append(kSources[i][0] == 0 ? _("any") : wxString(kSources[i]));
    }
    m_sourceChoice->SetSelection(0);
    filt->Add(m_sourceChoice, 0, wxALL, FromDIP(3));

    filt->Add(new wxStaticText(this, wxID_ANY, _("User:")),
              0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(3));
    m_userText = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxSize(FromDIP(120), -1));
    filt->Add(m_userText, 0, wxALL, FromDIP(3));

    filt->Add(new wxStaticText(this, wxID_ANY, _("Search:")),
              0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(3));
    m_searchText = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxSize(FromDIP(160), -1));
    filt->Add(m_searchText, 1, wxALL | wxEXPAND, FromDIP(3));

    main->Add(filt, 0, wxEXPAND);

    // ----- AuiToolBar (Luna theme) — mirrors ibDialogUserList -----
    m_toolbarMain = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition,
        wxDefaultSize, wxAUI_TB_HORZ_TEXT);
    m_toolbarMain->SetArtProvider(new wxAuiLunaToolBarArt());

    m_toolbarMain->AddTool(wxID_AUDIT_TOOL_APPLY,    _("Apply"),
        ibBackendPicture::GetPicture(g_picFilterSetCLSID));
    m_toolbarMain->AddTool(wxID_AUDIT_TOOL_CLEAR,    _("Clear"),
        ibBackendPicture::GetPicture(g_picFilterClearCLSID));
    m_toolbarMain->AddTool(wxID_AUDIT_TOOL_REFRESH,  _("Refresh"),
        ibBackendPicture::GetPicture(g_picUpdateFormCLSID));
    m_toolbarMain->AddSeparator();
    m_toolbarMain->AddTool(wxID_AUDIT_TOOL_OPEN_REF, _("Open object"),
        ibBackendPicture::GetPicture(g_picSelectCLSID));

    // Live-refresh checkbox sits on the toolbar's right edge.
    m_tailCheck = new wxCheckBox(m_toolbarMain, wxID_ANY, _("Live refresh (3 s)"));
    m_tailCheck->Bind(wxEVT_CHECKBOX, &ibDialogAuditLog::OnTailToggle, this);
    m_toolbarMain->AddStretchSpacer(1);
    m_toolbarMain->AddControl(m_tailCheck);

    m_toolbarMain->SetForegroundColour(wxDefaultStypeFGColour);
    m_toolbarMain->SetBackgroundColour(wxDefaultStypeBGColour);
    m_toolbarMain->Realize();
    m_toolbarMain->Bind(wxEVT_MENU, &ibDialogAuditLog::OnCommandMenu, this);

    main->Add(m_toolbarMain, 0, wxEXPAND);

    // ----- DataView -----
    m_dataEditor = new ibDataViewCtrl(this, wxID_ANY,
        wxDefaultPosition, wxDefaultSize, 0);
    m_dataEditor->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED,
                       &ibDialogAuditLog::OnItemActivated, this);
    m_dataEditor->Bind(wxEVT_MENU, &ibDialogAuditLog::OnCommandMenu, this);

    m_dataEditor->AppendColumn(new ibDataViewColumn(_("Time"),
        new ibDataViewTextRenderer(wxT("string"), wxDATAVIEW_CELL_INERT),
        colTime, FromDIP(140), wxALIGN_LEFT, wxDATAVIEW_COL_SORTABLE));
    m_dataEditor->AppendColumn(new ibDataViewColumn(_("Level"),
        new ibDataViewTextRenderer(wxT("string"), wxDATAVIEW_CELL_INERT),
        colLevel, FromDIP(60), wxALIGN_LEFT, wxDATAVIEW_COL_SORTABLE));
    m_dataEditor->AppendColumn(new ibDataViewColumn(_("User"),
        new ibDataViewTextRenderer(wxT("string"), wxDATAVIEW_CELL_INERT),
        colUser, FromDIP(120), wxALIGN_LEFT, wxDATAVIEW_COL_SORTABLE));
    m_dataEditor->AppendColumn(new ibDataViewColumn(_("Source"),
        new ibDataViewTextRenderer(wxT("string"), wxDATAVIEW_CELL_INERT),
        colSource, FromDIP(80), wxALIGN_LEFT, wxDATAVIEW_COL_SORTABLE));
    m_dataEditor->AppendColumn(new ibDataViewColumn(_("Event"),
        new ibDataViewTextRenderer(wxT("string"), wxDATAVIEW_CELL_INERT),
        colEvent, FromDIP(100), wxALIGN_LEFT, wxDATAVIEW_COL_SORTABLE));
    m_dataEditor->AppendColumn(new ibDataViewColumn(_("Message"),
        new ibDataViewTextRenderer(wxT("string"), wxDATAVIEW_CELL_INERT),
        colMessage, FromDIP(320), wxALIGN_LEFT, 0));
    m_dataEditor->AppendColumn(new ibDataViewColumn(_("Ref"),
        new ibDataViewTextRenderer(wxT("string"), wxDATAVIEW_CELL_INERT),
        colRef, FromDIP(80), wxALIGN_LEFT, 0));

    m_dataEditor->SetForegroundColour(wxDefaultStypeFGColour);
    wxItemAttr attr(wxDefaultStypeFGColour, wxDefaultStypeBGColour,
                    m_dataEditor->GetFont());
    m_dataEditor->SetHeaderAttr(attr);

    // Model ctor calls PrepareData immediately — by the time the
    // dataview sees AssociateModel, the row count is already populated.
    m_dataEditor->AssociateModel(new ibAuditLogModel(m_reader.get(), ReadFilter()));
    main->Add(m_dataEditor, 1, wxEXPAND | wxALL, FromDIP(3));

    SetSizer(main);
}

ibLogFilter ibDialogAuditLog::ReadFilter() const
{
    ibLogFilter f;

    if (m_fromDate->GetValue().IsValid()) {
        wxDateTime from = m_fromDate->GetValue();
        from.ResetTime();
        f.from_ms = from.GetValue().GetValue();
    }
    if (m_toDate->GetValue().IsValid()) {
        wxDateTime to = m_toDate->GetValue();
        to.ResetTime();
        to.Add(wxDateSpan::Day());                                  // inclusive day
        f.to_ms = to.GetValue().GetValue();
    }

    const int lvlSel = m_levelChoice->GetSelection();
    if (lvlSel >= 0 && lvlSel < static_cast<int>(sizeof(kLevels)/sizeof(kLevels[0])))
        f.min_level = kLevels[lvlSel].minLevel;

    const int srcSel = m_sourceChoice->GetSelection();
    if (srcSel > 0 && kSources[srcSel] != nullptr)
        f.source = kSources[srcSel];

    f.user_name = m_userText->GetValue();
    f.search    = m_searchText->GetValue();
    f.limit     = 2000;
    return f;
}

void ibDialogAuditLog::Reload()
{
    if (!m_dataEditor) return;
    auto* model = dynamic_cast<ibAuditLogModel*>(m_dataEditor->GetModel());
    if (!model) return;
    model->PrepareData(m_reader.get(), ReadFilter());
    SetTitle(wxString::Format(_("Registration journal (%zu)"), model->RowCount()));
}

void ibDialogAuditLog::OnCommandMenu(wxCommandEvent& event)
{
    switch (event.GetId()) {
    case wxID_AUDIT_TOOL_APPLY:
    case wxID_AUDIT_TOOL_REFRESH:
        Reload();
        break;

    case wxID_AUDIT_TOOL_CLEAR: {
        wxDateTime from = wxDateTime::Today();
        from.Subtract(wxDateSpan::Days(7));
        m_fromDate->SetValue(from);
        m_toDate->SetValue(wxDateTime::Today());
        m_levelChoice->SetSelection(4);
        m_sourceChoice->SetSelection(0);
        m_userText->Clear();
        m_searchText->Clear();
        Reload();
        break;
    }

    case wxID_AUDIT_TOOL_OPEN_REF: {
        ibDataViewItem sel = m_dataEditor->GetSelection();
        if (!sel.IsOk()) break;
        auto* model = static_cast<ibAuditLogModel*>(m_dataEditor->GetModel());
        if (!model) break;
        const ibLogRow* r = model->RowAt(sel);
        if (!r || r->ref_guid.IsEmpty()) break;

        const ibGuid guid(r->ref_guid);
        if (r->ref_meta_id == 0) {
            // Convention from Phase 2g — system-table sys_user row.
            // Open the User Admin dialog keyed by guid instead of
            // going through metadata (sys_user is not a metaobject).
            ibDialogUserItem dlg(this, wxID_ANY);
            if (dlg.ReadUserData(guid)) dlg.ShowModal();
        }
        else if (activeMetaData != nullptr) {
            // Metadata-backed object — Catalog / Document / ChartOf*.
            // Create() takes metaData + metaID + guid and constructs
            // the typed ibValueReferenceDataObject via the metadata
            // registry; ShowValue() opens the configured object form.
            ibValueReferenceDataObject* refVal =
                ibValueReferenceDataObject::Create(
                    activeMetaData, r->ref_meta_id, guid);
            if (refVal != nullptr) {
                refVal->ShowValue();
                // Form takes ownership of the value on Show; if the
                // metadata id is stale (object type was removed) the
                // Create returned nullptr and we silently no-op.
            }
        }
        break;
    }
    }
    event.Skip();
}

void ibDialogAuditLog::OnItemActivated(ibDataViewEvent& event)
{
    wxCommandEvent fake(wxEVT_MENU, wxID_AUDIT_TOOL_OPEN_REF);
    OnCommandMenu(fake);
    event.Skip();
}

void ibDialogAuditLog::OnTailToggle(wxCommandEvent&)
{
    if (!m_tailCheck) return;
    if (m_tailCheck->IsChecked()) m_tailTimer->Start(3000);
    else                          m_tailTimer->Stop();
}

void ibDialogAuditLog::OnTimer(wxTimerEvent&)
{
    Reload();
}
