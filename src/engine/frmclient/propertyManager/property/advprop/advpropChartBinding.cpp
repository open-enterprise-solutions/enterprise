#include "advpropChartBinding.h"

#include <algorithm>

#include "frmclient/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"
#include "frmclient/propertyManager/propertyEditor.h"
#include "frmclient/win/ctrls/checktree.h"
#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the ids' node

#define icon_size 16

// -----------------------------------------------------------------------
// ibPGChartBindingProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGChartBindingProperty, wxPGProperty, ComboBoxAndButton)

// register frontend property — one editor, every property that names metaobjects (see the header)
class ibPropertyChartBindingLoader
{
public:
	ibPropertyChartBindingLoader()
	{
		const auto maker = [](ibBackendProperty* property) -> wxPGProperty* {
			ibPropertyChoiceList choices;
			const ibPropertyChoiceMode mode = property->GetValueList(choices);
			return new ibPGChartBindingProperty(property->GetLabel(), property->GetName(),
				property->GetValue(), choices, mode);
		};
		for (const wxChar* className : {
				wxT("ibPropertyChartOfAccounts"), wxT("ibPropertyChartOfCharacteristicTypes"),
				wxT("ibPropertyChartOfCalculationTypes"), wxT("ibPropertyAccountingKind"),
				wxT("ibPropertyFunctionalOptions") })
			ibPropertyRegistry::Register(className, maker);
	}
} g_chartBindingLoader;

// The row's value, its node form: an Array of ids; anything else (no value yet) holds none.
std::vector<ibMetaID> ibPGChartBindingProperty::IdsOf(const wxVariant& value)
{
	std::vector<ibMetaID> ids;
	const ibDataValue node = ibVariantDataSerializable::GetNodeValue(value);
	if (node.Kind() != ibDataKind::Array)
		return ids;
	for (const ibDataValue& item : node.AsArray())
		ids.push_back(static_cast<ibMetaID>(item.AsInt()));
	return ids;
}

// …named by the cell (ValueToString).
wxVariant ibPGChartBindingProperty::ValueOf(const wxVariant& value, const std::vector<ibMetaID>& ids)
{
	std::vector<ibDataValue> items;
	for (const ibMetaID id : ids)
		items.push_back(ibDataValue::Int(id));
	return wxVariant(new ibVariantDataSerializable(value.GetType(), ibDataValue::Array(items), wxEmptyString));
}

ibPGChartBindingProperty::ibPGChartBindingProperty(const wxString& label, const wxString& strName,
	const wxVariant& value, const ibPropertyChoiceList& choices, ibPropertyChoiceMode mode)
	: wxPGProperty(label, strName), m_choiceMode(mode)
{
	for (unsigned int idx = 0; idx < choices.GetCount(); idx++)
		m_choices.Add(choices.GetLabel(idx), choices.GetBitmap(idx), choices.GetId(idx));

	m_flags |= wxPGPropertyFlags_ActiveButton; // Property button always enabled.

	SetValue(value);
}

// The names of what it holds, as the desktop's value string lists them.
wxString ibPGChartBindingProperty::ValueToString(wxVariant& value, wxPGPropValFormatFlags WXUNUSED(flags)) const
{
	wxString text;
	for (const ibMetaID id : IdsOf(value)) {
		const int idx = m_choices.Index(id);
		if (idx == wxNOT_FOUND)
			continue;   // an id no choice names any more: the server drops it on the way out
		if (!text.IsEmpty())
			text += wxT(", ");
		text += m_choices.GetLabel(idx);
	}
	return text;
}

bool ibPGChartBindingProperty::StringToValue(wxVariant& WXUNUSED(variant), const wxString& WXUNUSED(text),
	wxPGPropValFormatFlags WXUNUSED(flags)) const
{
	return false;
}

// One picked from the drop-down: it alone — the desktop's SetDefaultMetaType.
bool ibPGChartBindingProperty::IntToValue(wxVariant& value, int number, wxPGPropValFormatFlags WXUNUSED(flags)) const
{
	if (number < 0 || number >= static_cast<int>(m_choices.GetCount()))
		return false;
	value = ValueOf(value, std::vector<ibMetaID>{ static_cast<ibMetaID>(m_choices.GetValue(number)) });
	return true;
}

