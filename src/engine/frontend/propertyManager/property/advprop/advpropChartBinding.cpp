#include "advpropChartBinding.h"

#include "backend/propertyManager/property/variant/variantOwner.h"
#include "backend/propertyManager/property/propertyChartOfAccounts.h"
#include "backend/propertyManager/property/propertyChartOfCharacteristicTypes.h"
#include "backend/propertyManager/property/propertyChartOfCalculationTypes.h"

#include "frontend/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frontend/propertyManager/property/private/propertyRegistry.h"
#include "frontend/propertyManager/propertyEditor.h"

#define icon_size 16

// -----------------------------------------------------------------------
// ibPGChartBindingProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGChartBindingProperty, wxPGProperty, ComboBoxAndButton)

// register frontend property — one editor, every chart binding (see the header)
template <typename TProperty>
static wxPGProperty* ibCreateChartBindingProperty(TProperty* prop)
{
	ibPropertyChoiceList choices;
	const ibPropertyChoiceMode mode = prop->GetValueList(choices);
	return new ibPGChartBindingProperty(prop->GetPropertyObject(), prop->GetLabel(), prop->GetName(), prop->GetValue(), choices, mode);
}

class ibPropertyChartBindingLoader
{
public:
	ibPropertyChartBindingLoader()
	{
		ibPropertyRegistry::Register([](ibPropertyChartOfAccounts* prop) -> wxPGProperty* { return ibCreateChartBindingProperty(prop); });
		ibPropertyRegistry::Register([](ibPropertyChartOfCharacteristicTypes* prop) -> wxPGProperty* { return ibCreateChartBindingProperty(prop); });
		// Left out, a calculation register showed no chart at all in the designer — its binding existed,
		// saved and applied, and could only be set by a script.
		ibPropertyRegistry::Register([](ibPropertyChartOfCalculationTypes* prop) -> wxPGProperty* { return ibCreateChartBindingProperty(prop); });
	}
}g_chartBindingLoader;

#include "backend/metaData.h"

ibPGChartBindingProperty::ibPGChartBindingProperty(const ibPropertyObject* property, const wxString& label, const wxString& strName, const wxVariant& value,
	const ibPropertyChoiceList& choices, ibPropertyChoiceMode mode)
	: wxPGProperty(label, strName), m_ownerProperty(property), m_choiceMode(mode)
{
	for (unsigned int idx = 0; idx < choices.GetCount(); idx++)
		m_choices.Add(choices.GetLabel(idx), choices.GetBitmap(idx), choices.GetId(idx));

	m_flags |= wxPGPropertyFlags_ActiveButton; // Property button always enabled.

	SetValue(value);
}

wxString ibPGChartBindingProperty::ValueToString(wxVariant& value, wxPGPropValFormatFlags flags) const
{
	return value.GetString();
}

bool ibPGChartBindingProperty::StringToValue(wxVariant& variant,
	const wxString& text,
	wxPGPropValFormatFlags flags) const
{
	return false;
}

bool ibPGChartBindingProperty::IntToValue(wxVariant& value, int number, wxPGPropValFormatFlags flags) const
{
	ibVariantDataOwner* dataOwner = property_cast(value, ibVariantDataOwner);
	if (dataOwner != nullptr) {
		ibVariantDataOwner* newDataOwner = dataOwner->Clone();
		wxASSERT(newDataOwner);
		ibMetaDescription& md = newDataOwner->GetMetaDesc();
		md.SetDefaultMetaType(m_choices.GetValue(number));
		value = newDataOwner;
		return true;
	}
	return false;
}

#include "frontend/win/ctrls/checktree.h"

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

			ibVariantDataOwner* data = property_cast(dlgProp->GetValue(), ibVariantDataOwner);
			if (data == nullptr) return false;

			// launch editor dialog — titled by the property it edits, so one class serves every chart
			wxDialog* dlg = new wxDialog(pg, wxID_ANY, dlgProp->GetLabel(), wxDefaultPosition, wxDefaultSize,
				wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxCLIP_CHILDREN);

			dlg->SetFont(pg->GetFont()); // To allow entering chars of the same set as the propGrid

			const int spacing = wxPropertyGrid::IsSmallScreen() ? 4 : 8;
			wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);
			wxBoxSizer* rowsizer = new wxBoxSizer(wxHORIZONTAL);

			// AS MANY CHECKS AS THE PROPERTY SAYS — a binding that names one chart must not let two be
			// ticked in the first place (trimming the result afterwards would be a lie about what the user
			// did: they saw two accepted and got one), and a set must let every one be.
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

			// Make an state image list containing small icons
			tc->AssignImageList(
				new wxImageList(icon_size, icon_size)
			);

			// The candidates are the property's own answer (see the header) — one item per chart.
			wxImageList* imageList = tc->GetImageList();
			const ibMetaDescription& md = data->GetMetaDesc();
			const wxPGChoices& choices = dlgProp->GetChoices();
			for (unsigned int idx = 0; idx < choices.GetCount(); idx++) {
				const ibMetaID id = choices.GetValue(idx);
				const wxBitmapBundle& bitmap = choices.Item(idx).GetBitmap();
				const int icon = bitmap.IsOk() ? imageList->Add(bitmap.GetBitmap(wxSize(icon_size, icon_size))) : -1;
				const wxTreeItemId item = tc->AppendItem(tc->GetRootItem(), choices.GetLabel(idx),
					icon, icon, new ibTreeItemPropertyData(id));
				const bool checked = md.ContainMetaType(id);
				tc->SetItemState(item, checked ? ibCheckTree::CHECKED : ibCheckTree::UNCHECKED);
				tc->Check(item, checked);
			}

			tc->ExpandAll(); int res = dlg->ShowModal();

			// 🛑 ONLY AN ACCEPTED DIALOG CHANGES THE VALUE. The copies this class replaced cleared the
			// description first and filled it only on OK — and then set it either way, so CANCEL erased the
			// binding the user had just declined to change.
			if (res == wxID_OK) {
				ibVariantDataOwner* clone = data->Clone();
				ibMetaDescription& metaDesc = clone->GetMetaDesc(); metaDesc.ClearMetaType();
				wxArrayTreeItemIds ids;
				tc->GetSelections(ids);   // the count is in ids itself
				// BY VALUE: wxArrayTreeItemIds stores void*, so a reference here binds to a per-iteration temporary.
				for (const wxTreeItemId selItem : ids) {
					if (!selItem.IsOk())
						continue;
					if (ibTreeItemPropertyData* item = dynamic_cast<ibTreeItemPropertyData*>(tc->GetItemData(selItem)))
						metaDesc.AppendMetaType(item->GetMetaID());
				}
				SetValue(clone);
			}

			dlg->Destroy();
			return res == wxID_OK;
		}
	};

	return new ibPGEditorChartBindingDialogAdapter();
}
