#include "metaDiff.h"

#include "backend/metaCollection/metaGroups.h"   // what a group is called and where it stands
#include "backend/propertyManager/property/propertyForm.h"
#include "backend/propertyManager/property/propertyModule.h"
#include "backend/propertyManager/property/propertyPicture.h"
#include "backend/propertyManager/property/propertySource.h"
#include "backend/propertyManager/property/propertySpreadsheet.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

namespace {
// Convert a property's value to a display string. A live value's text
// (a module, a binary blob) can carry control characters, which render as
// garbage in the left/right columns: strip them and cap the length so the
// row stays readable.
ibString StringifyPropertyValue(const ibProperty* prop) {
	if (prop == nullptr) return ibString();

	const ibVariant& val = prop->GetValue();
	if (val.GetType() == ibValueTypes::TYPE_EMPTY) return ibString();
	if (val.GetType() == ibValueTypes::TYPE_STRING) return val.GetString();

	// Anything else — its text, control bytes stripped so the row stays
	// printable, truncated so a runaway blob does not push the right-side
	// columns off-screen.
	ibString raw = val.GetString();
	ibString clean;
	clean.reserve(raw.length());
	for (ibString::const_iterator it = raw.begin(); it != raw.end(); ++it) {
		const ibChar ch = *it;
		const int code = static_cast<int>(ch);
		if (code >= 0x20 || code == 0x09 /* tab */)
			clean += ch;
	}
	const size_t kMaxLen = 200;
	if (clean.length() > kMaxLen)
		clean = clean.Left(kMaxLen) + ibT("...");
	return clean;
}

// Heavy / structural property classes whose value is the object itself
// (script text, binary form data, picture bytes, spreadsheet cells).
// Listing them inline in the Properties group would duplicate what the
// containing object row already represents — its Same/Changed status
// already reflects any difference in these payloads.
bool IsStructuralProperty(const ibProperty* prop) {
	return dynamic_cast<const ibPropertyModule*>(prop) != nullptr
		|| dynamic_cast<const ibPropertyForm*>(prop) != nullptr
		|| dynamic_cast<const ibPropertyPicture*>(prop) != nullptr
		|| dynamic_cast<const ibPropertySpreadsheet*>(prop) != nullptr
		|| dynamic_cast<const ibPropertySource*>(prop) != nullptr;
}

// (The list that used to live here — predefined attributes, object modules, manager
// modules — is gone. Whether a child belongs under its owner is the owner's OWN answer
// (ibValueMetaObject::IsAcceptedByParent -> FilterChild), so the diff tree and the
// designer tree can no longer drift apart, and a new kind of child needs no line here.)
}

// g_diffCommonUmbrellaClsid / g_diffPropertiesGroupClsid are now header-defined
// inline constexpr (metaDiff.h) — constexpr + ODR-safe across DLLs.

// --- Group labels -----------------------------------------------------
//
// ⭐ BOTH ANSWERS COME FROM THE ONE DECLARATION — what a metatype's group is called and where it
// stands (backend/metaCollection/metaGroups.h). They were two tables right here, copied from the
// configuration tree, and copies drift: this tree listed calculation registers BEFORE information
// registers while the tree it was copied from listed them last, and the charts came in two
// different orders (2026-09-17). What stays here is what only a comparison has — the two synthetic
// rows it inserts itself.

// static
ibString ibMetaDiffWalker::GroupLabelFor(ibClassID clsid)
{
	if (clsid == g_diffCommonUmbrellaClsid)             return _("Common");
	if (clsid == g_diffPropertiesGroupClsid)            return _("Properties");

	const ibString caption = ibMetaGroupCaption(clsid);
	if (!caption.IsEmpty())
		return caption;

	// Unknown CLSID — show its raw symbol so the UI never blanks.
	return ibString::Format(ibT("[%llu]"),
		static_cast<unsigned long long>(clsid));
}

