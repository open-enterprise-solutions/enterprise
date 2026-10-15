////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaTree window
////////////////////////////////////////////////////////////////////////////

#include "treeConfiguration.h"

#include <algorithm>

#include <wx/clipbrd.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/wupdlock.h>

#include "clipboardLock.h"   // the Open/Close pair, taken as a guard — the desktop's
#include "designer-thin/docManager/templates/docViewModuleEditor.h"   // a module's tab — the line a stop shows
#include "frmclient/artProvider/artProvider.h"
#include "frmclient/backend/metaCollection/metaObjectMetadata.h"      // the objects a modal editor of its menu is over
#include "frmclient/backend/metaCollection/partial/commonObject.h"
#include "frmclient/mainFrame/mainFrame.h"
#include "frmclient/mainFrame/objinspect/objinspect.h"   // the inspector the row's object is shown in
#include "frmclient/win/picture.h"

// The clipboard's format of a copied metaobject — the desktop's (backend/backend_core.h): the copy's bytes are the
// engine's either way, so one designer pastes what the other copied.
#define oes_clipboard_metadata	wxT("oes_clipboard_metadata")

namespace {

// A NEW BLOCK STARTS HERE — the desktop's (designer/mainFrame/metaTree/treeConfiguration_impl.cpp): asked of the MENU,
// which is the only thing that knows what is already in it — never a line first, never two in a row.
void ibAppendSeparator(wxMenu* menu)
{
	if (menu == nullptr)
		return;

	const size_t count = menu->GetMenuItemCount();
	if (count == 0)
		return;   // nothing above it — the line would be the first thing in the menu

	const wxMenuItem* last = menu->FindItemByPosition(count - 1);
	if (last != nullptr && last->IsSeparator())
		return;   // …and never two in a row

	menu->AppendSeparator();
}

// A row's picture as a tree and a menu show it — 16 by 16.
wxBitmap ibRowPicture(const wxString& picture)
{
	if (picture.IsEmpty())
		return wxNullBitmap;
	wxBitmap bitmap = ibProtocolPicture(picture);
	if (bitmap.IsOk() && bitmap.GetSize() != wxSize(16, 16))
		bitmap = wxBitmap(bitmap.ConvertToImage().Rescale(16, 16, wxIMAGE_QUALITY_HIGH));
	return bitmap;
}

// The objects' ids under `row` — what a new one is told apart from.
void ibObjectIds(const ibProtocolNode& row, std::set<long long>& ids)
{
	if (row.GetId() > 0)
		ids.insert(row.GetId());
	for (const ibProtocolNode& child : row.Children())
		ibObjectIds(child, ids);
}

// The pictures of `row` and the rows under it, by their ids — asked of the server at once, not a call per row drawn.
void ibRowPictures(const ibProtocolNode& row, std::vector<wxString>& pictures)
{
	pictures.push_back(row.GetString(ibProtocolName::Picture));
	for (const ibProtocolNode& child : row.Children())
		ibRowPictures(child, pictures);
}

// The places of the rows under `row` (at `path`) — what an external file's tree opens the first time.
void ibRowPaths(const ibProtocolNode& row, const wxString& path, std::set<wxString>& paths)
{
	for (const ibProtocolNode& child : row.Children()) {
		const wxString childPath = path + wxString::Format(wxT("/%lld"), child.GetId());
		paths.insert(childPath);
		ibRowPaths(child, childPath, paths);
	}
}

// The place of the first object under `row` not among `drawn` — empty: none is.
wxString ibCreatedPath(const ibProtocolNode& row, const wxString& path, const std::set<long long>& drawn)
{
	if (row.GetId() > 0 && drawn.find(row.GetId()) == drawn.end())
		return path;
	for (const ibProtocolNode& child : row.Children()) {
		const wxString created = ibCreatedPath(child, path + wxString::Format(wxT("/%lld"), child.GetId()), drawn);
		if (!created.IsEmpty())
			return created;
	}
	return wxString();
}

} // namespace

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ibMetaTreeBase::Draw(const ibProtocolNode& root)
{
	const ibTreeRow* const selected = GetRow(m_metaTreeCtrl->GetSelection());
	const wxString selectedPath = selected != nullptr ? selected->m_path : wxString();

	const bool shown = root.IsNode() && root.GetId() != 0;
	m_root = shown ? root.Clone() : ibProtocolNode();

	std::vector<wxString> pictures;
	ibRowPictures(m_root, pictures);
	ibProtocolPictureReceive(pictures);

	// ⭐ FROZEN FOR THE WHOLE OF IT — the control is what freezes, not the panel around it (the desktop's).
	wxWindowUpdateLocker freeze(m_metaTreeCtrl);
	m_drawing = true;

	// Another metadata — another root: drawn anew.
	wxTreeItemId rootItem = m_metaTreeCtrl->GetRootItem();
	const ibTreeRow* const drawn = GetRow(rootItem);
	if (!shown || drawn == nullptr || drawn->m_row.GetId() != m_root.GetId()) {
		m_metaTreeCtrl->DeleteAllItems();
		rootItem = wxTreeItemId();
	}

	bool drawnAnew = false;
	if (shown) {
		drawnAnew = !rootItem.IsOk();
		const wxString path = wxString::Format(wxT("%lld"), m_root.GetId());
		if (drawnAnew) {
			// THE FIRST DRAWING OPENS — the desktop's fill did on its way out: the configuration's the root and Common, an
			// external file's (its root hidden) everything.
			if (m_tab == 0) {
				m_expanded.insert(path);
				m_expanded.insert(path + wxT("/-1"));
			}
			else {
				ibRowPaths(m_root, path, m_expanded);
			}
			const int image = GetImage(m_root.GetString(ibProtocolName::Picture));
			rootItem = m_metaTreeCtrl->AddRoot(m_root.GetString(ibProtocolName::Title), image, image, new ibTreeRow(m_root, path));
			// …the configuration's in bold (the desktop's); an external file's is hidden.
			if (m_tab == 0)
				m_metaTreeCtrl->SetItemBold(rootItem);
		}
		DrawRow(rootItem, m_root);

		// …the selection where it was, while its row is there; drawn anew, the root selected — an external file's first
		// group (its Attributes), as the desktop's fill did.
		const wxTreeItemId item = drawnAnew ? (m_tab == 0 ? rootItem : FindItem(rootItem, path + wxT("/-1")))
			: selectedPath.IsEmpty() ? wxTreeItemId() : FindItem(rootItem, selectedPath);
		if (item.IsOk() && item != m_metaTreeCtrl->GetSelection())
			m_metaTreeCtrl->SelectItem(item);
	}

	m_drawing = false;

	// A selection the rows moved — its row taken away, the next one selected in its place — is told as a click is.
	const ibTreeRow* const selection = GetRow(m_metaTreeCtrl->GetSelection());
	if (drawnAnew || (selection != nullptr ? selection->m_path : wxString()) != selectedPath)
		SelectItem();
	else
		UpdateToolbar();
}

