#include "backend/composition/listFilter.h"
#include "backend/composition/dataComposer.h"
#include "backend/model.h"             // ibValueModel::GetModelComposer — the FACADE resolves it lazily
#include "backend/compiler/typeCtor.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/metadataConfiguration.h"   // activeMetaData + ibMetaData::Deserialize — the DOOR for configuration types
#include "backend/query/queryRender.h"   // ibRenderQueryExpr — the AST writes itself out, brackets and all

#include <algorithm>   // std::sort — GroupChildren keeps the chosen lines in order

// READING GOES THROUGH THE DOOR, not straight to the mechanism.
// ibValue::FromNode only knows the process-wide value registry; a filter holds
// CONFIGURATION types too — a reference to a document, an enum member — and those
// exist only in the metadata's own registry. Asking the value factory for one
// raises "Unknown value type '<id>' in the data" on a filter that was saved
// perfectly well. ibMetaData::Deserialize IS that door: it creates what only a
// configuration has and redirects everything else to the same mechanism.
static ibValue ibReadFilterValue(const ibDataNode& node) {
	if (const ibMetaData* metaData = activeMetaData)
		return metaData->Deserialize(node);
	return ibValue::FromNode(node);   // no configuration open (tests, headless tools)
}

// ===========================================================================
//  ibValueFilterItem
// ===========================================================================

ibValueFilterItem::ibValueFilterItem()
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false),
	  m_use(true), m_left(new ibValueCompositionField()), m_comparison(ibComparisonKind_Equal) {
	m_members.Bind(this, &ibValueFilterItem::FillMembers);
}

ibValueFilterItem::ibValueFilterItem(const wxString& field, ibComparisonKind comparison, const ibValue& value, bool use)
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false),
	  m_use(use), m_left(new ibValueCompositionField(field)), m_comparison(comparison), m_right(value) {
	m_members.Bind(this, &ibValueFilterItem::FillMembers);
}

ibValueFilterItem::ibValueFilterItem(const ibValue& left, ibComparisonKind comparison, const ibValue& right, bool use)
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false),
	  m_use(use), m_left(left), m_comparison(comparison), m_right(right) {
	m_members.Bind(this, &ibValueFilterItem::FillMembers);
}

// ASKING is ConvertToValue, not ConvertToType. `ConvertToType` is an ASSERTION —
// it raises "variable type does not support this operation" when the value is
// something else, and it stays silent only in the Designer (value_cast.cpp), so
// a blind cast here reads as working right up until the Enterprise runs it.
// A side that holds a plain number is not an error; it is the other half of the
// answer. Same question, same door, everywhere below.
ibValueCompositionField* ibValueFilterItem::GetLeftField() const {
	ibValueCompositionField* field = nullptr;
	return m_left.ConvertToValue(field) ? field : nullptr;
}

ibValueCompositionField* ibValueFilterItem::GetRightField() const {
	ibValueCompositionField* field = nullptr;
	return m_right.ConvertToValue(field) ? field : nullptr;
}

ibValueCompositionField* ibValueFilterItem::GetFieldObject() const {
	return GetLeftField();
}

wxString ibValueFilterItem::GetField() const {
	// A left side that is not a field has no path — and a caller that speaks in
	// paths (the composer) skips it rather than inventing one.
	const ibValueCompositionField* field = GetLeftField();
	return field != nullptr ? field->GetPath() : wxString();
}

void ibValueFilterItem::SetField(ibValueCompositionField* field) {
	// NEVER NULL on the left: a line with no field chosen is a line with an EMPTY
	// field, so the dialog has something to show and a script has something to set
	// a path on.
	m_left = field != nullptr ? ibValue(field) : ibValue(new ibValueCompositionField());
}

ibMetaID ibValueFilterItem::GetLeafId() const {
	const ibValueCompositionField* field = GetLeftField();
	return field != nullptr ? field->GetLeafId() : wxNOT_FOUND;
}

const ibTypeDescription& ibValueFilterItem::GetTypeDescription() const {
	static const ibTypeDescription s_none;
	const ibValueCompositionField* field = GetLeftField();
	return field != nullptr ? field->GetTypeDescription() : s_none;
}

void ibValueFilterItem::SetTypeInfo(const ibMetaID& leafId, const ibTypeDescription& typeDesc) {
	if (ibValueCompositionField* field = GetLeftField())
		field->SetTypeInfo(leafId, typeDesc);
}

ibTypeDescription ibValueFilterItem::GetRightTypeDescription() const {
	// A FIELD on the left lends its own type — that is what makes the right-hand
	// cell open a quick choice for a reference and a two-item drop-down for a
	// boolean, through the machinery that already does this everywhere else.
	if (const ibValueCompositionField* field = GetLeftField()) {
		if (field->GetTypeDescription().GetClsidCount() > 0)
			return field->GetTypeDescription();
		return ibTypeDescription();   // chosen, but not bound to a source yet
	}

	// A PLAIN VALUE on the left lends the type it already is: `True = True` is
	// then two drop-downs rather than two text boxes.
	if (!m_left.IsEmpty())
		return ibTypeDescription(m_left.GetClassType());

	return ibTypeDescription();
}

void ibValueFilterItem::FillMembers(ibMemberTable& helper) const {
	helper.AppendProp(wxT("Use"));
	helper.AppendProp(wxT("Left"));
	helper.AppendProp(wxT("Comparison"));
	helper.AppendProp(wxT("Right"));
	helper.AppendProp(wxT("DisplayMode"));
	helper.AppendProp(wxT("Presentation"));
}

bool ibValueFilterItem::Init(ibValue** paParams, const long lSizeArray) {
	// New FilterItem(left, comparison, right [, use])
	//
	// The left side may arrive as a FIELD, as a PATH, or as a plain value. A
	// picker hands over the field it built (with its type and presentation); a
	// script usually names the path; `True = True` passes a value.
	if (lSizeArray >= 1 && paParams[0] != nullptr) {
		if (paParams[0]->GetType() == ibValueTypes::TYPE_STRING)
			m_left = new ibValueCompositionField(paParams[0]->GetString());
		else
			m_left = *paParams[0];
	}
	if (lSizeArray >= 2 && paParams[1] != nullptr)
		m_comparison = paParams[1]->ConvertToEnumValue<ibComparisonKind>();
	if (lSizeArray >= 3 && paParams[2] != nullptr)
		m_right = *paParams[2];
	if (lSizeArray >= 4 && paParams[3] != nullptr)
		m_use = paParams[3]->GetBoolean();
	return true;
}

bool ibValueFilterItem::GetPropVal(const long lPropNum, ibValue& pvarPropVal) {
	switch (lPropNum) {
	case enUse:          pvarPropVal = m_use; return true;
	case enLeft:         pvarPropVal = m_left; return true;
	case enComparison:   pvarPropVal = ibValue::CreateAndConvertEnumObjectRef<ibValueEnumComparisonKind>(m_comparison); return true;
	case enRight:        pvarPropVal = m_right; return true;
	case enDisplayMode:  pvarPropVal = ibValue::CreateAndConvertEnumObjectRef<ibValueEnumFilterDisplayMode>(m_displayMode); return true;
	case enPresentation: pvarPropVal = GetString(); return true;
	}
	return false;
}

bool ibValueFilterItem::SetPropVal(const long lPropNum, const ibValue& varPropVal) {
	switch (lPropNum) {
	case enUse:  m_use = varPropVal.GetBoolean(); return true;
	case enLeft:
		// A STRING on the left names a field — that is what a path is. Any other
		// value is taken as itself, which is what makes `True = True` expressible.
		if (varPropVal.GetType() == ibValueTypes::TYPE_STRING)
			m_left = new ibValueCompositionField(varPropVal.GetString());
		else
			m_left = varPropVal;
		return true;
	case enComparison:   m_comparison = varPropVal.ConvertToEnumValue<ibComparisonKind>(); return true;
	case enRight:        m_right = varPropVal; return true;
	case enDisplayMode:  m_displayMode = varPropVal.ConvertToEnumValue<ibFilterDisplayMode>(); return true;
	case enPresentation: m_presentation = varPropVal.GetString(); return true;
	}
	return false;
}

wxString ibValueFilterItem::GetString() const {
	// The user's own label wins — they wrote it because the generated one did not
	// say what they meant.
	if (!m_presentation.IsEmpty())
		return m_presentation;

	wxString text = m_left.GetString() + wxT(" ") + ComparisonKindToOp(m_comparison);
	if (!m_right.IsEmpty())
		text += wxT(" ") + m_right.GetString();
	return text;
}

