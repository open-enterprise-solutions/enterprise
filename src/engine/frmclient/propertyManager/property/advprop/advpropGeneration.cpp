#include "advpropGeneration.h"

#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the bases' node
#include "core/description/typeDescription.h"                                         // ibMetaDescriptionMemory

#include "frmclient/propertyManager/property/private/prop.h"             // ibPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"
#include "frmclient/propertyManager/propertyEditor.h"
#include "frmclient/backend/compiler/value.h"   // ibValue::GetAvailableCtor — a kind, as the server registered its class

#include <map>
#include "deskgui/univ/dialog.h"

#define icon_size 16

// -----------------------------------------------------------------------
// ibPGGenerationProperty
// -----------------------------------------------------------------------

ibPG_IMPLEMENT_PROPERTY_CLASS(ibPGGenerationProperty, ibPGProperty, ComboBoxAndButton)

namespace {

// The bases a value names, as the server stores them…
ibMetaDescription ReadMeta(const ibVariant& value)
{
	ibMetaDescription metaDesc;
	ibMetaDescriptionMemory::ReadNode(ibVariantSerializable::GetNodeValue(value), metaDesc);
	return metaDesc;
}

// …and the value of others: the same property's, named by the cell (ValueToString).
ibVariant WriteMeta(const ibMetaDescription& metaDesc)
{
	ibDataValue node;
	ibMetaDescriptionMemory::WriteNode(node, metaDesc);
	return ibVariant(new ibVariantSerializable(node, wxEmptyString));
}

} // namespace

// register frontend property
class ibPropertyGenerationLoader
{
public:
	ibPropertyGenerationLoader()
	{
		// ibPropertyGeneration — the bases as the server stores them (ibMetaDescriptionMemory: the metaIds it names),
		// what may fill it the row's choices.
		ibPropertyRegistry::Register(wxT("ibPropertyGeneration"), [](ibBackendProperty* property) -> ibPGProperty* {
			ibPropertyChoiceList choices;
			property->GetValueList(choices);
			return new ibPGGenerationProperty(property->GetPropertyObject(), property->GetLabel(), property->GetName(),
				property->GetValue(), choices);
		});
	}
}g_generationLoader;

ibPGGenerationProperty::ibPGGenerationProperty(const ibPropertyObject* property, const wxString& label, const wxString& strName, const ibVariant& value,
	const ibPropertyChoiceList& choices)
	: ibPGProperty(label, strName), m_ownerProperty(property)
{
	for (unsigned int idx = 0; idx < choices.GetCount(); idx++) {
		m_choices.Add(choices.GetLabel(idx), choices.GetBitmap(idx), choices.GetId(idx));
		m_choiceData.push_back(choices.GetData(idx));
	}

	m_flags |= ibPGPropertyFlags_ActiveButton; // Property button always enabled.

	SetValue(value);
}

wxString ibPGGenerationProperty::ValueToString(ibVariant& value, ibPGPropValFormatFlags flags) const
{
	// ⚠ NAMED BY THE CHOICES — the client holds no configuration to name an id by (ibPGChartBindingProperty).
	wxString strDescr;
	const ibMetaDescription md = ReadMeta(value);
	for (unsigned int idx = 0; idx < md.GetTypeCount(); idx++) {
		const int index = m_choices.Index(static_cast<int>(md.GetByIdx(idx)));
		if (index == wxNOT_FOUND)
			continue;
		strDescr = strDescr.IsEmpty() ? m_choices.GetLabel(index) : strDescr + wxT(", ") + m_choices.GetLabel(index);
	}
	return strDescr;
}

bool ibPGGenerationProperty::StringToValue(ibVariant& variant,
	const wxString& text,
	ibPGPropValFormatFlags flags) const
{
	return false;
}

bool ibPGGenerationProperty::IntToValue(ibVariant& value, int number, ibPGPropValFormatFlags flags) const
{
	ibMetaDescription md = ReadMeta(value);
	md.SetDefaultMetaType(m_choices.GetValue(number));
	value = WriteMeta(md);
	return true;
}

#include "frmclient/win/ctrls/checktree.h"

