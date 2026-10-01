#ifndef __VISUAL_EDITOR_DRAG_ITEM_H__
#define __VISUAL_EDITOR_DRAG_ITEM_H__

////////////////////////////////////////////////////////////////////////////
//	Description : form-editor DRAG ITEMS — one polymorphic KIND per draggable
////////////////////////////////////////////////////////////////////////////

// A form-editor DRAG ITEM — one draggable KIND (a source path -> a bound control, a command -> a command button).
// Polymorphic: each kind owns its wire FORMAT, its (de)serialization, and what it DOES when dropped on the form.
// The drop target (ibSourceDragDropTarget) holds the registered kinds, matches the RECEIVED format and dispatches
// — so adding a draggable is a NEW SUBCLASS + one line in the target ctor, never a branch in OnData. Mirrors the
// engine's ibXxxDescription / ibXxxDescriptionMemory pair pattern (each kind wraps its own description + serializer).

#include "visualEditor.h"                // ibVisualEditorNotebook::ibVisualEditor (ApplyDrop target), ibValueFrame
#include "backend/sourceDescription.h"   // ibSourceDescription / ibSourceDescriptionMemory
#include "backend/commandDescription.h"  // ibCommandDescription / ibCommandDescriptionMemory
#include "backend/fileSystem/fs.h"       // ibWriterMemory / ibReaderMemory (payload buffer)

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

	// A heap copy of the DECODED item — the target clones it for a DEFERRED apply, so it outlives the target
	// (a tablebox grid re-wires / frees its own drop target mid-drop).
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
// drop on empty canvas); CreateBoundControl falls back to the form root, so null resolves to a valid parent.
class ibSourceDragItem : public ibFormDragItem {
public:
	ibSourceDragItem() {}
	explicit ibSourceDragItem(const ibSourceDescription& desc) : m_desc(desc) {}
	// Construct-AND-drag: wrap the path and immediately start the drag from `source` (the tree's one-liner).
	ibSourceDragItem(wxWindow* source, const ibSourceDescription& desc) : m_desc(desc) { DoDrag(source); }

	wxDataFormat GetFormat() const override { return wxDataFormat(wxT("oes_source_drag")); }
	bool LoadPayload(ibReaderMemory& reader) override { ibSourceDescriptionMemory::LoadData(reader, m_desc); return m_desc.IsOk(); }
	void ApplyDrop(ibVisualEditorNotebook::ibVisualEditor* editor, ibValueFrame* target) const override
	{
		if (editor != nullptr)
			editor->CreateBoundControl(target, m_desc);   // null target -> the form root (CreateBoundControl's fallback)
	}
	ibFormDragItem* Clone() const override { return new ibSourceDragItem(m_desc); }

protected:
	void SavePayload(ibWriterMemory& writer) const override { ibSourceDescriptionMemory::SaveData(writer, m_desc); }

private:
	ibSourceDescription m_desc;
};

// A COMMAND hop path -> a bound command button on the nearest command bar up from the drop point.
class ibCommandDragItem : public ibFormDragItem {
public:
	ibCommandDragItem() {}
	explicit ibCommandDragItem(const ibCommandDescription& desc) : m_desc(desc) {}
	// Construct-AND-drag: wrap the command path and immediately start the drag from `source` (the tree's one-liner).
	ibCommandDragItem(wxWindow* source, const ibCommandDescription& desc) : m_desc(desc) { DoDrag(source); }

	wxDataFormat GetFormat() const override { return wxDataFormat(wxT("oes_command_drag")); }
	bool LoadPayload(ibReaderMemory& reader) override { ibCommandDescriptionMemory::LoadData(reader, m_desc); return m_desc.IsOk(); }
	void ApplyDrop(ibVisualEditorNotebook::ibVisualEditor* editor, ibValueFrame* target) const override
	{
		if (editor != nullptr)
			editor->CreateCommandButton(target, m_desc);   // climbs from target to the nearest command bar
	}
	ibFormDragItem* Clone() const override { return new ibCommandDragItem(m_desc); }

protected:
	void SavePayload(ibWriterMemory& writer) const override { ibCommandDescriptionMemory::SaveData(writer, m_desc); }

private:
	ibCommandDescription m_desc;
};

#endif // __VISUAL_EDITOR_DRAG_ITEM_H__
