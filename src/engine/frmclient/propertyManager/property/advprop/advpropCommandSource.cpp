#include "advpropCommandSource.h"

#include "frmclient/backend/propertyManager/property/variant/variantCommandSource.h"    // ibVariantDataCommandSource (the cell value)

#include "frmclient/propertyManager/property/private/prop.h"                    // wxPGPropertyFlags_*, property_cast
#include "frmclient/propertyManager/property/private/propertyRegistry.h"        // ibPropertyRegistry::Register
#include "frmclient/propertyManager/propertyEditor.h"
#include "frmclient/win/picture.h"                                             // ibProtocolPicture — a command's picture

#include "protocol/protocol.h"   // ibProtocol::ibSchema::ibProperties — the tree's keys

#include <wx/treectrl.h>
#include <functional>
#include <wx/imaglist.h>
#include <wx/artprov.h>

namespace {

// A row of the tree that is a command — it has its path; a section's, a sub-group's has none.
bool IsCommandRow(const ibDataNode& row)
{
	return row.FindField(ibProtocol::ibSchema::ibProperties::CommandPath) != nullptr;
}

// The command a row names, as the form editor's tree reads it back (ibFormDesignView::ReadPath).
ibCommandDescription ReadCommand(const ibDataNode& row)
{
	ibCommandDescription desc;
	const ibDataValue* const hops = row.FindField(ibProtocol::ibSchema::ibProperties::CommandPath);
	if (hops == nullptr || hops->Kind() != ibDataKind::Array)
		return desc;
	for (const ibDataValue& hop : hops->AsArray()) {
		if (hop.Kind() == ibDataKind::Child && hop.AsChild() != nullptr)
			desc.AppendCommand(static_cast<ibMetaID>(hop.AsChild()->GetValue<s32>(ibProtocol::ibSchema::ibProperties::Id)));
	}
	const s32 kind = row.GetValue<s32>(ibProtocol::ibSchema::ibProperties::Kind);
	desc.SetCommandType(kind != 0 ? static_cast<ibInterfaceCommandType>(kind) : ibInterfaceCommandType_Default);
	return desc;
}

// One tree item that is a command: what it binds, and the words the cell shows until the server's come back.
class ibTreeItemCommand : public wxTreeItemData {
public:
	ibTreeItemCommand(const ibCommandDescription& desc, const wxString& label) : m_desc(desc), m_label(label) {}
	ibCommandDescription m_desc;
	wxString m_label;
};

} // namespace

// -----------------------------------------------------------------------
// ibPGCommandSourceProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGCommandSourceProperty, wxPGProperty, TextCtrlAndButton)

// register the frontend editor for the server's command-source property — the binding as it stores it
// (ibCommandDescriptionMemory), read as it wrote it (the row's Presentation).
class ibPropertyCommandSourceLoader
{
public:
	ibPropertyCommandSourceLoader()
	{
		ibPropertyRegistry::Register(wxT("ibPropertyCommandSource"), [](ibBackendProperty* property) -> wxPGProperty* {
			ibCommandDescription commandDesc;
			ibCommandDescriptionMemory::ReadNode(property->GetNodeValue(), commandDesc);
			ibDataNode row;
			const ibDataNode* const tree = property->GetProtocolValue(row) ? row.FindChild(ibProtocol::ibSchema::ibProperties::CommandTree) : nullptr;
			return new ibPGCommandSourceProperty(property->GetPropertyObject(), property->GetLabel(), property->GetName(),
				wxVariant(new ibVariantDataCommandSource(commandDesc, property->GetValue().GetString())), tree);
		},
		[](const wxVariant& value) {
			ibDataValue node;
			if (const ibVariantDataCommandSource* const data = property_cast(value, ibVariantDataCommandSource))
				ibCommandDescriptionMemory::WriteNode(node, data->GetCommandDesc());
			return node;
		});
	}
} g_commandSourceLoader;

ibPGCommandSourceProperty::ibPGCommandSourceProperty(const ibPropertyObject* property, const wxString& label,
	const wxString& name, const wxVariant& value, const ibDataNode* tree) : wxPGProperty(label, name), m_propertyObject(property)
{
	if (tree != nullptr) {
		m_tree = *tree;
		m_hasTree = true;
	}
	m_flags |= wxPGPropertyFlags_ActiveButton;   // the "…" button is always live
	SetValue(value);
}

wxString ibPGCommandSourceProperty::ValueToString(wxVariant& variant, wxPGPropValFormatFlags WXUNUSED(flags)) const
{
	// Just the variant's own string — the binding's name as the server wrote it.
	ibVariantDataCommandSource* data = property_cast(variant, ibVariantDataCommandSource);
	wxString s;
	if (data != nullptr)
		data->Write(s);
	return s;
}

void ibPGCommandSourceProperty::OnSetValue()
{
	m_valueBitmapBundle = wxBitmapBundle();   // no bound command -> no image (cell shows text only)
	wxVariant v = GetValue();                 // keep the variant alive so the cast target outlives the read
	ibVariantDataCommandSource* data = property_cast(v, ibVariantDataCommandSource);
	if (data == nullptr || !data->GetCommandDesc().IsOk() || !m_hasTree)
		return;

	// The bound command's OWN picture, so the cell shows WHICH command is pulled. The desktop resolves it through the
	// owner's command door over the form's commands; here they are the server's, and the row's command tree already
	// names each command's picture — the row of the bound command wears it. Left empty when the command has no picture
	// or was deleted.
	const ibCommandDescription& desc = data->GetCommandDesc();
	wxString pictureId;
	std::function<bool(const ibDataNode&)> find = [&](const ibDataNode& node) {
		for (const ibDataNode& row : node.Children()) {
			if (!IsCommandRow(row)) {
				if (find(row))
					return true;
				continue;
			}
			if (ReadCommand(row) == desc) {
				pictureId = row.GetValue<wxString>(ibProtocol::ibSchema::ibProperties::Picture);
				return true;
			}
		}
		return false;
	};
	if (!find(m_tree) || pictureId.IsEmpty())
		return;
	const wxBitmap icon = ibProtocolPicture(pictureId);
	if (icon.IsOk())
		m_valueBitmapBundle = icon;
}