wxTreeItemId ibMetaTreeBase::InsertRow(const wxTreeItemId& parent, size_t position, const ibProtocolNode& row,
	const wxString& parentPath)
{
	const wxString path = parentPath + wxString::Format(wxT("/%lld"), row.GetId());
	const int image = GetImage(row.GetString(ibProtocolName::Picture));
	const wxTreeItemId item = m_metaTreeCtrl->InsertItem(parent, position, row.GetString(ibProtocolName::Title),
		image, image, new ibTreeRow(row, path));
	DrawRow(item, row);
	return item;
}

void ibMetaTreeBase::DrawRow(const wxTreeItemId& item, const ibProtocolNode& row)
{
	ibTreeRow* const data = static_cast<ibTreeRow*>(m_metaTreeCtrl->GetItemData(item));
	data->m_row = row;

	const wxString title = row.GetString(ibProtocolName::Title);
	if (m_metaTreeCtrl->GetItemText(item) != title)
		m_metaTreeCtrl->SetItemText(item, title);
	const int image = GetImage(row.GetString(ibProtocolName::Picture));
	if (m_metaTreeCtrl->GetItemImage(item) != image) {
		m_metaTreeCtrl->SetItemImage(item, image, wxTreeItemIcon_Normal);
		m_metaTreeCtrl->SetItemImage(item, image, wxTreeItemIcon_Selected);
	}

	// ITS CHILDREN, MATCHED BY THEIR IDS in the rows' order — a row shown is drawn over, one not shown put in, an item no
	// row stands for any more taken out. One moved is put in at its new place, and goes from its old one.
	std::vector<ibProtocolNode> rows;
	std::set<long long> ids;
	for (const ibProtocolNode& child : row.Children()) {
		if (!IsFound(child))
			continue;
		rows.push_back(child);
		ids.insert(child.GetId());
	}

	std::vector<wxTreeItemId> items;
	wxTreeItemIdValue cookie;
	for (wxTreeItemId child = m_metaTreeCtrl->GetFirstChild(item, cookie); child.IsOk();
		child = m_metaTreeCtrl->GetNextChild(item, cookie))
		items.push_back(child);

	size_t next = 0;
	for (size_t position = 0; position < rows.size(); ++position) {
		const long long id = rows[position].GetId();
		while (next < items.size() && ids.find(GetRow(items[next])->m_row.GetId()) == ids.end())
			m_metaTreeCtrl->Delete(items[next++]);
		if (next < items.size() && GetRow(items[next])->m_row.GetId() == id)
			DrawRow(items[next++], rows[position]);
		else
			InsertRow(item, position, rows[position], data->m_path);
	}
	while (next < items.size())
		m_metaTreeCtrl->Delete(items[next++]);

	// A hidden root (an external file's) is never opened or closed: its rows are the tree.
	if (m_metaTreeCtrl->HasFlag(wxTR_HIDE_ROOT) && item == m_metaTreeCtrl->GetRootItem())
		return;

	// OPEN AS THE PERSON LEFT IT — and while searching, everything: a match three levels down is not an answer until it
	// can be seen.
	const bool expanded = m_search.IsEmpty() ? m_expanded.find(data->m_path) != m_expanded.end() : true;
	if (expanded && m_metaTreeCtrl->ItemHasChildren(item) && !m_metaTreeCtrl->IsExpanded(item))
		m_metaTreeCtrl->Expand(item);
	else if (!expanded && m_metaTreeCtrl->IsExpanded(item))
		m_metaTreeCtrl->Collapse(item);
}

