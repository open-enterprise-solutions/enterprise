#include "advpropList.h"
#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the chosen one's id
#include "frmclient/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"
#include "frmclient/propertyManager/propertyEditor.h"
#include "gui/univ/dialog.h"
#include "gui/univ/dataview/dataview.h"   // ibDataViewListCtrl — the dialog's list of choices

#define icon_size 16

// -----------------------------------------------------------------------
// ibPGListProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGListProperty, wxPGProperty, ComboBoxAndButton)

namespace {

// The id a value holds — the chosen one's; none picked: wxNOT_FOUND…
long ReadId(const wxVariant& value)
{
	const ibDataValue node = ibVariantDataSerializable::GetNodeValue(value);
	return node.Kind() == ibDataKind::Number ? static_cast<long>(node.AsInt()) : wxNOT_FOUND;
}

// …and the value of another: the same property's, named by the cell (ValueToString).
wxVariant WriteId(const wxVariant& value, long id)
{
	return wxVariant(new ibVariantDataSerializable(value.GetType(), ibDataValue::Int(id), wxEmptyString));
}

} // namespace

// register frontend property 
class ibPropertyListLoader
{
public:
	ibPropertyListLoader()
	{
		// ibPropertyList, whatever it lists — a language, a role: the choices are the row's (GetValueList, asked on the
		// server), the value the chosen one's id.
		const ibPropertyRegistry::ibPropertyMaker maker = [](ibBackendProperty* property) -> wxPGProperty* {
			ibPropertyChoiceList choices;
			property->GetValueList(choices);
			return new ibPGListProperty(property->GetLabel(), property->GetName(), choices, property->GetValue());
		};
		ibPropertyRegistry::Register(wxT("ibPropertyList"), maker);

		// …and a DYNAMIC LIST'S SOURCE, the table it reads: the choices are the sources the configuration registers, the
		// value the chosen one's table id (none picked: no value).
		ibPropertyRegistry::Register(wxT("ibPropertyDynamicSource"), maker);
	}
}g_listLoader;

ibPGListProperty::ibPGListProperty(const wxString& label, const wxString& strName,
	const ibPropertyChoiceList& choices, const wxVariant& value)
	: wxPGProperty(label, strName)
{
	wxPGChoices ch;
	for (unsigned int idx = 0; idx < choices.GetCount(); idx++) {
		wxPGChoiceEntry item(choices.GetLabel(idx), choices.GetId(idx));
		item.SetBitmap(choices.GetBitmap(idx));
		ch.Add(item);
	}
	m_choices.Assign(ch);

	m_flags |= wxPGPropertyFlags_ActiveButton; // Property button always enabled.
	SetValue(value);
}

wxString ibPGListProperty::ValueToString(wxVariant& value, wxPGPropValFormatFlags flags) const
{
	const long id = ReadId(value);
	for (unsigned int idx = 0; idx < m_choices.GetCount(); idx++) {
		if (id == m_choices.GetValue(idx))
			return m_choices.GetLabel(idx);
	}

	return wxEmptyString;
}

bool ibPGListProperty::StringToValue(wxVariant& variant,
	const wxString& text,
	wxPGPropValFormatFlags flags) const
{
	if (text.IsEmpty()) {
		variant = WriteId(variant, wxNOT_FOUND);
		return true;
	}

	return false;
}

bool ibPGListProperty::IntToValue(wxVariant& value, int number, wxPGPropValFormatFlags flags) const
{
	value = WriteId(value, m_choices.GetValue(number));
	return true;
}

int ibPGListProperty::GetChoiceSelection() const
{
	return m_choices.Index(ReadId(m_value));
}

wxPGEditorDialogAdapter* ibPGListProperty::GetEditorDialog() const
{
	class wxPGListEventAdapter : public wxPGEditorDialogAdapter {
	public:

		virtual bool DoShowDialog(wxPropertyGrid* pg, wxPGProperty* prop) wxOVERRIDE
		{
			ibPGListProperty* dlgProp = wxDynamicCast(prop, ibPGListProperty);
			wxCHECK_MSG(dlgProp, false, "Function called for incompatible property");

			// launch editor dialog
			ibDialog* dlg = new ibDialog(pg, wxID_ANY, _("Select object"), wxDefaultPosition, wxDefaultSize,
				wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxCLIP_CHILDREN);

			dlg->SetFont(pg->GetFont()); // To allow entering chars of the same set as the propGrid

			// Multi-line text editor dialog.
			const int spacing = wxPropertyGrid::IsSmallScreen() ? 4 : 8;
			wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);
			wxBoxSizer* rowsizer = new wxBoxSizer(wxHORIZONTAL);

			// The list's one column — the choice's name and picture.
			ibDataViewListCtrl* lc = new ibDataViewListCtrl(dlg, wxID_ANY,
				wxDefaultPosition, wxDefaultSize, wxDV_NO_HEADER | wxDV_ROW_LINES);
			lc->AppendIconTextColumn(wxT("name"), wxDATAVIEW_CELL_INERT, wxCOL_WIDTH_DEFAULT,
				wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);

			rowsizer->Add(lc, wxSizerFlags(1).Expand().Border(wxALL, spacing));
			topsizer->Add(rowsizer, wxSizerFlags(1).Expand());

			lc->SetDoubleBuffered(true);
			lc->Enable(!dlgProp->HasFlag(wxPGFlags::ReadOnly));

			ibStdDialogButtonSizer* buttonSizer = dlg->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
			topsizer->Add(buttonSizer, wxSizerFlags(0).Right().Border(wxBOTTOM | wxRIGHT, spacing));

			dlg->SetSizer(topsizer);
			topsizer->SetSizeHints(dlg);

			if (!wxPropertyGrid::IsSmallScreen()) {
				dlg->SetSize(dlg->FromDIP(wxSize(400, 300)));
				dlg->Move(pg->GetGoodEditorDialogPosition(prop, dlg->GetSize()));
			}

			lc->SetFocus();

			const wxPGChoices& choices = dlgProp->GetChoices();
			const long selected = ReadId(prop->GetValue());

			for (unsigned int idx = 0; idx < choices.GetCount(); idx++) {
				wxVector<ibVariant> values;
				values.push_back(ibVariant(new ibDataViewIconText(choices.GetLabel(idx), choices.Item(idx).GetBitmap())));
				lc->AppendItem(values);
				if (selected == choices.GetValue(idx))
					lc->SelectRow(idx);
			}

			const int res = dlg->ShowModal();
			const int row = lc->GetSelectedRow();
			if (res == wxID_OK && row != wxNOT_FOUND)
				SetValue(WriteId(prop->GetValue(), choices.GetValue(static_cast<unsigned int>(row))));
			else if (res == wxID_OK)
				SetValue(WriteId(prop->GetValue(), wxNOT_FOUND));

			dlg->Destroy();
			return res == wxID_OK;
		}
	};

	return new wxPGListEventAdapter();
}