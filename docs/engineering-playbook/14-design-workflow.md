# 14. UI Design and Workflow

## Design tools for wxWidgets

### When to use graphical tools

| Scenario | Tool |
|----------|-----------|
| Prototype of a new dialog / window | wxFormBuilder or hand sketch |
| Complex layout with nested sizers | wxFormBuilder (emits C++ code) |
| Data tables and grids | wxGrid + manual tweaks |
| Custom widget design | Sketch on paper / Figma for concept |
| Reports and printed forms | OES report designer (built-in) |

**Rule:** for standard dialogs — wxFormBuilder. For complex custom widgets — sketch first, discuss, then code.

---

## OES design system

### Principles

1. **Native look** — UI must look native on the target platform. Don't imitate other OS styles.
2. **Consistency** — same margins, font sizes, button behaviour throughout the app.
3. **Accessibility** — keyboard navigation support, correct Tab order, labels for every control.
4. **Performance** — widgets are created once, only the content is updated (no recreation).

### Design tokens (C++ constants)

```cpp
// ui_constants.h — single place for all visual constants

namespace OesUI
{
    // Margins
    constexpr int MARGIN_SMALL  =  4;
    constexpr int MARGIN_NORMAL =  8;
    constexpr int MARGIN_LARGE  = 16;
    constexpr int PADDING_DIALOG = 12;

    // Button sizes
    // Note: constexpr wxSize requires wxWidgets 3.2+.
    // If using an earlier version replace constexpr with const.
    constexpr wxSize BTN_SIZE_NORMAL  { 90, 28 };
    constexpr wxSize BTN_SIZE_WIDE    { 120, 28 };
    constexpr wxSize BTN_SIZE_ICON    { 28, 28 };

    // Minimum dialog sizes
    constexpr wxSize DLG_MIN_SMALL    { 320, 200 };
    constexpr wxSize DLG_MIN_NORMAL   { 480, 320 };
    constexpr wxSize DLG_MIN_LARGE    { 640, 480 };

    // Fonts
    inline wxFont GetMonoFont(int ptSize = 9)
    {
        return wxFont(ptSize, wxFONTFAMILY_TELETYPE,
                      wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    }

    inline wxFont GetBoldFont(int ptSize = 0)
    {
        wxFont f = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
        if (ptSize > 0) f.SetPointSize(ptSize);
        f.SetWeight(wxFONTWEIGHT_BOLD);
        return f;
    }

    // Colours (use system colours where possible)
    inline wxColour GetErrorColour()   { return wxColour(200, 50, 50);   }
    inline wxColour GetWarningColour() { return wxColour(200, 130, 0);   }
    inline wxColour GetSuccessColour() { return wxColour(40, 140, 60);   }
    inline wxColour GetMutedColour()
    {
        wxColour sys = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
        return sys;
    }
}
```

### Typography

| Usage | Setting |
|-----------|-----------|
| Body text | `wxSYS_DEFAULT_GUI_FONT` (system) |
| Dialog titles | system font + `wxFONTWEIGHT_BOLD` |
| Code, SQL, scripts | `wxFONTFAMILY_TELETYPE`, pt 9 |
| Table column labels | system font + `wxFONTWEIGHT_BOLD` |
| Hints / small text | system font - 1pt |

---

## Layout

### Mandatory use of sizers

**Never** use absolute widget positions (`wxPoint`, `wxSize` in the constructor). Always use sizers — that's what makes layouts scale correctly across different DPIs and font sizes.

```cpp
// Right — through a sizer
wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

wxBoxSizer* formSizer = new wxBoxSizer(wxHORIZONTAL);
formSizer->Add(new wxStaticText(this, wxID_ANY, "Name:"),
               0, wxALIGN_CENTER_VERTICAL | wxRIGHT, OesUI::MARGIN_NORMAL);
formSizer->Add(m_nameCtrl, 1, wxEXPAND);

mainSizer->Add(formSizer, 0, wxEXPAND | wxALL, OesUI::PADDING_DIALOG);
mainSizer->Add(CreateButtonSizer(wxOK | wxCANCEL),
               0, wxEXPAND | wxALL, OesUI::PADDING_DIALOG);

SetSizerAndFit(mainSizer);

// Wrong — absolute positioning
m_nameCtrl = new wxTextCtrl(this, wxID_ANY, "", wxPoint(100, 20), wxSize(200, 24));
```

