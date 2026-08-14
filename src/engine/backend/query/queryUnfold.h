#ifndef __QUERY_UNFOLD_H__
#define __QUERY_UNFOLD_H__

////////////////////////////////////////////////////////////////////////////
//	Description : HOW FAR DOWN A HIERARCHY A VALUE REACHES - three words, three venues
////////////////////////////////////////////////////////////////////////////
//
//   Elements       «in»            - exactly the values named, and nothing under them
//   Hierarchy      «in hierarchy»  - each value AND everything subordinate to it
//   HierarchyOnly                  - the subordinates WITHOUT the value that names them
//
// ⭐⭐ ONE VOCABULARY, AND IT LIVES ALONE FOR THAT REASON. The three words are said in a GROUPING
// (`TOTALS BY x HIERARCHY`, an L4 concept), in a FILTER (`x IN HIERARCHY (&p)`, also L4) and in a
// CONDITION handed to a source (an L3 `ibQueryCondition`, which may carry the word so the source can
// FOLD by it rather than merely select). Three tiers, and a report, a filter and a reading that all
// say "in hierarchy" have to mean the same thing by it.
//
// It used to sit in queryAst.h, which is deliberately L2/L3-FREE — so an L3 condition could not name
// it without dragging the whole L4 tree down a tier. A file with one enum and no dependencies is the
// cheapest thing both tiers can include, and it is the reason this header exists at all.
//
// PLAIN enum, not enum class: it is a registered runtime enumeration (ibValueEnumGroupKind), and
// ibValueEnumeration<T> converts its value to a number. Qualified use (ibQueryDimUnfold::Elements)
// stays legal either way.

enum ibQueryDimUnfold { Elements, Hierarchy, HierarchyOnly };

#endif // __QUERY_UNFOLD_H__
