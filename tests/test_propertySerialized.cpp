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
#include <wx/dir.h>

#include <algorithm>
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

}  // namespace

// A metatype declares its properties in `<name>.h` and serialises them in
// `<name>Metadata.cpp`. Both halves are hand-written and neither compiler nor
// linker relates them — this test is what does.
TEST(PropertySerialization, EveryDeclaredPropertyIsReadAndWritten) {
	const wxString dir = PartialsDir();
	ASSERT_TRUE(wxDirExists(dir)) << "metatype partials not found at " << dir.ToStdString();

	wxString header;
	wxDir walker(dir);
	ASSERT_TRUE(walker.IsOpened());

	bool more = walker.GetFirst(&header, wxT("*.h"), wxDIR_FILES);
	ASSERT_TRUE(more) << "no metatype headers in " << dir.ToStdString();

	size_t checked = 0;
	for (; more; more = walker.GetNext(&header)) {
		wxFileName headerFile(dir, header);

		// ⚠ EVERY UNIT OF THE METATYPE, not just `<name>Metadata.cpp`.
		//
		// A metatype is split by ASPECT (CLAUDE.md § per-metatype file split), and which aspect holds
		// ReadData / WriteData is not fixed: the parameterized job serialises in
		// `parameterizedJobMetadata_res.cpp`. Looking in one file named by convention reported four
		// perfectly serialised properties as missing — a test that cries wolf on correct code is worse
		// than no test, because the next real finding is read as noise too.
		const wxString stem = headerFile.GetName();
		wxString serialised;
		wxString unit;
		for (bool got = walker.GetFirst(&unit, stem + wxT("Metadata*.cpp"), wxDIR_FILES); got;
		     got = walker.GetNext(&unit)) {
			serialised += ReadWhole(wxFileName(dir, unit).GetFullPath());
		}
		if (serialised.IsEmpty())
			continue;   // not a metatype with a serialisation unit of its own

		const std::set<wxString> declared = PropertyNames(ReadWhole(headerFile.GetFullPath()));
		if (declared.empty())
			continue;

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

			EXPECT_TRUE(serialised.Contains(name))
				<< headerFile.GetFullName().ToStdString() << " declares " << name.ToStdString()
				<< " but no " << stem.ToStdString() << "Metadata*.cpp names it — a setting that cannot"
				   " survive a save makes the baseline lie (docs/register-shared-machinery.md § 4d)";
		}
		++checked;
	}

	EXPECT_GT(checked, 0u) << "no metatype was actually checked — the layout must have moved";
}