// static
int ibMetaDiffWalker::GroupOrderRank(ibClassID clsid)
{
	// The two rows the comparison adds itself stand ahead of every declared kind: the umbrella above
	// what it gathers, and an object's properties before its children.
	if (clsid == g_diffCommonUmbrellaClsid)             return -2;
	if (clsid == g_diffPropertiesGroupClsid)            return -1;
	return ibMetaGroupOrder(clsid);
}

// --- Walk -------------------------------------------------------------

// static
std::vector<ibMetaDiffRecord> ibMetaDiffWalker::Walk(
	ibValueMetaObject* leftRoot,
	ibValueMetaObject* rightRoot)
{
	std::vector<ibMetaDiffRecord> out;
	out.reserve(512);

	WalkPair(leftRoot, rightRoot, /*depth*/ 0, /*parentIndex*/ -1, out);

	// Propagate "Changed" up the tree: any non-Same descendant lifts a
	// Same parent (including synthetic group rows) to Changed so the
	// user can scan top-down to find diffs without expanding every
	// branch.
	if (!out.empty()) {
		for (size_t i = out.size(); i-- > 0;) {
			ibMetaDiffRecord& rec = out[i];
			if (rec.m_status != ibMetaDiffStatus::Same)
				continue;
			if (SubtreeHasDifference(out, i))
				rec.m_status = ibMetaDiffStatus::Changed;
		}
	}

	return out;
}

