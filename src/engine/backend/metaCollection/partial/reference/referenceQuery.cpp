////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : reference - db
////////////////////////////////////////////////////////////////////////////

#include "reference.h"
#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaCollection/partial/tabularSection/tabularSection.h"
#include "backend/query/dataQueryBuilder.h"   // L3 door — reference read by key / scan
#include "backend/logger/logger.h"        // a read that FAILED is said out loud, unlike a row that is absent
#include "backend/diagnostics/journal.h"  // every read is counted — how many there are is a measurement, not a guess
#include "backend/utils/debugTrace.h"     // ibDebugTraceEnabled — the same gate as the hit line


bool ibValueReferenceDataObject::ReadData(bool createData)
{
	if (m_metaObject == nullptr || !m_objGuid.isValid())
		return false;

	// ⭐ NO CACHE OF ROWS HERE, deliberately. There used to be one, keyed by (metaobject, guid), and it
	// existed to stop the SAME row being read once per reference object holding that identity. The
	// register removed the twins it was compensating for: there is now one object per identity per
	// session, its own m_initializedRef says the read already happened, and a row read twice would
	// mean two objects — which can no longer occur. A cache on top of that would only add a second
	// answer to the question the object already answers, and one that goes stale after a write.

	// Load the row by its own key through the L3 door — the FB FIRST / others
	// LIMIT fork and the raw-concat '%s' guid (injection-shaped) are gone. Values
	// come from the L3 selection (GetValue), not the raw result set.
	try {
		ibDataQueryBuilder q;
		q.From(m_metaObject->GetQueryable()).WhereKey(m_objGuid);
		ibReadPageRequest page;
		page.m_count = 1;
		ibDataQueryResult selection = q.Execute(page);
		if (selection.Next()) {
			for (const auto object : m_metaObject->GetGenericAttributeArrayObject())
				if (!m_metaObject->IsDataReference(object->GetMetaID()))
					m_listObjectValue[object->GetMetaID()] = selection.GetValue(object->GetQueryColumn());

			// ⭐ ONE LINE PER ROW ACTUALLY READ — the other half of the register's measurement, paired
			// with the "hit" line and behind the same gate for the same reason: rendering a guid and
			// flushing a line per read costs more than the read on a list of forty.
			static const bool s_traceRefs = ibDebugTraceEnabled("OES_TRACE_REFS");
			if (s_traceRefs)
				ibJournalInfo(wxT("reference"), wxT("read %s <%i>"),
					m_objGuid.str(), static_cast<int>(m_metaObject->GetMetaID()));
			return true;
		}
		return false;   // NO SUCH ROW - a legitimate answer, and the only one this returns quietly
	}
	// THE TWO ANSWERS ARE NOT THE SAME. "There is no such object" is an ordinary result - a reference
	// to something deleted, a key nobody wrote - and it is the `return false` above. "The row could
	// not be read" is a fault: a table that does not exist, a refused connection, a column the query
	// named and the base does not have. Both used to leave through one empty `catch (...)`, so a
	// failure was indistinguishable from an absence, and the caller went on as if the object simply
	// was not there. Five "-204 table unknown" in a row passed through here without a trace while an
	// apply built the very table it was asking about.
	//
	// It stays non-throwing (a presentation asking "who is this reference" must not blow up a form),
	// but the fault is now SAID, with the table it happened on.
	catch (const ibBackendException& err) {
		if (ibLogger* const log = ibApplicationData::GetLogger())
			log->Error(wxT("reference"), wxT("read"),
				m_metaObject->GetPhysicalTableName() + wxT(": ") + err.GetErrorDescription());
	}
	catch (...) {
		if (ibLogger* const log = ibApplicationData::GetLogger())
			log->Error(wxT("reference"), wxT("read"),
				m_metaObject->GetPhysicalTableName() + wxT(": ") + _("unknown exception"));
	}
	return false;
}