bool ibMetaTreeBase::IsFound(const ibProtocolNode& row) const
{
	if (m_search.IsEmpty())
		return true;

	if (row.GetId() > 0) {
		const wxString needle = m_search.Lower();
		if (row.GetString(ibProtocolName::Title).Lower().Find(needle) != wxNOT_FOUND
			|| row.GetString(ibProtocolName::Synonym).Lower().Find(needle) != wxNOT_FOUND)
			return true;
	}

	for (const ibProtocolNode& child : row.Children()) {
		if (IsFound(child))
			return true;
	}
	return false;
}

int ibMetaTreeBase::GetImage(const wxString& picture)
{
	const auto found = m_images.find(picture);
	if (found != m_images.end())
		return found->second;

	// A ROW WITH NO PICTURE OF ITS OWN IS A FOLDER — Common, the desktop's wxART_COMMON_FOLDER.
	const wxBitmap bitmap = picture.IsEmpty()
		? wxArtProvider::GetBitmapBundle(wxART_COMMON_FOLDER, wxART_METATREE).GetBitmap(wxSize(16, 16))
		: ibRowPicture(picture);
	const int image = bitmap.IsOk() ? m_metaTreeCtrl->GetImageList()->Add(bitmap) : wxNOT_FOUND;
	m_images.emplace(picture, image);
	return image;
}

