#include "advpropRecord.h"

#include "frmclient/backend/propertyManager/property/variant/variantRecord.h"

#include "frmclient/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"
#include "frmclient/propertyManager/propertyEditor.h"

#define icon_size 16

// -----------------------------------------------------------------------
// wxPGRecordSelectorProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGRecordProperty, wxPGProperty, ComboBoxAndButton)

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
			ibMetaDescription metaDesc;
			ibMetaDescriptionMemory::ReadNode(property->GetNodeValue(), metaDesc);
			return new ibPGRecordProperty(property->GetPropertyObject(), property->GetLabel(), property->GetName(),
				wxVariant(new ibVariantDataRecord(property->GetPropertyObject(), metaDesc)), choices);
		};
		const ibPropertyRegistry::ibPropertyWriter writer = [](const wxVariant& value) {
			ibDataValue node;
			if (const ibVariantDataRecord* const data = property_cast(value, ibVariantDataRecord))
				ibMetaDescriptionMemory::WriteNode(node, data->GetMetaDesc());
			return node;
		};
		ibPropertyRegistry::Register(wxT("ibPropertyRecord"), maker, writer);
		// …and its heir, the sequences a document is registered in: the desktop's registry matched it as an
		// ibPropertyRecord (a cast), the server names it by its own class.
		ibPropertyRegistry::Register(wxT("ibPropertySequenceRecord"), maker, writer);
	}
}g_recordLoader;

ibPGRecordProperty::ibPGRecordProperty(const ibPropertyObject* property, const wxString& label, const wxString& strName, const wxVariant& value,
	const ibPropertyChoiceList& choices)
	: wxPGProperty(label, strName), m_ownerProperty(property)
{
	// The classes AND the has-a-recorder rule both live in the server's ibPropertyRecord::GetValueList, so the
	// list here is the same one every other caller sees.
	for (unsigned int idx = 0; idx < choices.GetCount(); idx++)
		m_choices.Add(choices.GetLabel(idx), choices.GetBitmap(idx), choices.GetId(idx));

	m_flags |= wxPGPropertyFlags_ActiveButton; // Property button always enabled.

	SetValue(value);
}

wxString ibPGRecordProperty::ValueToString(wxVariant& value, wxPGPropValFormatFlags flags) const
{
	// ⚠ NAMED BY THE CHOICES — the client holds no configuration to name an id by (ibPGChartBindingProperty).
	const ibVariantDataRecord* const data = property_cast(value, ibVariantDataRecord);
	if (data == nullptr)
		return wxEmptyString;
	wxString strDescr;
	const ibMetaDescription& md = data->GetMetaDesc();
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
	ibVariantDataRecord* dataRecord = property_cast(value, ibVariantDataRecord);
	if (dataRecord != nullptr) {
		ibVariantDataRecord* newDataRecord = dataRecord->Clone();
		wxASSERT(newDataRecord);
		ibMetaDescription& md = newDataRecord->GetMetaDesc();
		md.SetDefaultMetaType(m_choices.GetValue(number));
		value = newDataRecord;
		return true;
	}
	return false;
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

		// ⭐⭐ WHAT IS OFFERED IS THE PROPERTY'S OWN ANSWER — the choices it was built with, one item each. The
		// desktop's grouped them by kind, asking the configuration each one's class (FillByClsid); the client holds
		// no configuration, and the row's choices are listed as the server's property listed them.
		void FillFromChoices(const wxPGChoices& choices, ibCheckTree* tc, ibVariantDataRecord* data) {

			wxImageList* imageList = tc->GetImageList();
			wxASSERT(imageList);
			for (unsigned int idx = 0; idx < choices.GetCount(); idx++) {
				const ibMetaID id = choices.GetValue(idx);
				const wxBitmapBundle& bitmap = choices.Item(idx).GetBitmap();
				const int icon = bitmap.IsOk() ? imageList->Add(bitmap.GetBitmap(wxSize(icon_size, icon_size))) : -1;
				wxTreeItemId newItem = tc->AppendItem(tc->GetRootItem(), choices.GetLabel(idx),
					icon, icon, new ibTreeItemPropertyData(id));

				const bool checked = data != nullptr && data->GetMetaDesc().ContainMetaType(id);
				tc->SetItemState(newItem, checked ? ibCheckTree::CHECKED : ibCheckTree::UNCHECKED);
				tc->Check(newItem, checked);
			}
		}

	public:

		virtual bool DoShowDialog(wxPropertyGrid* pg, wxPGProperty* prop) override
		{
			ibPGRecordProperty* dlgProp = wxDynamicCast(prop, ibPGRecordProperty);
			wxCHECK_MSG(dlgProp, false, "Function called for incompatible property");

			ibVariantDataRecord* data = property_cast(dlgProp->GetValue(), ibVariantDataRecord);
			if (data == nullptr) return false;

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

			FillFromChoices(dlgProp->GetChoices(), tc, data);

			tc->ExpandAll(); int res = dlg->ShowModal();
			ibVariantDataRecord* clone = data->Clone();
			{
				ibMetaDescription& metaDesc = clone->GetMetaDesc(); metaDesc.ClearMetaType();
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
			SetValue(clone);
			dlg->Destroy();
			return res == wxID_OK;
		}
	};
	return new ibPGEditorRecordDialogAdapter();
}
