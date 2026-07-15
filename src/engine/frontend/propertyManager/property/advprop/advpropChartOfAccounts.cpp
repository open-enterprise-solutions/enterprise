#include "advpropChartOfAccounts.h"

#include "backend/propertyManager/property/variant/variantOwner.h"
#include "backend/propertyManager/property/propertyChartOfAccounts.h"

#include "frontend/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frontend/propertyManager/property/private/propertyRegistry.h"
#include "frontend/propertyManager/propertyEditor.h"

#define icon_size 16

// -----------------------------------------------------------------------
// ibPGChartOfAccountsProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGChartOfAccountsProperty, wxPGProperty, ComboBoxAndButton)

// register frontend property
class ibPropertyChartOfAccountsLoader
{
public:
	ibPropertyChartOfAccountsLoader()
	{
		ibPropertyRegistry::Register([](ibPropertyChartOfAccounts* prop) -> wxPGProperty* {
			return new ibPGChartOfAccountsProperty(prop->GetPropertyObject(), prop->GetLabel(), prop->GetName(), prop->GetValue());
		});
	}
}g_chartOfAccountsLoader;

#include "backend/metadata.h"

void ibPGChartOfAccountsProperty::FillByClsid(const ibClassID& clsid)
{
	const ibValueMetaObjectGenericData* metaGenericData = dynamic_cast<const ibValueMetaObjectGenericData*>(m_ownerProperty);
	if (metaGenericData != nullptr) {
		const ibMetaData* metaData = metaGenericData->GetMetaData();
		wxASSERT(metaData);
		for (auto metaOwner : metaData->GetAnyArrayObject(clsid)) {
			m_choices.Add(metaOwner->GetName(), metaOwner->GetIcon(), metaOwner->GetMetaID());
		}
	}
}

ibPGChartOfAccountsProperty::ibPGChartOfAccountsProperty(const ibPropertyObject* property, const wxString& label, const wxString& strName, const wxVariant& value)
	: wxPGProperty(label, strName), m_ownerProperty(property)
{
	FillByClsid(g_metaChartOfAccountsCLSID);

	//m_flags |= wxPGFlags::ReadOnly;
	m_flags |= wxPGPropertyFlags_ActiveButton; // Property button always enabled.

	SetValue(value);
}

wxString ibPGChartOfAccountsProperty::ValueToString( wxVariant& value, wxPGPropValFormatFlags flags ) const
{
	return value.GetString();
}

bool ibPGChartOfAccountsProperty::StringToValue(wxVariant& variant,
	const wxString& text,
	wxPGPropValFormatFlags flags) const
{
	return false;
}

bool ibPGChartOfAccountsProperty::IntToValue(wxVariant& value, int number, wxPGPropValFormatFlags flags) const
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

wxPGEditorDialogAdapter* ibPGChartOfAccountsProperty::GetEditorDialog() const
{
	class ibPGEditorChartOfAccountsDialogAdapter : public wxPGEditorDialogAdapter {

		class ibTreeItemPropertyData : public wxTreeItemData {
			ibValueMetaObject* m_metaObject;
		public:
			ibTreeItemPropertyData(ibValueMetaObject* opt) : wxTreeItemData(), m_metaObject(opt) {}
			ibMetaID GetMetaID() const { return m_metaObject->GetMetaID(); }
		};

		void FillByClsid(const ibMetaData* metaData, const ibClassID& clsid,
			ibCheckTree* tc, ibVariantDataOwner* data) {

			wxImageList* imageList = tc->GetImageList();
			wxASSERT(imageList);
			const ibCtorAbstractType* so = ibValue::GetAvailableCtor(clsid);
			int groupIcon = imageList->Add(so->GetClassIcon());
			const wxTreeItemId& parentID = tc->AppendItem(tc->GetRootItem(), so->GetClassName(),
				groupIcon, groupIcon);
			for (auto metaObject : metaData->GetAnyArrayObject(clsid)) {
				ibValueMetaObjectRecordDataRef* registerData = dynamic_cast<ibValueMetaObjectRecordDataRef*>(metaObject);
				if (registerData != nullptr) {
					{
						int icon = imageList->Add(registerData->GetIcon());
						ibTreeItemPropertyData* itemData = new ibTreeItemPropertyData(metaObject);
						wxTreeItemId newItem = tc->AppendItem(parentID, registerData->GetName(),
							icon, icon,
							itemData);

						if (data != nullptr) {
							const ibMetaDescription& md = data->GetMetaDesc();
							tc->SetItemState(newItem, md.ContainMetaType(registerData->GetMetaID()) ? ibCheckTree::CHECKED : ibCheckTree::UNCHECKED);
							tc->Check(newItem, md.ContainMetaType(registerData->GetMetaID()));
						}
						else {
							tc->SetItemState(newItem, ibCheckTree::UNCHECKED);
							tc->Check(newItem, false);
						}
					}
				}
			}
		}

	public:

		virtual bool DoShowDialog(wxPropertyGrid* pg, wxPGProperty* prop) wxOVERRIDE
		{
			ibPGChartOfAccountsProperty* dlgProp = wxDynamicCast(prop, ibPGChartOfAccountsProperty);
			wxCHECK_MSG(dlgProp, false, "Function called for incompatible property");

			ibVariantDataOwner* data = property_cast(dlgProp->GetValue(), ibVariantDataOwner);
			if (data == nullptr) return false;
			const ibValueMetaObjectGenericData* metaGenericData = dynamic_cast<const ibValueMetaObjectGenericData*>(dlgProp->GetPropertyObject());
			if (metaGenericData == nullptr) return false;

			// launch editor dialog
			wxDialog* dlg = new wxDialog(pg, wxID_ANY, _("Choice chart of accounts"), wxDefaultPosition, wxDefaultSize,
				wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxCLIP_CHILDREN);

			dlg->SetFont(pg->GetFont()); // To allow entering chars of the same set as the propGrid

			// Multi-line text editor dialog.
			const int spacing = wxPropertyGrid::IsSmallScreen() ? 4 : 8;
			wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);
			wxBoxSizer* rowsizer = new wxBoxSizer(wxHORIZONTAL);

			ibCheckTree* tc = new ibCheckTree(dlg, wxID_ANY,
				wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxTR_HIDE_ROOT | wxCR_MULTIPLE_CHECK | wxCR_EMPTY_CHECK | wxSUNKEN_BORDER | wxTR_TWIST_BUTTONS);

			wxTreeItemId rootItem = tc->AddRoot(wxEmptyString);

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

			const ibMetaData* metaData = metaGenericData->GetMetaData();
			wxASSERT(metaData);
			if (metaData != nullptr) {
				FillByClsid(metaData, g_metaChartOfAccountsCLSID, tc, data);
			}

			tc->ExpandAll(); int res = dlg->ShowModal();

			ibVariantDataOwner* clone = data->Clone();
			{
				ibMetaDescription& metaDesc = clone->GetMetaDesc(); metaDesc.ClearMetaType();
				wxArrayTreeItemIds ids;
				unsigned int selCount = tc->GetSelections(ids);
				for (const wxTreeItemId& selItem : ids) {
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

	return new ibPGEditorChartOfAccountsDialogAdapter();
}