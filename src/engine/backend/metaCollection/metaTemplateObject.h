#ifndef __META_TEMPLATE_OBJECT_H__
#define __META_TEMPLATE_OBJECT_H__

////////////////////////////////////////////////////////////////////////////
//	Description : template object — a spreadsheet or a composition, by its kind
////////////////////////////////////////////////////////////////////////////
//
// ⭐ ONE METAOBJECT, ITS KIND CHOSEN WHEN IT IS ADDED — the way a form's is (Max, 2026-11-03). What it holds is
// ONE description (core/description/templateDescription.h): the kind and the schema of that kind. A SPREADSHEET
// holds the cells, named areas and parameters a print form is assembled from; a COMPOSER holds a composition —
// what a report reads and how it is laid out. The owner's GetTemplate(name) hands code a fresh value of either,
// and the designer opens the kind's editor.
//
// ⭐ IN A REPORT A COMPOSER IS DECLARED, NOT ADDED AS AN ATTRIBUTE. It sits inside the report the way a form does
// (Max, 2026-08-20: "an attribute you add by hand; this type lives in the object"). Declaring one is what makes
// `Object.<Name>` exist. The report's DEFAULT composer decides what the report IS: the generated form is built
// from it, so a report that declares one needs no form at all. (docs/private/report-engine.md §4b) Under any other
// owner — a document, a data processor — it is what code takes with GetTemplate(name) and composes into a sheet.
//
// ⚠ What a composer stores is the DEFAULT of the user's settings, not a separate "author's schema": saved
// settings replace it as a SNAPSHOT, and what no longer resolves is dropped by
// ibDataComposer::PruneUnresolvedSettings, which answers HOW MUCH went so a person can be told.

#include "metaObject.h"
#include "backend/query/queryColumn.h"                           // ibBackendSourceColumn — a composer IS a column of its report
#include "backend/propertyManager/property/propertyTemplate.h"   // what it holds, of its kind

class ibTemplateProvider;

class BACKEND_API ibValueMetaObjectTemplateBase : public ibValueMetaObject {
public:

	//support icons
	virtual ibPicture GetIcon() const override;

	//prepare menu for item
	virtual bool CollectContextMenu(std::vector<ibMetaMenuItem>& items) override;

	// ⭐ WHAT IT HOLDS, by reference — its schema is edited in place, the other kind's asked for raises. Set, it is
	// a new cell (ibPropertyTemplate::SetValue) and nobody is told: a kind chosen goes through the property.
	ibTemplateDescription& GetTemplateDesc() const { return m_propertyTemplate->GetValueAsTemplateDesc(); }
	void SetTemplateDesc(const ibTemplateDescription& templateDesc) { m_propertyTemplate->SetValue(templateDesc); }

	// …and what a template of that kind does (ibTemplateProvider, below).
	const ibTemplateProvider& GetTemplateProvider() const;

	static ibPicture GetIconGroup();

protected:

	//read & write
	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

	ibPropertyCategory* m_categoryTemplate = ibPropertyObject::CreatePropertyCategory(ibT("Template"), _("Template"));
	ibPropertyTemplate* m_propertyTemplate = ibPropertyObject::CreateProperty<ibPropertyTemplate>(m_categoryTemplate, ibT("TemplateData"), _("Template data"),
		_("What the template holds, its kind chosen when it is added: a spreadsheet document (cells, named areas and parameters a print form is assembled from) or a data composer (its query, resources, parameters, structure and settings variants). Code gets a fresh value of it with the owner's GetTemplate(name), a common one with GetCommonTemplate(name)."));
};

// ⭐ AS A COMPOSER IT IS A COLUMN OF THE OBJECT THAT DECLARES IT — the same way an attribute is. The source-binding
// walk (WalkColumns) returns an ibBackendSourceColumn as the LEAF, so a node with no descriptor behind it cannot be
// walked to: without this the inspector fell back to the owner's type and dragging one onto a form created nothing
// (Max, 2026-08-20). The report lists its composer templates alone as columns (GetComposerArrayObject), so a
// spreadsheet template is never walked to.
class BACKEND_API ibValueMetaObjectTemplate : public ibValueMetaObjectTemplateBase, public ibBackendSourceColumn {
public:

