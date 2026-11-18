////////////////////////////////////////////////////////////////////////////
//	Description : the registration journal as a query source — Journal.Events, read through ibLoggerReader
//	              into memory and filtered there by the computed provider. See logQueryable.h.
////////////////////////////////////////////////////////////////////////////

#include "logQueryable.h"
#include "backend/appData.h"                                     // ibLog — the base's journal, where its files are
#include "backend/logger/logger.h"                               // ibLogger::GetLogDir
#include "backend/logger/loggerReader.h"                         // ibLoggerReader — the journal's one reader
#include "backend/session/session.h"                             // ibSession::Current — whose right is asked
#include "backend/metadataConfiguration.h"                       // ibMetaDataConfigurationBase::AccessRight_ActiveUsers
#include "backend/metaCollection/partial/reference/reference.h"  // ibValueReferenceDataObject — the Object of a row
#include "backend/compiler/typeCtor.h"                           // ibCtorAbstractType::GetClassType — the AnyRef family
#include "backend/objCtor.h"                                     // ibOpenBarriers — the references the Object may be
#include "backend/backend_exception.h"                           // ibBackendCoreException — a period that is not a date
#include "backend/metadataSerialization.h"                       // ibDeserializeValue — an event's details, as written
#include "core/serialize/dataBuilder.h"                          // ibDataNode + ibBinaryProvider — the details payload
#include "core/memory/memoryStream.h"                            // ibReaderMemory — the payload's bytes, read
#include "core/diagnostics/journal.h"                            // ibJournalWarning — a payload this process cannot read
#include "core/picturePredefined.h"                              // g_picUserActiveCLSID — the journal window's picture
#include "backend/query/provider/database/registerQueryLowering.h"   // ibRegArg — an argument slot, read as a register reads it

#include <algorithm>                                              // max, min — the narrowest period asked

static constexpr const ibChar* g_logNamespace = ibT("Journal");
static constexpr const ibChar* g_logName      = ibT("Events");

// AN EVENT'S DETAILS, as the value that was written — through the door that knows the configuration's types
// (ibDeserializeValue), because the payload worth journalling is a reference as often as not, and the value registry
// alone throws on one (journal_read found the same, mcpToolJournal.cpp). A payload this process cannot read — written
// by a version that is gone — leaves the cell empty and says so in the technology journal: the event itself is still
// worth its row.
static ibValue DetailsOf(const ibMemoryBuffer& details, const ibMetaData* metaData)
{
	ibReaderMemory reader(details);
	ibDataNode payload;
	if (!ibBinaryProvider().Read(reader, payload))
		return ibValue();
	try {
		return ibDeserializeValue(payload, metaData);
	}
	catch (const ibCoreException& err) {
		ibJournalWarning(ibT("query.road"), ibT("Journal.Events: an event's details could not be read - %s"),
		                 err.GetErrorDescription());
	}
	return ibValue();
}

// ⭐ THE ONE LINE THAT MAKES THE JOURNAL A SOURCE: every configuration's factory makes its own as it is made.
static const ibPlatformSourceRegister s_logSource(
	[](const ibMetaData* metaData) -> std::unique_ptr<ibQueryableSourceDescriptor> {
		return std::make_unique<ibLogQueryDescriptor>(metaData);
	});

//////////////////////////////////////////////////////////////////////
// ibLogQueryable
//////////////////////////////////////////////////////////////////////

ibLogQueryable::ibLogQueryable(const ibMetaData* metaData, const ibDateTime& begin, const ibDateTime& end)
	: m_metaData(metaData), m_begin(begin), m_end(end)
{
	const ibTypeDescription text(g_valueStringCLSID);
	// The object an event is about is a reference of any kind — a catalog's item, a document: declared `AnyRef`, and
	// holding the configuration's references, as a field declared so does — so a walk and a picker open it by type.
	const ibCtorAbstractType* const anyRef = ibValue::GetAvailableCtor(ibT("AnyRef"));
	const ibTypeDescription object = anyRef != nullptr ? ibTypeDescription(anyRef->GetClassType()) : ibTypeDescription();

	ibMetaID place = 0;
	const auto column = [this, &place](const ibString& name, const ibTypeDescription& type,
	                                   const ibTypeDescription& typeValue = ibTypeDescription()) {
		const std::shared_ptr<const ibBackendQueryColumn> made = std::make_shared<ibTempColumn>(name, type, typeValue,
			ibBackendQueryColumn::SyntheticId(ibBackendQueryColumn::SyntheticKind::Derived, ++place), m_metaData);
		m_columns.push_back(made);
		return made;
	};
	m_date    = column(ibT("Date"), ibTypeDescription(g_valueDateCLSID));
	m_level   = column(ibT("Level"), text);
	m_session = column(ibT("Session"), text);
	m_user    = column(ibT("User"), text);
	m_host    = column(ibT("Host"), text);
	m_source  = column(ibT("Source"), text);
	m_event   = column(ibT("Event"), text);
	m_message = column(ibT("Message"), text);
	m_object  = column(ibT("Object"), object, ibOpenBarriers(object, metaData));
	// What an event carried beside its words — a value of any type (a structure, mostly), as it was written.
	m_details = column(ibT("Details"), ibTypeDescription());
}