bool ibValueFilterItem::DoSerialize(ibDataNode& node) const {
	node.SetValue(wxT("u"), m_use);
	node.SetValue(wxT("c"), (s32)m_comparison);
	node.SetValue(wxT("d"), (s32)m_displayMode);
	node.SetValue(wxT("n"), m_presentation);

	// BOTH SIDES AS CHILDREN, in order: left, right. Each packs ITSELF — a field
	// writes its path and presentation, a number writes a number — so this never
	// learns what either side is.
	if (!m_left.Serialize(node.AddChild(m_left.GetClassType(), 0)))
		return false;
	return m_right.Serialize(node.AddChild(m_right.GetClassType(), 0));
}

bool ibValueFilterItem::DoDeserialize(const ibDataNode& node) {
	m_use = node.GetValue<bool>(wxT("u"));
	m_comparison = (ibComparisonKind)node.GetValue<s32>(wxT("c"));
	m_displayMode = (ibFilterDisplayMode)node.GetValue<s32>(wxT("d"));
	m_presentation = node.GetValue<wxString>(wxT("n"));

	const std::vector<ibDataNode>& children = node.Children();
	// A condition has TWO sides or it is not one. Fewer means the record was
	// written by something else — refuse rather than restore half a comparison.
	if (children.size() < 2)
		return false;

	m_left = ibReadFilterValue(children[0]);
	m_right = ibReadFilterValue(children[1]);
	return true;
}

// ===========================================================================
//  ibValueFilterGroup — the branch node of the filter tree
// ===========================================================================

ibValueFilterGroup::ibValueFilterGroup()
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false) {
	m_members.Bind(this, &ibValueFilterGroup::FillMembers);
}

ibValueFilterGroup::ibValueFilterGroup(ibFilterGroupKind kind, bool use)
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false), m_use(use), m_kind(kind) {
	m_members.Bind(this, &ibValueFilterGroup::FillMembers);
}

void ibValueFilterGroup::FillMembers(ibMemberTable& helper) const {
	helper.AppendConstructor(2, wxT("FilterGroup(kind? : FilterGroupKind, use? : bool)"));

	helper.AppendFunc(wxT("Add"), 4, wxT("Add(left : any, comparison : ComparisonKind, right : any, use? : bool)"));
	helper.AppendFunc(wxT("AddGroup"), 1, wxT("AddGroup(kind? : FilterGroupKind)"));
	helper.AppendFunc(wxT("Count"), wxT("Count()"));
	helper.AppendFunc(wxT("Get"), 1, wxT("Get(index : number)"));
	helper.AppendProc(wxT("Remove"), 1, wxT("Remove(index : number)"));
	helper.AppendProc(wxT("Clear"), wxT("Clear()"));

	helper.AppendProp(wxT("Use"));
	helper.AppendProp(wxT("Kind"));
	helper.AppendProp(wxT("DisplayMode"));
	helper.AppendProp(wxT("Presentation"));
}

bool ibValueFilterGroup::Init(ibValue** paParams, const long lSizeArray) {
	// New FilterGroup([kind [, use]])
	if (lSizeArray >= 1 && paParams[0] != nullptr)
		m_kind = paParams[0]->ConvertToEnumValue<ibFilterGroupKind>();
	if (lSizeArray >= 2 && paParams[1] != nullptr)
		m_use = paParams[1]->GetBoolean();
	return true;
}

ibValue ibValueFilterGroup::GetChild(size_t idx) const {
	return idx < m_children.size() ? m_children[idx] : ibValue();
}

// "Is this child a condition?" is a QUESTION — a group child answering no is the
// normal case, not a type error.
ibValueFilterItem* ibValueFilterGroup::GetItem(size_t idx) const {
	ibValueFilterItem* item = nullptr;
	return idx < m_children.size() && m_children[idx].ConvertToValue(item) ? item : nullptr;
}

ibValueFilterGroup* ibValueFilterGroup::GetGroup(size_t idx) const {
	ibValueFilterGroup* group = nullptr;
	return idx < m_children.size() && m_children[idx].ConvertToValue(group) ? group : nullptr;
}

ibValueFilterItem* ibValueFilterGroup::Add(const ibValue& left, ibComparisonKind comparison, const ibValue& right, bool use) {
	ibValueFilterItem* item = new ibValueFilterItem(left, comparison, right, use);
	m_children.push_back(ibValue(item));
	return item;
}

ibValueFilterGroup* ibValueFilterGroup::AddGroup(ibFilterGroupKind kind) {
	ibValueFilterGroup* group = new ibValueFilterGroup(kind);
	m_children.push_back(ibValue(group));
	return group;
}

void ibValueFilterGroup::Append(const ibValue& child) {
	m_children.push_back(child);
}

void ibValueFilterGroup::Remove(size_t idx) {
	if (idx < m_children.size())
		m_children.erase(m_children.begin() + idx);
}

size_t ibValueFilterGroup::IndexOf(const ibValue& child) const {
	for (size_t i = 0; i < m_children.size(); ++i) {
		// By the VALUE OBJECT: two lines can read identically and still be two
		// different lines.
		if (m_children[i].GetRef() == child.GetRef())
			return i;
	}
	return m_children.size();
}

size_t ibValueFilterGroup::MoveChild(size_t idx, int delta) {
	if (idx >= m_children.size())
		return idx;
	const int target = (int)idx + delta;
	if (target < 0 || target >= (int)m_children.size())
		return idx;   // already at that end — nothing to do, and nothing to report

	std::swap(m_children[idx], m_children[(size_t)target]);
	return (size_t)target;
}

ibValueFilterGroup* ibValueFilterGroup::GroupChildren(const std::vector<size_t>& indexes, ibFilterGroupKind kind) {
	if (indexes.empty())
		return nullptr;

	// IN PLACE OF THE FIRST. The chosen lines keep their position relative to
	// everything else — grouping re-shapes the logic, it does not shuffle the
	// list, and a user who groups two middle lines expects the group to sit where
	// they were.
	std::vector<size_t> sorted = indexes;
	std::sort(sorted.begin(), sorted.end());
	if (sorted.back() >= m_children.size())
		return nullptr;

	ibValuePtr<ibValueFilterGroup> group(new ibValueFilterGroup(kind));
	for (size_t idx : sorted)
		group->Append(m_children[idx]);

	// Erase from the back so the earlier indexes stay valid.
	const size_t insertAt = sorted.front();
	for (auto it = sorted.rbegin(); it != sorted.rend(); ++it)
		m_children.erase(m_children.begin() + *it);

	m_children.insert(m_children.begin() + insertAt, ibValue(group));
	return group;
}

bool ibValueFilterGroup::UngroupChild(size_t idx) {
	ibValueFilterGroup* group = GetGroup(idx);
	if (group == nullptr)
		return false;

	// The children take the group's place, in order — the inverse of grouping,
	// and for the same reason: the logic changes, the reading order does not.
	std::vector<ibValue> lifted;
	for (size_t i = 0; i < group->Count(); ++i)
		lifted.push_back(group->GetChild(i));

	m_children.erase(m_children.begin() + idx);
	m_children.insert(m_children.begin() + idx, lifted.begin(), lifted.end());
	return true;
}

bool ibValueFilterGroup::DoSerialize(ibDataNode& node) const {
	node.SetValue(wxT("u"), m_use);
	node.SetValue(wxT("k"), (s32)m_kind);
	node.SetValue(wxT("d"), (s32)m_displayMode);
	node.SetValue(wxT("n"), m_presentation);

	// CHILDREN PACK THEMSELVES — a condition or a nested group, asked the same
	// question, so depth costs nothing here.
	for (const ibValue& child : m_children) {
		if (!child.Serialize(node.AddChild(child.GetClassType(), 0)))
			return false;
	}
	return true;
}

bool ibValueFilterGroup::DoDeserialize(const ibDataNode& node) {
	m_use = node.GetValue<bool>(wxT("u"));
	m_kind = (ibFilterGroupKind)node.GetValue<s32>(wxT("k"));
	m_displayMode = (ibFilterDisplayMode)node.GetValue<s32>(wxT("d"));
	m_presentation = node.GetValue<wxString>(wxT("n"));

	m_children.clear();
	for (const ibDataNode& child : node.Children())
		m_children.push_back(ibReadFilterValue(child));
	return true;
}