wxTreeItemId ibMetaTreeBase::FindItem(const wxTreeItemId& item, const wxString& path) const
{
	const ibTreeRow* const row = GetRow(item);
	if (row == nullptr)
		return wxTreeItemId();
	if (row->m_path == path)
		return item;
	if (!path.StartsWith(row->m_path + wxT("/")))
		return wxTreeItemId();

	wxTreeItemIdValue cookie;
	for (wxTreeItemId child = m_metaTreeCtrl->GetFirstChild(item, cookie); child.IsOk();
		child = m_metaTreeCtrl->GetNextChild(item, cookie)) {
		const wxTreeItemId found = FindItem(child, path);
		if (found.IsOk())
			return found;
	}
	return wxTreeItemId();
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const ibMetaTreeBase::ibTreeRow* ibMetaTreeBase::GetRow(const wxTreeItemId& item) const
{
	return item.IsOk() ? static_cast<const ibTreeRow*>(m_metaTreeCtrl->GetItemData(item)) : nullptr;
}

long long ibMetaTreeBase::GetObject(const wxTreeItemId& item) const
{
	const ibTreeRow* const row = GetRow(item);
	return row != nullptr && row->m_row.GetId() > 0 ? row->m_row.GetId() : 0;
}

bool ibMetaTreeBase::GetPlace(const wxTreeItemId& item, wxString& type, long long& parent) const
{
	wxTreeItemId at = item;
	for (; at.IsOk(); at = m_metaTreeCtrl->GetItemParent(at)) {
		const ibTreeRow* const row = GetRow(at);
		if (row != nullptr && row->m_row.Has(ibProtocolName::Group))
			break;
	}
	if (!at.IsOk())
		return false;

	type = GetRow(at)->m_row.GetString(ibProtocolName::Group);
	parent = 0;
	for (const wxTreeItemId root = m_metaTreeCtrl->GetRootItem(); at.IsOk() && at != root; at = m_metaTreeCtrl->GetItemParent(at)) {
		parent = GetObject(at);
		if (parent != 0)
			break;
	}
	return true;
}

ibProtocolNode ibMetaTreeBase::GetParams(ibProtocolMetaDataCommand command, ibProtocolNode args) const
{
	if (m_tab != 0)
		args.SetValue(ibProtocolName::Tab, m_tab);

	ibProtocolNode params;
	params.SetValue(ibProtocolName::Schema, static_cast<long long>(ibProtocolSchema::MetaData));
	params.SetValue(ibProtocolName::Command, static_cast<long long>(command));
	params.SetValue(ibProtocolName::Args, args);
	return params;
}

bool ibMetaTreeBase::Command(ibProtocolMetaDataCommand command, ibProtocolNode args) const
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	ibProtocolNode answer;
	return frame != nullptr && frame->Call(ibProtocolMethod::Schema, GetParams(command, args), answer);
}

void ibMetaTreeBase::UpdateChoiceSelection(wxChoice* choice, const ibProtocolNode& property)
{
	choice->Clear();
	choice->AppendString(_("<not selected>"));

	int defSelection = 0;

	const long long value = property.GetInt(ibProtocolName::Value);
	for (const ibProtocolNode& offered : property.Children()) {
		const long long id = offered.GetInt(ibProtocolName::Id);
		const int selection_id = choice->Append(offered.GetString(ibProtocolName::Title),
			reinterpret_cast<void*>(static_cast<intptr_t>(id)));
		if (id == value)
			defSelection = selection_id;
	}

	choice->SetSelection(defSelection);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ibMetaTreeBase::CreateItem()
{
	wxString type;
	long long parent = 0;
	if (!GetPlace(m_metaTreeCtrl->GetSelection(), type, parent))
		return;

	// ASK — the new object's row comes back with the frame and is selected, the inspector showing it; then its editor is
	// opened, as the desktop's click did (NewItem, then OpenObjectForm): made while the row is shown, it shows its own.
	ibProtocolNode args;
	args.SetValue(ibProtocolName::Type, type);
	args.SetValue(ibProtocolName::Parent, parent);
	std::set<long long> drawn;
	ibObjectIds(m_root, drawn);
	if (Command(ibProtocolMetaDataCommand::Create, args) && SelectCreated(drawn))
		EditItem();
}

void ibMetaTreeBase::EditItem()
{
	const long long id = GetObject(m_metaTreeCtrl->GetSelection());
	if (id == 0)
		return;

	ibProtocolNode args;
	args.SetValue(ibProtocolName::Id, id);
	Command(ibProtocolMetaDataCommand::Open, args);
}

void ibMetaTreeBase::EditModule(const ibGuid& moduleName, int line, bool setRunLine)
{
	ibProtocolNode args;
	args.SetValue(ibProtocolName::Module, moduleName.str());
	if (!Command(ibProtocolMetaDataCommand::Open, args))
		return;

	// The tab is drawn by now — the frame came with the answer; its module found by the address its State says.
	for (ibFrontendDocument* const doc : docManager->GetDocumentsVector()) {
		ibModuleEditDocument* const moduleDoc = dynamic_cast<ibModuleEditDocument*>(doc);
		if (moduleDoc != nullptr && ibGuid(moduleDoc->GetDocPath()) == moduleName) {
			moduleDoc->SetCurrentLine(line, setRunLine);
			return;
		}
	}
}

void ibMetaTreeBase::RemoveItem()
{
	const wxTreeItemId selection = m_metaTreeCtrl->GetSelection();
	const long long id = GetObject(selection);
	if (id == 0 || selection == m_metaTreeCtrl->GetRootItem())
		return;

	// ASK, then let the answer come back — the row goes with the frame, the editors over it with the server's Removed.
	ibProtocolNode args;
	args.SetValue(ibProtocolName::Id, id);
	Command(ibProtocolMetaDataCommand::Remove, args);
}

void ibMetaTreeBase::PropertyItem()
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return;

	if (!frame->IsShownInspector())
		frame->ShowInspector();

	ActivateTree();
}

