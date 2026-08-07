#ifndef __QUERY_FIELD_TREE_H__
#define __QUERY_FIELD_TREE_H__

////////////////////////////////////////////////////////////////////////////
// THE FIELD TREE — one mechanism, every window that shows fields.
////////////////////////////////////////////////////////////////////////////
//
// A field tree is not a list of strings. Every row has to answer four questions, and answering
// them is the whole of it:
//
//   * WHAT PATH DOES THIS WRITE?  `Supplier.Region.Country` — the string the AST carries, built
//     while walking down, because past the first level there is no index to look one up by.
//   * CAN IT BE WALKED INTO?      A reference field can; the clsid it points at is both "there
//     are children" and the key they are asked for by. No lookup table, no second question.
//   * WHICH TABLE IS IT FROM?     Carried all the way down, so a field five hops deep still knows
//     which source it hangs off.
//   * WHAT IS IT CALLED HERE?     The presentation, which is never what a query writes.
//
// That was written twice — once in the constructor, once in the expression editor — with two node
// classes that held the same four facts under different names, and the drift had already started
// (one grew the table path, the other did not). The trees themselves stay where they are: they are
// laid out differently and have different verbs. What they share is the ROW.
//
// Frontend on purpose: this is wxTreeCtrl, and the path-building it does is one call to the
// engine's ibQueryColumnFromPath when a verb needs the AST node.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/query/queryConstructorModel.h"

#include <wx/treectrl.h>
#include <wx/dnd.h>

#include <utility>
#include <vector>

// A node of a constructor tree carries what it IS, not what it looks like — the source path for
// the catalogue, the (source, field) pair for a chosen table.
class ibQueryTreeNode : public wxTreeItemData
{
public:
	ibQueryTreeNode(std::vector<wxString> path, bool temp)
		: m_path(std::move(path)), m_temp(temp) {}
	ibQueryTreeNode(int sourceIndex, wxString field)
		: m_sourceIndex(sourceIndex), m_field(std::move(field)) {}

	std::vector<wxString> m_path;              // catalogue node: the dotted source path
	                                           // field node: the table it came out of
	bool                  m_temp = false;      // catalogue node: a temp table the package made
	int                   m_sourceIndex = -1;  // chosen-table node: 0 = FROM, 1..n = joins[i-1]
	wxString              m_field;             // field node: the path (empty = the table row itself)

	// A REFERENCE FIELD CAN BE UNFOLDED. Non-zero says what it points at, which is both "this node
	// has children" and the key those children are asked for — so the tree needs no second question
	// and no type lookup of its own. Filled lazily: a config with deep reference chains would
	// otherwise walk the whole graph to draw one table.
	ibClassID             m_referenceClsid = 0;
	bool                  m_expanded = false;   // its fields have been asked for already

	// The TABLE this row belongs to, as a person reads it. Carried down every level of a walk so a
	// field five hops inside a reference still groups under its own table.
	wxString              m_sourceLabel;
};

// The node behind a row, or nullptr when the row carries none (a group header, the placeholder).
inline ibQueryTreeNode* ibQueryNodeOf(wxTreeCtrl* tree, const wxTreeItemId& item)
{
	return tree != nullptr && item.IsOk()
		? dynamic_cast<ibQueryTreeNode*>(tree->GetItemData(item)) : nullptr;
}

// APPEND ONE FIELD ROW. `pathPrefix` is what has been walked so far — empty at the top level, the
// parent's path below it — and the row's own path is the two joined by a dot, which is exactly what
// every verb in the window puts into the AST.
inline void ibQueryAddFieldNode(wxTreeCtrl* tree, const wxTreeItemId& parent,
                                const ibQueryConstructorField& field, int icon,
                                int sourceIndex = -1,
                                const wxString& pathPrefix = wxEmptyString,
                                const std::vector<wxString>& tablePath = std::vector<wxString>(),
                                const wxString& sourceLabel = wxEmptyString)
{
	if (tree == nullptr)
		return;

	const wxString path = pathPrefix.IsEmpty() ? field.m_name : pathPrefix + wxT(".") + field.m_name;

	ibQueryTreeNode* node = new ibQueryTreeNode(sourceIndex, path);
	node->m_referenceClsid = field.m_referenceClsid;
	node->m_path           = tablePath;   // the table this field came out of — carried all the way down
	// At the TOP level the field itself says which source it came from; below it, the walk inherits
	// what its parent carried, because the model has no way of knowing that from a clsid alone.
	node->m_sourceLabel    = sourceLabel.IsEmpty() ? field.m_source : sourceLabel;
	const wxTreeItemId item = tree->AppendItem(parent, field.m_presentation, icon, icon, node);

	// A PLACEHOLDER, so the [+] is there to click. wxTreeCtrl shows one only for a node that has
	// children, and the children are not known until somebody asks.
	if (field.m_referenceClsid != 0)
		tree->AppendItem(item, wxT("..."));
}

// THE DOT WALK, answered when the [+] is clicked. `Supplier.Region.Country` has always parsed and
// always run (ExpandDotWalkJoins in the lowering); what was missing was a way to SAY it without
// typing it, so a reference sat in every field tree as a dead leaf. Unfolding one asks the model
// what is behind it — which the model answers from the reference's own clsid, no lookup table.
inline void ibQueryExpandFieldNode(wxTreeCtrl* tree, const wxTreeItemId& item,
                                   const ibQueryConstructorModel& model, int icon)
{
	ibQueryTreeNode* node = ibQueryNodeOf(tree, item);
	if (node == nullptr || node->m_referenceClsid == 0 || node->m_expanded)
		return;

	node->m_expanded = true;
	tree->DeleteChildren(item);   // the placeholder that made the [+] appear

	// The walk inherits the SOURCE it started from — a field five hops in still belongs to the table
	// it hangs off, and the trees group by that.
	for (const ibQueryConstructorField& behind
	     : model.GetReferenceFields(node->m_referenceClsid, node->m_sourceLabel))
		ibQueryAddFieldNode(tree, item, behind, icon, node->m_sourceIndex, node->m_field, node->m_path,
		                    node->m_sourceLabel);
}

// ⚠ THE DRAG IS RUN BY US, and the event must NOT be Allow()ed. Allowing it starts wx's own drag as
// well, and the two collide inside wxDragImage::BeginDrag with an assert. The source window is the
// TREE — passing the dialog produced the same assert.
inline void ibQueryBeginTextDrag(wxTreeCtrl* tree, const wxTreeItemId& item, const wxString& text)
{
	if (tree == nullptr || text.IsEmpty())
		return;
	if (item.IsOk())
		tree->SelectItem(item);
	wxTextDataObject payload(text);
	wxDropSource drag(payload, tree);
	drag.DoDragDrop(wxDrag_CopyOnly);
}

#endif