bool ibValueFilterGroup::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) {
	switch (lMethodNum) {
	case enAdd:
		pvarRetValue = Add(lSizeArray > 0 && paParams[0] != nullptr ? *paParams[0] : ibValue(),
			lSizeArray > 1 && paParams[1] != nullptr ? paParams[1]->ConvertToEnumValue<ibComparisonKind>() : ibComparisonKind_Equal,
			lSizeArray > 2 && paParams[2] != nullptr ? *paParams[2] : ibValue(),
			lSizeArray > 3 && paParams[3] != nullptr ? paParams[3]->GetBoolean() : true);
		return true;
	case enAddGroup:
		pvarRetValue = AddGroup(lSizeArray > 0 && paParams[0] != nullptr
			? paParams[0]->ConvertToEnumValue<ibFilterGroupKind>() : ibFilterGroupKind_And);
		return true;
	case enCount:
		pvarRetValue = (unsigned int)Count();
		return true;
	case enGet:
		pvarRetValue = GetChild(lSizeArray > 0 && paParams[0] != nullptr ? paParams[0]->GetUInteger() : 0);
		return true;
	}
	return false;
}

bool ibValueFilterGroup::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) {
	switch (lMethodNum) {
	case enRemove:
		if (lSizeArray > 0 && paParams[0] != nullptr)
			Remove(paParams[0]->GetUInteger());
		return true;
	case enClear:
		Clear();
		return true;
	}
	return false;
}

bool ibValueFilterGroup::GetPropVal(const long lPropNum, ibValue& pvarPropVal) {
	switch (lPropNum) {
	case enUse:          pvarPropVal = m_use; return true;
	case enKind:         pvarPropVal = ibValue::CreateAndConvertEnumObjectRef<ibValueEnumFilterGroupKind>(m_kind); return true;
	case enDisplayMode:  pvarPropVal = ibValue::CreateAndConvertEnumObjectRef<ibValueEnumFilterDisplayMode>(m_displayMode); return true;
	case enPresentation: pvarPropVal = GetString(); return true;
	}
	return false;
}

bool ibValueFilterGroup::SetPropVal(const long lPropNum, const ibValue& varPropVal) {
	switch (lPropNum) {
	case enUse:          m_use = varPropVal.GetBoolean(); return true;
	case enKind:         m_kind = varPropVal.ConvertToEnumValue<ibFilterGroupKind>(); return true;
	case enDisplayMode:  m_displayMode = varPropVal.ConvertToEnumValue<ibFilterDisplayMode>(); return true;
	case enPresentation: m_presentation = varPropVal.GetString(); return true;
	}
	return false;
}

wxString ibValueFilterGroup::GetString() const {
	if (!m_presentation.IsEmpty())
		return m_presentation;

	// THE OPERATOR, AND ONLY THE OPERATOR. The cell this lands in is the group's
	// operator cell, so the word "group" in it said what the row already showed by
	// being a row — and it read as "group this WITH the next line", which is the
	// one thing an operator does not mean: it joins the group's own children.
	switch (m_kind) {
	case ibFilterGroupKind_Or:  return _("Or");
	case ibFilterGroupKind_Not: return _("Not");
	default: break;
	}
	return _("And");
}

// ===========================================================================
//  ibValueFilterList
// ===========================================================================

ibValueFilterList::ibValueFilterList(ibValueFilterGroup* root)
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false), m_root(root) {
	// NO ROOT GIVEN — stand up your own. A `New FilterList()` from a script has no
	// settings behind it, and a list with nowhere to put a condition would accept
	// Add() and keep nothing.
	if (m_root == nullptr) {
		m_ownRoot = new ibValueFilterGroup();
		m_root = m_ownRoot;
	}
	m_members.Bind(this, &ibValueFilterList::FillMembers);
}

ibValueFilterList::ibValueFilterList(ibValueFilterGroup* root, ibValueModel& model, std::function<void()> onChange)
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false),
	  m_root(root), m_model(&model), m_onChange(std::move(onChange)) {
	m_members.Bind(this, &ibValueFilterList::FillMembers);
}

// FACADE: the model's composer is polymorphic + lazily created — resolve it on demand (by first USE the model
// is fully constructed, so GetModelComposer picks the right realisation). null model = BUFFER mode.
ibDataComposer* ibValueFilterList::Composer() const { return m_model != nullptr ? &m_model->GetModelComposer() : nullptr; }

void ibValueFilterList::FillMembers(ibMemberTable& helper) const {
	helper.AppendFunc(wxT("Add"),   3, wxT("Add(field, comparison, value)"));
	helper.AppendFunc(wxT("Count"),    wxT("Count()"));
	helper.AppendFunc(wxT("Get"),   1, wxT("Get(index)"));
	helper.AppendProc(wxT("Clear"));
}

bool   ibValueFilterList::IsEmpty() const { return Count() == 0; }
size_t ibValueFilterList::Count() const { return m_root != nullptr ? m_root->Count() : 0; }

// THE WHOLE TREE, RE-STATED. Not "append one more condition to the composer":
// after a Clear there is nothing to append, and the composer would happily keep
// the filters nobody asked it to forget. Rebuilding from the tree is the only
// form that makes an emptied filter actually empty.
void ibValueFilterList::ApplyToComposer() {
	ibDataComposer* c = Composer();
	if (c == nullptr)
		return;
	c->ClearFilters();
	c->FilterAst(ibBuildFilterAst(*c, m_root));
	if (m_onChange) m_onChange();
}

void ibValueFilterList::Clear() {
	if (m_root != nullptr)
		m_root->Clear();
	ApplyToComposer();
}

ibValueFilterItem* ibValueFilterList::Add(const wxString& field, ibComparisonKind comparison, const ibValue& value, bool use) {
	if (m_root == nullptr)
		return nullptr;
	// A CONDITION ADDED THROUGH THE FLAT DOOR lands in the root group — the same
	// place the dialog's top-level lines live, because it is the same filter.
	ibValueFilterItem* item = m_root->Add(
		ibValue(new ibValueCompositionField(field)), comparison, value, use);
	ApplyToComposer();
	return item;
}

ibValueFilterItem* ibValueFilterList::Add(ibValueCompositionField* field, ibComparisonKind comparison, const ibValue& value, bool use) {
	// THE FIELD SURVIVES. Adding by path would rebuild it from its path alone and
	// lose what the picker resolved — the type that makes the value editable and
	// the presentation the user reads.
	if (m_root == nullptr)
		return nullptr;
	ibValueFilterItem* item = m_root->Add(ibValue(field), comparison, value, use);
	ApplyToComposer();
	return item;
}

ibValueFilterItem* ibValueFilterList::GetItem(size_t idx) const {
	// The flat door sees the root's own lines. A condition nested in a group is
	// not reachable from here — it belongs to a shape the flat list cannot spell,
	// and pretending otherwise would hand out lines whose meaning changed on the
	// way out.
	return m_root != nullptr ? m_root->GetItem(idx) : nullptr;
}

bool ibValueFilterList::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) {
	switch (lMethodNum) {
	case enAdd: {
		const wxString field = (lSizeArray >= 1 && paParams[0] != nullptr) ? paParams[0]->GetString() : wxString();
		const ibComparisonKind cmp = (lSizeArray >= 2 && paParams[1] != nullptr)
			? paParams[1]->ConvertToEnumValue<ibComparisonKind>() : ibComparisonKind_Equal;
		const ibValue val = (lSizeArray >= 3 && paParams[2] != nullptr) ? *paParams[2] : ibValue();
		pvarRetValue = Add(field, cmp, val, true);
		return true;
	}
	case enCount:
		pvarRetValue = static_cast<signed int>(Count());
		return true;
	case enGet: {
		const size_t idx = (lSizeArray >= 1 && paParams[0] != nullptr) ? static_cast<size_t>(paParams[0]->GetInteger()) : 0;
		ibValueFilterItem* item = GetItem(idx);
		if (item == nullptr) return false;
		pvarRetValue = item;
		return true;
	}
	}
	return false;
}

bool ibValueFilterList::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) {
	switch (lMethodNum) {
	case enClear: Clear(); return true;
	}
	return false;
}

wxString ibValueFilterList::GetString() const {
	return wxString::Format(wxT("Filter(%u)"), (unsigned)Count());
}

// ===========================================================================
//  ibValueSortItem
// ===========================================================================

ibValueSortItem::ibValueSortItem()
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false),
	  m_direction(ibSortDirection_Ascending) {
	m_members.Bind(this, &ibValueSortItem::FillMembers);
}

ibValueSortItem::ibValueSortItem(const wxString& field, ibSortDirection direction)
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false),
	  m_field(new ibValueCompositionField(field)), m_direction(direction) {
	m_members.Bind(this, &ibValueSortItem::FillMembers);
}

void ibValueSortItem::SetField(ibValueCompositionField* field) {
	m_field = field != nullptr ? field : new ibValueCompositionField();
}