// static
void ibMetaDiffWalker::WalkPair(
	ibValueMetaObject* left,
	ibValueMetaObject* right,
	int depth,
	int parentIndex,
	std::vector<ibMetaDiffRecord>& out)
{
	const int myIndex = static_cast<int>(out.size());

	ibMetaDiffRecord rec;
	rec.m_left = left;
	rec.m_right = right;
	rec.m_parentIndex = parentIndex;
	rec.m_depth = depth;

	if (left != nullptr && right != nullptr) {
		if (!left->CompareObject(right)) {
			rec.m_status = ibMetaDiffStatus::Changed;
		}
		else if (ChildOrderDiffers(left, right)) {
			rec.m_status = ibMetaDiffStatus::Reordered;
		}
		else {
			rec.m_status = ibMetaDiffStatus::Same;
		}
	}
	else if (left != nullptr) {
		rec.m_status = ibMetaDiffStatus::OnlyInLeft;
	}
	else if (right != nullptr) {
		rec.m_status = ibMetaDiffStatus::OnlyInRight;
	}
	else {
		return;
	}

	out.emplace_back(rec);

	// For paired metadata objects (both sides present), emit a synthetic
	// Properties group with one child row per property. CompareObject
	// itself does property-by-property equality, so this surface just
	// makes that data visible — the user sees which property drove a
	// Changed verdict instead of having to open the inspector.
	if (left != nullptr && right != nullptr) {
		const unsigned int propCount = left->GetPropertyCount();
		if (propCount > 0) {
			const int propGroupIdx = static_cast<int>(out.size());
			ibMetaDiffRecord propGroupRec;
			propGroupRec.m_groupClsid = g_diffPropertiesGroupClsid;
			propGroupRec.m_parentIndex = myIndex;
			propGroupRec.m_depth = depth + 1;
			propGroupRec.m_status = ibMetaDiffStatus::Same;
			out.emplace_back(propGroupRec);

			for (unsigned int i = 0; i < propCount; ++i) {
				const ibProperty* propLeft = left->GetProperty(i);
				if (propLeft == nullptr) continue;

				// Structural properties (Module text, Form/Picture/
				// Spreadsheet payloads) would duplicate what the parent
				// object row already represents — skip them so the user
				// sees one row per module / form, not two.
				if (IsStructuralProperty(propLeft))
					continue;

				const ibString& propName = propLeft->GetName();
				const ibProperty* propRight = right->GetProperty(propName);

				// Safe stringifier — a live value's text can carry control
				// bytes that render as garbage in the value columns.
				const ibString leftStr = StringifyPropertyValue(propLeft);
				const ibString rightStr = StringifyPropertyValue(propRight);

				ibMetaDiffRecord propRec;
				propRec.m_propertyName = propLeft->GetLabel().IsEmpty()
					? propName
					: propLeft->GetLabel();
				propRec.m_leftValue = leftStr;
				propRec.m_rightValue = rightStr;
				propRec.m_parentIndex = propGroupIdx;
				propRec.m_depth = depth + 2;
				propRec.m_status =
					(propRight != nullptr && leftStr == rightStr)
					? ibMetaDiffStatus::Same
					: ibMetaDiffStatus::Changed;
				out.emplace_back(propRec);
			}
		}
	}

	// Collect every CLSID present in either side's direct children,
	// then sort by GroupOrderRank so the resulting groups appear in
	// the same sequence the standard metadata tree uses (Catalogs
	// before Documents before Enumerations, etc.).
	std::vector<ibClassID> clsids;
	auto addClsid = [&](ibClassID c) {
		for (ibClassID existing : clsids) {
			if (existing == c) return;
		}
		clsids.push_back(c);
	};
	if (left != nullptr) {
		for (unsigned int i = 0; i < left->GetChildCount(); ++i) {
			ibValueMetaObject* lc = left->GetChild(i);
			if (lc != nullptr && lc->IsAcceptedByParent())
				addClsid(lc->GetClassType());
		}
	}
	if (right != nullptr) {
		for (unsigned int j = 0; j < right->GetChildCount(); ++j) {
			ibValueMetaObject* rc = right->GetChild(j);
			if (rc != nullptr && rc->IsAcceptedByParent())
				addClsid(rc->GetClassType());
		}
	}

	std::sort(clsids.begin(), clsids.end(),
		[](ibClassID a, ibClassID b) {
			return GroupOrderRank(a) < GroupOrderRank(b);
		});

	// At the root config (depth 0), wrap common-tier groups (modules /
	// forms / templates / pictures / interfaces / roles / languages)
	// inside a synthetic "Common" umbrella row — mirrors the m_treeCOMMON
	// node in treeConfiguration_impl.cpp. Inner pairs (Catalog,
	// Document, ...) keep a flat group structure.
	if (depth == 0) {
		// WHICH BAND A GROUP BELONGS TO IS THE GROUP'S OWN ANSWER (ibMetaGroupBandOf) — it used to be
		// read out of the rank's number, "10..70 means common", a fact hidden in an arithmetic range
		// that any renumbering would have broken silently.
		std::vector<ibClassID> commonClsids, otherClsids;
		for (ibClassID c : clsids) {
			if (ibMetaGroupBandOf(c) == ibMetaGroupBand::Common)
				commonClsids.push_back(c);
			else
				otherClsids.push_back(c);
		}

		if (!commonClsids.empty()) {
			const int umbrellaIdx = static_cast<int>(out.size());
			ibMetaDiffRecord umb;
			umb.m_groupClsid = g_diffCommonUmbrellaClsid;
			umb.m_parentIndex = myIndex;
			umb.m_depth = depth + 1;
			umb.m_status = ibMetaDiffStatus::Same;
			out.emplace_back(umb);

			for (ibClassID c : commonClsids)
				WalkGroup(c, left, right, depth + 2, umbrellaIdx, out);
		}

		for (ibClassID c : otherClsids)
			WalkGroup(c, left, right, depth + 1, myIndex, out);
	}
	else {
		for (ibClassID clsid : clsids) {
			WalkGroup(clsid, left, right, depth + 1, myIndex, out);
		}
	}
}

