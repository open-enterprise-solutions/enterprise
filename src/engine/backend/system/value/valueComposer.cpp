////////////////////////////////////////////////////////////////////////////
//	L5-1 — runtime script value object "DataComposer" (valueComposer.h)
////////////////////////////////////////////////////////////////////////////

#include "valueComposer.h"

#include "valueQuery.h"                  // ibValueQueryResult — Execute returns the standard L4-1 result
#include "backend/compiler/typeCtor.h"   // VALUE_TYPE_REGISTER
#include "backend/appData.h"             // appData->DesignerMode()
#include "backend/metadataConfiguration.h"  // ibMetaDataConfigurationBase — full def for the GetActiveMetaData() -> const ibMetaData* upcast

void ibValueDataComposer_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendConstructor(1, wxT("DataComposer(source : string)"));
	helper.AppendProc(wxT("SetParameter"), 2, wxT("SetParameter(name : string, value)"));
	helper.AppendFunc(wxT("Execute"), wxT("Execute()"));
	helper.AppendFunc(wxT("QueryText"), wxT("QueryText()"));
}

bool ibValueDataComposer::Init(ibValue** paParams, const long lSizeArray)
{
	const wxString source = (lSizeArray >= 1) ? paParams[0]->GetString() : wxString();

	// A source-less composer is a valid, empty object (designer introspection keeps the
	// method chain reachable); Execute() on it yields an empty QueryResult.
	if (source.IsEmpty())
		return true;

	// The query runs ON BEHALF OF the config the script executes in — resolve its by-name source there (per-config),
	// not the global factory. A script value has no owner metadata of its own, so the running (active) config IS the
	// query's config; the composer threads it into the lowering at Execute.
	m_composer.SetMetaData(ibApplicationData::GetActiveMetaData());

	// Any whitespace => the author's verbatim query text; otherwise a registered source
	// `Kind.Name` (the name may be composite — a register's virtual table). A dot-less
	// token falls through to the text path so the query parser reports it with a span.
	if (source.find_first_of(wxT(" \t\r\n")) != wxString::npos) {
		m_composer.FromText(source);
		return true;
	}
	const size_t dot = source.find(wxT('.'));
	if (dot == wxString::npos || dot == 0 || dot + 1 >= source.length()) {
		m_composer.FromText(source);
		return true;
	}
	m_composer.FromSource(source.Mid(0, dot), source.Mid(dot + 1));
	return true;
}

bool ibValueDataComposer::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	if (lMethodNum == enSetParameter) {
		if (lSizeArray < 2)
			return false;
		m_composer.Parameter(paParams[0]->GetString(), *paParams[1]);
		return true;
	}
	return false;
}

bool ibValueDataComposer::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue,
                                     ibValue** /*paParams*/, const long /*lSizeArray*/)
{
	switch (lMethodNum) {
	case enQueryText:
		// The rendered L4-1 text — the schema's debug view. Designer-tolerant: an
		// unresolvable source degrades to an empty string so the editor eval survives.
		if (m_composer.HasSource()) {
			try {
				pvarRetValue = ibValue(m_composer.RenderText());
				return true;
			}
			catch (...) {
				if (!appData->DesignerMode())
					throw;
			}
		}
		pvarRetValue = ibValue(wxString());
		return true;

	case enExecute:
		// Render -> parse -> lower -> run; the result is the STANDARD QueryResult, so the
		// script walks it exactly as a hand-written New Query(text).Execute() — the L5 bug
		// repro path is a copy-paste of QueryText(). Designer degrades to an empty result
		// (no live session to read from), runtime surfaces the real error.
		if (m_composer.HasSource()) {
			try {
				std::vector<ibQueryLowering::OutputColumn> schema;
				bool hasTotals = false;
				ibDataQueryResult r = m_composer.Execute(schema, hasTotals);
				pvarRetValue = new ibValueQueryResult(std::move(r), std::move(schema), hasTotals);
				return true;
			}
			catch (...) {
				if (!appData->DesignerMode())
					throw;
			}
		}
		pvarRetValue = new ibValueQueryResult();
		return true;

	default:
		return false;
	}
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueDataComposer, "DataComposer", value_to_clsid("VL_CMPS"));
