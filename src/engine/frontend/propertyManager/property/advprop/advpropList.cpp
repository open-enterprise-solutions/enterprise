#include "advpropList.h"
#include "backend/propertyManager/property/propertyList.h"
#include "frontend/propertyManager/property/private/prop.h"
#include "frontend/propertyManager/propertyEditor.h"

#define icon_size 16

// -----------------------------------------------------------------------
// ibPGListProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGListProperty, wxPGProperty, ComboBoxAndButton)

// register frontend property 
class ibPropertyListLoader
{
public:
	ibPropertyListLoader()
	{
		ibPG_IMPLEMENT_PROPERTY_CALLBACK(ibPGListProperty, ibPropertyList::ms_propertyList);
	}
}g_listLoader;

ibPGListProperty::ibPGListProperty(const wxString& label, const wxString& strName,
	wxPGChoices choices, int value)
	: wxPGProperty(label, strName)
{
	m_choices.Assign(choices);
	m_value = wxVariant(0L);

	m_flags |= wxPGPropertyFlags_ActiveButton; // Property button always enabled.
	SetValue(value);
}

wxString ibPGListProperty::ValueToString(wxVariant& value, wxPGPropValFormatFlags flags) const
{
	for (unsigned int idx = 0; idx < m_choices.GetCount(); idx++) {
		if (value.GetLong() == m_choices.GetValue(idx))
			return m_choices.GetLabel(idx);
	}

	return wxEmptyString;
}

bool ibPGListProperty::StringToValue(wxVariant& variant,
	const wxString& text,
	wxPGPropValFormatFlags flags) const
{
	if (text.IsEmpty()) {
		variant = WXVARIANT(wxNOT_FOUND);
		return true;
	}

	return false;
}

bool ibPGListProperty::IntToValue(wxVariant& value, int number, wxPGPropValFormatFlags flags) const
{
	value = WXVARIANT(m_choices.GetValue(number));
	return true;
}

#include <wx/dataview.h>
#include <wx/treectrl.h>

wxPGEditorDialogAdapter* ibPGListProperty::GetEditorDialog() const
{
	class wxPGListEventAdapter : public wxPGEditorDialogAdapter {

		enum
		{
			list_column_name = 0
		};

		class wxDataViewListCtrl : public wxDataViewCtrl {

			class wxDataViewPropertyListStore : public wxDataViewListStore {
			public:
				wxDataViewPropertyListStore() { AppendColumn(wxT("wxDataViewIconText")); }
			};

			wxDataViewListStore* GetStore() {
				return (wxDataViewPropertyListStore*)GetModel();
			}

		public:

			wxDataViewListCtrl(wxWindow* parent, wxWindowID id,
				const wxPoint& pos = wxDefaultPosition,
				const wxSize& size = wxDefaultSize, long style = wxDV_NO_HEADER | wxDV_ROW_LINES,
				const wxValidator& validator = wxDefaultValidator,
				const wxString& name = wxASCII_STR(wxDataViewCtrlNameStr))
				: wxDataViewCtrl(parent, id, pos, size, style, validator, name)
			{
				wxDataViewListCtrl::AssociateModel(new wxDataViewPropertyListStore());
				wxDataViewListCtrl::AppendColumn(
					new wxDataViewColumn(wxT("name"), new wxDataViewIconTextRenderer, list_column_name,
						-1, static_cast<wxAlignment>(wxALIGN_LEFT), wxDATAVIEW_COL_RESIZABLE)
				);
			}

			void AppendItem(const wxString& strName, const wxBitmapBundle& bitmap, wxUIntPtr data = 0) {
				wxVector<wxVariant> values;
				wxVariant value;
				value << wxDataViewIconText(strName, bitmap);
				values.push_back(value);
				GetStore()->AppendItem(values, data);
			}
		};

	public:

		virtual bool DoShowDialog(wxPropertyGrid* pg, wxPGProperty* prop) wxOVERRIDE
		{
			ibPGListProperty* dlgProp = wxDynamicCast(prop, ibPGListProperty);
			wxCHECK_MSG(dlgProp, false, "Function called for incompatible property");

			// launch editor dialog
			wxDialog* dlg = new wxDialog(pg, wxID_ANY, _("Select object"), wxDefaultPosition, wxDefaultSize,
				wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxCLIP_CHILDREN);

			dlg->SetFont(pg->GetFont()); // To allow entering chars of the same set as the propGrid

			// Multi-line text editor dialog.
			const int spacing = wxPropertyGrid::IsSmallScreen() ? 4 : 8;
			wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);
			wxBoxSizer* rowsizer = new wxBoxSizer(wxHORIZONTAL);

			wxDataViewListCtrl* lc = new wxDataViewListCtrl(dlg, wxID_ANY,
				wxDefaultPosition, wxDefaultSize, wxDV_NO_HEADER | wxDV_ROW_LINES);

			rowsizer->Add(lc, wxSizerFlags(1).Expand().Border(wxALL, spacing));
			topsizer->Add(rowsizer, wxSizerFlags(1).Expand());

			lc->SetDoubleBuffered(true);
			lc->Enable(!dlgProp->HasFlag(wxPGFlags::ReadOnly));

			wxStdDialogButtonSizer* buttonSizer = dlg->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
			topsizer->Add(buttonSizer, wxSizerFlags(0).Right().Border(wxBOTTOM | wxRIGHT, spacing));

			dlg->SetSizer(topsizer);
			topsizer->SetSizeHints(dlg);

			if (!wxPropertyGrid::IsSmallScreen()) {
				dlg->SetSize(400, 300);
				dlg->Move(pg->GetGoodEditorDialogPosition(prop, dlg->GetSize()));
			}

			lc->SetFocus();

			const wxPGChoices& choices = dlgProp->GetChoices();
			const long selected = prop->GetValue().GetLong();

			for (unsigned int idx = 0; idx < choices.GetCount(); idx++) {
				const wxBitmapBundle& bitmap = choices.Item(idx).GetBitmap();
				lc->AppendItem(
					choices.GetLabel(idx),
					bitmap,
					idx + 1
				);
				if (selected == choices.GetValue(idx))
					lc->Select(wxDataViewItem(reinterpret_cast<void*>(idx + 1)));
			}

			const int res = dlg->ShowModal();
			const wxDataViewItem& item = lc->GetSelection();
			if (res == wxID_OK && item.IsOk()) SetValue(
				choices.GetValue(
					static_cast<unsigned int>(reinterpret_cast<uintptr_t>(item.GetID())) - 1));
			else if (res == wxID_OK)
				SetValue(wxNOT_FOUND);

			dlg->Destroy();
			return res == wxID_OK;
		}
	};

	return new wxPGListEventAdapter();
}