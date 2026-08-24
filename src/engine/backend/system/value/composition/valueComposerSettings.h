#ifndef __VALUE_COMPOSER_SETTINGS_H__
#define __VALUE_COMPOSER_SETTINGS_H__

// ---------------------------------------------------------------------------
// THE COMPOSER'S SETTINGS AT RUNTIME — the live surface over a composition
// description, and the one place that surface is allowed to live.
//
// ⭐⭐ WHY THE FILE EXISTS BEFORE THE THING DOES (Max, 2026-08-23: "make a new
// class in composition, a blank — call it the composer's settings; that is where
// all the runtime lives").
//
// There WAS such a surface — ibValueListSettings and the Filter / Sort / Group
// value objects under it, in composition/listFilter.h. It went under the knife,
// deliberately and whole, because it had become a SECOND model of what a
// composition is: it kept its own storage, mirrored the description's parts
// without their arrays, and was wired to a composer that had a third copy of the
// same settings. Nothing read all three consistently, so a filter set one way was
// invisible the other.
//
// What is left is the single answer: the DESCRIPTION is the model
// (backend/compositionDescription.h). Filters, sorts, groupings, resources,
// levels and variants are data with arrays that can be walked, copied, compared
// and written out. A window edits a COPY of it and puts the copy back on OK; the
// composer reads it to build the query.
//
// ⚠ SO THIS IS A HOME, NOT A HALF-BUILT TENANT. The runtime wrapper gets built
// over the settled structure, in an operation of its own — a container that
// borrows a description, unfolds its arrays into script-visible collections,
// lets a script add and remove lines, and tells its source to refresh. Until
// then nothing here pretends to be it: an object that half exists is exactly
// what was just removed.
//
// ⚠ AND IT IS ONE SURFACE FOR BOTH. A list and a report differ in what their
// description CONTAINS, never in what it is — so whatever grows here serves both,
// the way the description already does.

#include "backend/compiler/enumUnit.h"        // ibValueEnumeration — a registered word IS a picker
#include "backend/compositionDescription.h"   // the words themselves, and the data they describe
#include "backend/system/value/composition/valueComposerField.h"   // the other half of the surface

// ============================ the pickers ==================================
//
// ⭐ A REGISTERED ENUMERATION IS THE EDITOR'S DROP-DOWN. A bare C++ enum is something no form can
// offer — which is exactly how a grouping's kind spent a long time being unchangeable. Registered,
// the same value becomes offerable, writable and script-visible at once.
//
// ⚠ THEY LIVE HERE, NOT WITH THE DESCRIPTION. A description holds no runtime at all; a stored value
// is the only live type it is allowed to know. These are runtime — the first tenants of this file,
// and the part of the old wrapper that had to survive it, because a settings window without its
// drop-downs is a settings window nobody can use.

class BACKEND_API ibValueEnumFilterGroupKind : public ibValueEnumeration<ibFilterGroupKind> {
public:
	ibValueEnumFilterGroupKind() : ibValueEnumeration() {}
	virtual void CreateEnumeration() override {
		// Captions are the OPERATOR alone — the choice opens on a group's operator cell, and it
		// must offer what it sets.
		AddEnumeration(ibFilterGroupKind_And, wxT("And"), _("And"));
		AddEnumeration(ibFilterGroupKind_Or,  wxT("Or"),  _("Or"));
		AddEnumeration(ibFilterGroupKind_Not, wxT("Not"), _("Not"));
	}
};

class BACKEND_API ibValueEnumFilterDisplayMode : public ibValueEnumeration<ibFilterDisplayMode> {
public:
	ibValueEnumFilterDisplayMode() : ibValueEnumeration() {}
	virtual void CreateEnumeration() override {
		AddEnumeration(ibFilterDisplayMode_Normal,       wxT("Normal"),       _("Normal"));
		AddEnumeration(ibFilterDisplayMode_QuickAccess,  wxT("QuickAccess"),  _("Quick access"));
		AddEnumeration(ibFilterDisplayMode_Inaccessible, wxT("Inaccessible"), _("Inaccessible"));
	}
};

// HOW A GROUPING UNFOLDS — the language's own three words, over the one enum every tier names
// (query/queryUnfold.h). There is no second vocabulary to keep in step, which is the point.
class BACKEND_API ibValueEnumGroupKind : public ibValueEnumeration<ibQueryDimUnfold> {
public:
	ibValueEnumGroupKind() : ibValueEnumeration() {}
	virtual void CreateEnumeration() override {
		AddEnumeration(ibQueryDimUnfold::Elements,      wxT("Elements"),      _("Elements"));
		AddEnumeration(ibQueryDimUnfold::Hierarchy,     wxT("Hierarchy"),     _("Elements and hierarchy"));
		AddEnumeration(ibQueryDimUnfold::HierarchyOnly, wxT("HierarchyOnly"), _("Hierarchy only"));
	}
};

class BACKEND_API ibValueEnumComparisonKind : public ibValueEnumeration<ibComparisonKind> {
public:
	ibValueEnumComparisonKind() : ibValueEnumeration() {}
	virtual void CreateEnumeration() override {
		AddEnumeration(ibComparisonKind_Equal,        wxT("Equal"),        _("Equal"));
		AddEnumeration(ibComparisonKind_NotEqual,     wxT("NotEqual"),     _("Not equal"));
		AddEnumeration(ibComparisonKind_Greater,      wxT("Greater"),      _("Greater"));
		AddEnumeration(ibComparisonKind_Less,         wxT("Less"),         _("Less"));
		AddEnumeration(ibComparisonKind_GreaterEqual, wxT("GreaterEqual"), _("Greater or equal"));
		AddEnumeration(ibComparisonKind_LessEqual,    wxT("LessEqual"),    _("Less or equal"));
		AddEnumeration(ibComparisonKind_Contains,     wxT("Contains"),     _("Contains"));
		// Naming a comparison here is what makes it offerable — the language could say both of
		// these long before a filter could.
		AddEnumeration(ibComparisonKind_In,           wxT("In"),           _("In"));
		AddEnumeration(ibComparisonKind_InHierarchy,  wxT("InHierarchy"),  _("In hierarchy"));
	}
};

class BACKEND_API ibValueEnumSortDirection : public ibValueEnumeration<ibSortDirection> {
public:
	ibValueEnumSortDirection() : ibValueEnumeration() {}
	virtual void CreateEnumeration() override {
		AddEnumeration(ibSortDirection_Ascending,  wxT("Ascending"),  _("Ascending"));
		AddEnumeration(ibSortDirection_Descending, wxT("Descending"), _("Descending"));
	}
};

#endif // __VALUE_COMPOSER_SETTINGS_H__
