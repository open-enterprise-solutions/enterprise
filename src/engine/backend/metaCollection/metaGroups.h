#ifndef _META_GROUPS_H__
#define _META_GROUPS_H__

////////////////////////////////////////////////////////////////////////////
//	Description : the groups a configuration is read in — one declaration
////////////////////////////////////////////////////////////////////////////
//
// ⭐⭐ WHAT A METATYPE'S GROUP IS CALLED, AND WHERE IT STANDS. One answer, asked by everything that
// lists metaobjects by kind:
//
//   the configuration tree   (designer/mainFrame/metaTree/treeConfiguration_impl.cpp)
//   the comparison tree      (backend/metaCollection/metaDiff.cpp)
//   the role editor          (designer/win/editor/roleEditor)
//   the section editor       (designer/win/editor/interfaceEditor)
//
// Each of them used to keep its own copy, and the copies had drifted in both directions:
// "Information Registers" in two of them against "Information registers" in the others; the
// comparison tree listed calculation registers BEFORE information registers while the configuration
// tree listed them last; the charts came in two different orders; one tree called an enumeration's
// values "Enums" and the other "Enum values". Two of the four had no order at all — their groups
// appeared as the metaobjects happened to be walked — and showed the metatype's registered class
// name ("CalculationRegister") where the tree showed a caption (Max, 2026-09-17: "the order does not
// match the way it is shown in the tree").
//
// ⭐ THE ORDER IS THE READING ORDER OF A CONFIGURATION, and it is a decision, not an accident: a
// register is expressed in terms of what stands above it — an accumulation register by its
// dimensions, an accounting register by the chart of accounts that types its account — so the
// charts come before the registers and the registers come last. Inside an object the same rule
// holds: what the object IS (its attributes, its dimensions, its resources) before what is done
// with it (its forms, its commands, its templates).
//
// A metatype not declared here is not an error: it answers with no caption (the caller shows the
// name the type registered for itself) and sorts after every declared one. That is what a plugin's
// kind gets, and it is a place in the list, not a crash.
//
// ⚠ THE CAPTION IS A GROUP'S, NOT A METAOBJECT'S. "Catalogs" is what the branch holding catalogs is
// called; a catalog's own name comes from the catalog.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/backend_core.h"
#include "backend/clsid.h"

#include <wx/string.h>

// WHERE A GROUP BELONGS — the third fact about it, beside its caption and its place.
//
//   Common    — the configuration's own things: its modules, its pictures, its roles. The
//               configuration tree gathers them under one folder and the comparison tree under one
//               umbrella; both used to decide that from the rank's NUMBER (10..70 meant common),
//               which is a fact hidden in an arithmetic range.
//   Metadata  — the business objects: catalogs, documents, registers.
//   Inner     — the groups INSIDE one object: its attributes, its forms, its templates.
enum class ibMetaGroupBand { Common, Metadata, Inner };

// The caption of the group `clsid` is listed under, translated for the session's language.
// Empty for a metatype this table does not name.
BACKEND_API wxString ibMetaGroupCaption(const ibClassID& clsid);

// The band that group stands in. A kind this table does not name answers Inner — it is not put in
// either of the configuration's two top bands by guesswork.
BACKEND_API ibMetaGroupBand ibMetaGroupBandOf(const ibClassID& clsid);

// Where that group stands among the others — lower comes first. A metatype this table does not name
// answers a rank past every declared one, so it lands at the end instead of at the front.
BACKEND_API int ibMetaGroupOrder(const ibClassID& clsid);

#endif // !_META_GROUPS_H__