	ibValueMetaObjectTemplate();

	// ⚠ TWO BASES ANSWER THESE, so the metaobject's answers are named as the ones — the column face inherits
	// its name, synonym and whether it is allowed rather than holding its own.
	virtual ibString GetName()    const override { return ibValueMetaObject::GetName(); }
	virtual ibString GetSynonym() const override { return ibValueMetaObject::GetSynonym(); }
	virtual ibString GetComment() const override { return ibValueMetaObject::GetComment(); }
	virtual bool     IsAllowed()  const override { return ibValueMetaObject::IsAllowed(); }

	// The column face: what this "field" of the report holds — a composition, always.
	virtual ibTypeDescription& GetTypeDesc() const override { return m_typeDesc; }
	// A metaobject column wears its own picture, like an attribute does.
	virtual ibPictureDescription GetColumnPicture() const override { return GetClassType(); }

	//events — and the provider of its kind's (a composer's report is told, ibComposerTemplateProvider)
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags) override;
	virtual bool OnDeleteMetaObject() override;
	virtual bool OnSaveMetaObject(int flags) override;
	virtual bool OnAfterRunMetaObject(int flags) override;
	virtual bool OnReloadMetaObject() override;

	// THE KIND CHOSEN — the provider of the kind it was lets it go, the provider of the kind it is takes it.
	virtual void OnPropertyChanged(ibProperty* property, const ibVariant& oldValue, const ibVariant& newValue) override;

	// THE COMPOSITION BELOW SAID IT CHANGED — mark the configuration modified.
	virtual void OnChildChanged() override;

private:

	// Stated once, never derived: the column face answers "a composition" for every composer.
	// Mutable because the column contract hands back a non-const reference from a const method.
	mutable ibTypeDescription m_typeDesc;
};

// A common template is a spreadsheet, always.
class BACKEND_API ibValueMetaObjectCommonTemplate : public ibValueMetaObjectTemplateBase {
public:
	ibValueMetaObjectCommonTemplate();
};

// ⭐ WHAT A TEMPLATE OF ONE KIND DOES — the events of its kind and the value code is handed, beside its schema
// (core/description/templateDescription.h). One provider per kind and without state, the way a format has its
// provider (ibFormatProvider): the template asks the provider of its kind and goes on, so a kind with behaviour of
// its own is a class in the .cpp rather than an `if` in every event of the template. One with nothing of its own
// answers the defaults.
class BACKEND_API ibTemplateProvider {
public:

	virtual ~ibTemplateProvider() = default;

	//events — the template's own, of its kind
	virtual bool OnCreateMetaObject(ibValueMetaObjectTemplate* metaTemplate, int flags) const { return true; }
	virtual bool OnDeleteMetaObject(ibValueMetaObjectTemplate* metaTemplate) const { return true; }
	virtual bool OnSaveMetaObject(ibValueMetaObjectTemplate* metaTemplate, int flags) const { return true; }
	virtual bool OnAfterRunMetaObject(ibValueMetaObjectTemplate* metaTemplate, int flags) const { return true; }
	virtual bool OnReloadMetaObject(ibValueMetaObjectTemplate* metaTemplate) const { return true; }

	//prepare menu for item
	virtual void CollectContextMenu(ibValueMetaObjectTemplateBase* metaTemplate, std::vector<ibMetaMenuItem>& items) const = 0;

	// What code is handed — the owner's GetTemplate(name), GetCommonTemplate(name) and the property's GetDataValue,
	// one road: a fresh value of the kind over a copy of its schema, made in the configuration the template belongs to.
	virtual ibValue GetDataValue(ibTemplateDescription& templateDesc, const ibMetaData* metaData) const = 0;

	// The kind's — one with none chosen yet has nothing of its own, as a spreadsheet has not.
	static const ibTemplateProvider& Get(ibTemplateType type);
};

#endif