void ibValueSortItem::FillMembers(ibMemberTable& helper) const {
	helper.AppendProp(wxT("Field"));
	helper.AppendProp(wxT("Direction"));
}

bool ibValueSortItem::Init(ibValue** paParams, const long lSizeArray) {
	// New SortItem(field [, direction]) — a FIELD object or the path as text; the
	// second is the common one from a script, so it must not read as a type error.
	ibValueCompositionField* field = nullptr;
	if (lSizeArray >= 1 && paParams[0] != nullptr) {
		if (paParams[0]->ConvertToValue(field))
			SetField(field);
		else
			m_field->SetPath(paParams[0]->GetString());
	}
	if (lSizeArray >= 2 && paParams[1] != nullptr)
		m_direction = paParams[1]->ConvertToEnumValue<ibSortDirection>();
	return true;
}

bool ibValueSortItem::GetPropVal(const long lPropNum, ibValue& pvarPropVal) {
	switch (lPropNum) {
	case enField:     pvarPropVal = ibValue(m_field); return true;
	case enDirection: pvarPropVal = ibValue::CreateAndConvertEnumObjectRef<ibValueEnumSortDirection>(m_direction); return true;
	}
	return false;
}

bool ibValueSortItem::SetPropVal(const long lPropNum, const ibValue& varPropVal) {
	switch (lPropNum) {
	case enField: {
		ibValueCompositionField* field = nullptr;
		if (varPropVal.ConvertToValue(field))
			SetField(field);
		else
			m_field->SetPath(varPropVal.GetString());
		return true;
	}
	case enDirection: m_direction = varPropVal.ConvertToEnumValue<ibSortDirection>(); return true;
	}
	return false;
}

wxString ibValueSortItem::GetString() const {
	return m_field->GetString() + (m_direction == ibSortDirection_Descending ? wxT(" DESC") : wxT(" ASC"));
}

// ===========================================================================
//  ibValueSortList
// ===========================================================================

ibValueSortList::ibValueSortList()
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false) {
	m_members.Bind(this, &ibValueSortList::FillMembers);
}

ibValueSortList::ibValueSortList(ibValueModel& model, std::function<void()> onChange)
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false), m_model(&model), m_onChange(std::move(onChange)) {
	m_members.Bind(this, &ibValueSortList::FillMembers);
}

ibDataComposer* ibValueSortList::Composer() const { return m_model != nullptr ? &m_model->GetModelComposer() : nullptr; }

void ibValueSortList::FillMembers(ibMemberTable& helper) const {
	helper.AppendFunc(wxT("Add"),   2, wxT("Add(field, direction)"));
	helper.AppendFunc(wxT("Count"),    wxT("Count()"));
	helper.AppendFunc(wxT("Get"),   1, wxT("Get(index)"));
	helper.AppendProc(wxT("Clear"));
}

bool   ibValueSortList::IsEmpty() const { return Count() == 0; }
size_t ibValueSortList::Count() const { const ibDataComposer* c = Composer(); return c != nullptr ? c->SortCount() : m_items.size(); }

void ibValueSortList::Clear() {
	if (ibDataComposer* c = Composer()) { c->ClearSorts(); if (m_onChange) m_onChange(); }
	else                                     m_items.clear();
}

// REMOVE AND REORDER, on the same storage Add writes. In FACADE mode the store is
// the composer, which has no per-line remove — so the list re-states the whole
// order after the edit, the same shape the filter uses.
bool ibValueSortList::Remove(size_t idx) {
	if (ibDataComposer* c = Composer()) {
		std::vector<std::pair<wxString, bool>> lines;
		for (size_t i = 0; i < c->SortCount(); ++i) {
			wxString path; bool ascending = true;
			if (c->GetSortAt(i, path, ascending) && i != idx)
				lines.emplace_back(path, ascending);
		}
		c->ClearSorts();
		for (const auto& line : lines)
			c->Sort(line.first, line.second);
		if (m_onChange) m_onChange();
		return true;
	}
	if (idx >= m_items.size())
		return false;
	m_items.erase(m_items.begin() + idx);
	return true;
}

bool ibValueSortList::Move(size_t idx, int delta) {
	if (ibDataComposer* c = Composer()) {
		std::vector<std::pair<wxString, bool>> lines;
		for (size_t i = 0; i < c->SortCount(); ++i) {
			wxString path; bool ascending = true;
			if (c->GetSortAt(i, path, ascending))
				lines.emplace_back(path, ascending);
		}
		const int target = (int)idx + delta;
		if (idx >= lines.size() || target < 0 || target >= (int)lines.size())
			return false;
		std::swap(lines[idx], lines[(size_t)target]);
		c->ClearSorts();
		for (const auto& line : lines)
			c->Sort(line.first, line.second);
		if (m_onChange) m_onChange();
		return true;
	}
	const int target = (int)idx + delta;
	if (idx >= m_items.size() || target < 0 || target >= (int)m_items.size())
		return false;   // already at that end
	std::swap(m_items[idx], m_items[(size_t)target]);
	return true;
}

ibValueSortItem* ibValueSortList::Add(const wxString& field, ibSortDirection direction) {
	// FACADE: write the composer (the store) IMMEDIATELY + fire the model refresh; hand the script a value item.
	if (ibDataComposer* c = Composer()) {
		if (!field.IsEmpty())
			c->Sort(field, direction == ibSortDirection_Ascending);
		if (m_onChange) m_onChange();
		return new ibValueSortItem(field, direction);
	}
	// BUFFER: own storage (the dialog's transactional copy).
	ibValueSortItem* item = new ibValueSortItem(field, direction);
	m_items.push_back(ibValuePtr<ibValueSortItem>(item));
	return item;
}

ibValueSortItem* ibValueSortList::GetItem(size_t idx) const {
	// FACADE: build a transient item reflecting the composer's sort (script-owned). BUFFER: the stored item.
	if (const ibDataComposer* c = Composer()) {
		if (idx >= c->SortCount()) return nullptr;
		wxString path; bool ascending = true;
		if (!c->GetSortAt(idx, path, ascending)) return nullptr;
		return new ibValueSortItem(path, ascending ? ibSortDirection_Ascending : ibSortDirection_Descending);
	}
	return idx < m_items.size() ? static_cast<ibValueSortItem*>(m_items[idx]) : nullptr;
}

bool ibValueSortList::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) {
	switch (lMethodNum) {
	case enAdd: {
		const wxString field = (lSizeArray >= 1 && paParams[0] != nullptr) ? paParams[0]->GetString() : wxString();
		const ibSortDirection dir = (lSizeArray >= 2 && paParams[1] != nullptr)
			? paParams[1]->ConvertToEnumValue<ibSortDirection>() : ibSortDirection_Ascending;
		pvarRetValue = Add(field, dir);
		return true;
	}
	case enCount:
		pvarRetValue = static_cast<signed int>(Count());
		return true;
	case enGet: {
		const size_t idx = (lSizeArray >= 1 && paParams[0] != nullptr) ? static_cast<size_t>(paParams[0]->GetInteger()) : 0;
		ibValueSortItem* item = GetItem(idx);
		if (item == nullptr) return false;
		pvarRetValue = item;
		return true;
	}
	}
	return false;
}

bool ibValueSortList::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) {
	switch (lMethodNum) {
	case enClear: Clear(); return true;
	}
	return false;
}

wxString ibValueSortList::GetString() const {
	return wxString::Format(wxT("Order(%u)"), (unsigned)Count());
}

// ===========================================================================
//  ibValueGroupList
// ===========================================================================

ibValueGroupList::ibValueGroupList()
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false) {
	m_members.Bind(this, &ibValueGroupList::FillMembers);
}

ibValueGroupList::ibValueGroupList(ibValueModel& model, std::function<void()> onChange)
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false), m_model(&model), m_onChange(std::move(onChange)) {
	m_members.Bind(this, &ibValueGroupList::FillMembers);
}

ibDataComposer* ibValueGroupList::Composer() const { return m_model != nullptr ? &m_model->GetModelComposer() : nullptr; }

void ibValueGroupList::FillMembers(ibMemberTable& helper) const {
	helper.AppendFunc(wxT("Add"),   1, wxT("Add(field)"));
	helper.AppendFunc(wxT("Count"),    wxT("Count()"));
	helper.AppendFunc(wxT("Get"),   1, wxT("Get(index)"));
	helper.AppendProc(wxT("Clear"));
}

bool   ibValueGroupList::IsEmpty() const { return Count() == 0; }
size_t ibValueGroupList::Count() const { const ibDataComposer* c = Composer(); return c != nullptr ? c->GroupCount() : m_items.size(); }

