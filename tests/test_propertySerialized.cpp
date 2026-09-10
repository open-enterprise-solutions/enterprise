// =============================================================================
// OES Enterprise — every declared property must be serialised
//
// A property that is written nowhere round-trips perfectly: both sides simply
// lack it. Byte-equality tests therefore cannot see it, and neither can any
// test that only checks what the object does in memory. It surfaces much later,
// and far from its cause.
//
// That is not hypothetical. The accounting register's `Correspondence` and
// `SplitTotals` were declared, edited, and used to BUILD THE SCHEMA, but named
// in neither ReadData nor WriteData. They lived as long as the designer held the
// object and came back at their defaults whenever the saved configuration was
// re-read — and that re-read produces the BASELINE the next apply diffs against.
// So a switch turned OFF applied, came back ON, and the next apply compared ON
// with ON, emitted nothing, and left the physical table without the column its
// maintenance was about to be written for. It looked intermittent because
// breakage depended on which way the setting differed from the default.
// (docs/register-shared-machinery.md § 4d)
//
// The check is mechanical and needs no database, no session and no metadata: for
// every metatype header, take the `m_property*` members it DECLARES and require
// each to appear in the metatype's own serialisation translation unit. It reads
// the sources as text on purpose — the fact being checked is "somebody wrote
// this line", which no amount of running the code can establish.
// =============================================================================

#include <gtest/gtest.h>

#include <wx/filename.h>
#include <wx/textfile.h>
#include <wx/tokenzr.h>
#include <wx/dir.h>

#include <algorithm>
#include <map>
#include <set>
#include <vector>

namespace {

// The partials live beside the tests, two levels up; CMake runs the binary from
// the build tree, so the path is resolved from the source location rather than
// the cwd (the same trick test_scriptCorpus.cpp uses for its corpus).
wxString PartialsDir()
{
	wxFileName here(wxString::FromUTF8(__FILE__));
	here.SetFullName(wxEmptyString);
	here.RemoveLastDir();                      // tests -> enterprise
	here.AppendDir(wxT("src"));
	here.AppendDir(wxT("engine"));
	here.AppendDir(wxT("backend"));
	here.AppendDir(wxT("metaCollection"));
	here.AppendDir(wxT("partial"));
	return here.GetPath();
}

wxString ReadWhole(const wxString& path)
{
	wxTextFile file;
	if (!file.Open(path))
		return wxEmptyString;
	wxString out;
	for (size_t i = 0; i < file.GetLineCount(); i++)
		out += file[i] + wxT("\n");
	return out;
}

// Every `m_propertyXxx` identifier mentioned in a text, in first-seen order.
std::set<wxString> PropertyNames(const wxString& text)
{
	std::set<wxString> names;
	size_t pos = 0;
	const wxString needle = wxT("m_property");
	while ((pos = text.find(needle, pos)) != wxString::npos) {
		size_t end = pos + needle.length();
		while (end < text.length()) {
			const wxUniChar c = text[end];
			if (!(wxIsalnum(c) || c == wxT('_')))
				break;
			++end;
		}
		names.insert(text.Mid(pos, end - pos));
		pos = end;
	}
	return names;
}

// The lists of PROPERTIES a header names once — `Name() const { return { m_propertyA, m_propertyB }; }`,
// every item a bare member — keyed by the function that returns them. A unit that calls `Name(` names
// each item: the chart of calculation types serialises its three relation sections through one such
// list (GetRelationProperties), so that a fourth section cannot be forgotten at one of the doors that
// treat them alike. A list of what the members HOLD (`m_propertyX->GetMetaObject()`) is not a list of
// properties, and does not count — reading a section is not writing the setting.
std::map<wxString, std::vector<wxString>> PropertyLists(const wxString& text)
{
	std::map<wxString, std::vector<wxString>> lists;
	const wxString needle = wxT("return {");
	size_t pos = 0;
	while ((pos = text.find(needle, pos)) != wxString::npos) {
		const size_t open = pos + needle.length();
		const size_t close = text.find(wxT('}'), open);
		if (close == wxString::npos)
			break;

		std::vector<wxString> items;
		bool bare = true;
		wxStringTokenizer tokens(text.Mid(open, close - open), wxT(","));
		while (bare && tokens.HasMoreTokens()) {
			wxString item = tokens.GetNextToken();
			item.Trim().Trim(false);
			const std::set<wxString> found = PropertyNames(item);
			bare = found.size() == 1 && *found.begin() == item;
			items.push_back(item);
		}

		// The function it is returned from: the identifier in front of the last '(' before it — unless
		// that is a statement's (`if (x) return {…}`), which names no list and would credit every unit
		// that happens to write `if(`.
		static const std::set<wxString> statements = { wxT("if"), wxT("for"), wxT("while"), wxT("switch"), wxT("catch") };
		const size_t paren = text.rfind(wxT('('), pos);
		if (bare && !items.empty() && paren != wxString::npos) {
			size_t begin = paren;
			while (begin > 0) {
				const wxUniChar c = text[begin - 1];
				if (!(wxIsalnum(c) || c == wxT('_')))
					break;
				--begin;
			}
			const wxString function = text.Mid(begin, paren - begin);
			if (!function.IsEmpty() && statements.count(function) == 0)
				lists[function] = items;
		}
		pos = close;
	}
	return lists;
}

// The header a serialisation unit belongs to: its own (`<stem>Metadata*.cpp`), or — for a class with no
// header of its own — the header whose name is the LONGEST prefix of the unit's. A class NESTED in a
// metatype's header is serialised in a unit named after it: the calculation register's Recalculation is
// declared in `calculationRegister.h` and written in `calculationRegisterRecalculationMetadata.cpp`, which
// `calculationRegisterMetadata*.cpp` does not match. A unit whose class HAS a header stays that header's,
// so a metatype is never credited with what a neighbour sharing its prefix writes.
wxString OwnerOf(const wxString& unit, const std::set<wxString>& stems)
{
	const int at = unit.Find(wxT("Metadata"));
	if (at == wxNOT_FOUND)
		return wxEmptyString;
	const wxString unitStem = unit.Left(static_cast<size_t>(at));
	if (stems.count(unitStem) != 0)
		return unitStem;
	wxString owner;
	for (const wxString& stem : stems)
		if (unitStem.StartsWith(stem) && stem.length() > owner.length())
			owner = stem;
	return owner;
}

}  // namespace

