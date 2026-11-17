#ifndef __LOG_QUERYABLE_H__
#define __LOG_QUERYABLE_H__

// THE REGISTRATION JOURNAL AS A QUERY SOURCE — `Journal.Events`. The base keeps its journal in SQLite month files
// (backend/logger), outside its own database, so no statement of the base can read it: this source reads the rows
// through ibLoggerReader — the reader the journal window and the assistant's journal_read use — and the computed
// provider takes them from there, which no statement of the base ever joins (CanJoinOn). A query joins it to the
// configuration's tables as it joins any source read in memory: each side by its own provider, stitched in memory. It
// is one of the platform's sources (ibPlatformSourceRegister): every configuration's factory makes its own, because
// the journal's Object is a reference of that configuration.

#include "backend/query/queryableFactory.h"                      // ibQueryableSourceDescriptor — what the factory finds by name
#include "backend/query/tempColumn.h"                            // ibTempColumn — its columns

#include <memory>
#include <vector>

// THE JOURNAL'S EVENTS, read for one configuration over one period — a virtual table, as a register's turnovers:
// `Journal.Events(&BeginOfPeriod, &EndOfPeriod)`, both moments inclusive, either left out (empty) for no bound. Its columns are no
// metaobject's, so their ids are synthetic (SyntheticKind::Derived over their place) and can never be a
// configuration's number; a source whose numbers they meet in one query is the lowering's to wrap, as for any other.
class ibLogQueryable : public ibBackendQueryable
{
public:
	explicit ibLogQueryable(const ibMetaData* metaData, const ibDateTime& begin = ibDateTime(), const ibDateTime& end = ibDateTime());

	virtual ibBackendQueryProvider& GetProvider() const override;
	virtual bool ContainColumn(const std::shared_ptr<const ibBackendQueryColumn>& col) const override;
	virtual std::vector<std::shared_ptr<const ibBackendQueryColumn>> GetQueryColumns() const override { return m_columns; }
	virtual ibString GetQueryTableName() const override { return ibEmptyString; }   // no table of the base stands behind it
	virtual ibString GetQueryName() const override;
	virtual const ibMetaData* GetMetaData() const override { return m_metaData; }
	// The journal window's right — the one that opens "Active users" beside it (ibSchemaAuditLog::AccessRight) —
	// asked by the access policy at the door, so SELECT ALLOWED reads a refusal as no rows.
	virtual bool AccessRight_Show() const override;

	// THE ONE READ OF THE JOURNAL — the provider's and the composer's alike. The period is applied here, exactly; the
	// conditions it can narrow by go to the reader (the date, the level, the user, the session, the source, the event,
	// the object), and the provider filters every condition again over what comes back.
	virtual ibQueryRamTable ComputeRows(const std::vector<ibQueryCondition>& extra) const override;

private:
	const ibMetaData* m_metaData = nullptr;
	ibDateTime m_begin;   // the first moment read, inclusive; empty = from the first event
	ibDateTime m_end;     // the last moment read, inclusive; empty = to the last event

	std::shared_ptr<const ibBackendQueryColumn> m_date;
	std::shared_ptr<const ibBackendQueryColumn> m_level;
	std::shared_ptr<const ibBackendQueryColumn> m_session;
	std::shared_ptr<const ibBackendQueryColumn> m_user;
	std::shared_ptr<const ibBackendQueryColumn> m_host;
	std::shared_ptr<const ibBackendQueryColumn> m_source;
	std::shared_ptr<const ibBackendQueryColumn> m_event;
	std::shared_ptr<const ibBackendQueryColumn> m_message;
	std::shared_ptr<const ibBackendQueryColumn> m_object;
	std::shared_ptr<const ibBackendQueryColumn> m_details;
	std::vector<std::shared_ptr<const ibBackendQueryColumn>> m_columns;   // the ten above, in the order a query lists them
};

// `Journal.Events` in the factory of one configuration — a reading made for every call, with its period or without
// one: the factory is made as the configuration starts to load, before the references its Object may be are there.
class ibLogQueryDescriptor : public ibQueryableSourceDescriptor
{
public:
	explicit ibLogQueryDescriptor(const ibMetaData* metaData);

	virtual ibString GetNamespace() const override;
	virtual ibString GetName() const override;
	virtual ibPictureID GetPicture() const override;   // the journal window's own — no class stands behind `Journal`
	virtual std::shared_ptr<const ibBackendQueryable> CreateQueryable(ibValue** paParams, long lSizeArray,
		const std::vector<ibQueryPredicatePtr>& conditions, const ibQueryReadColumns& read) override;
	// BeginOfPeriod, EndOfPeriod — the turnovers' words, in their order.
	virtual void DescribeParameters(std::vector<ibQuerySourceParameter>& out) const override;
	virtual std::vector<std::shared_ptr<const ibBackendQueryColumn>> GetQueryColumns(const std::vector<ibValue>& args) const override;

private:
	const ibMetaData* m_metaData = nullptr;   // the configuration its readings belong to
};

#endif