ibBackendQueryProvider& ibLogQueryable::GetProvider() const
{
	return ibComputedProviderInstance();   // its rows are built in memory, by ComputeRows below
}

// Its columns are no attributes, so the base's answer (by attribute) cannot find them: the column itself is asked.
bool ibLogQueryable::ContainColumn(const std::shared_ptr<const ibBackendQueryColumn>& col) const
{
	return std::find(m_columns.begin(), m_columns.end(), col) != m_columns.end();
}

ibString ibLogQueryable::GetQueryName() const
{
	return ibString(g_logNamespace) + ibT(".") + g_logName;
}

bool ibLogQueryable::AccessRight_Show() const
{
	const ibSession* const session = ibSession::Current();
	const ibMetaDataConfigurationBase* const configuration = session != nullptr ? session->GetMetaData() : nullptr;
	return configuration != nullptr && configuration->AccessRight_ActiveUsers();
}

ibQueryRamTable ibLogQueryable::ComputeRows(const std::vector<ibQueryCondition>& extra) const
{
	ibQueryRamTable rows;
	for (const std::shared_ptr<const ibBackendQueryColumn>& c : m_columns)
		rows.AddColumn(c->GetColumnId(), c->GetName(), c->GetTypeDesc());

	const ibLogger* const logger = ibLog;
	if (logger == nullptr)
		return rows;   // a base that keeps no journal has no events

	// WHAT THE READER CAN NARROW BY, so it does not hand back every month there is. Only ever WIDER than what was
	// asked — the period is cut exactly below, and the statement over these rows' copy filters by each condition
	// again (ibQueryComposer::OverMemorySpace):
	//   a moment — a date is the wall clock's reading and the file keeps the instant, so a moment is read as if
	//     the clock were UTC and its bound widened by a day each way, more than any zone is from UTC;
	//   the level — the reader's ladder: this level and up, the audit by itself;
	//   the object — by its guid;
	//   a word of the row — the user, the session, the source, the event — by equality, which the file decides as
	//     the query does: the text whole, the case kept.
	// A LIKE over the message is not narrowed: the file's LIKE minds the case of every letter past the Latin ones, the
	// query's of none, so the file would hand back fewer rows than the query asks for.
	constexpr long long day = 24LL * 60 * 60 * 1000;
	constexpr long long epoch = ibDateTime(1970, 1, 1).GetValue();
	ibLogFilter filter;
	filter.limit = 0;   // every row — the reader's own default is a page of a thousand
	const auto from = [&filter, epoch, day](const ibDateTime& moment) {
		filter.from_ms = std::max(filter.from_ms, moment.GetValue() - epoch - day);
	};
	const auto to = [&filter, epoch, day](const ibDateTime& moment) {
		const long long bound = moment.GetValue() - epoch + day;
		filter.to_ms = filter.to_ms == 0 ? bound : std::min(filter.to_ms, bound);
	};
	if (!m_begin.IsEmpty())
		from(m_begin);
	if (!m_end.IsEmpty())
		to(m_end);
	for (const ibQueryCondition& c : extra) {
		if (c.m_col == nullptr || !c.m_path.empty() || c.m_expr || c.m_valueExpr || c.m_semiJoin || c.m_asExists)
			continue;
		const ibMetaID id = c.m_col->GetColumnId();
		if (id == m_date->GetColumnId() && c.m_value.GetType() == ibValueTypes::TYPE_DATE) {
			if (c.m_op == ibQueryFilterOp::Equal || c.m_op == ibQueryFilterOp::Greater || c.m_op == ibQueryFilterOp::GreaterEqual)
				from(c.m_value.GetDate());
			if (c.m_op == ibQueryFilterOp::Equal || c.m_op == ibQueryFilterOp::Less || c.m_op == ibQueryFilterOp::LessEqual)
				to(c.m_value.GetDate());
		}
		else if (id == m_level->GetColumnId() && c.m_op == ibQueryFilterOp::Equal) {
			const int level = ibLevelFromWord(c.m_value.GetString());
			if (level >= 0)
				filter.min_level = level;
		}
		else if (id == m_object->GetColumnId() && c.m_op == ibQueryFilterOp::Equal) {
			ibValueReferenceDataObject* ref = nullptr;
			if (c.m_value.ConvertToValue(ref) && ref != nullptr && !ref->IsEmpty()) {
				const ibGuid guid = ref->GetGuid();
				filter.ref_guid = guid.str();
			}
		}
		else if (c.m_op == ibQueryFilterOp::Equal && c.m_value.GetType() == ibValueTypes::TYPE_STRING) {
			if (id == m_user->GetColumnId())
				filter.user_name = c.m_value.GetString();
			else if (id == m_session->GetColumnId())
				filter.session_id = c.m_value.GetString();
			else if (id == m_source->GetColumnId())
				filter.source = c.m_value.GetString();
			else if (id == m_event->GetColumnId())
				filter.event_type = c.m_value.GetString();
		}
	}

	ibLoggerReader reader(logger->GetLogDir());
	for (const ibLogRow& row : reader.Query(filter)) {
		// The period is this reading's own, so it is cut here, exactly, on the clock it was written in.
		const ibDateTime date = ibDateTime::OfInstant(row.ts_ms);
		if ((!m_begin.IsEmpty() && date < m_begin) || (!m_end.IsEmpty() && m_end < date))
			continue;
		const long r = rows.AppendRow();
		rows.SetCell(r, m_date->GetColumnId(), ibValue(date));
		rows.SetCell(r, m_level->GetColumnId(), ibValue(ibLevelName(row.level)));
		rows.SetCell(r, m_session->GetColumnId(), ibValue(row.session_id));
		rows.SetCell(r, m_user->GetColumnId(), ibValue(row.user_name));
		rows.SetCell(r, m_host->GetColumnId(), ibValue(row.host));
		rows.SetCell(r, m_source->GetColumnId(), ibValue(row.source));
		rows.SetCell(r, m_event->GetColumnId(), ibValue(row.event_type));
		rows.SetCell(r, m_message->GetColumnId(), ibValue(row.message));
		// The object as a reference of this configuration — its key only, the row is read when it is asked for.
		if (row.ref_meta_id != 0 && !row.ref_guid.IsEmpty())
			if (ibValueReferenceDataObject* const ref = ibValueReferenceDataObject::Create(m_metaData, row.ref_meta_id, ibGuid(row.ref_guid)))
				rows.SetCell(r, m_object->GetColumnId(), ibValue(ref));
		if (row.details.GetDataLen() > 0)
			rows.SetCell(r, m_details->GetColumnId(), DetailsOf(row.details, m_metaData));
	}
	return rows;
}