// static
void ibMetaDiffWalker::WalkGroup(
	ibClassID clsid,
	ibValueMetaObject* leftParent,
	ibValueMetaObject* rightParent,
	int depth,
	int parentIndex,
	std::vector<ibMetaDiffRecord>& out)
{
	// Collect children of the given CLSID from each side. Pairing
	// happens by GUID inside the group, not across groups — the parent
	// already partitioned by class, so cross-class moves are intentional
	// OnlyInLeft / OnlyInRight at the new group's level.
	std::vector<ibValueMetaObject*> leftKids;
	std::vector<ibValueMetaObject*> rightKids;

	if (leftParent != nullptr) {
		for (unsigned int i = 0; i < leftParent->GetChildCount(); ++i) {
			ibValueMetaObject* lc = leftParent->GetChild(i);
			if (lc != nullptr && lc->GetClassType() == clsid)
				leftKids.push_back(lc);
		}
	}
	if (rightParent != nullptr) {
		for (unsigned int j = 0; j < rightParent->GetChildCount(); ++j) {
			ibValueMetaObject* rc = rightParent->GetChild(j);
			if (rc != nullptr && rc->GetClassType() == clsid)
				rightKids.push_back(rc);
		}
	}

	if (leftKids.empty() && rightKids.empty())
		return;  // nothing to group — skip the synthetic row entirely

	// Emit the synthetic group row. Status starts as Same; the post-
	// recursion lift in Walk() upgrades it to Changed if any descendant
	// differs.
	const int groupIndex = static_cast<int>(out.size());
	ibMetaDiffRecord groupRec;
	groupRec.m_groupClsid = clsid;
	groupRec.m_parentIndex = parentIndex;
	groupRec.m_depth = depth;
	groupRec.m_status = ibMetaDiffStatus::Same;
	out.emplace_back(groupRec);

	// Pair within the group by GUID.
	std::unordered_set<const ibValueMetaObject*> matchedRight;
	for (ibValueMetaObject* lc : leftKids) {
		ibValueMetaObject* rc = nullptr;
		const ibGuid guid = lc->GetGuid();
		if (guid.isValid()) {
			for (ibValueMetaObject* candidate : rightKids) {
				if (matchedRight.find(candidate) != matchedRight.end())
					continue;
				if (candidate->CompareGuid(guid)) {
					rc = candidate;
					matchedRight.insert(rc);
					break;
				}
			}
		}
		WalkPair(lc, rc, depth + 1, groupIndex, out);
	}
	for (ibValueMetaObject* rc : rightKids) {
		if (matchedRight.find(rc) != matchedRight.end())
			continue;
		WalkPair(/*left*/ nullptr, rc, depth + 1, groupIndex, out);
	}
}

// static
bool ibMetaDiffWalker::SubtreeHasDifference(
	const std::vector<ibMetaDiffRecord>& out,
	size_t recordIndex)
{
	const int rootDepth = out[recordIndex].m_depth;
	for (size_t i = recordIndex + 1; i < out.size(); ++i) {
		if (out[i].m_depth <= rootDepth)
			break;
		if (out[i].m_status != ibMetaDiffStatus::Same)
			return true;
	}
	return false;
}

// static
bool ibMetaDiffWalker::ChildOrderDiffers(
	const ibValueMetaObject* left,
	const ibValueMetaObject* right)
{
	if (left == nullptr || right == nullptr)
		return false;

	const unsigned int n = left->GetChildCount();
	if (n != right->GetChildCount())
		return false;

	bool positionMatches = true;
	for (unsigned int i = 0; i < n; ++i) {
		const ibValueMetaObject* lc = left->GetChild(i);
		const ibValueMetaObject* rc = right->GetChild(i);
		if (lc == nullptr || rc == nullptr)
			return false;
		const ibGuid leftGuid = lc->GetGuid();
		if (!leftGuid.isValid())
			return false;
		if (!rc->CompareGuid(leftGuid)) {
			positionMatches = false;
			break;
		}
	}
	if (positionMatches)
		return false;

	for (unsigned int i = 0; i < n; ++i) {
		const ibValueMetaObject* lc = left->GetChild(i);
		if (lc == nullptr)
			return false;
		const ibGuid leftGuid = lc->GetGuid();
		if (!leftGuid.isValid())
			return false;

		bool found = false;
		for (unsigned int j = 0; j < n; ++j) {
			const ibValueMetaObject* rc = right->GetChild(j);
			if (rc != nullptr && rc->CompareGuid(leftGuid)) {
				found = true;
				break;
			}
		}
		if (!found)
			return false;
	}

	return true;
}