void ibMetaTreeBase::SelectItem()
{
	UpdateToolbar();

	// WHAT THE INSPECTOR SHOWS — the row's object, made as the row is selected, what the server says of it asked as it is
	// shown; a group row shows nothing.
	const long long id = GetObject(m_metaTreeCtrl->GetSelection());
	std::shared_ptr<ibValueMetaObject> object;
	if (id != 0)
		object = std::make_shared<ibValueMetaObject>(static_cast<ibMetaID>(id), ibMetaData(m_tab));
	m_object = std::move(object);
	ActivateTree();
}

void ibMetaTreeBase::ActivateTree()
{
	// Hidden, the inspector holds it all the same — Properties shows what it holds.
	ibObjectInspector* const inspector = objectInspector;
	if (inspector != nullptr)
		inspector->SelectObject(m_object.get());
}

void ibMetaTreeBase::UpItem()
{
	const wxTreeItemId selection = m_metaTreeCtrl->GetSelection();
	if (selection.IsOk())
		MoveItem(selection, m_metaTreeCtrl->GetPrevSibling(selection));
}

void ibMetaTreeBase::DownItem()
{
	const wxTreeItemId selection = m_metaTreeCtrl->GetSelection();
	if (selection.IsOk())
		MoveItem(selection, m_metaTreeCtrl->GetNextSibling(selection));
}

void ibMetaTreeBase::MoveItem(const wxTreeItemId& item, const wxTreeItemId& sibling)
{
	const long long id = GetObject(item);
	if (id == 0 || GetObject(sibling) == 0 || item == m_metaTreeCtrl->GetRootItem())
		return;

	// The door moves it and announces `Moved`; the rows follow.
	ibProtocolNode args;
	args.SetValue(ibProtocolName::Id, id);
	args.SetValue(ibProtocolName::Position, GetRow(sibling)->m_row.GetInt(ibProtocolName::Position));
	Command(ibProtocolMetaDataCommand::Move, args);
}

// A NEW OBJECT IS LEFT SELECTED AND OPENED, as the desktop's create and paste left it — its row the one the frame brought
// back that was not drawn before. Opened first, so the shape is final before the selection aims at it.
bool ibMetaTreeBase::SelectCreated(const std::set<long long>& drawn)
{
	const wxTreeItemId root = m_metaTreeCtrl->GetRootItem();
	const ibTreeRow* const row = GetRow(root);
	if (row == nullptr)
		return false;

	const wxString path = ibCreatedPath(m_root, row->m_path, drawn);
	const wxTreeItemId item = path.IsEmpty() ? wxTreeItemId() : FindItem(root, path);
	if (!item.IsOk())
		return false;

	m_metaTreeCtrl->Expand(item);
	m_metaTreeCtrl->SelectItem(item);
	return true;
}

// The sort button: the group's objects by name, each put where the object at its place in the group stands now (the
// desktop's SortItemsByName) — the places read from the rows each move brings back.
void ibMetaTreeBase::SortItem()
{
	const wxTreeItemId selection = m_metaTreeCtrl->GetSelection();
	if (GetObject(selection) == 0 || selection == m_metaTreeCtrl->GetRootItem())
		return;

	const wxString parentPath = GetRow(m_metaTreeCtrl->GetItemParent(selection))->m_path;
	const auto objects = [this, &parentPath]() {
		std::vector<ibProtocolNode> rows;
		const wxTreeItemId parentItem = FindItem(m_metaTreeCtrl->GetRootItem(), parentPath);
		wxTreeItemIdValue cookie;
		for (wxTreeItemId row = parentItem.IsOk() ? m_metaTreeCtrl->GetFirstChild(parentItem, cookie) : wxTreeItemId();
			row.IsOk(); row = m_metaTreeCtrl->GetNextChild(parentItem, cookie)) {
			if (GetObject(row) != 0)
				rows.push_back(GetRow(row)->m_row);
		}
		std::sort(rows.begin(), rows.end(), [](const ibProtocolNode& a, const ibProtocolNode& b) {
			return a.GetInt(ibProtocolName::Position) < b.GetInt(ibProtocolName::Position);
		});
		return rows;
	};

	std::vector<ibProtocolNode> sorted = objects();
	std::stable_sort(sorted.begin(), sorted.end(), [](const ibProtocolNode& a, const ibProtocolNode& b) {
		return wxStrcmp(a.GetString(ibProtocolName::Title), b.GetString(ibProtocolName::Title)) < 0;
	});

	for (size_t i = 0; i < sorted.size(); ++i) {
		const std::vector<ibProtocolNode> placed = objects();
		if (i >= placed.size())
			return;
		if (placed[i].GetId() == sorted[i].GetId())
			continue;

		ibProtocolNode args;
		args.SetValue(ibProtocolName::Id, sorted[i].GetId());
		args.SetValue(ibProtocolName::Position, placed[i].GetInt(ibProtocolName::Position));
		if (!Command(ibProtocolMetaDataCommand::Move, args))
			return;
	}
}