### Standard dialog structure

```cpp
// OES dialog template
class OesExampleDialog : public wxDialog
{
public:
    OesExampleDialog(wxWindow* parent, const wxString& title)
        : wxDialog(parent, wxID_ANY, title,
                   wxDefaultPosition, wxDefaultSize,
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    {
        BuildUI();
        BindEvents();
        SetMinSize(OesUI::DLG_MIN_NORMAL);
        Centre();
    }

private:
    void BuildUI()
    {
        wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);

        // Content area
        wxPanel* content = new wxPanel(this);
        wxBoxSizer* cs = new wxBoxSizer(wxVERTICAL);
        // ... add controls into cs
        content->SetSizer(cs);

        // Separator + buttons
        root->Add(content, 1, wxEXPAND | wxALL, OesUI::PADDING_DIALOG);
        root->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT,
                  OesUI::PADDING_DIALOG);
        root->Add(CreateButtonSizer(wxOK | wxCANCEL),
                  0, wxEXPAND | wxALL, OesUI::PADDING_DIALOG);

        SetSizerAndFit(root);
    }

    void BindEvents()
    {
        Bind(wxEVT_BUTTON, &OesExampleDialog::OnOK, this, wxID_OK);
    }

    void OnOK(wxCommandEvent&)
    {
        if (!Validate()) return;
        EndModal(wxID_OK);
    }
};
```

---

## Standard UI patterns

> **i18n note:** the string literals in the examples below are written inline for clarity.
> In production code every user-visible string must be wrapped in `_()`:
> `wxMessageBox(_("The 'Name' field is required."), ...)`.

### Forms with validation

```cpp
bool OesDocumentDialog::Validate()
{
    wxString name = m_nameCtrl->GetValue().Trim();
    if (name.IsEmpty())
    {
        wxMessageBox("The 'Name' field is required.",
                     "Error", wxOK | wxICON_WARNING, this);
        m_nameCtrl->SetFocus();
        return false;
    }

    if (name.Length() > 255)
    {
        wxMessageBox("Name must not exceed 255 characters.",
                     "Error", wxOK | wxICON_WARNING, this);
        m_nameCtrl->SetFocus();
        return false;
    }

    return true;
}
```

### Long-running operations: wxProgressDialog

```cpp
void OesReportView::ExportToFile(const wxString& path)
{
    wxProgressDialog progress(
        "Export",
        "Preparing data...",
        100,  // max
        this,
        wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_ELAPSED_TIME | wxPD_CAN_ABORT
    );

    for (size_t i = 0; i < m_rows.size(); ++i)
    {
        if (!progress.Update((int)(i * 100 / m_rows.size()),
                wxString::Format("Row %d of %d...", (int)(i+1), (int)m_rows.size())))
        {
            // The user pressed Cancel
            break;
        }
        // ... write the row
    }
}
```

### Tables (wxGrid)

```cpp
// Configure the grid's look
void OesDataGrid::ApplyStyle()
{
    SetDefaultCellFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
    SetLabelFont(OesUI::GetBoldFont());

    SetRowLabelSize(wxGRID_AUTOSIZE);
    SetColLabelAlignment(wxALIGN_LEFT, wxALIGN_CENTER);

    EnableGridLines(true);
    SetGridLineColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNSHADOW));

    // Alternating row colours
    SetDefaultCellBackgroundColour(
        wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    // Even rows — subtle highlight via wxGridCellAttr
}
```

### Context menu

```cpp
void OesListPanel::OnContextMenu(wxContextMenuEvent& event)
{
    wxMenu menu;
    menu.Append(ID_OPEN,   "Open\tEnter");
    menu.Append(ID_EDIT,   "Edit\tF2");
    menu.AppendSeparator();
    menu.Append(ID_DELETE, "Delete\tDel");
    menu.Append(ID_EXPORT, "Export...");

    // Disable items when nothing is selected
    if (m_list->GetSelectedItemCount() == 0)
    {
        menu.Enable(ID_OPEN,   false);
        menu.Enable(ID_EDIT,   false);
        menu.Enable(ID_DELETE, false);
        menu.Enable(ID_EXPORT, false);
    }

    PopupMenu(&menu);
}
```

---

## Accessibility

### Required

