#ifndef __META_COMPOSER_OBJECT_H__
#define __META_COMPOSER_OBJECT_H__

////////////////////////////////////////////////////////////////////////////
//	Description : composer object — WHAT A REPORT READS AND HOW IT IS LAID OUT
////////////////////////////////////////////////////////////////////////////
//
// ⭐ A COMPOSER IS DECLARED, NOT ADDED AS AN ATTRIBUTE. It sits inside the report the way a form, a
// template or a tabular section does — and that is the whole difference (Max, 2026-08-20: "an
// attribute you add by hand; this type lives in the object"). Declaring one is what makes
// `Object.<Name>` exist, so nobody has to remember to create an attribute beside it and keep the
// two in step.
//
// The report's DEFAULT composer decides what the report IS: the generated form is built from it, so
// a report that declares one needs no form at all. Several composers = several of them on the form,
// which is how one report shows four different compositions. (docs/report-engine.md §4b)
//
// It carries the composition itself — source, query, resources, parameters, structure, variants —
// through ibValueDataComposition, which already serialises whole (ReadProperty / WriteProperty), so
// storing it here is one call in each direction rather than a second copy of those rules.
//
// ⚠ What is stored here is the DEFAULT of the user's settings, not a separate "author's schema":
// saved settings (the next arc) replace it as a SNAPSHOT, and what no longer resolves is dropped by
// ibDataComposer::PruneUnresolvedSettings, which answers HOW MUCH went so a person can be told.

#include "backend/metaCollection/metaObject.h"
#include "backend/query/queryColumn.h"                   // ibBackendSourceColumn — a composer IS a column of its report
#include "backend/system/value/valueDataComposition.h"
#include "backend/propertyManager/property/propertyComposition.h"   // the composition lives in a PROPERTY, like a template's sheet

// ⭐ AND IT IS A COLUMN OF THE OBJECT THAT DECLARES IT — the same way an attribute is. That is not
// decoration: the source-binding walk (WalkColumns) returns an ibBackendSourceColumn as the LEAF, so
// a node with no descriptor behind it cannot be walked to. Without this the composer showed up in
// the picker and nowhere else: the inspector fell back to the owner's type ("ReportObject.Report1"
// instead of the composition) and dragging one onto a form created nothing at all, because the drop
// asks the walk for the leaf's type first (Max, 2026-08-20).
class BACKEND_API ibValueMetaObjectComposer : public ibValueMetaObject, public ibBackendSourceColumn {
protected:

public:

	ibValueMetaObjectComposer();
	virtual ~ibValueMetaObjectComposer();

	// THE COMPOSITION THIS METAOBJECT DECLARES. Handed out, never copied: the settings window edits
	// this very object, and a copy handed to it would be the one the user is looking at while the
	// saved one stayed behind. It is held by the property below — one place, which is also the one
	// that saves it.
	// (NO LIVE COMPOSITION HERE EITHER. A metaobject DECLARES — what it has is the description below,
	//  and that is what the designer edits and what a report copies when it opens. Whoever needs a
	//  running composition builds one over this description and owns it themselves.)

	// ⭐ WHAT IT DECLARES, AS DATA — reached STRAIGHT FROM THE METAOBJECT and managed there, the way a
	// tabular section's value is reached in a value table: a reference to change in place, and a CONST
	// one for whoever only reads. A report seeding its object's composition takes a COPY of this; it
	// does not serialise one out and read it back.
	ibCompositionDescription& GetCompositionDesc() { return m_propertyComposition->GetValueAsCompositionDesc(); }
	const ibCompositionDescription& GetCompositionDesc() const { return m_propertyComposition->GetValueAsCompositionDesc(); }
	void SetCompositionDesc(const ibCompositionDescription& desc) { m_propertyComposition->SetValue(desc); }

	// ⚠ TWO BASES ANSWER THESE, so the metaobject's answers are named as the ones. A composer is a
	// metaobject that ALSO wears a column face; its name, its synonym and whether it is allowed are
	// facts about the metaobject, and the column face inherits them rather than holding its own.
	virtual wxString GetName()    const override { return ibValueMetaObject::GetName(); }
	virtual wxString GetSynonym() const override { return ibValueMetaObject::GetSynonym(); }
	virtual wxString GetComment() const override { return ibValueMetaObject::GetComment(); }
	virtual bool     IsAllowed()  const override { return ibValueMetaObject::IsAllowed(); }

	// The column face: what this "field" of the report holds — a composition, always.
	virtual ibTypeDescription& GetTypeDesc() const override { return m_typeDesc; }
	// A metaobject column wears its own picture, like an attribute does.
	virtual wxIcon GetColumnIcon() const override { return GetIcon(); }

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//events — the owner is told, so the FIRST composer becomes the report's default one
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);
	virtual bool OnDeleteMetaObject();

	// ⭐ …and the owner's OBJECT is told that its fields changed — a composition is a field of it,
	// exactly as an attribute and a tabular section are, so this is their road (see the .cpp).
	virtual bool OnAfterRunMetaObject(int flags);
	virtual bool OnReloadMetaObject();

	//module manager is started or exit
	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterCloseMetaObject();

	//prepare menu for item
	virtual bool CollectContextMenu(std::vector<ibMetaMenuItem>& items);

	//read & write
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

	// THE COMPOSITION BELOW SAID IT CHANGED — mark the configuration modified. The composition is
	// this metaobject's structural child (the ctor names it), and this is the top of that chain.
	virtual void OnChildChanged() override;

private:
	// ⭐ THE COMPOSITION LIVES IN A PROPERTY, the way a template's spreadsheet does
	// (ibValueMetaObjectSpreadsheet / ibPropertySpreadsheet). That is what makes this metatype's
	// serialisation the same two symmetric lines every other metatype has — one property out, one
	// property in — and what keeps its node's CHILDREN area for metaobjects alone.
	ibPropertyCategory*    m_categoryComposer = ibPropertyObject::CreatePropertyCategory(wxT("Composer"), _("Composer"));
	ibPropertyComposition* m_propertyComposition = ibPropertyObject::CreateProperty<ibPropertyComposition>(m_categoryComposer, wxT("CompositionData"), _("Composition data"));


	// Stated once, never derived: the column face answers "a composition" for every composer.
	// Mutable because the column contract hands back a non-const reference from a const method.
	mutable ibTypeDescription m_typeDesc;
};

#endif // !__META_COMPOSER_OBJECT_H__