// THE COPY IS THE SERVER'S — the object's bytes written into a file uploaded empty and taken down from it, then onto the
// clipboard as the desktop put them.
void ibMetaTreeBase::CopyItem()
{
	const wxTreeItemId selection = m_metaTreeCtrl->GetSelection();
	const long long id = GetObject(selection);
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (id == 0 || selection == m_metaTreeCtrl->GetRootItem() || frame == nullptr)
		return;

	const wxString title = GetRow(selection)->m_row.GetString(ibProtocolName::Title);
	const wxString local = wxFileName::CreateTempFileName(wxT("oes"));
	if (local.IsEmpty())
		return;

	wxMemoryBuffer bytes;
	wxString file;
	ibProtocolNode args;
	args.SetValue(ibProtocolName::Id, id);
	if (frame->UploadFile(local, file)) {
		args.SetValue(ibProtocolName::File, file);
		if (Command(ibProtocolMetaDataCommand::Copy, args) && frame->DownloadFile(file, local)) {
			wxFile read(local);
			const wxFileOffset length = read.IsOpened() ? read.Length() : 0;
			if (length > 0) {
				void* const buffer = bytes.GetWriteBuf(static_cast<size_t>(length));
				const ssize_t done = read.Read(buffer, static_cast<size_t>(length));
				bytes.UngetWriteBuf(done == length ? static_cast<size_t>(length) : 0);
			}
		}
	}
	wxRemoveFile(local);

	if (bytes.IsEmpty())
		return;

	const ibClipboardLock clipboard;   // closes on every path out — see clipboardLock.h
	if (!clipboard.IsOpen())
		return;

	wxDataObjectComposite* composite_object = new wxDataObjectComposite;
	wxCustomDataObject* custom_object = new wxCustomDataObject(oes_clipboard_metadata);
	custom_object->SetData(bytes.GetDataLen(), bytes.GetData());

	composite_object->Add(custom_object);
	composite_object->Add(new wxTextDataObject(title), true);

	// tell clipboard
	wxTheClipboard->SetData(composite_object);
}

// …and back: the clipboard's bytes uploaded, read by the server where the selection says — the class and the object.
void ibMetaTreeBase::PasteItem()
{
	wxString type;
	long long parent = 0;
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr || !GetPlace(m_metaTreeCtrl->GetSelection(), type, parent))
		return;

	wxCustomDataObject data(oes_clipboard_metadata);
	{
		const ibClipboardLock clipboard;
		if (!clipboard.IsOpen() || !wxTheClipboard->IsSupported(oes_clipboard_metadata) || !wxTheClipboard->GetData(data))
			return;
	}

	const wxString local = wxFileName::CreateTempFileName(wxT("oes"));
	if (local.IsEmpty())
		return;

	bool written = false;
	{
		wxFile write(local, wxFile::write);
		written = write.IsOpened() && write.Write(data.GetData(), data.GetDataSize()) == data.GetDataSize();
	}

	wxString file;
	if (written && frame->UploadFile(local, file)) {
		ibProtocolNode args;
		args.SetValue(ibProtocolName::Type, type);
		args.SetValue(ibProtocolName::Parent, parent);
		args.SetValue(ibProtocolName::File, file);
		std::set<long long> drawn;
		ibObjectIds(m_root, drawn);
		if (Command(ibProtocolMetaDataCommand::Paste, args))
			SelectCreated(drawn);
	}
	wxRemoveFile(local);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ibMetaTreeBase::UpdateToolbar()
{
	const wxTreeItemId item = m_metaTreeCtrl->GetSelection();
	const wxTreeItemId root = m_metaTreeCtrl->GetRootItem();
	const ibTreeRow* const row = GetRow(item);
	// Common — the configuration root's first row, -1 by its place (an external file's is its Attributes).
	const bool common = m_tab == 0 && row != nullptr && row->m_row.GetId() == -1 && m_metaTreeCtrl->GetItemParent(item) == root;
	const bool object = GetObject(item) != 0 && item != root;

	m_metaTreeToolbar->EnableTool(ID_METATREE_NEW, row != nullptr && item != root && !common);
	m_metaTreeToolbar->EnableTool(ID_METATREE_EDIT, object);
	m_metaTreeToolbar->EnableTool(ID_METATREE_DELETE, object);

	m_metaTreeToolbar->EnableTool(ID_METATREE_UP, object);
	m_metaTreeToolbar->EnableTool(ID_METATREE_DOWM, object);
	m_metaTreeToolbar->EnableTool(ID_METATREE_SORT, object);

	m_metaTreeToolbar->Refresh();
}