ibPGEditorDialogAdapter* ibPGGenerationProperty::GetEditorDialog() const
{
	class ibPGGenerationEventAdapter : public ibPGEditorDialogAdapter {
		class ibTreeItemPropertyData : public wxTreeItemData {
			ibMetaID m_metaID;
		public:
			ibTreeItemPropertyData(ibMetaID id) : wxTreeItemData(), m_metaID(id) {}
			ibMetaID GetMetaID() const { return m_metaID; }
		};

		// ⭐ WHAT THE PROPERTY OFFERS, grouped by kind — the choices it was built with (ibPropertyGeneration::GetValueList),
		// not a list of kinds of this dialog's own. Each one's kind is its data — its class, as the server's property
		// listed it — named and drawn as the server registered it (the row's Classes).
		void FillFromChoices(const ibPGChoices& choices, const std::vector<std::uint64_t>& choiceData,
			ibCheckTree* tc, const ibMetaDescription& held) {

			wxImageList* imageList = tc->GetImageList();
			wxASSERT(imageList);
			std::map<ibClassID, wxTreeItemId> groups;   // one per kind, opened where the kind first appears
			for (unsigned int idx = 0; idx < choices.GetCount(); idx++) {
				const ibMetaID id = choices.GetValue(idx);

				const ibClassID clsid = choiceData[idx];
				auto group = groups.find(clsid);
				if (group == groups.end()) {
					const ibCtorAbstractType* so = ibValue::GetAvailableCtor(clsid);
					const int groupIcon = so != nullptr ? imageList->Add(so->GetClassIcon()) : -1;
					group = groups.emplace(clsid, tc->AppendItem(tc->GetRootItem(),
						so != nullptr ? so->GetClassName() : ibString(), groupIcon, groupIcon)).first;
				}

				const wxBitmapBundle& bitmap = choices.Item(idx).GetBitmap();
				const int icon = bitmap.IsOk() ? imageList->Add(bitmap.GetBitmap(wxSize(icon_size, icon_size))) : -1;
				wxTreeItemId newItem = tc->AppendItem(group->second, choices.GetLabel(idx),
					icon, icon, new ibTreeItemPropertyData(id));

				const bool checked = held.ContainMetaType(id);
				tc->SetItemState(newItem, checked ? ibCheckTree::CHECKED : ibCheckTree::UNCHECKED);
				tc->Check(newItem, checked);
			}
		}

	public:

		virtual bool DoShowDialog(ibPropertyGrid* pg, ibPGProperty* prop) override
		{
			ibPGGenerationProperty* dlgProp = wxDynamicCast(prop, ibPGGenerationProperty);
			wxCHECK_MSG(dlgProp, false, "Function called for incompatible property");

			const ibMetaDescription held = ReadMeta(dlgProp->GetValue());

			// launch editor dialog
			ibDialog* dlg = new ibDialog(pg, wxID_ANY, _("Choice generation"), wxDefaultPosition, wxDefaultSize,
				wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxCLIP_CHILDREN);

			dlg->SetFont(pg->GetFont()); // To allow entering chars of the same set as the propGrid

			// Multi-line text editor dialog.
			const int spacing = ibPropertyGrid::IsSmallScreen() ? 4 : 8;
			wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);
			wxBoxSizer* rowsizer = new wxBoxSizer(wxHORIZONTAL);

			ibCheckTree* tc = new ibCheckTree(dlg, wxID_ANY,
				wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxTR_HIDE_ROOT | wxCR_MULTIPLE_CHECK | wxCR_EMPTY_CHECK | wxSUNKEN_BORDER | wxTR_TWIST_BUTTONS);

			tc->AddRoot(wxEmptyString);   // the root is hidden (wxTR_HIDE_ROOT); only its existence matters

			rowsizer->Add(tc, wxSizerFlags(1).Expand().Border(wxALL, spacing));
			topsizer->Add(rowsizer, wxSizerFlags(1).Expand());

			tc->SetDoubleBuffered(true);
			tc->Enable(!dlgProp->HasFlag(ibPGFlags::ReadOnly));

			ibStdDialogButtonSizer* buttonSizer = dlg->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
			topsizer->Add(buttonSizer, wxSizerFlags(0).Right().Border(wxBOTTOM | wxRIGHT, spacing));

			dlg->SetSizer(topsizer);
			topsizer->SetSizeHints(dlg);

			if (!ibPropertyGrid::IsSmallScreen()) {
				dlg->SetSize(dlg->FromDIP(wxSize(400, 300)));
				dlg->Move(pg->GetGoodEditorDialogPosition(prop, dlg->GetSize()));
			}

			tc->SetFocus();

			// Make an state image list containing small icons
			tc->AssignImageList(
				new wxImageList(icon_size, icon_size)
			);

			FillFromChoices(dlgProp->GetChoices(), dlgProp->m_choiceData, tc, held);

			tc->ExpandAll(); int res = dlg->ShowModal();

			ibMetaDescription metaDesc = held;
			{
				metaDesc.ClearMetaType();
				wxArrayTreeItemIds ids;
				tc->GetSelections(ids);   // the count is in ids itself
				// BY VALUE: wxArrayTreeItemIds stores void*, so a reference binds to a per-iteration temporary.
				for (const wxTreeItemId selItem : ids) {
					if (selItem.IsOk()) {
						wxTreeItemData* dataItem = tc->GetItemData(selItem);
						if (dataItem && res == wxID_OK) {
							ibTreeItemPropertyData* item = dynamic_cast<ibTreeItemPropertyData*>(dataItem);
							wxASSERT(item);
							metaDesc.AppendMetaType(item->GetMetaID());
						}
					}
				}
			}
			SetValue(WriteMeta(metaDesc));

			dlg->Destroy();
			return res == wxID_OK;
		}
	};

	return new ibPGGenerationEventAdapter();
}