wxPGEditorDialogAdapter* ibPGChartBindingProperty::GetEditorDialog() const
{
	class ibPGEditorChartBindingDialogAdapter : public wxPGEditorDialogAdapter {

		class ibTreeItemPropertyData : public wxTreeItemData {
			ibMetaID m_metaId;
		public:
			ibTreeItemPropertyData(ibMetaID id) : wxTreeItemData(), m_metaId(id) {}
			ibMetaID GetMetaID() const { return m_metaId; }
		};

	public:

		virtual bool DoShowDialog(wxPropertyGrid* pg, wxPGProperty* prop) wxOVERRIDE
		{
			ibPGChartBindingProperty* dlgProp = wxDynamicCast(prop, ibPGChartBindingProperty);
			wxCHECK_MSG(dlgProp, false, "Function called for incompatible property");

			const std::vector<ibMetaID> held = IdsOf(dlgProp->GetValue());

			// launch editor dialog — titled by the property it edits, so one class serves every binding
			wxDialog* dlg = new wxDialog(pg, wxID_ANY, dlgProp->GetLabel(), wxDefaultPosition, wxDefaultSize,
				wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxCLIP_CHILDREN);

			dlg->SetFont(pg->GetFont()); // To allow entering chars of the same set as the propGrid

			const int spacing = wxPropertyGrid::IsSmallScreen() ? 4 : 8;
			wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);
			wxBoxSizer* rowsizer = new wxBoxSizer(wxHORIZONTAL);

			// AS MANY CHECKS AS THE PROPERTY SAYS — one for a binding that names one object, any for a set.
			const long checkStyle = dlgProp->IsSet() ? wxCR_MULTIPLE_CHECK : wxCR_SINGLE_CHECK;
			ibCheckTree* tc = new ibCheckTree(dlg, wxID_ANY,
				wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxTR_HIDE_ROOT | checkStyle | wxCR_EMPTY_CHECK | wxSUNKEN_BORDER | wxTR_TWIST_BUTTONS);

			tc->AddRoot(wxEmptyString);   // the root is hidden (wxTR_HIDE_ROOT); only its existence matters

			rowsizer->Add(tc, wxSizerFlags(1).Expand().Border(wxALL, spacing));
			topsizer->Add(rowsizer, wxSizerFlags(1).Expand());

			tc->SetDoubleBuffered(true);
			tc->Enable(!dlgProp->HasFlag(wxPGFlags::ReadOnly));

			wxStdDialogButtonSizer* buttonSizer = dlg->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
			topsizer->Add(buttonSizer, wxSizerFlags(0).Right().Border(wxBOTTOM | wxRIGHT, spacing));

			dlg->SetSizer(topsizer);
			topsizer->SetSizeHints(dlg);

			if (!wxPropertyGrid::IsSmallScreen()) {
				dlg->SetSize(dlg->FromDIP(wxSize(400, 300)));
				dlg->Move(pg->GetGoodEditorDialogPosition(prop, dlg->GetSize()));
			}

			tc->SetFocus();

			tc->AssignImageList(new wxImageList(icon_size, icon_size));

			// The candidates are the row's own choices — one item per object.
			wxImageList* imageList = tc->GetImageList();
			const wxPGChoices& choices = dlgProp->GetChoices();
			for (unsigned int idx = 0; idx < choices.GetCount(); idx++) {
				const ibMetaID id = choices.GetValue(idx);
				const wxBitmapBundle& bitmap = choices.Item(idx).GetBitmap();
				const int icon = bitmap.IsOk() ? imageList->Add(bitmap.GetBitmap(wxSize(icon_size, icon_size))) : -1;
				const wxTreeItemId item = tc->AppendItem(tc->GetRootItem(), choices.GetLabel(idx),
					icon, icon, new ibTreeItemPropertyData(id));
				const bool checked = std::find(held.begin(), held.end(), id) != held.end();
				tc->SetItemState(item, checked ? ibCheckTree::CHECKED : ibCheckTree::UNCHECKED);
				tc->Check(item, checked);
			}

			tc->ExpandAll(); int res = dlg->ShowModal();

			// 🛑 ONLY AN ACCEPTED DIALOG CHANGES THE VALUE.
			if (res == wxID_OK) {
				std::vector<ibMetaID> ids;
				wxArrayTreeItemIds selected;
				tc->GetSelections(selected);
				// BY VALUE: wxArrayTreeItemIds stores void*, so a reference here binds to a per-iteration temporary.
				for (const wxTreeItemId selItem : selected) {
					if (!selItem.IsOk())
						continue;
					if (ibTreeItemPropertyData* item = dynamic_cast<ibTreeItemPropertyData*>(tc->GetItemData(selItem)))
						ids.push_back(item->GetMetaID());
				}
				SetValue(ValueOf(dlgProp->GetValue(), ids));
			}

			dlg->Destroy();
			return res == wxID_OK;
		}
	};

	return new ibPGEditorChartBindingDialogAdapter();
}
