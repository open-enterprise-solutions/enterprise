#include "advpropGeneration.h"

#include "backend/propertyManager/property/variant/variantGen.h"

#include "frontend/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frontend/propertyManager/property/private/propertyRegistry.h"
#include "frontend/propertyManager/propertyEditor.h"

#include <map>

#define icon_size 16

// -----------------------------------------------------------------------
// ibPGGenerationProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGGenerationProperty, wxPGProperty, ComboBoxAndButton)

// register frontend property 
class ibPropertyGenerationLoader
{
public:
    ibPropertyGenerationLoader()
    {
		ibPropertyRegistry::Register([](ibPropertyGeneration* prop) -> wxPGProperty* {
			ibPropertyChoiceList choices;
			prop->GetValueList(choices);
			return new ibPGGenerationProperty(prop->GetPropertyObject(), prop->GetLabel(), prop->GetName(), prop->GetValue(), choices);
		});
    }
}g_generationLoader;

ibPGGenerationProperty::ibPGGenerationProperty(const ibPropertyObject* property, const wxString& label, const wxString& strName, const wxVariant& value,
	const ibPropertyChoiceList& choices)
    : wxPGProperty(label, strName), m_ownerProperty(property)
{
    for (unsigned int idx = 0; idx < choices.GetCount(); idx++)
        m_choices.Add(choices.GetLabel(idx), choices.GetBitmap(idx), choices.GetId(idx));

    //m_flags |= wxPGFlags::ReadOnly;
    m_flags |= wxPGPropertyFlags_ActiveButton; // Property button always enabled.

    SetValue(value);
}

wxString ibPGGenerationProperty::ValueToString( wxVariant& value, wxPGPropValFormatFlags flags ) const
{
    return value.GetString();
}

bool ibPGGenerationProperty::StringToValue(wxVariant& variant,
    const wxString& text,
    wxPGPropValFormatFlags flags) const
{
    return false;
}

bool ibPGGenerationProperty::IntToValue(wxVariant& value, int number, wxPGPropValFormatFlags flags) const
{
    ibVariantDataGeneration* dataGen = property_cast(value, ibVariantDataGeneration);
    if (dataGen != nullptr) {
        ibVariantDataGeneration* newDataGen = dataGen->Clone();
        wxASSERT(newDataGen);
        ibMetaDescription& md = newDataGen->GetMetaDesc();
        md.SetDefaultMetaType(m_choices.GetValue(number));
        value = newDataGen;
        return true;
    }
    return false;
}

#include "frontend/win/ctrls/checktree.h"

wxPGEditorDialogAdapter* ibPGGenerationProperty::GetEditorDialog() const
{
    class wxPGGenerationEventAdapter : public wxPGEditorDialogAdapter {
        class ibTreeItemPropertyData : public wxTreeItemData {
            ibMetaID m_metaID;
        public:
            ibTreeItemPropertyData(ibMetaID id) : wxTreeItemData(), m_metaID(id) {}
            ibMetaID GetMetaID() const { return m_metaID; }
        };

        // ⭐ WHAT THE PROPERTY OFFERS, grouped by kind — the choices it was built with
        // (ibPropertyGeneration::GetValueList), not a list of kinds of this dialog's own. It kept one, a
        // copy of the property's that had fallen behind: a chart of calculation types was offered nowhere.
        void FillFromChoices(const ibMetaData* metaData, const wxPGChoices& choices,
            ibCheckTree* tc, ibVariantDataGeneration* data) {

            wxImageList* imageList = tc->GetImageList();
            wxASSERT(imageList);
            std::map<ibClassID, wxTreeItemId> groups;   // one per kind, opened where the kind first appears
            for (unsigned int idx = 0; idx < choices.GetCount(); idx++) {
                const ibMetaID id = choices.GetValue(idx);
                const ibValueMetaObject* metaObject = metaData->FindAnyObjectByFilter(id);
                if (metaObject == nullptr)
                    continue;

                const ibClassID clsid = metaObject->GetClassType();
                auto group = groups.find(clsid);
                if (group == groups.end()) {
                    const ibCtorAbstractType* so = ibValue::GetAvailableCtor(clsid);
                    const int groupIcon = so != nullptr ? imageList->Add(so->GetClassIcon()) : -1;
                    group = groups.emplace(clsid, tc->AppendItem(tc->GetRootItem(),
                        so != nullptr ? so->GetClassName() : wxString(), groupIcon, groupIcon)).first;
                }

                const int icon = imageList->Add(metaObject->GetIcon());
                wxTreeItemId newItem = tc->AppendItem(group->second, metaObject->GetName(),
                    icon, icon, new ibTreeItemPropertyData(id));

                const bool checked = data != nullptr && data->GetMetaDesc().ContainMetaType(id);
                tc->SetItemState(newItem, checked ? ibCheckTree::CHECKED : ibCheckTree::UNCHECKED);
                tc->Check(newItem, checked);
            }
        }

    public:

        virtual bool DoShowDialog(wxPropertyGrid* pg, wxPGProperty* prop) wxOVERRIDE
        {
            ibPGGenerationProperty* dlgProp = wxDynamicCast(prop, ibPGGenerationProperty);
            wxCHECK_MSG(dlgProp, false, "Function called for incompatible property");

            ibVariantDataGeneration* data = property_cast(dlgProp->GetValue(), ibVariantDataGeneration);
            if (data == nullptr) return false;
            // No cast: the dialog needs the owner only to reach its metadata, which a (const) property
            // object answers itself.
            const ibMetaData* metaData = dlgProp->GetPropertyObject() != nullptr ? dlgProp->GetPropertyObject()->GetMetaData() : nullptr;
            if (metaData == nullptr) return false;

            // launch editor dialog
            wxDialog* dlg = new wxDialog(pg, wxID_ANY, _("Choice generation"), wxDefaultPosition, wxDefaultSize,
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

            FillFromChoices(metaData, dlgProp->GetChoices(), tc, data);

            tc->ExpandAll(); int res = dlg->ShowModal();

            ibVariantDataGeneration* clone = data->Clone();
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

    return new wxPGGenerationEventAdapter();
}