wxString ibValueGroupList::GetField(size_t idx) const {
	if (const ibDataComposer* c = Composer()) {
		wxString f; ibQueryDimUnfold k = ibQueryDimUnfold::Elements;
		return c->GetGroupAt(idx, f, k) ? f : wxString();
	}
	return idx < m_items.size() ? m_items[idx].m_field->GetPath() : wxString();
}

ibValueCompositionField* ibValueGroupList::GetFieldObject(size_t idx) const {
	// FACADE mode has no stored field — the composer keeps a PATH. It used to mint a
	// new field here on every call and hand back a raw pointer nobody owned: the
	// cells ask this while painting, so it leaked once per repaint. A caller that
	// needs the path in that mode asks GetField(idx), which is what the presentation
	// falls back to anyway.
	if (Composer() != nullptr)
		return nullptr;
	return idx < m_items.size() ? static_cast<ibValueCompositionField*>(m_items[idx].m_field) : nullptr;
}

ibQueryDimUnfold ibValueGroupList::GetKind(size_t idx) const {
	if (const ibDataComposer* c = Composer()) {
		wxString f; ibQueryDimUnfold k = ibQueryDimUnfold::Elements;
		return c->GetGroupAt(idx, f, k) ? k : ibQueryDimUnfold::Elements;
	}
	return idx < m_items.size() ? m_items[idx].m_kind : ibQueryDimUnfold::Elements;
}

void ibValueGroupList::Add(const wxString& field, ibQueryDimUnfold kind) {
	// FACADE: write the composer's grouping (the store) IMMEDIATELY + fire the model refresh. BUFFER: own storage.
	if (ibDataComposer* c = Composer()) {
		if (!field.IsEmpty())
			c->TotalBy(field, kind);
		if (m_onChange) m_onChange();
	}
	else
		m_items.push_back({ ibValuePtr<ibValueCompositionField>(new ibValueCompositionField(field)), kind });
}

void ibValueGroupList::Add(ibValueCompositionField* field, ibQueryDimUnfold kind) {
	// BY FIELD — keeps what the picker resolved (the type, the presentation)
	// instead of rebuilding a bare field from its path.
	if (field == nullptr)
		return;

	if (ibDataComposer* c = Composer()) {
		if (!field->GetPath().IsEmpty())
			c->TotalBy(field->GetPath(), kind);
		if (m_onChange) m_onChange();
		return;
	}
	m_items.push_back({ ibValuePtr<ibValueCompositionField>(field), kind });
}

void ibValueGroupList::Clear() {
	if (ibDataComposer* c = Composer()) { c->ClearGroups(); if (m_onChange) m_onChange(); }
	else                                     m_items.clear();
}

bool ibValueGroupList::Remove(size_t idx) {
	if (ibDataComposer* c = Composer()) {
		std::vector<std::pair<wxString, ibQueryDimUnfold>> lines;
		for (size_t i = 0; i < c->GroupCount(); ++i) {
			wxString path; ibQueryDimUnfold kind = ibQueryDimUnfold::Elements;
			if (c->GetGroupAt(i, path, kind) && i != idx)
				lines.emplace_back(path, kind);
		}
		c->ClearGroups();
		for (const auto& line : lines)
			c->TotalBy(line.first, line.second);
		if (m_onChange) m_onChange();
		return true;
	}
	if (idx >= m_items.size())
		return false;
	m_items.erase(m_items.begin() + idx);
	return true;
}

bool ibValueGroupList::Move(size_t idx, int delta) {
	if (ibDataComposer* c = Composer()) {
		std::vector<std::pair<wxString, ibQueryDimUnfold>> lines;
		for (size_t i = 0; i < c->GroupCount(); ++i) {
			wxString path; ibQueryDimUnfold kind = ibQueryDimUnfold::Elements;
			if (c->GetGroupAt(i, path, kind))
				lines.emplace_back(path, kind);
		}
		const int target = (int)idx + delta;
		if (idx >= lines.size() || target < 0 || target >= (int)lines.size())
			return false;
		std::swap(lines[idx], lines[(size_t)target]);
		c->ClearGroups();
		for (const auto& line : lines)
			c->TotalBy(line.first, line.second);
		if (m_onChange) m_onChange();
		return true;
	}
	const int target = (int)idx + delta;
	if (idx >= m_items.size() || target < 0 || target >= (int)m_items.size())
		return false;
	std::swap(m_items[idx], m_items[(size_t)target]);
	return true;
}

bool ibValueGroupList::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) {
	switch (lMethodNum) {
	case enAdd: {
		const wxString field = (lSizeArray >= 1 && paParams[0] != nullptr) ? paParams[0]->GetString() : wxString();
		Add(field);
		pvarRetValue = field;
		return true;
	}
	case enCount:
		pvarRetValue = static_cast<signed int>(Count());
		return true;
	case enGet: {
		const size_t idx = (lSizeArray >= 1 && paParams[0] != nullptr) ? static_cast<size_t>(paParams[0]->GetInteger()) : 0;
		if (idx >= Count()) return false;
		pvarRetValue = GetField(idx);
		return true;
	}
	}
	return false;
}

bool ibValueGroupList::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) {
	switch (lMethodNum) {
	case enClear: Clear(); return true;
	}
	return false;
}

wxString ibValueGroupList::GetString() const {
	return wxString::Format(wxT("Group(%u)"), (unsigned)Count());
}

// ===========================================================================
//  ibValueListSettings
// ===========================================================================

ibValueListSettings::ibValueListSettings()
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false) {
	m_members.Bind(this, &ibValueListSettings::FillMembers);
	// THE ROOT FIRST — the flat Filter door is a view onto it, not a second store.
	m_filterRoot = new ibValueFilterGroup();
	m_filter = new ibValueFilterList(m_filterRoot);
	m_order  = new ibValueSortList();
	m_group  = new ibValueGroupList();
}

// FACADE container — Filter / Order / Group are thin live wrappers over `composer` (the store); each mutation
// fires `onChange` (the owning model's refresh). The model owns ONE of these as its runtime/script settings.
ibValueListSettings::ibValueListSettings(ibValueModel& model, std::function<void()> onChange)
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, false) {
	m_members.Bind(this, &ibValueListSettings::FillMembers);
	m_filterRoot = new ibValueFilterGroup();
	m_filter = new ibValueFilterList(m_filterRoot, model, onChange);
	m_order  = new ibValueSortList(model, onChange);
	m_group  = new ibValueGroupList(model, onChange);
}

void ibValueListSettings::FillMembers(ibMemberTable& helper) const {
	helper.AppendProp(wxT("Filter"), true, false, wxNOT_FOUND);   // read-only collections
	helper.AppendProp(wxT("Order"),  true, false, wxNOT_FOUND);
	helper.AppendProp(wxT("Group"),  true, false, wxNOT_FOUND);
}

bool ibValueListSettings::GetPropVal(const long lPropNum, ibValue& pvarPropVal) {
	switch (lPropNum) {
	case enFilter: pvarPropVal = GetFilter(); return true;
	case enOrder:  pvarPropVal = GetOrder();  return true;
	case enGroup:  pvarPropVal = GetGroup();  return true;
	}
	return false;
}

// Object-level node save/load — called by the dynamic list's ReadData/WriteData so the
// settings persist on the form. TODO(spike): (de)serialize Filter/Order/Group items as
// child nodes — the seam is here; item-level round-trip is the remaining harden.
// ⚠ SORT AND GROUP ARE READ AND WRITTEN THROUGH THE FACADE (Count / Get… / Add), never off the
// buffer fields — and that is the whole of the defect this replaced.
//
// These lists have TWO modes. In BUFFER mode (the settings dialog) the lines live in m_items; in
// FACADE mode (a live model) they live in the COMPOSER, which is the store, and m_items is empty.
// Write used to serialise the filter tree and nothing else, so a sort or a grouping set on a live
// list had nothing to write and simply never reached the disk — set it, save, reopen, gone.
//
// Asking the facade makes one door for both modes: the dialog's buffer and the model's composer
// answer the same three questions, and neither is named here.
//
// ⚠ AND THE LINES ARE DATA, NOT OBJECTS. The filter packs itself because a filter TREE is an object
// in both modes; a sort line is not — GetFieldObject deliberately returns null in facade mode (it
// used to mint one per repaint and leak it). So a line travels as what it IS: a path plus its
// direction, a path plus its unfold kind. Nothing has to exist for it to be written.
namespace {

const wxChar* const kOrderNode = wxT("Order");
const wxChar* const kGroupNode = wxT("Group");
const wxChar* const kFieldName = wxT("Field");
const wxChar* const kDirName   = wxT("Ascending");
const wxChar* const kKindName  = wxT("Kind");

} // namespace