- **Tab order**: every interactive control must be reachable from the keyboard. Order — left to right, top to bottom.
- **Labels**: every `wxTextCtrl`, `wxComboBox`, `wxSpinCtrl` must have an associated `wxStaticText` (via `wxStaticText` + sizer or `wxWindow::SetLabel`).
- **Hotkeys**: for frequent actions, assign accelerators (`wxAcceleratorTable` or `&` in the menu text).
- **Tooltips**: `SetToolTip()` for icon buttons and non-obvious elements.
- **Error messages**: use `wxMessageBox` with `wxICON_WARNING` or `wxICON_ERROR`; don't swallow errors silently.

```cpp
// Right — the label is associated with the input
wxStaticText* lblName = new wxStaticText(this, wxID_ANY, "&Name:");
m_nameCtrl = new wxTextCtrl(this, wxID_ANY);
m_nameCtrl->SetToolTip("Enter the document name (up to 255 characters)");

// Tooltips for icon buttons
m_btnAdd->SetToolTip("Add record (Ins)");
m_btnDel->SetToolTip("Delete selected (Del)");
```

### Tab order

wxWidgets automatically uses the widget creation order as the Tab order. Create controls in logical order: field 1, field 2, ..., OK, Cancel.

---

## AI and UI code generation

### When AI helps with wxWidgets UI

| Task | AI usage |
|--------|-----------------|
| Generate a dialog skeleton | Describe the structure — AI emits the scaffold |
| Configure wxGrid | Request a ready example with the required columns |
| Implement sort/filter in a list | Algorithms + wxListCtrl or wxGrid |
| Write event handlers | AI generates from a description |

### Rules for porting AI code

**Must verify:**
- All margin constants replaced with `OesUI::MARGIN_*`
- No absolute positioning — sizers only
- Fonts are not hardcoded — use `wxSystemSettings` or `OesUI::Get*Font()`
- Colours use system `wxSYS_COLOUR_*` or constants from `OesUI`
- Event IDs declared in the module's enum, not as magic numbers
- No memory leaks: wx objects either have a parent or are added to a sizer (which owns them)

**Checklist after porting AI code:**
- [ ] Dialog scales correctly when resized
- [ ] Tab order is logical
- [ ] All icon buttons have tooltips
- [ ] Validation happens before `EndModal(wxID_OK)`
- [ ] Build with no compiler warnings
- [ ] Verified across different system fonts and DPIs

---

## wxFormBuilder

### Workflow

1. Create the layout in wxFormBuilder
2. Set all variable names per the project convention (`m_nameCtrl`, `m_listGrid`)
3. Export as a `.cpp`/`.h` pair (`form_generated.cpp`, `form_generated.h`)
4. Create a derived class; never hand-edit the generated file
5. Implement all business logic in the derived class

```cpp
// form_generated.h — do NOT edit
class OesDocumentFormBase : public wxDialog { ... };

// document_dialog.h — business logic only
class OesDocumentDialog : public OesDocumentFormBase
{
public:
    OesDocumentDialog(wxWindow* parent);
    DocumentData GetResult() const;

protected:
    void OnOKClicked(wxCommandEvent& event) override;
    void OnNameChanged(wxCommandEvent& event) override;

private:
    bool ValidateInput();
};
```

### Naming conventions in wxFormBuilder

| Element | Pattern | Example |
|---------|--------|--------|
| Form class | `Oes<Name>FormBase` | `OesDocumentFormBase` |
| TextCtrl | `m_<name>Ctrl` | `m_titleCtrl` |
| ComboBox | `m_<name>Combo` | `m_statusCombo` |
| Grid | `m_<name>Grid` | `m_itemsGrid` |
| Button | `m_btn<Action>` | `m_btnAdd`, `m_btnOK` |
| CheckBox | `m_chk<Name>` | `m_chkActive` |
| StaticText (label) | `m_lbl<Name>` | `m_lblTitle` |

---

## UI checklist before handoff

- [ ] Dialog opens and closes without memory leaks (verified under Dr. Memory)
- [ ] Behaves correctly at the minimum window size
- [ ] Tab order checked (Tab traversal covers every control)
- [ ] Hotkeys work (Enter = OK, Escape = Cancel)
- [ ] Error messages are clear to the user
- [ ] Long-running operations show progress (wxProgressDialog)
- [ ] No hardcoded Russian strings in C++ code — only through `_()` or resources (i18n-ready)
- [ ] Appearance verified on Windows 10/11 with different themes (light/dark)