bool ibValueReferenceDataObject::FindValue(const wxString& findData, std::vector<ibValue>& listValue) const
{
	// ⭐⭐ THE ROW BEING JUDGED IS THE ROW ALREADY IN HAND.
	//
	// This used to run a QUERY PER ROW: the scan below walked the table, and for every row it built a
	// comparator that went back to the database — `WHERE key = <this row>` — to read the very values
	// the scan had just passed over. A table of N rows cost N + 1 queries to answer one question, and
	// each of them opened a statement, a cursor and a page request of its own.
	//
	// It is now filled FROM the current row of the scan: same values, no second read, and the object
	// lives on the stack for the length of one comparison (the old one was `new` … `wxDELETE`, which
	// also leaked whenever a comparison threw between the two).
	class ibValueDataObjectComparator : public ibValueDataObject {
	public:
		ibValueDataObjectComparator(const ibValueMetaObjectRecordDataRef* metaObject, const ibGuid& guid,
			const ibDataQueryResult& selection)
			: ibValueDataObject(guid, false), m_metaObject(metaObject)
		{
			// Every attribute except the row's own reference — that one IS this object, and reading it
			// back into the value map would only restate the key the object already carries.
			for (const auto object : metaObject->GetGenericAttributeArrayObject())
				if (!metaObject->IsDataReference(object->GetMetaID()))
					m_listObjectValue[object->GetMetaID()] = selection.GetValue(object->GetQueryColumn());
		}

		// Does this row answer to `findData`? The SEARCHED attributes first (a catalog's code and
		// description), then the presentation, which is what a kind with no searchable fields of its
		// own — an enumeration — is recognised by.
		bool Matches(const wxString& findData) const
		{
			for (const auto object : m_metaObject->GetSearchedAttributeObjectArray()) {
				const auto it = m_listObjectValue.find(object->GetMetaID());
				if (it != m_listObjectValue.end() && it->second.GetString().Contains(findData))
					return true;
			}
			wxString desc;
			return m_metaObject->GenerateDataDesc(this, desc) && desc.Contains(findData);
		}

		//get metaData from object
		virtual const ibValueMetaObjectRecordData* GetMetaObject() const {
			return m_metaObject;
		}

	private:
		const ibValueMetaObjectRecordDataRef* m_metaObject;
	};

	// ⭐⭐ THERE IS A CEILING, AND OVER IT THE ANSWER IS "NO LIST".
	//
	// What this feeds is a quick choice — a popup a person picks from at a glance. Past a hundred
	// entries it is not that any more: it does not fit, it cannot be scrolled sensibly, and picking
	// from it is slower than the selection form the caller falls through to when this says no. So the
	// ceiling is a property of the ANSWER, not of the widget drawing it — offering a thousand values
	// as a "quick" choice is the wrong answer even on a screen that could show them.
	//
	// It also bounds the READ. This used to scan the whole table with no limit, so asking a catalog of
	// a million rows for a quick choice read a million rows to build a list nobody could use.
	// One row PAST the ceiling is fetched deliberately: that is what tells "exactly a hundred" (a
	// legitimate list) from "at least a hundred and one" (no list at all).
	static const size_t kQuickListCeiling = 100;
	try {
		ibDataQueryBuilder q;
		q.From(m_metaObject->GetQueryable());

		ibReadPageRequest page;
		page.m_count = static_cast<int>(kQuickListCeiling) + 1;
		ibDataQueryResult selection = q.Execute(page);
		// ⭐⭐ THE IDENTITY IS ASKED FOR BY NAME, NOT PICKED OFF THE END OF A SORT.
		//
		// This used to take the LAST item of the identity sort and call it the identity column — true
		// only for as long as that sort ended with the key. It stopped being true the moment an object
		// gained an ordering of its own (an enumeration sorts by Order first, by reference second), and
		// the "identity" read was then whatever column happened to sit last. A number, and the guid was
		// duly parsed out of its text.
		//
		// The reference IS the identity of a reference object and the metaobject says so directly, so
		// there is nothing to infer and nothing that a change of sort can move out from under this.
		const ibBackendQueryColumn* const keyCol = m_metaObject->GetDataReference()->GetQueryColumn();

		// ⭐ AN EMPTY REQUEST IS "EVERYTHING", and it is the commonest one — it is what a quick choice
		// asks. `Contains(wxEmptyString)` is true of every string, so the old code proved that per row
		// by materialising every attribute of it first. Nothing about the row can change the answer,
		// so nothing about the row is read.
		const bool everything = findData.IsEmpty();

		// ⭐⭐ THE IDENTITY COLUMN ALREADY *IS* THE REFERENCE — there is nothing to convert.
		//
		// It used to read that column, boil it down to a guid and build a NEW reference from it, which
		// was two objects for one row and one identity making a round trip through a form it does not
		// hold. It also broke the moment the identity column stopped being a raw guid field and became
		// the object's own reference: the guid was then extracted from the value's TEXT, which for a
		// reference is its PRESENTATION — a description, or "Not found <…>" — and identity by
		// appearance is not identity.
		//
		// The row hands out the very value the caller wants. Ask it what it is, and pass it on.
		while (selection.Next()) {
			const ibValue rowValue = selection.GetValue(keyCol);
			const ibValueReferenceDataObject* const rowReference = rowValue.ConvertToType<ibValueReferenceDataObject>();
			// A reference object's identity column ALWAYS holds a reference — anything else means this
			// read is looking at the wrong column, which is a defect here and not a property of the row.
			// Skipping rows quietly would hide it and hand back a short list that looks complete.
			if (rowReference == nullptr) {
				wxASSERT_MSG(false, wxT("FindValue: the identity column did not answer with a reference"));
				return false;
			}
			if (everything || ibValueDataObjectComparator(m_metaObject, rowReference->GetGuid().GetGuid(), selection).Matches(findData))
				listValue.push_back(rowValue);
		}
		// Over the ceiling — hand back NOTHING rather than a truncated list. A list cut off at a
		// hundred looks complete and is not, so the value a person wants may simply be absent from it
		// with nothing said; refusing sends them to the selection form, where everything is reachable.
		if (listValue.size() > kQuickListCeiling) {
			listValue.clear();
			return false;
		}

		std::sort(listValue.begin(), listValue.end(),
				[](const ibValue& a, const ibValue& b) { return a.GetString() < b.GetString(); });
		return listValue.size() > 0;
	}
	catch (...) { return false; }
}