bool ibValueListSettings::ReadData(const ibDataNode& node)
{
	// THE FILTER TREE is the only METAOBJECT child here — it packs itself, so it goes into the child
	// list. Sort and grouping are NAMED children (ibDataNode::Child / FindChild), which live in the
	// PROPERTIES area and never appear in Children() at all.
	//
	// ⚠ That is what makes this scan safe, and it is worth writing down because it looks unsafe: the
	// worry was that once sort and grouping lie beside the filter, "the first child that converts"
	// becomes a lottery. They do not lie beside it — they are somewhere else entirely. Matching on
	// the child's clsid instead was tried and it matched nothing: a value writes its type INTO the
	// node when it serialises, and the node's own chunk id is not the concrete value's.
	//
	// A settings record written before a part existed simply has no such child, and that part reads
	// back empty — which is what it was.
	// ⚠⚠ THE VALUE IS HELD IN A NAMED LOCAL, and that is not style — it is the difference between
	// working and a use-after-free.
	//
	// `ConvertToValue` hands back a RAW pointer INTO the value. Written as
	// `if (ibReadFilterValue(child).ConvertToValue(root))` the ibValue is a TEMPORARY: it dies at the
	// end of the condition, taking the object with it, and the body then stores a pointer to freed
	// memory. It survived for a long time only because the body was `SetFilterRoot(root); return;` —
	// nothing ran in between to reuse the memory. Adding one statement after it (a `break`, so the
	// sort and grouping could be read next) was enough for the tree to come back EMPTY.
	//
	// The rule: an ibValue you take a pointer out of must outlive the pointer.
	for (const ibDataNode& child : node.Children()) {
		const ibValue restored = ibReadFilterValue(child);
		ibValueFilterGroup* root = nullptr;
		if (restored.ConvertToValue(root)) {
			SetFilterRoot(root);   // the flat door follows the tree it stands for
			break;
		}
	}

	if (const ibDataNode* order = node.FindChild(kOrderNode)) {
		GetOrder()->Clear();
		for (const ibDataNode& line : order->Children()) {
			const wxString field = line.GetValue<wxString>(kFieldName);
			if (field.IsEmpty())
				continue;
			GetOrder()->Add(field, line.GetValue<s32>(kDirName) != 0
				? ibSortDirection_Ascending : ibSortDirection_Descending);
		}
	}

	if (const ibDataNode* group = node.FindChild(kGroupNode)) {
		GetGroup()->Clear();
		for (const ibDataNode& line : group->Children()) {
			const wxString field = line.GetValue<wxString>(kFieldName);
			if (field.IsEmpty())
				continue;
			// THE UNFOLD KIND TRAVELS WITH IT. A hierarchy grouping IS what makes the list a tree;
			// dropping the kind would reload every tree as a flat grouping and look like data loss.
			GetGroup()->Add(field, static_cast<ibQueryDimUnfold>(line.GetValue<s32>(kKindName)));
		}
	}
	return true;
}

bool ibValueListSettings::WriteData(ibDataNode& node) const
{
	// THE TREE IS THE FILTER. It packs itself (the root group, children and all), so the settings
	// only say where it goes.
	if (m_filterRoot) {
		if (!ibValue(static_cast<ibValueFilterGroup*>(m_filterRoot))
				.Serialize(node.AddChild(m_filterRoot->GetClassType(), 0)))
			return false;
	}

	ibValueSortList*  order = GetOrder();   // both are const accessors — the cast was doing nothing
	ibValueGroupList* group = GetGroup();

	if (order != nullptr && order->Count() > 0) {
		ibDataNode& sub = node.Child(kOrderNode);
		for (size_t i = 0; i < order->Count(); ++i) {
			const wxString field = order->GetField(i);
			if (field.IsEmpty())
				continue;
			ibDataNode& line = sub.AddChild(value_to_clsid("VL_SORTI"), static_cast<ibMetaID>(i));
			line.SetValue<wxString>(kFieldName, field);
			line.SetValue<s32>(kDirName, order->GetDirection(i) == ibSortDirection_Ascending ? 1 : 0);
		}
	}

	if (group != nullptr && group->Count() > 0) {
		ibDataNode& sub = node.Child(kGroupNode);
		for (size_t i = 0; i < group->Count(); ++i) {
			const wxString field = group->GetField(i);
			if (field.IsEmpty())
				continue;
			ibDataNode& line = sub.AddChild(make_clsid(wxT("GroupLine"), ibClassKind_None), static_cast<ibMetaID>(i));
			line.SetValue<wxString>(kFieldName, field);
			line.SetValue<s32>(kKindName, static_cast<s32>(group->GetKind(i)));
		}
	}
	return true;
}

// ===========================================================================
//  Transactional bridge — composer (committed STORE) <-> ListSettings (edit BUFFER)
// ===========================================================================

// LOAD (dialog open): composer → buffer. Mirror the committed Filter / Sort / Group into the dialog's
// ListSettings so it shows the current state for editing. The buffer's value items carry the comparison KIND;
// the composer stores the op spelling, so reverse-map it (OpToComparisonKind).
void ibValueListSettings::SetFilterRoot(ibValueFilterGroup* root)
{
	if (root == nullptr)
		return;   // an empty filter is an empty root group, never a missing one
	m_filterRoot = root;
	if (m_filter)
		m_filter->SetRoot(root);
}

void ibLoadSettingsFromComposer(ibValueListSettings* settings, const ibDataComposer& composer,
	const ibValueListSettings* live)
{
	if (settings == nullptr)
		return;
	// THE TREE IS WHAT THE FORM EDITS, so it is what gets filled. The composer
	// keeps a flat list of conditions (or one rendered expression, which has no
	// flat reading — GetFilterAt says no to those), and a flat list IS a tree one
	// level deep: every condition becomes a child of the root AND group.
	//
	// A filter that ARRIVED as an expression is not taken apart again: the tree
	// that produced it is the one in the settings, and re-parsing query text to
	// recover it would be inventing a second source of truth.
	// ONE STORE, SO ONE FILL. The flat `Filter` list is a door onto this same root
	// group, so filling both put every condition in twice; and filling the root
	// only "if it is empty" meant a second open showed whatever the first one left
	// behind rather than what the list is actually filtered by now.
	//
	// THE LIVE TREE WINS when there is one. The composer keeps a tree filter as ONE
	// expression — FilterCount() does not see it — so rebuilding the buffer from
	// the composer alone would show an empty Filter tab over a list that is very
	// visibly filtered. A tree copies itself through its own packed form (the node
	// serialisation), so the buffer is a real copy and Cancel still discards.
	if (ibValueFilterGroup* root = settings->GetFilterRoot()) {
		root->Clear();
		ibValueFilterGroup* liveRoot = live != nullptr ? live->GetFilterRoot() : nullptr;
		if (liveRoot != nullptr && liveRoot->Count() > 0) {
			// THE NODE IS CREATED WITH ITS TYPE, like every other packed value: a
			// default-constructed node is not a node the tree can be written into,
			// and the copy came back empty — which is what left the settings form
			// blank over a list that was very obviously filtered.
			ibDataNode packed(liveRoot->GetClassType(), 0);
			const bool packedOk = ibValue(liveRoot).Serialize(packed);
			if (packedOk) {
				// THE UNPACKED VALUE IS HELD, not read out of a temporary. A value
				// owns what it wraps: `FromNode(packed).ConvertToValue(copy)` frees
				// the tree at the end of that expression, so `copy` was already
				// dangling by the time the settings took it — the form then opened
				// on a root that had been destroyed, and showed nothing.
				const ibValue restored = ibReadFilterValue(packed);
				ibValueFilterGroup* copy = nullptr;
				const bool gotCopy = restored.ConvertToValue(copy);
				if (gotCopy)
					settings->SetFilterRoot(copy);
			}
		}
		else {
			for (size_t i = 0; i < composer.FilterCount(); ++i) {
				wxString path, op; ibValue value;
				if (composer.GetFilterAt(i, path, op, value))
					root->Add(ibValue(new ibValueCompositionField(path)), OpToComparisonKind(op), value, true);
			}
		}
	}
	if (ibValueSortList* order = settings->GetOrder()) {
		order->Clear();
		for (size_t i = 0; i < composer.SortCount(); ++i) {
			wxString path; bool ascending = true;
			if (composer.GetSortAt(i, path, ascending))
				order->Add(path, ascending ? ibSortDirection_Ascending : ibSortDirection_Descending);
		}
	}
	if (ibValueGroupList* group = settings->GetGroup()) {
		group->Clear();
		for (size_t i = 0; i < composer.GroupCount(); ++i) {
			wxString path; ibQueryDimUnfold kind = ibQueryDimUnfold::Elements;
			if (composer.GetGroupAt(i, path, kind))
				group->Add(path, kind);
		}
	}
}

