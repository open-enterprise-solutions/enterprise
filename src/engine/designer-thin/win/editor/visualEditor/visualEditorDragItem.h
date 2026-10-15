#ifndef __VISUAL_EDITOR_DRAG_ITEM_H__
#define __VISUAL_EDITOR_DRAG_ITEM_H__

////////////////////////////////////////////////////////////////////////////
//	Description : form-editor DRAG ITEMS — one polymorphic KIND per draggable
////////////////////////////////////////////////////////////////////////////

// A form-editor DRAG ITEM — the desktop's (designer visualEditorDragItem.h): one draggable KIND (a source path -> a
// bound control). Polymorphic: each kind owns its wire FORMAT, its (de)serialization, and what it DOES when dropped on
// the form — said to the server's editor (ApplyDrop → the editor's door). The drop target (ibSourceDragDropTarget)
// holds the registered kinds, matches the RECEIVED format and dispatches — so adding a draggable (the command tree's
// command) is a NEW SUBCLASS + one line in the target ctor, never a branch in OnData.

#include "visualEditor.h"                         // ibVisualEditorNotebook::ibVisualEditor (ApplyDrop target), ibValueFrame
#include "core/description/commandDescription.h"  // ibCommandDescription / ibCommandDescriptionMemory
#include "core/description/sourceDescription.h"   // ibSourceDescription / ibSourceDescriptionMemory
#include "core/fileSystem/fs.h"                   // ibWriterMemory / ibReaderMemory (payload buffer)

#include <wx/dnd.h>       // wxDropSource / wxCustomDataObject / wxDataFormat
#include <wx/dataobj.h>

class ibFormDragItem {
public:
	virtual ~ibFormDragItem() {}

	// The wire format that identifies THIS kind on both the drag and the drop side.
	virtual wxDataFormat GetFormat() const = 0;

	// DROP side: read the payload bytes into self. false = malformed (the target skips it).
	virtual bool LoadPayload(ibReaderMemory& reader) = 0;

	// DROP side: enact the drop — `target` is the frame the surface resolved the drop point to (may be null).
	virtual void ApplyDrop(ibVisualEditorNotebook::ibVisualEditor* editor, ibValueFrame* target) const = 0;

	// DROP side: a copy of the decoded item — a deferred drop outlives the target that decoded it.
	virtual ibFormDragItem* Clone() const = 0;

protected:
	// DRAG side: encode self into the writer (the twin of LoadPayload).
	virtual void SavePayload(ibWriterMemory& writer) const = 0;

	// DRAG side: serialize self and start the drag from `source`. Called by a subclass's (source, desc) ctor —
	// the drag IS the construction, so a tree just writes `ibXxxDragItem(tree, desc)`. Payload bytes = SavePayload.
	void DoDrag(wxWindow* source) const
	{
		ibWriterMemory writer;
		SavePayload(writer);
		const wxMemoryBuffer buf = writer.buffer();
		wxCustomDataObject payload(GetFormat());
		payload.SetData(buf.GetDataLen(), buf.GetData());
		wxDropSource drag(payload, source);
		drag.DoDragDrop(wxDrag_CopyOnly);
	}
};

// A SOURCE path (attribute / table column) -> a bound control at the drop point. The drop target may be null (a
// drop on empty canvas); the server's CreateBoundControl falls back to the form root.
class ibSourceDragItem : public ibFormDragItem {
public:
	ibSourceDragItem() {}
	explicit ibSourceDragItem(const ibSourceDescription& desc) : m_desc(desc) {}
	// Construct-AND-drag: wrap the path and immediately start the drag from `source` (the tree's one-liner).
	ibSourceDragItem(wxWindow* source, const ibSourceDescription& desc) : m_desc(desc) { DoDrag(source); }

	virtual wxDataFormat GetFormat() const override { return wxDataFormat(wxT("oes_source_drag")); }
	virtual bool LoadPayload(ibReaderMemory& reader) override { ibSourceDescriptionMemory::LoadData(reader, m_desc); return m_desc.IsOk(); }
	virtual void ApplyDrop(ibVisualEditorNotebook::ibVisualEditor* editor, ibValueFrame* target) const override
	{
		if (editor != nullptr)
			editor->CreateBoundControl(target, m_desc);   // null target -> the form root (CreateBoundControl's fallback)
	}
	virtual ibFormDragItem* Clone() const override { return new ibSourceDragItem(m_desc); }

protected:
	virtual void SavePayload(ibWriterMemory& writer) const override { ibSourceDescriptionMemory::SaveData(writer, m_desc); }

private:
	ibSourceDescription m_desc;
};

// A COMMAND path (a form command, a standard action, a global command) -> a bound command projection at the drop point:
// the server climbs from the target to the bar it joins, or makes a button (CreateCommandButton).
class ibCommandDragItem : public ibFormDragItem {
public:
	ibCommandDragItem() {}
	explicit ibCommandDragItem(const ibCommandDescription& desc) : m_desc(desc) {}
	// Construct-AND-drag: wrap the command path and immediately start the drag from `source` (the tree's one-liner).
	ibCommandDragItem(wxWindow* source, const ibCommandDescription& desc) : m_desc(desc) { DoDrag(source); }

	virtual wxDataFormat GetFormat() const override { return wxDataFormat(wxT("oes_command_drag")); }
	virtual bool LoadPayload(ibReaderMemory& reader) override { ibCommandDescriptionMemory::LoadData(reader, m_desc); return m_desc.IsOk(); }
	virtual void ApplyDrop(ibVisualEditorNotebook::ibVisualEditor* editor, ibValueFrame* target) const override
	{
		if (editor != nullptr)
			editor->CreateCommandButton(target, m_desc);   // null target -> the form (the server's climb)
	}
	virtual ibFormDragItem* Clone() const override { return new ibCommandDragItem(m_desc); }

protected:
	virtual void SavePayload(ibWriterMemory& writer) const override { ibCommandDescriptionMemory::SaveData(writer, m_desc); }

private:
	ibCommandDescription m_desc;
};

#endif // __VISUAL_EDITOR_DRAG_ITEM_H__