bool ibMetaTreeBase::AppendMetaMenu(wxMenu& menu, const ibProtocolNode& row)
{
	const ibProtocolNode items = row.FindChild(ibProtocolName::Menu);
	const bool standard = items.GetBool(ibProtocolName::Standard, true);

	const std::vector<ibProtocolNode> entries = items.Children();
	if (entries.empty())
		return standard;

	std::vector<wxString> pictures;
	for (const ibProtocolNode& entry : entries)
		pictures.push_back(entry.GetString(ibProtocolName::Picture));
	ibProtocolPictureReceive(pictures);

	for (size_t idx = 0; idx < entries.size(); idx++) {

		const ibProtocolNode& entry = entries[idx];

		// THE LINE GOES WHERE THE KIND CHANGES — never where somebody asked for one.
		if (idx > 0 && entries[idx - 1].GetInt(ibProtocolName::Kind) != entry.GetInt(ibProtocolName::Kind))
			ibAppendSeparator(&menu);

		const int id = wxWindow::NewControlId();
		wxMenuItem* menuItem = menu.Append(id, entry.GetString(ibProtocolName::Title));

		const long long object = entry.GetInt(ibProtocolName::Object);
		const wxBitmap bitmap = ibRowPicture(entry.GetString(ibProtocolName::Picture));
		if (bitmap.IsOk())
			menuItem->SetBitmap(bitmap);

		// ⭐⭐ THE ITEM CARRIES ITS OWN ACTION — what it opens, by its id (a module's carries its Command too). A modal
		// editor — a Command with nothing to open — is the tree's, over the row's object (the engine's ProcessCommand
		// called it so), by the item's Name; one this tree does not have: shown, shut.
		if (object == 0) {
			const wxString name = entry.GetString(ibProtocolName::Name);
			const ibMetaID owner = static_cast<ibMetaID>(row.GetId());
			if (name == wxT("PredefinedValues")) {
				menu.Bind(wxEVT_MENU, [this, owner](wxCommandEvent&) {
					ibValueMetaObjectRecordDataHierarchyMutableRef metaObject(owner, ibMetaData(m_tab));
					EditPredefinedValues(&metaObject);
				}, id);
			}
			else if (name == wxT("HomePage")) {
				menu.Bind(wxEVT_MENU, [this, owner](wxCommandEvent&) {
					ibValueMetaObjectConfiguration metaObject(owner, ibMetaData(m_tab));
					EditHomePage(&metaObject);
				}, id);
			}
			else {
				menuItem->Enable(false);
			}
			continue;
		}
		menu.Bind(wxEVT_MENU, [this, object](wxCommandEvent&) {
			ibProtocolNode args;
			args.SetValue(ibProtocolName::Id, object);
			Command(ibProtocolMetaDataCommand::Open, args);
		}, id);
	}

	// …and the boundary to the tree's OWN block below.
	ibAppendSeparator(&menu);

	return standard;
}