// COMMIT (dialog OK): buffer → composer. CLEAR the committed Filter / Sort / Group, then re-apply the buffer's
// items — the whole transaction lands atomically. Disabled (Use=false) filter lines are dropped (the composer
// has no use flag; an off line simply does not filter). Dot-walk fields → auto-JOIN on the door at lowering.
// ===========================================================================
//  The filter TREE, as a query AST
// ===========================================================================
//
// A flat filter needs no building: the composer joins its lines with AND. A TREE
// does — `a AND (b OR c)` has no flat form — and what it builds is not text but
// an AST: the SAME `ibQueryAstExpr` the `Restrict` clause compiles to. One shape
// for "a condition over a source", whoever wrote it.
//
// Text still exists, for the composer, which speaks the query language — but it
// comes from the ordinary AST renderer (ibRenderQueryExpr), so parentheses and
// precedence are handled once, where every other query already handles them.
//
// SIDES ARE VALUES. A field becomes a Column node (its dotted path); anything
// else becomes a Param node, so a string is never read as syntax and a date
// never in somebody's locale.
//
// A side that is an EMPTY field is a condition nobody finished writing — skipped
// rather than built into a comparison against nothing.

static ibQueryAstExprPtr BuildFilterSide(ibDataComposer& composer, const ibValue& side)
{
	ibValueCompositionField* field = nullptr;
	if (side.ConvertToValue(field))
		// The path travels as SEGMENTS — that is what the lowering dot-walks to
		// build its joins; one glued string would have to be split again. Splitting it is
		// the RENDERER's, so a dotted path becomes a column the one way everywhere.
		return ibQueryColumnFromPath(field->GetPath());

	ibQueryAstExprPtr e = ibQueryAstExpr::Make(ibQueryAstExprKind::Param);
	e->m_paramName = composer.AddParam(side);
	return e;
}

static bool FilterSideIsUnfinished(const ibValue& side)
{
	ibValueCompositionField* field = nullptr;
	return side.ConvertToValue(field) && field->GetPath().IsEmpty();
}

static ibQueryAstExprPtr BuildFilterNode(ibDataComposer& composer, const ibValue& node);

static ibQueryAstExprPtr BuildFilterGroup(ibDataComposer& composer, const ibValueFilterGroup* group)
{
	if (group == nullptr || !group->GetUse())
		return nullptr;

	// LEFT-FOLDED into binary Logical nodes — the AST has no n-ary AND, and the
	// fold is what a reader expects: `a AND b AND c` groups as `(a AND b) AND c`.
	ibQueryAstExprPtr acc;
	for (size_t i = 0; i < group->Count(); ++i) {
		ibQueryAstExprPtr child = BuildFilterNode(composer, group->GetChild(i));
		if (!child)
			continue;   // unfinished or switched off — as if it were not written
		if (!acc) {
			acc = child;
			continue;
		}
		ibQueryAstExprPtr joined = ibQueryAstExpr::Make(ibQueryAstExprKind::Logical);
		joined->m_isOr = (group->GetKind() == ibFilterGroupKind_Or);
		joined->m_lhs = acc;
		joined->m_rhs = child;
		acc = joined;
	}

	if (!acc)
		return nullptr;

	if (group->GetKind() != ibFilterGroupKind_Not)
		return acc;

	// NOT negates the WHOLE group, not its first child.
	ibQueryAstExprPtr negated = ibQueryAstExpr::Make(ibQueryAstExprKind::Not);
	negated->m_lhs = acc;
	return negated;
}

static ibQueryAstExprPtr BuildFilterItem(ibDataComposer& composer, const ibValueFilterItem* item)
{
	if (item == nullptr || !item->GetUse())
		return nullptr;
	if (FilterSideIsUnfinished(item->GetLeft()) || FilterSideIsUnfinished(item->GetRight()))
		return nullptr;

	// CONTAINS IS A LIKE, not a comparison — its own node kind, so the lowering
	// can do what a LIKE needs instead of being handed an operator it has no
	// meaning for.
	const bool isLike = (item->GetComparison() == ibComparisonKind_Contains);

	// ⭐⭐ «IN HIERARCHY» IS AN *IN* CARRYING A WORD — one node kind, two comparisons.
	//
	// The AST already holds the unfold word on the In node (queryAst.h: `m_unfold`), and L4 resolves
	// the subtree into the values it stands for before anything below sees it — so both comparisons
	// reuse the whole mechanism by choosing that node, and «in hierarchy» differs from «in» by the
	// word alone. An operator of its own would have been a second way to ask what the language asks.
	const ibComparisonKind comparison = item->GetComparison();
	const bool isIn = (comparison == ibComparisonKind_In
	                || comparison == ibComparisonKind_InHierarchy);

	ibQueryAstExprPtr e = ibQueryAstExpr::Make(isLike ? ibQueryAstExprKind::Like
	                                            : isIn ? ibQueryAstExprKind::In
	                                                   : ibQueryAstExprKind::Compare);
	if (comparison == ibComparisonKind_InHierarchy)
		e->m_unfold = ibQueryDimUnfold::Hierarchy;

	if (!isLike && !isIn) {
		switch (item->GetComparison()) {
		case ibComparisonKind_NotEqual:     e->m_cmp = ibQueryCompareOp::Ne; break;
		case ibComparisonKind_Greater:      e->m_cmp = ibQueryCompareOp::Gt; break;
		case ibComparisonKind_Less:         e->m_cmp = ibQueryCompareOp::Lt; break;
		case ibComparisonKind_GreaterEqual: e->m_cmp = ibQueryCompareOp::Ge; break;
		case ibComparisonKind_LessEqual:    e->m_cmp = ibQueryCompareOp::Le; break;
		default:                            e->m_cmp = ibQueryCompareOp::Eq; break;
		}
	}

	e->m_lhs = BuildFilterSide(composer, item->GetLeft());

	// An IN takes a LIST, not a right-hand operand: one entry here, because a filter row holds one
	// value. Where that value is itself a list (an array chosen in the cell) the lowering flattens it
	// — the node is already the set-valued one. The unfold word set above is the whole difference
	// between «in» and «in hierarchy».
	if (isIn)
		e->m_list.push_back(BuildFilterSide(composer, item->GetRight()));
	else
		e->m_rhs = BuildFilterSide(composer, item->GetRight());
	return e;
}

static ibQueryAstExprPtr BuildFilterNode(ibDataComposer& composer, const ibValue& node)
{
	// A node is one or the other — asking must not raise on the branch not taken.
	ibValueFilterGroup* group = nullptr;
	if (node.ConvertToValue(group))
		return BuildFilterGroup(composer, group);
	ibValueFilterItem* item = nullptr;
	if (node.ConvertToValue(item))
		return BuildFilterItem(composer, item);
	return nullptr;
}

ibQueryAstExprPtr ibBuildFilterAst(ibDataComposer& composer, const ibValueFilterGroup* root)
{
	return BuildFilterGroup(composer, root);
}

wxString ibRenderFilterTree(ibDataComposer& composer, const ibValueFilterGroup* root)
{
	const ibQueryAstExprPtr ast = ibBuildFilterAst(composer, root);
	return ast ? ibRenderQueryExpr(*ast) : wxString();
}

// Sort and grouping travel the same road whichever shape the filter took — so
// they live apart from it, and neither path can forget them.
static void ibCommitSortAndGroup(ibDataComposer& composer, const ibValueListSettings* settings)
{
	if (const ibValueSortList* order = settings->GetOrder()) {
		for (size_t i = 0; i < order->Count(); ++i) {
			const ibValueSortItem* item = order->GetItem(i);
			if (item == nullptr || item->GetField().IsEmpty())
				continue;
			composer.Sort(item->GetField(), item->IsAscending());
		}
	}
	if (const ibValueGroupList* group = settings->GetGroup()) {
		for (size_t i = 0; i < group->Count(); ++i) {
			if (group->GetField(i).IsEmpty())
				continue;
			// The grouping KIND drives it: HIERARCHY / HIERARCHYONLY unfolds the reference field's parent tree
			// (a hierarchical catalog); ELEMENTS folds rows by value. The ONE switch between a hierarchical and a flat view.
			composer.TotalBy(group->GetField(i), group->GetKind(i));
		}
	}
}