bool ibPGCommandSourceProperty::StringToValue(wxVariant& WXUNUSED(variant), const wxString& WXUNUSED(text),
	wxPGPropValFormatFlags WXUNUSED(flags)) const
{
	return false;   // read-only cell — the value is set only through the dialog
}

wxPGEditorDialogAdapter* ibPGCommandSourceProperty::GetEditorDialog() const
{
	class ibPGEditorCommandSourceDialogAdapter : public wxPGEditorDialogAdapter {
	public:
		virtual bool DoShowDialog(wxPropertyGrid* pg, wxPGProperty* prop) override
		{
			ibPGCommandSourceProperty* dlgProp = wxDynamicCast(prop, ibPGCommandSourceProperty);
			wxCHECK_MSG(dlgProp, false, "Function called for incompatible property");

			const ibDataNode* const tree = dlgProp->GetCommandTree();
			if (tree == nullptr) {
				// An older server's row: the command sources are gathered over its form, and it did not send them.
				wxMessageBox(_("Choosing a command is not available in the thin designer yet."),
					_("Choice command"), wxOK | wxICON_INFORMATION, pg);
				return false;
			}

			// The desktop's window (frontend advpropCommandSource.cpp), over the row's tree: the three sections, a
			// source's sub-groups, the commands with their own pictures.
			wxDialog* dlg = new wxDialog(pg, wxID_ANY, _("Choice command"), wxDefaultPosition, wxDefaultSize,
				wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxCLIP_CHILDREN);
			dlg->SetFont(pg->GetFont());
			const int spacing = wxPropertyGrid::IsSmallScreen() ? 4 : 8;

			wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);
			wxTreeCtrl* tc = new wxTreeCtrl(dlg, wxID_ANY, wxDefaultPosition, wxDefaultSize,
				wxTR_HIDE_ROOT | wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxSUNKEN_BORDER | wxTR_TWIST_BUTTONS);
			tc->SetDoubleBuffered(true);
			topsizer->Add(tc, wxSizerFlags(1).Expand().Border(wxALL, spacing));

			wxImageList* images = new wxImageList(16, 16);
			images->Add(wxArtProvider::GetBitmap(wxART_FOLDER, wxART_OTHER, wxSize(16, 16)));   // 0 — a group, a command without a picture
			tc->AssignImageList(images);

			wxVariant curVal = dlgProp->GetValue();
			ibCommandDescription current;
			if (ibVariantDataCommandSource* data = property_cast(curVal, ibVariantDataCommandSource))
				current = data->GetCommandDesc();

			wxTreeItemId selNode;
			std::function<void(const wxTreeItemId&, const ibDataNode&)> fill =
				[&](const wxTreeItemId& parent, const ibDataNode& node) {
				for (const ibDataNode& row : node.Children()) {
					const wxString name = row.GetValue<wxString>(ibProtocol::ibSchema::ibProperties::Name);
					if (!IsCommandRow(row)) {
						const wxTreeItemId group = tc->AppendItem(parent, name, 0, 0);
						fill(group, row);
						continue;
					}
					int icon = 0;
					const wxBitmap picture = ibProtocolPicture(row.GetValue<wxString>(ibProtocol::ibSchema::ibProperties::Picture));
					if (picture.IsOk())
						icon = images->Add(picture.ConvertToImage().Rescale(16, 16));
					const ibCommandDescription desc = ReadCommand(row);
					const wxTreeItemId leaf = tc->AppendItem(parent, name, icon, icon, new ibTreeItemCommand(desc, name));
					if (current.IsOk() && current == desc)
						selNode = leaf;   // re-opened on the command bound now
				}
			};
			const wxTreeItemId root = tc->AddRoot(wxEmptyString);
			fill(root, *tree);
			tc->ExpandAll();
			if (selNode.IsOk()) {
				tc->SelectItem(selNode);
				tc->EnsureVisible(selNode);
			}

			wxStdDialogButtonSizer* buttonSizer = dlg->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
			topsizer->Add(buttonSizer, wxSizerFlags(0).Right().Border(wxBOTTOM | wxRIGHT, spacing));
			dlg->SetSizer(topsizer);
			topsizer->SetSizeHints(dlg);
			if (!wxPropertyGrid::IsSmallScreen()) {
				dlg->SetSize(dlg->FromDIP(wxSize(360, 320)));
				dlg->Move(pg->GetGoodEditorDialogPosition(dlgProp, dlg->GetSize()));
			}
			tc->SetFocus();

			const int res = dlg->ShowModal();
			const wxTreeItemId sel = tc->GetSelection();
			bool applied = false;
			if (res == wxID_OK && sel.IsOk()) {
				// A group has no command — only a command commits; the same one again is no edit.
				if (const ibTreeItemCommand* const d = dynamic_cast<const ibTreeItemCommand*>(tc->GetItemData(sel))) {
					if (d->m_desc.IsOk() && !(current.IsOk() && current == d->m_desc)) {
						SetValue(new ibVariantDataCommandSource(d->m_desc, d->m_label));
						applied = true;
					}
				}
			}
			dlg->Destroy();
			return applied;
		}
	};
	return new ibPGEditorCommandSourceDialogAdapter();
}