// A metatype declares its properties in `<name>.h` and serialises them in
// `<name>Metadata.cpp`. Both halves are hand-written and neither compiler nor
// linker relates them — this test is what does.
TEST(PropertySerialization, EveryDeclaredPropertyIsReadAndWritten) {
	const wxString dir = PartialsDir();
	ASSERT_TRUE(wxDirExists(dir)) << "metatype partials not found at " << dir.ToStdString();

	// ⚠ COLLECT FIRST, WALK AFTER. ONE wxDir cannot drive two traversals: the inner search for
	// `<stem>Metadata*.cpp` below calls GetFirst on the SAME object, which restarts it with that
	// filter — so the outer loop would carry on over .cpp files instead of headers and stop after
	// one step. It fell out as `checked == 0` on Linux (whose directory order differs) while
	// Windows happened to check one header and pass, hiding it.
	std::vector<wxString> headers;
	{
		wxDir walker(dir);
		ASSERT_TRUE(walker.IsOpened());
		wxString header;
		for (bool more = walker.GetFirst(&header, wxT("*.h"), wxDIR_FILES); more;
		     more = walker.GetNext(&header))
			headers.push_back(header);
	}
	ASSERT_FALSE(headers.empty()) << "no metatype headers in " << dir.ToStdString();

	std::vector<wxString> units;
	{
		wxDir walker(dir);                 // its OWN traversal — see the note above
		ASSERT_TRUE(walker.IsOpened());
		wxString unit;
		for (bool more = walker.GetFirst(&unit, wxT("*Metadata*.cpp"), wxDIR_FILES); more;
		     more = walker.GetNext(&unit))
			units.push_back(unit);
	}
	std::set<wxString> stems;
	for (const wxString& header : headers)
		stems.insert(wxFileName(header).GetName());

	size_t checked = 0;
	for (const wxString& header : headers) {
		wxFileName headerFile(dir, header);

		// ⚠ EVERY UNIT OF THE METATYPE, not just `<name>Metadata.cpp`.
		//
		// A metatype is split by ASPECT (CLAUDE.md § per-metatype file split), and which aspect holds
		// ReadData / WriteData is not fixed: the parameterized job serialises in
		// `parameterizedJobMetadata_res.cpp`. Looking in one file named by convention reported four
		// perfectly serialised properties as missing — a test that cries wolf on correct code is worse
		// than no test, because the next real finding is read as noise too. The same happened on
		// 2026-09-11 with the two shapes OwnerOf and PropertyLists describe: five properties of the
		// calculation register and its chart, all written, reported as missing.
		const wxString stem = headerFile.GetName();
		wxString serialised;
		for (const wxString& unit : units)
			if (OwnerOf(unit, stems) == stem)
				serialised += ReadWhole(wxFileName(dir, unit).GetFullPath());
		if (serialised.IsEmpty())
			continue;   // not a metatype with a serialisation unit of its own

		const wxString headerText = ReadWhole(headerFile.GetFullPath());
		const std::set<wxString> declared = PropertyNames(headerText);
		if (declared.empty())
			continue;

		const std::map<wxString, std::vector<wxString>> lists = PropertyLists(headerText);
		auto namedThroughList = [&](const wxString& name) {
			for (const std::pair<const wxString, std::vector<wxString>>& list : lists)
				if (std::find(list.second.begin(), list.second.end(), name) != list.second.end()
				    && serialised.Contains(list.first + wxT("(")))
					return true;
			return false;
		};

		for (const wxString& name : declared) {
			// EXEMPT, and each for a reason of its own — not a convenience list:
			//   * DefForm…  — the default-form bindings are stored by id through their own path;
			//   * …Module   — carries the module OBJECT, serialised as a child, not as a value;
			//   * Attribute…— a PREDEFINED ATTRIBUTE. It is a metaobject in the tree and is written
			//                 as one (FillArrayObjectByPredefinedAttribute), so the property is a
			//                 handle to it rather than a setting of its own.
			if (name.Contains(wxT("DefForm")) || name.Contains(wxT("Module"))
			 || name.StartsWith(wxT("m_propertyAttribute")))
				continue;

			EXPECT_TRUE(serialised.Contains(name) || namedThroughList(name))
				<< headerFile.GetFullName().ToStdString() << " declares " << name.ToStdString()
				<< " but no " << stem.ToStdString() << "Metadata*.cpp names it, by itself or through a"
				   " list of properties it calls — a setting that cannot survive a save makes the baseline"
				   " lie (docs/register-shared-machinery.md § 4d)";
		}
		++checked;
	}

	EXPECT_GT(checked, 0u) << "no metatype was actually checked — the layout must have moved";
}