// See listFilter.h — the one validation both faces use.
static void ibValidateFilterGroup(const ibValueFilterGroup* group)
{
	if (group == nullptr || !group->GetUse())
		return;
	for (size_t i = 0; i < group->Count(); ++i) {
		if (const ibValueFilterGroup* child = group->GetGroup(i)) {
			ibValidateFilterGroup(child);
			continue;
		}
		const ibValueFilterItem* item = group->GetItem(i);
		if (item == nullptr || !item->GetUse())
			continue;

		// A CONDITION NEEDS A FIELD. Without one there is nothing to compare.
		if (item->GetField().IsEmpty())
			ibBackendCoreException::Error(_("A filter condition has no field chosen"));

		// AND A VALUE THE FIELD CAN HOLD. An empty right side is legal (it compares
		// against the empty value of that type); a value of another type is not.
		const ibValue& right = item->GetRight();
		const ibTypeDescription expected = item->GetRightTypeDescription();
		if (!right.IsEmpty() && expected.GetClsidCount() > 0) {
			const std::vector<ibClassID>& allowed = expected.GetClsidList();
			if (std::find(allowed.begin(), allowed.end(), right.GetClassType()) == allowed.end())
				ibBackendCoreException::Error(
					_("The value of condition '%s' does not fit the field's type"), item->GetField());
		}
	}
}

void ibValidateSettings(const ibValueListSettings* settings)
{
	if (settings == nullptr)
		return;
	ibValidateFilterGroup(settings->GetFilterRoot());

	// A SORT OR A GROUPING WITHOUT A FIELD is the same kind of half-written line.
	if (const ibValueSortList* order = settings->GetOrder()) {
		for (size_t i = 0; i < order->Count(); ++i) {
			const ibValueSortItem* line = order->GetItem(i);
			if (line != nullptr && line->GetField().IsEmpty())
				ibBackendCoreException::Error(_("A sort line has no field chosen"));
		}
	}
	if (const ibValueGroupList* group = settings->GetGroup()) {
		for (size_t i = 0; i < group->Count(); ++i) {
			if (group->GetField(i).IsEmpty())
				ibBackendCoreException::Error(_("A grouping line has no field chosen"));
		}
	}
}

void ibCommitSettingsToComposer(ibDataComposer& composer, const ibValueListSettings* settings)
{
	if (settings == nullptr)
		return;
	// REFUSED BEFORE ANYTHING IS TOUCHED: a half-written line must not take the
	// composer's previous, working settings down with it.
	ibValidateSettings(settings);

	composer.ClearFilters();
	composer.ClearSorts();
	composer.ClearGroups();
	// A filter the user emptied must LEAVE. Without this the previous condition
	// would survive its own deletion — the worst kind of stale: invisible in the
	// form, still narrowing the list.
	composer.FilterAst(nullptr);

	// THE TREE, AS AN AST — the one road. The flat `Filter` list is a door onto
	// the same root group (ibValueFilterList), so a condition added through it is
	// already here; reading it again would apply every such condition twice.
	if (const ibQueryAstExprPtr condition = ibBuildFilterAst(composer, settings->GetFilterRoot()))
		composer.FilterAst(condition);

	ibCommitSortAndGroup(composer, settings);
}

// ===========================================================================
//  Registration
// ===========================================================================

ENUM_TYPE_REGISTER(ibValueEnumComparisonKind, "ComparisonKind", enum_to_clsid("EN_CMPK"));
ENUM_TYPE_REGISTER(ibValueEnumSortDirection,  "SortDirection",  enum_to_clsid("EN_SDIR"));
ENUM_TYPE_REGISTER(ibValueEnumGroupKind,      "GroupKind",      enum_to_clsid("EN_GRPK"));
ENUM_TYPE_REGISTER(ibValueEnumFilterGroupKind,   "FilterGroupKind",   enum_to_clsid("EN_FGRP"));
ENUM_TYPE_REGISTER(ibValueEnumFilterDisplayMode, "FilterDisplayMode", enum_to_clsid("EN_FDSP"));

VALUE_TYPE_REGISTER(ibValueFilterItem,    "FilterItem",   value_to_clsid("VL_FILTI"));
VALUE_TYPE_REGISTER(ibValueFilterGroup,   "FilterGroup",  value_to_clsid("VL_FILTG"));
VALUE_TYPE_REGISTER(ibValueFilterList,    "FilterList",   value_to_clsid("VL_FILTL"));
VALUE_TYPE_REGISTER(ibValueSortItem,      "SortItem",     value_to_clsid("VL_SORTI"));
VALUE_TYPE_REGISTER(ibValueSortList,      "SortList",     value_to_clsid("VL_SORTL"));
VALUE_TYPE_REGISTER(ibValueGroupList,     "GroupList",    value_to_clsid("VL_GRPL"));
VALUE_TYPE_REGISTER(ibValueListSettings,  "ListSettings", value_to_clsid("VL_LSET"));

// THE LINE AS DATA, in both modes — see the declaration. The composer keeps a path plus a
// direction; the buffer keeps an item that holds the same two things.
wxString ibValueSortList::GetField(size_t idx) const {
	if (const ibDataComposer* c = Composer()) {
		wxString path; bool ascending = true;
		return c->GetSortAt(idx, path, ascending) ? path : wxString();
	}
	return idx < m_items.size() ? m_items[idx]->GetField() : wxString();
}

ibSortDirection ibValueSortList::GetDirection(size_t idx) const {
	if (const ibDataComposer* c = Composer()) {
		wxString path; bool ascending = true;
		if (!c->GetSortAt(idx, path, ascending))
			return ibSortDirection_Ascending;
		return ascending ? ibSortDirection_Ascending : ibSortDirection_Descending;
	}
	return idx < m_items.size() ? m_items[idx]->GetDirection() : ibSortDirection_Ascending;
}

// ⭐ CHANGE A LINE WHERE IT STANDS — the ONE door for editing one, and the answer to a whole family
// of defects rather than to one of them.
//
// These lists have two modes. In BUFFER mode the line is an OBJECT and a caller could reach in and
// set its field. In FACADE mode the store is the composer and there IS no line object: GetItem mints
// a transient one and GetFieldObject deliberately answers null. Every cell in the settings dialog was
// written against the first mode, so on a LIVE list:
//
//   * editing a sort's field or direction wrote into a temporary and was silently LOST;
//   * changing a grouping's kind read a null object and CRASHED (Max, 2026-08-07: "added a field,
//     switched it to HierarchyOnly, crash").
//
// A line is a PATH plus one more fact, in both modes — so it is set as that, and the store decides
// how. ⚠ POSITION IS KEPT: order is the meaning of these lists ("by Date, then by Number" is not the
// other sort), and a remove-then-add would move the edited line to the end.
bool ibValueSortList::SetLine(size_t idx, const wxString& field, ibSortDirection direction) {
	if (ibDataComposer* c = Composer()) {
		std::vector<std::pair<wxString, bool>> lines;
		for (size_t i = 0; i < c->SortCount(); ++i) {
			wxString path; bool ascending = true;
			if (!c->GetSortAt(i, path, ascending))
				continue;
			if (i == idx) { path = field; ascending = direction == ibSortDirection_Ascending; }
			lines.emplace_back(path, ascending);
		}
		if (idx >= lines.size())
			return false;
		c->ClearSorts();
		for (const auto& line : lines)
			c->Sort(line.first, line.second);
		if (m_onChange) m_onChange();
		return true;
	}
	if (idx >= m_items.size())
		return false;
	// BUFFER mode holds a line OBJECT, and its field is a composition FIELD value — so the path is
	// wrapped into one. An empty path clears it, which is what the clear button sends.
	m_items[idx]->SetField(field.IsEmpty() ? nullptr : new ibValueCompositionField(field));
	m_items[idx]->SetDirection(direction);
	return true;
}

bool ibValueGroupList::SetLine(size_t idx, const wxString& field, ibQueryDimUnfold kind) {
	if (ibDataComposer* c = Composer()) {
		std::vector<std::pair<wxString, ibQueryDimUnfold>> lines;
		for (size_t i = 0; i < c->GroupCount(); ++i) {
			wxString path; ibQueryDimUnfold k = ibQueryDimUnfold::Elements;
			if (!c->GetGroupAt(i, path, k))
				continue;
			if (i == idx) { path = field; k = kind; }
			lines.emplace_back(path, k);
		}
		if (idx >= lines.size())
			return false;
		c->ClearGroups();
		for (const auto& line : lines)
			c->TotalBy(line.first, line.second);
		if (m_onChange) m_onChange();
		return true;
	}
	if (idx >= m_items.size())
		return false;
	m_items[idx].m_field = new ibValueCompositionField(field);
	m_items[idx].m_kind  = kind;
	return true;
}
