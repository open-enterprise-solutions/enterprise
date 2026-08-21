#ifndef __SETTINGS_FIELD_TREE_H__
#define __SETTINGS_FIELD_TREE_H__

// ---------------------------------------------------------------------------
// "WHICH FIELDS DOES THIS THING HAVE" — asked once, answered once.
//
// Every settings surface needs the same left-hand pane: the source's fields, a
// reference field unfolding lazily into its target's (Supplier.Region.Country),
// double-click or drag putting one into the list on the right. And every value
// cell that holds a FIELD needs the same picker over it.
//
// That is one question, so it is one class — and it is what lets the dynamic
// list's window and the composer's window share their filter and sort editors
// without either of them containing the other (Max, 2026-08-20: "two different
// worlds; what is really shared is filter and sort").
//
// It is NOT a widget. A tree control belongs to whoever laid the pane out; this
// fills one, wires the two behaviours a field tree has (unfold, drag) and opens
// the picker. A window can drive several trees with one of these — which is what
// the list's four tabs do.
// ---------------------------------------------------------------------------

#include <wx/treectrl.h>

#include <vector>
#include <functional>

#include "backend/composition/compositionField.h"   // ibValueCompositionField — a field IS a value
#include "backend/backend_type.h"                   // ibTypeDescription

class ibSourceDataObject;
class ibMetaData;

// A field as a FLAT list gives it — the fallback for a source that cannot describe
// itself (a plain table model's columns). A self-describing source needs none of
// this: its explorer is walked directly.
struct ibSettingsPlainField {
	wxString          m_name;
	ibMetaID          m_id = wxNOT_FOUND;
	ibTypeDescription m_type;
};

class ibSettingsFieldTree {
public:

	// WHERE THE FIELDS COME FROM — a source that describes itself (a dynamic list, a
	// value-table model, a composition) walks its explorer, so a reference column
	// gets its [+] and unfolds.
	void SetSource(const ibSourceDataObject* source, const ibMetaData* metaData) {
		m_source = source;
		m_metaData = metaData;
		m_plain.clear();
	}

	// …or a flat list, when there is nothing to walk. A reference-typed field still
	// gets its [+]: what a value may BE is a question about the type, not about who
	// listed it.
	void SetPlainFields(std::vector<ibSettingsPlainField> fields, const ibMetaData* metaData) {
		m_plain = std::move(fields);
		m_metaData = metaData;
		m_source = nullptr;
	}

	// ⭐ WHICH OF THESE FIELDS ARE RESOURCES — asked of the HOST, because being a resource is not a
	// property of the field: it is a DECLARATION the composition makes about it (Max, 2026-08-22:
	// once a field is added to the resources, the settings list must already show that it is one).
	//
	// A predicate rather than a list, so the tree never holds a second copy of the resources and
	// cannot fall out of step with them; unset, every field is drawn as it always was.
	void SetResourceTest(std::function<bool(const wxString& path)> isResource) {
		m_isResource = std::move(isResource);
	}

	// ⭐⭐ AND WHICH OF THEM MAY BE USED AT ALL — the AVAILABLE fields, asked of the host for the
	// same reason: what a node may see is a statement the COMPOSITION makes about it, inherited
	// from the report down through the outputs to the levels. Narrowing it is the whole point of
	// having it, and a set that narrows nothing is a list a person fills in and never sees again.
	//
	// A ROAD IS NEVER HIDDEN. A reference is asked about its own path, and answering "no" for
	// `Producer` would take away `Producer.Region` with it — so a node that unfolds stays, and only
	// LEAVES are narrowed. Unset (every other host) shows everything, as before.
	void SetVisibleTest(std::function<bool(const wxString& path)> isVisible) {
		m_isVisible = std::move(isVisible);
	}

	// The config that resolves reference targets — the source's own, else the active one.
	const ibMetaData* GetMetaData() const;

	// FILL a tree control. REBUILT, not built once: changing the query changes which
	// fields exist, and this runs again to say so.
	void Populate(wxTreeCtrl* tree) const;

	// Walk a dotted technical path down the tree, loading each reference on the way,
	// and land the cursor on the leaf.
	void SelectByPath(wxTreeCtrl* tree, const wxString& path) const;

	// WIRE the two behaviours every field tree has: unfold a reference lazily, and
	// drag a field out (dropping on the right-hand pane adds it — the dropped field
	// is GetDragItem()). The host binds what it does with a double-click itself,
	// because that differs per tab.
	void Attach(wxTreeCtrl* tree);

	// The field being dragged out of a tree, or an invalid id.
	const wxTreeItemId& GetDragItem() const { return m_dragItem; }

	// Read one tree node as a FIELD — the path, the readable presentation, the leaf
	// id and the type behind it. Null when the node is a road (a reference) or empty.
	static class ibValueCompositionField* FieldAt(const wxTreeCtrl* tree, const wxTreeItemId& item);

	// THE PICKER — the same tree as a form. A field is a VALUE, so choosing one is
	// choosing a value, and it happens where every other value choice happens: the
	// Select button of the cell. Returns null when the user closed without picking.
	ibValueCompositionField* ChooseField(wxWindow* parent, const wxString& currentPath = wxEmptyString) const;

private:

	const ibSourceDataObject*         m_source   = nullptr;   // PATH B — a self-describing source
	std::vector<ibSettingsPlainField> m_plain;                // PATH A — flat columns
	const ibMetaData*                 m_metaData = nullptr;
	// "Is this path one of the composition's resources?" — see SetResourceTest.
	std::function<bool(const wxString& path)> m_isResource;
	// "May this node use this field?" — see SetVisibleTest.
	std::function<bool(const wxString& path)> m_isVisible;

	wxTreeItemId m_dragItem;   // field being dragged from a tree
};

#endif // __SETTINGS_FIELD_TREE_H__
