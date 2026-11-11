#include "advpropList.h"
#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the chosen one's id
#include "frmclient/propertyManager/property/private/prop.h"             // ibPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"
#include "frmclient/propertyManager/propertyEditor.h"
#include "deskgui/univ/dialog.h"
#include "deskgui/univ/dataview/dataview.h"   // ibDataViewListCtrl — the dialog's list of choices

#define icon_size 16

// -----------------------------------------------------------------------
// ibPGListProperty
// -----------------------------------------------------------------------

ibPG_IMPLEMENT_PROPERTY_CLASS(ibPGListProperty, ibPGProperty, ComboBoxAndButton)

namespace {

// The id a value holds — the chosen one's; none picked: wxNOT_FOUND…
long ReadId(const ibVariant& value)
{
	const ibDataValue node = ibVariantSerializable::GetNodeValue(value);
	return node.Kind() == ibDataKind::Number ? static_cast<long>(node.AsInt()) : wxNOT_FOUND;
}

// …and the value of another: the same property's, named by the cell (ValueToString).
ibVariant WriteId(long id)
{
	return ibVariant(new ibVariantSerializable(ibDataValue::Int(id), wxEmptyString));
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
		const ibPropertyRegistry::ibPropertyMaker maker = [](ibBackendProperty* property) -> ibPGProperty* {
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
	const ibPropertyChoiceList& choices, const ibVariant& value)
	: ibPGProperty(label, strName)
{
	ibPGChoices ch;
	for (unsigned int idx = 0; idx < choices.GetCount(); idx++) {
		ibPGChoiceEntry item(choices.GetLabel(idx), choices.GetId(idx));
		item.SetBitmap(choices.GetBitmap(idx));
		ch.Add(item);
	}
	m_choices.Assign(ch);

	m_flags |= ibPGPropertyFlags_ActiveButton; // Property button always enabled.
	SetValue(value);
}

wxString ibPGListProperty::ValueToString(ibVariant& value, ibPGPropValFormatFlags flags) const
{
	const long id = ReadId(value);
	for (unsigned int idx = 0; idx < m_choices.GetCount(); idx++) {
		if (id == m_choices.GetValue(idx))
			return m_choices.GetLabel(idx);
	}

	return wxEmptyString;
}

bool ibPGListProperty::StringToValue(ibVariant& variant,
	const wxString& text,
	ibPGPropValFormatFlags flags) const
{
	if (text.IsEmpty()) {
		variant = WriteId(wxNOT_FOUND);
		return true;
	}

	return false;
}

bool ibPGListProperty::IntToValue(ibVariant& value, int number, ibPGPropValFormatFlags flags) const
{
	value = WriteId(m_choices.GetValue(number));
	return true;
}

int ibPGListProperty::GetChoiceSelection() const
{
	return m_choices.Index(ReadId(m_value));
}

ibPGEditorDialogAdapter* ibPGListProperty::GetEditorDialog() const
{
	class ibPGListEventAdapter : public ibPGEditorDialogAdapter {
	public:

		virtual bool DoShowDialog(ibPropertyGrid* pg, ibPGProperty* prop) wxOVERRIDE
		{
			ibPGListProperty* dlgProp = wxDynamicCast(prop, ibPGListProperty);
			wxCHECK_MSG(dlgProp, false, "Function called for incompatible property");

			// launch editor dialog
			ibDialog* dlg = new ibDialog(pg, wxID_ANY, _("Select object"), wxDefaultPosition, wxDefaultSize,
				wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxCLIP_CHILDREN);

			dlg->SetFont(pg->GetFont()); // To allow entering chars of the same set as the propGrid

			// Multi-line text editor dialog.
			const int spacing = ibPropertyGrid::IsSmallScreen() ? 4 : 8;
			wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);
			wxBoxSizer* rowsizer = new wxBoxSizer(wxHORIZONTAL);

			// The list's one column — the choice's name and picture.
			ibDataViewListCtrl* lc = new ibDataViewListCtrl(dlg, wxID_ANY,
				wxDefaultPosition, wxDefaultSize, wxDV_NO_HEADER | wxDV_ROW_LINES);
			lc->AppendIconTextColumn(wxT("name"), wxDATAVIEW_CELL_INERT, wxCOL_WIDTH_DEFAULT,
				wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);

			rowsizer->Add(lc, wxSizerFlags(1).Expand().Border(wxALL, spacing));
			topsizer->Add(rowsizer, wxSizerFlags(1).Expand());

			lc->Enable(!dlgProp->HasFlag(ibPGFlags::ReadOnly));

			ibStdDialogButtonSizer* buttonSizer = dlg->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
			topsizer->Add(buttonSizer, wxSizerFlags(0).Right().Border(wxBOTTOM | wxRIGHT, spacing));

			dlg->SetSizer(topsizer);
			topsizer->SetSizeHints(dlg);

			if (!ibPropertyGrid::IsSmallScreen()) {
				dlg->SetSize(dlg->FromDIP(wxSize(400, 300)));
				dlg->Move(pg->GetGoodEditorDialogPosition(prop, dlg->GetSize()));
			}

			lc->SetFocus();

			const ibPGChoices& choices = dlgProp->GetChoices();
			const long selected = ReadId(prop->GetValue());

			int selectedRow = wxNOT_FOUND;
			for (unsigned int idx = 0; idx < choices.GetCount(); idx++) {
				wxVector<ibVariant> values;
				values.push_back(ibVariant(new ibDataViewIconText(choices.GetLabel(idx), choices.Item(idx).GetBitmap())));
				lc->AppendItem(values);
				if (selected == choices.GetValue(idx))
					selectedRow = static_cast<int>(idx);
			}

			// …the value's row selected once all are in: a row appended is selected (DoItemInserted - a table's Add lands
			// on its new row), and the value's, selected in the middle, gave way to the last row (Max, 2026-11-10: "I choose
			// English and the Russian row stays selected")
			if (selectedRow != wxNOT_FOUND)
				lc->SelectRow(static_cast<unsigned int>(selectedRow));
			else
				lc->UnselectAll();

			const int res = dlg->ShowModal();
			const int row = lc->GetSelectedRow();
			if (res == wxID_OK && row != wxNOT_FOUND)
				SetValue(WriteId(choices.GetValue(static_cast<unsigned int>(row))));
			else if (res == wxID_OK)
				SetValue(WriteId(wxNOT_FOUND));

			dlg->Destroy();
			return res == wxID_OK;
		}
	};

	return new ibPGListEventAdapter();
}