void ibMetaTreeBase::ShowContextMenu(const wxTreeItemId& item, const wxPoint& pos)
{
	const ibTreeRow* const row = GetRow(item);
	if (row == nullptr)
		return;

	wxMenu menu;   // stack — PopupMenu does not take ownership, and it blocks until dismissed

	if (row->m_row.GetId() > 0) {
		// ⭐ ASKED ONCE. The object offers what it opens, this draws it, and the answer says whether the standard New /
		// Edit / Remove block applies.
		if (AppendMetaMenu(menu, row->m_row)) {
			wxMenuItem* menuItem = nullptr;
			menuItem = menu.Append(ID_METATREE_NEW, _("New"));
			menuItem->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_ADD, wxART_FRONTEND, wxSize(16, 16)));
			menuItem = menu.Append(ID_METATREE_EDIT, _("Edit"));
			menuItem->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_EDIT, wxART_FRONTEND, wxSize(16, 16)));
			menuItem = menu.Append(ID_METATREE_DELETE, _("Remove"));
			menuItem->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_DELETE, wxART_FRONTEND, wxSize(16, 16)));
		}

		// ⭐ THE TWO TEXTS AND PROPERTIES ALWAYS APPLY — every object has them, the configuration root included.
		ibAppendSeparator(&menu);
		menu.Append(ID_METATREE_HELP, _("Help information..."));
		menu.Append(ID_METATREE_NOTES, _("Technical information..."));
		menu.AppendSeparator();
		wxMenuItem* propertyItem = menu.Append(ID_METATREE_PROPERTY, _("Properties"));
		propertyItem->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_PROPERTY, wxART_SERVICE));
	}
	else if (!(m_tab == 0 && row->m_row.GetId() == -1 && m_metaTreeCtrl->GetItemParent(item) == m_metaTreeCtrl->GetRootItem())) {
		wxMenuItem* menuItem = menu.Append(ID_METATREE_NEW, _("New"));
		menuItem->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_ADD, wxART_FRONTEND, wxSize(16, 16)));
	}

	if (menu.GetMenuItemCount() > 0)
		m_metaTreeCtrl->PopupMenu(&menu, pos);
}

void ibMetaTreeBase::Search(const wxString& search)
{
	if (search == m_search)
		return;

	// The rows drawn again through the text — what it keeps, opened; none, as the person left them.
	m_search = search;
	Draw(m_root);

	// …and the root selected, as the desktop's fill after a search did.
	const wxTreeItemId root = m_metaTreeCtrl->GetRootItem();
	if (root.IsOk())
		m_metaTreeCtrl->SelectItem(root);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ibConfigurationTree::IsModified() const
{
	return m_root.GetBool(ibProtocolName::Modified);
}

bool ibConfigurationTree::IsEdited() const
{
	return m_root.GetBool(ibProtocolName::Edited);
}

wxString ibConfigurationTree::GetConfigName() const
{
	return m_root.GetString(ibProtocolName::Title);
}

bool ibConfigurationTree::SaveDatabase() const
{
	return Command(ibProtocolMetaDataCommand::Save, ibProtocolNode());
}

bool ibConfigurationTree::RollbackConfiguration() const
{
	return Command(ibProtocolMetaDataCommand::Rollback, ibProtocolNode());
}

bool ibConfigurationTree::ApplyConfiguration() const
{
	return Command(ibProtocolMetaDataCommand::Apply, ibProtocolNode());
}

bool ibConfigurationTree::LoadConfigFromFile(const wxString& strFileName) const
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	wxString file;
	if (frame == nullptr || !frame->UploadFile(strFileName, file))
		return false;

	ibProtocolNode args;
	args.SetValue(ibProtocolName::File, file);
	return Command(ibProtocolMetaDataCommand::Load, args);
}

// THE FILE IT IS WRITTEN INTO IS THE SERVER'S — a temporary one, made by uploading an empty file of this computer's
// (the tree's Copy): the server writes the configuration into it, and it is taken down to where the person chose.
bool ibConfigurationTree::SaveConfigToFile(const wxString& strFileName) const
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	const wxString local = frame != nullptr ? wxFileName::CreateTempFileName(wxT("oes")) : wxString();
	if (local.IsEmpty())
		return false;

	wxString file;
	const bool uploaded = frame->UploadFile(local, file);
	wxRemoveFile(local);

	ibProtocolNode args;
	args.SetValue(ibProtocolName::File, file);
	return uploaded
		&& Command(ibProtocolMetaDataCommand::Unload, args)
		&& frame->DownloadFile(file, strFileName);
}
