#ifndef __TEMP_COLUMN_H__
#define __TEMP_COLUMN_H__

// A column with no metaobject behind it — the column of a source the engine makes for itself: a temp table
// in memory, a temp table in the base, a named query (tempTableQueryable.h). Both providers' sources are built
// of it, so it lives in the face.

#include "backend/query/queryColumn.h"   // ibBackendQueryColumn — the contract it answers

// A generic temp-table column — name + type + source-id. NO attribute behind it; the
// source-id (GetColumnId) is the key the RAM table stores the column's value at.
// `metaData` — the configuration whose values it holds (GetMetaData), its maker's.
class ibTempColumn : public ibBackendQueryColumn
{
public:
	ibTempColumn(const ibString& name, const ibTypeDescription& type, ibMetaID modelId, const ibMetaData* metaData)
		: m_name(name), m_physical(name), m_type(type), m_modelId(modelId), m_metaData(metaData) {}
	// …and one whose value is told apart from its declaration, as a declared field's is: `AnyRef` declared, the
	// configuration's references held (ibOpenBarriers). The declaration is what it shows; what it holds is what a
	// walk and a picker read (GetTypeValueDesc).
	ibTempColumn(const ibString& name, const ibTypeDescription& type, const ibTypeDescription& typeValue, ibMetaID modelId,
	             const ibMetaData* metaData)
		: m_name(name), m_physical(name), m_type(type), m_typeValue(typeValue), m_modelId(modelId), m_metaData(metaData) {}

	// ⭐ THE NAME A QUERY WRITES AND THE NAME THE STORAGE KEEPS ARE TWO DIFFERENT NAMES.
	//
	// For a temp table they are the same string and that is right — its columns are named by whoever
	// made it. A register's TOTALS view is the other case: its columns live in a generated table
	// (`fld1124_D`, `fld1124_D_Week`, `Resource1_Receipt`), and those names reached the constructor's
	// catalogue exactly as stored — so the field list of `Turnovers` read as a dump of a physical
	// schema instead of `Period`, `PeriodWeek`, `Resource1Turnover`.
	//
	// One string became two: the renderer keeps asking for the PHYSICAL name (that is the contract
	// with the generated table, and it must not drift), everybody who shows or resolves a field asks
	// for the ordinary one.
	ibTempColumn(const ibString& name, const ibString& physical,
	             const ibTypeDescription& type, ibMetaID modelId, const ibMetaData* metaData,
	             const ibString& synonym = ibEmptyString, Kind kind = Kind::Composite)
		: m_name(name), m_physical(physical), m_type(type), m_modelId(modelId), m_metaData(metaData), m_synonym(synonym),
		  m_kind(kind) {}

	// ⭐ THE PICTURE THIS COLUMN IS DRAWN WITH — that of what it stands for (StandsFor), asked of it: a derived
	// surface's column is computed — a balance, a turnover, a side of a dimension — and is drawn as the
	// declaration it is a figure of. Standing for nothing, the base's default, which is right for an ordinary
	// temp table: its columns are made by whoever filled it.
	ibPictureDescription GetColumnPicture() const override {
		return m_standsFor != nullptr ? m_standsFor->GetColumnPicture() : ibBackendQueryColumn::GetColumnPicture();
	}

	// ⭐ A TEMP TABLE'S COLUMN IS COMPOSITE — it has real fields and spreads into them. A DECLARED
	// query's may not: an output with no column behind it is COMPUTED, one field read by name, and
	// saying so is what stops the reader looking for a `_TYPE` a constant cannot have.
	Kind GetColumnKind() const override { return m_kind; }

	ibString           GetName()         const override { return m_name; }
	ibString           GetPhysicalName() const override { return m_physical; }
	ibTypeDescription& GetTypeDesc()     const override { return m_type; }   // interface returns a non-const ref
	ibTypeDescription& GetTypeValueDesc() const override { return m_typeValue.GetClsidCount() > 0 ? m_typeValue : m_type; }
	ibMetaID           GetColumnId()      const override { return m_modelId; }

	// ⭐ AND A THIRD NAME: THE ONE A PERSON READS. The two above are for the query and for the
	// storage; neither is a caption. A derived surface publishes figures like `Resource1Balance`,
	// which is a correct NAME and a poor thing to put at the head of a column an accountant reads —
	// and the runtime tables that show these rows used to build their captions separately, which is
	// why the same figure had a presentation through one door and none through the other.
	//
	// Empty = the name, which is the base class's own answer and right for an ordinary temp table:
	// its columns are named by whoever made it, and that name IS the caption.
	ibString           GetSynonym()      const override { return m_synonym.IsEmpty() ? m_name : m_synonym; }

	// ⭐ WHAT IT STANDS FOR, when that is something the configuration declared — a register's dimension,
	// resource or analytics slot published into a view: its QUERY face (the attribute's bridge), handed over by
	// the publisher, and the road to the attribute from there — its picture, its availability.
	ibTempColumn& StandsFor(const std::shared_ptr<const ibBackendQueryColumn>& declared) { m_standsFor = declared; return *this; }

	// …AND WHAT IT IS IN A BALANCE (ibBalanceRole), handed over at the same moment by the view that
	// publishes it. An ordinary temp table's column is an ordinary value.
	// `rank` — a period's seniority (GetPeriodRank): 1 the period, 2 a recorder, 3 a line number.
	ibTempColumn& PlaysInBalance(ibBalanceRole role, int rank = 0) { m_balanceRole = role; m_periodRank = rank; return *this; }
	ibBalanceRole GetBalanceRole() const override { return m_balanceRole; }
	int           GetPeriodRank()  const override { return m_periodRank; }

	// ⭐ AVAILABLE AS WHAT IT STANDS FOR (functional options) — asked live, so a view column of a dimension this
	// base does not use goes with the dimension. An ordinary temp table's column stands for nothing: available.
	bool IsAvailable() const override { return m_standsFor == nullptr || m_standsFor->IsAvailable(); }

	const ibMetaData* GetMetaData() const override { return m_metaData; }

private:
	ibString                  m_name;
	ibString                  m_physical;   // == m_name unless the storage spells it differently
	mutable ibTypeDescription m_type;     // mutable: GetTypeDesc() is const but returns a non-const ref
	mutable ibTypeDescription m_typeValue;   // what it holds; empty = the declaration answers
	ibMetaID                  m_modelId;
	const ibMetaData*         m_metaData;   // see GetMetaData
	ibString                  m_synonym;    // empty = the name
	Kind                      m_kind = Kind::Composite;   // see GetColumnKind
	std::shared_ptr<const ibBackendQueryColumn> m_standsFor;   // see StandsFor
	ibBalanceRole                m_balanceRole = ibBalanceRole::None;   // see PlaysInBalance
	int                          m_periodRank  = 0;
};

#endif // __TEMP_COLUMN_H__
