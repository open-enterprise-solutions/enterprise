#include "advpropRecord.h"

#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the registers' node
#include "core/description/typeDescription.h"                                         // ibMetaDescriptionMemory

#include "frmclient/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"
#include "frmclient/propertyManager/propertyEditor.h"
#include "frmclient/backend/compiler/value.h"   // ibValue::GetAvailableCtor — a kind, as the server registered its class

#include <map>

#define icon_size 16

// -----------------------------------------------------------------------
// wxPGRecordSelectorProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGRecordProperty, wxPGProperty, ComboBoxAndButton)

namespace {

// The registers a value names, as the server stores them…
ibMetaDescription ReadMeta(const wxVariant& value)
{
	ibMetaDescription metaDesc;
	ibMetaDescriptionMemory::ReadNode(ibVariantDataSerializable::GetNodeValue(value), metaDesc);
	return metaDesc;
}

// …and the value of others: the same property's, named by the cell (ValueToString).
wxVariant WriteMeta(const wxVariant& value, const ibMetaDescription& metaDesc)
{
	ibDataValue node;
	ibMetaDescriptionMemory::WriteNode(node, metaDesc);
	return wxVariant(new ibVariantDataSerializable(value.GetType(), node, wxEmptyString));
}

} // namespace

// register frontend property
class ibPropertyRecordLoader
{
public:
	ibPropertyRecordLoader()
	{
		// The registers as the server stores them (ibMetaDescriptionMemory: the metaIds it names), what may fill it the
		// row's choices.
		const ibPropertyRegistry::ibPropertyMaker maker = [](ibBackendProperty* property) -> wxPGProperty* {
			ibPropertyChoiceList choices;
			property->GetValueList(choices);
			return new ibPGRecordProperty(property->GetPropertyObject(), property->GetLabel(), property->GetName(),
				property->GetValue(), choices);
		};
		ibPropertyRegistry::Register(wxT("ibPropertyRecord"), maker);
		// …and its heir, the sequences a document is registered in: the desktop's registry matched it as an
		// ibPropertyRecord (a cast), the server names it by its own class.
		ibPropertyRegistry::Register(wxT("ibPropertySequenceRecord"), maker);
	}
}g_recordLoader;

ibPGRecordProperty::ibPGRecordProperty(const ibPropertyObject* property, const wxString& label, const wxString& strName, const wxVariant& value,
	const ibPropertyChoiceList& choices)
	: wxPGProperty(label, strName), m_ownerProperty(property)
{
	// The classes AND the has-a-recorder rule both live in the server's ibPropertyRecord::GetValueList, so the
	// list here is the same one every other caller sees.
	for (unsigned int idx = 0; idx < choices.GetCount(); idx++) {
		m_choices.Add(choices.GetLabel(idx), choices.GetBitmap(idx), choices.GetId(idx));
		m_choiceData.push_back(choices.GetData(idx));
	}

	m_flags |= wxPGPropertyFlags_ActiveButton; // Property button always enabled.

	SetValue(value);
}

wxString ibPGRecordProperty::ValueToString(wxVariant& value, wxPGPropValFormatFlags flags) const
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

bool ibPGRecordProperty::StringToValue(wxVariant& variant,
	const wxString& text,
	wxPGPropValFormatFlags flags) const
{
	return false;
}

bool ibPGRecordProperty::IntToValue(wxVariant& value, int number, wxPGPropValFormatFlags flags) const
{
	ibMetaDescription md = ReadMeta(value);
	md.SetDefaultMetaType(m_choices.GetValue(number));
	value = WriteMeta(value, md);
	return true;
}

#include "frmclient/win/ctrls/checktree.h"

wxPGEditorDialogAdapter* ibPGRecordProperty::GetEditorDialog() const
{
	class ibPGEditorRecordDialogAdapter : public wxPGEditorDialogAdapter {

		class ibTreeItemPropertyData : public wxTreeItemData {
			ibMetaID m_metaID;
		public:
			ibTreeItemPropertyData(ibMetaID id) : wxTreeItemData(), m_metaID(id) {}
			ibMetaID GetMetaID() const { return m_metaID; }
		};

		// ⭐⭐ WHAT IS OFFERED IS THE PROPERTY'S OWN ANSWER — the choices it was built with, one item each, grouped by kind
		// as the desktop's (FillByClsid). Each one's kind is its data — its class, as the server's property listed it —
		// named and drawn as the server registered it (the row's Classes).
		void FillFromChoices(const wxPGChoices& choices, const std::vector<std::uint64_t>& choiceData,
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

		virtual bool DoShowDialog(wxPropertyGrid* pg, wxPGProperty* prop) override
		{
			ibPGRecordProperty* dlgProp = wxDynamicCast(prop, ibPGRecordProperty);
			wxCHECK_MSG(dlgProp, false, "Function called for incompatible property");

			const ibMetaDescription held = ReadMeta(dlgProp->GetValue());

			// launch editor dialog
			wxDialog* dlg = new wxDialog(pg, wxID_ANY, _("Choice recorder"), wxDefaultPosition, wxDefaultSize,
				wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxCLIP_CHILDREN);

			dlg->SetFont(pg->GetFont()); // To allow entering chars of the same set as the propGrid

			// Multi-line text editor dialog.
			const int spacing = wxPropertyGrid::IsSmallScreen() ? 4 : 8;
			wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);
			wxBoxSizer* rowsizer = new wxBoxSizer(wxHORIZONTAL);

			ibCheckTree* tc = new ibCheckTree(dlg, wxID_ANY,
				wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxTR_HIDE_ROOT | wxCR_MULTIPLE_CHECK | wxCR_EMPTY_CHECK | wxSUNKEN_BORDER | wxTR_TWIST_BUTTONS);

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
			SetValue(WriteMeta(dlgProp->GetValue(), metaDesc));
			dlg->Destroy();
			return res == wxID_OK;
		}
	};
	return new ibPGEditorRecordDialogAdapter();
}
