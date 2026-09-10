#ifndef __CHART_OF_CALCULATION_TYPES_RELATION_TABLE_H__
#define __CHART_OF_CALCULATION_TYPES_RELATION_TABLE_H__

#include "backend/metaCollection/table/metaTableObject.h"

//********************************************************************************************
//*       Predefined tabular section of a Chart of Calculation Types: ONE RELATION           *
//*                                                                                          *
//*       ONE ROW = ONE EDGE: "the calculation type that owns this row is related to the     *
//*       type the row names". A chart carries three of them, and they are the same table:   *
//*         Displacing — the owner's action period is cut away by the named type;            *
//*         Base       — the named type's results make up the owner's base;                  *
//*         Leading    — a change to the named type's records makes the owner's records      *
//*                      need recalculating.                                                 *
//*       The relations are authored as DATA on the chart's own records — which type          *
//*       displaces, feeds or leads which is a decision of the payroll being built, not of   *
//*       the platform.                                                                      *
//*                                                                                          *
//*       Shaped after ibValueMetaObjectAccountDimensionKindsTable, which answers the same    *
//*       structural question for a chart of accounts: a predefined section whose rows belong *
//*       to a SAVED record of the owning chart.                                              *
//********************************************************************************************

// ONE CLASS, THREE INSTANCES — and not three classes. The three sections differ in NAME and in who
// reads them; their shape (one column naming a calculation type of the same chart), their storage and
// their life are identical, which is what a class is for. It began as a class of its own for the
// displacement relation; the base and the leading relations were the second and third copy of it
// waiting to be written. The instance is named by the property that holds it (ibPropertyContainer
// passes its own name down), so `Displacing`, `Base` and `Leading` come from the chart, not from here.
//
// DB-BACKED, therefore ibValueMetaObjectTableDataRef and not the shared base — the same reason the
// account dimension kinds table gives, and the same scar behind it: the base is abstract about
// storage (its GetQueryable() answers null, it has no physical table), which reads back fine and
// fails only where a row is WRITTEN. These rows belong to a saved calculation type, so the Ref
// variant's L4 source descriptor is what is needed.
class ibValueMetaObjectCalculationTypeRelationTable : public ibValueMetaObjectTableDataRef {
public:

	ibValueMetaObjectCalculationTypeRelationTable(const wxString& name, const wxString& synonym, const wxString& comment = wxEmptyString);
	ibValueMetaObjectCalculationTypeRelationTable();
	virtual ~ibValueMetaObjectCalculationTypeRelationTable();

	// The one predefined column: WHICH calculation type the owner of this row is related to.
	ibValueMetaObjectAttributePredefined* GetCalculationType() const { return m_propertyCalculationType->GetMetaObject(); }

	// ⚠ A SECTION THE SAVED CHART NEVER CARRIED. A chart written before this section (or its column)
	// existed has no node for it, so the load leaves it at id 0 — and two such sections would then be
	// ONE table name. The ids are handed out here, by the create event, exactly as a slot born after its
	// register's creation asks for its own (accountingRegisterMetadata.cpp). Called from the chart's
	// before-run, never from the load: GenerateNewID seeds itself ONCE from the tree it can see, and a
	// half-loaded tree would seed it below ids that are about to arrive.
	//
	// 🛑⭐ AND ONLY IN THE COPY THAT SAVES ITSELF. The schema is the diff of two snapshots and nothing
	// else (the database is never asked what it holds), and both copies — the one the database keeps
	// and the one being edited — are loaded by the same code. Stamped in both, the section is in both
	// snapshots, the diff is empty and the table is never created: MEASURED 2026-09-10, database_diff
	// answered "the database already holds this configuration" over a Base section it had no table for.
	// So the copy that mirrors the database leaves it at 0, and a part at 0 is not part of anything —
	// no table (the chart's list), no column (below), no relation (ReadRelation) — until the edited copy,
	// which did stamp it, is applied and saved.
	bool StampIfNeverSaved(ibMetaData* metaData);
	bool IsNeverSaved() const { return GetMetaID() == 0; }

	// …and the section SAYS it is not there, through the one question every metadata walk asks first
	// (FillArrayObjectByFilter tests IsAllowed, which asks this) — so the schema, the object's sections
	// and the queries all leave it out without any of them knowing why.
	virtual bool IsEnabled() const override {
		return !IsNeverSaved() && ibValueMetaObjectTableDataRef::IsEnabled();
	}

	//events
	virtual bool OnCreateMetaObject(ibMetaData* metaData, int flags);
	virtual bool OnLoadMetaObject(ibMetaData* metaData);
	virtual bool OnSaveMetaObject(int flags);
	virtual bool OnDeleteMetaObject();

	virtual bool OnBeforeRunMetaObject(int flags);
	virtual bool OnAfterRunMetaObject(int flags);
	virtual bool OnBeforeCloseMetaObject();
	virtual bool OnAfterCloseMetaObject();

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

protected:

	virtual bool FillArrayObjectByPredefinedAttribute(std::vector<ibValueMetaObjectAttributeBase*>& array) const override {
		ibValueMetaObjectTableDataRef::FillArrayObjectByPredefinedAttribute(array);
		// A column the saved section never carried is not a column of it yet — see StampIfNeverSaved.
		if ((*m_propertyCalculationType)->GetMetaID() != 0)
			array.push_back(m_propertyCalculationType->GetMetaObject());
		return true;
	}

private:

	ibPropertyCategory* m_categoryRelation = ibPropertyObject::CreatePropertyCategory(wxT("Relation"), _("Relation"));

	// FILL-CHECKED: a row of this table IS an edge. A row with no type named carries an edge to
	// nothing — for the displacement relation it would be a displacer that does not exist, which is
	// indistinguishable from a type that declares no displacers at all.
	//
	// ⭐ NO `Priority` COLUMN, and this is the same decision the account dimension kinds table made
	// about `Order`. What the edges imply is DERIVED from them where it is used
	// (ibComputeActionPeriodDisplacementByRelation for Displacing) — a number stored beside the row
	// would be a second spelling of what the edges already say, and the stored one is the one that can
	// disagree with them.
	//
	// The type is left EMPTY here and bound to the OWNING chart by the chart itself, in
	// ibValueMetaObjectChartOfCalculationTypes::OnBeforeRunMetaObject — the same place and the same way
	// its Parent attribute is typed as a self-reference. A chart's relations run between its own
	// calculation types, and naming the target by class rather than by the owner would let a chart
	// declare an edge into a different chart. (This note once promised the binding before anything
	// performed it; an untyped column then stored a type tag and no reference, and the relation read
	// back empty.)
	ibPropertyContainer<>* m_propertyCalculationType = ibPropertyObject::CreateProperty<ibPropertyContainer<>>(m_categoryRelation,
		ibValueMetaObjectCompositeData::CreateEmptyType(wxT("CalculationType"), _("Calculation type"), wxEmptyString, /*fillCheck*/ true, ibItemMode::ibItemMode_Item));
};

#endif