//////////////////////////////////////////////////////////////////////
// ibLogQueryDescriptor
//////////////////////////////////////////////////////////////////////

ibLogQueryDescriptor::ibLogQueryDescriptor(const ibMetaData* metaData)
	: m_metaData(metaData)
{
}

ibString ibLogQueryDescriptor::GetNamespace() const
{
	return g_logNamespace;
}

ibString ibLogQueryDescriptor::GetName() const
{
	return g_logName;
}

// The picture the journal's window wears (ibSchemaAuditLog), so the table and the window are known by one face.
ibPictureID ibLogQueryDescriptor::GetPicture() const
{
	return g_picUserActiveCLSID;
}

// A moment of the period, as an argument holds it: empty for no bound, a date for one, anything else refused —
// a period silently dropped would read the whole journal and look like an answer.
static ibDateTime PeriodMoment(const ibValue& argument, const ibString& slot)
{
	if (argument.GetType() == ibValueTypes::TYPE_EMPTY || argument.GetType() == ibValueTypes::TYPE_NULL)
		return ibDateTime();
	if (argument.GetType() != ibValueTypes::TYPE_DATE)
		ibBackendCoreException::Error(_("'%s.%s' takes a date as %s"), ibString(g_logNamespace), ibString(g_logName), slot);
	return argument.GetDate();
}

std::shared_ptr<const ibBackendQueryable> ibLogQueryDescriptor::CreateQueryable(ibValue** paParams, long lSizeArray,
	const std::vector<ibQueryPredicatePtr>& /*conditions*/, const ibQueryReadColumns& /*read*/)
{
	const ibDateTime begin = PeriodMoment(ibRegArg(paParams, lSizeArray, 0), ibT("BeginOfPeriod"));
	const ibDateTime end   = PeriodMoment(ibRegArg(paParams, lSizeArray, 1), ibT("EndOfPeriod"));
	return std::make_shared<const ibLogQueryable>(m_metaData, begin, end);
}

void ibLogQueryDescriptor::DescribeParameters(std::vector<ibQuerySourceParameter>& out) const
{
	ibQuerySourceParameter begin;
	begin.m_name = ibT("BeginOfPeriod");
	begin.m_description = _("The first moment read, inclusive. Left out, the reading starts at the first event "
	                        "the journal keeps.");
	begin.m_type = ibTypeDescription(g_valueDateCLSID);
	out.push_back(begin);

	ibQuerySourceParameter end;
	end.m_name = ibT("EndOfPeriod");
	end.m_description = _("The last moment read, inclusive. Left out, the reading runs to the last event there is.");
	end.m_type = ibTypeDescription(g_valueDateCLSID);
	out.push_back(end);
}

std::vector<std::shared_ptr<const ibBackendQueryColumn>> ibLogQueryDescriptor::GetQueryColumns(const std::vector<ibValue>& /*args*/) const
{
	return ibLogQueryable(m_metaData).GetQueryColumns();
}
