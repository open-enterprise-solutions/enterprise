////////////////////////////////////////////////////////////////////////////
//	Description : an Excel workbook, read into our own document
////////////////////////////////////////////////////////////////////////////

// ⭐⭐ THE WORKBOOK BECOMES ONE DOCUMENT, ITS SHEETS SEPARATED BY PAGE BREAKS
// (Max, 2026-08-26). We have no tabs; what a second sheet means here is "this
// part prints separately", and the document already says that. So the sheets are
// laid one under another and a row break is put between them.
//
// ⚠ A ZIP IS READ FORWARD ONLY. wxZipInputStream hands out entries in the order
// they were stored and cannot seek back, and the parts refer to each other
// (workbook -> rels -> sheets, cells -> sharedStrings). So the pass below COLLECTS
// what it needs into memory first and interprets afterwards — a spreadsheet's
// parts are text and small beside its cells.

#include "backend/sheetFormat/sheetFormatXlsx.h"

#include <wx/wfstream.h>
#include <wx/zipstrm.h>
#include <wx/mstream.h>
#include <wx/sstream.h>
#include <wx/xml/xml.h>

#include <map>
#include <vector>

namespace {

// A1 -> (row, col), zero-based. Returns false on anything that is not a reference.
bool CellAt(const wxString& ref, int& row, int& col)
{
	col = 0;
	size_t at = 0;
	for (; at < ref.length() && wxIsalpha(ref[at]); at++)
		col = col * 26 + (wxToupper(ref[at]).GetValue() - wxT('A') + 1);

	if (at == 0 || at >= ref.length())
		return false;

	long number = 0;
	if (!ref.Mid(at).ToLong(&number) || number <= 0)
		return false;

	row = static_cast<int>(number) - 1;
	col -= 1;
	return true;
}

// Every part of the package, by name. Read in one forward pass.
using ibPackage = std::map<wxString, wxString>;

// The four things a sheet is made of, as far as this reader is concerned: which sheets there are
// and in what order (workbook + its rels), the strings they share, and the sheets themselves.
// Styles are deliberately absent — the `s` attribute is not read yet, and reading the part to
// ignore it costs the part.
bool IsPartWeRead(const wxString& name)
{
	return name == wxT("xl/workbook.xml")
		|| name == wxT("xl/_rels/workbook.xml.rels")
		|| name == wxT("xl/sharedStrings.xml")
		|| name.StartsWith(wxT("xl/worksheets/"));
}

bool ReadPackage(const wxString& fileName, ibPackage& parts)
{
	wxFileInputStream file(fileName);
	if (!file.IsOk())
		return false;

	wxZipInputStream zip(file);
	if (!zip.IsOk())
		return false;

	for (;;) {
		std::unique_ptr<wxZipEntry> entry(zip.GetNextEntry());
		if (!entry)
			break;

		const wxString name = entry->GetInternalName();

		// ⭐ ONLY THE PARTS THIS READER SPEAKS, NAMED — not everything except a list of what to
		// skip. The two read the same on the files we thought of and differently on the rest:
		// a workbook also carries themes, printer settings, drawings, certificates, and
		// xl/calcChain.xml — which is the FORMULA DEPENDENCY GRAPH and on a large sheet rivals
		// the sheet itself in size. Skipping four names left all of those being read into memory
		// to be ignored, which is most of what made a big workbook slow to open.
		if (!IsPartWeRead(name))
			continue;

		wxStringOutputStream out;
		zip.Read(out);
		parts[name] = out.GetString();
	}

	return !parts.empty();
}

bool ParseXml(const wxString& text, wxXmlDocument& xml)
{
	if (text.IsEmpty())
		return false;

	wxStringInputStream in(text);
	// ⚠ THE ENGINE'S OWN VERDICT, kept quiet. A part that does not parse is not our
	// error to report per part — the caller is told the file could not be read, once.
	wxLogNull noLog;
	return xml.Load(in);
}

// <sst><si><t>text</t></si>… — the table Excel puts repeated strings in. A cell
// then says t="s" and carries the INDEX.
//
// ⚠ A STRING MAY BE SPLIT ACROSS RUNS (<si><r><t>Total</t></r><r><t> 2026</t></r></si>)
// when part of it is formatted differently — reading only the first <t> silently
// truncates every such cell, which is exactly what a heading looks like.
void ReadSharedStrings(const ibPackage& parts, std::vector<wxString>& strings)
{
	const auto found = parts.find(wxT("xl/sharedStrings.xml"));
	if (found == parts.end())
		return;

	wxXmlDocument xml;
	if (!ParseXml(found->second, xml) || xml.GetRoot() == nullptr)
		return;

	for (wxXmlNode* si = xml.GetRoot()->GetChildren(); si != nullptr; si = si->GetNext()) {
		if (si->GetName() != wxT("si"))
			continue;

		wxString text;
		for (wxXmlNode* part = si->GetChildren(); part != nullptr; part = part->GetNext()) {
			if (part->GetName() == wxT("t"))
				text += part->GetNodeContent();
			else if (part->GetName() == wxT("r")) {
				for (wxXmlNode* run = part->GetChildren(); run != nullptr; run = run->GetNext())
					if (run->GetName() == wxT("t"))
						text += run->GetNodeContent();
			}
		}
		strings.push_back(text);
	}
}

// The sheets, in the order the workbook lists them — which is the order a person
// sees the tabs in, and therefore the order they must be laid out in.
void ReadSheetOrder(const ibPackage& parts, std::vector<wxString>& sheetParts)
{
	// workbook.xml gives each sheet an r:id; the rels part says which file that is.
	std::map<wxString, wxString> targetById;

	wxXmlDocument rels;
	const auto relsPart = parts.find(wxT("xl/_rels/workbook.xml.rels"));
	if (relsPart != parts.end() && ParseXml(relsPart->second, rels) && rels.GetRoot() != nullptr) {
		for (wxXmlNode* rel = rels.GetRoot()->GetChildren(); rel != nullptr; rel = rel->GetNext()) {
			const wxString id = rel->GetAttribute(wxT("Id"));
			wxString target = rel->GetAttribute(wxT("Target"));
			if (id.IsEmpty() || target.IsEmpty())
				continue;
			if (!target.StartsWith(wxT("/")))
				target = wxT("xl/") + target;
			targetById[id] = target;
		}
	}

	wxXmlDocument workbook;
	const auto workbookPart = parts.find(wxT("xl/workbook.xml"));
	if (workbookPart != parts.end() && ParseXml(workbookPart->second, workbook) && workbook.GetRoot() != nullptr) {
		for (wxXmlNode* node = workbook.GetRoot()->GetChildren(); node != nullptr; node = node->GetNext()) {
			if (node->GetName() != wxT("sheets"))
				continue;
			for (wxXmlNode* sheet = node->GetChildren(); sheet != nullptr; sheet = sheet->GetNext()) {
				const wxString id = sheet->GetAttribute(wxT("r:id"), sheet->GetAttribute(wxT("id")));
				const auto target = targetById.find(id);
				if (target != targetById.end())
					sheetParts.push_back(target->second);
			}
		}
	}

	// A WORKBOOK THAT NAMES NOTHING WE CAN FOLLOW still usually has the first sheet
	// where everyone puts it. Better one sheet than an empty document.
	if (sheetParts.empty() && parts.find(wxT("xl/worksheets/sheet1.xml")) != parts.end())
		sheetParts.push_back(wxT("xl/worksheets/sheet1.xml"));
}

// One worksheet, laid into the document starting at `topRow`. Returns how many
// rows it occupied.
int ReadSheet(const wxString& partText, const std::vector<wxString>& strings,
              ibSpreadsheetDescription& document, int topRow)
{
	wxXmlDocument xml;
	if (!ParseXml(partText, xml) || xml.GetRoot() == nullptr)
		return 0;

	int usedRows = 0;

	for (wxXmlNode* node = xml.GetRoot()->GetChildren(); node != nullptr; node = node->GetNext()) {

		// --- column widths ---------------------------------------------------------
		if (node->GetName() == wxT("cols")) {
			for (wxXmlNode* col = node->GetChildren(); col != nullptr; col = col->GetNext()) {
				long from = 0, to = 0;
				double width = 0.0;
				if (!col->GetAttribute(wxT("min"), wxT("0")).ToLong(&from) ||
					!col->GetAttribute(wxT("max"), wxT("0")).ToLong(&to) ||
					!col->GetAttribute(wxT("width"), wxT("0")).ToCDouble(&width))
					continue;

				// Characters back to pixels — the ratio the writer uses in reverse.
				const int pixels = col->GetAttribute(wxT("hidden"), wxT("0")) == wxT("1")
					? 0 : static_cast<int>(width * 7.0 + 0.5);

				for (long at = from; at <= to && at > 0; at++)
					document.SetColSize(static_cast<int>(at) - 1, pixels);
			}
			continue;
		}

		if (node->GetName() != wxT("sheetData"))
			continue;

		// --- the cells --------------------------------------------------------------
		for (wxXmlNode* row = node->GetChildren(); row != nullptr; row = row->GetNext()) {
			if (row->GetName() != wxT("row"))
				continue;

			long rowNumber = 0;
			if (!row->GetAttribute(wxT("r"), wxT("0")).ToLong(&rowNumber) || rowNumber <= 0)
				continue;

			const int documentRow = topRow + static_cast<int>(rowNumber) - 1;
			usedRows = wxMax(usedRows, static_cast<int>(rowNumber));

			double height = 0.0;
			if (row->GetAttribute(wxT("ht"), wxEmptyString).ToCDouble(&height) && height > 0.0)
				document.SetRowSize(documentRow, static_cast<int>(height / 0.75 + 0.5));

			for (wxXmlNode* cell = row->GetChildren(); cell != nullptr; cell = cell->GetNext()) {
				if (cell->GetName() != wxT("c"))
					continue;

				int cellRow = 0, cellCol = 0;
				if (!CellAt(cell->GetAttribute(wxT("r")), cellRow, cellCol))
					continue;

				const wxString type = cell->GetAttribute(wxT("t"), wxT("n"));

				wxString value;
				for (wxXmlNode* part = cell->GetChildren(); part != nullptr; part = part->GetNext()) {
					// ⚠ <v> IS TAKEN AND <f> IS NOT. A cell may carry a formula and the
					// value the other program last computed for it; we do not evaluate
					// formulas, so the value is what a person saw there — and a formula
					// dropped into a cell as text would be a lie about what it is.
					if (part->GetName() == wxT("v"))
						value = part->GetNodeContent();
					else if (part->GetName() == wxT("is")) {
						for (wxXmlNode* inline_ = part->GetChildren(); inline_ != nullptr; inline_ = inline_->GetNext())
							if (inline_->GetName() == wxT("t"))
								value += inline_->GetNodeContent();
					}
				}

				if (type == wxT("s")) {
					long index = 0;
					if (value.ToLong(&index) && index >= 0 && static_cast<size_t>(index) < strings.size())
						value = strings[static_cast<size_t>(index)];
					else
						value.clear();
				}

				if (value.IsEmpty())
					continue;

				document.SetCellValue(topRow + cellRow, cellCol, value);
			}
		}
	}

	// --- merged cells ---------------------------------------------------------------
	for (wxXmlNode* node = xml.GetRoot()->GetChildren(); node != nullptr; node = node->GetNext()) {
		if (node->GetName() != wxT("mergeCells"))
			continue;

		for (wxXmlNode* merge = node->GetChildren(); merge != nullptr; merge = merge->GetNext()) {
			const wxString ref = merge->GetAttribute(wxT("ref"));
			const int colon = ref.Find(wxT(':'));
			if (colon == wxNOT_FOUND)
				continue;

			int fromRow = 0, fromCol = 0, toRow = 0, toCol = 0;
			if (!CellAt(ref.Left(colon), fromRow, fromCol) || !CellAt(ref.Mid(colon + 1), toRow, toCol))
				continue;

			document.SetCellSize(topRow + fromRow, fromCol, toRow - fromRow + 1, toCol - fromCol + 1);
		}
	}

	return usedRows;
}

} // namespace

bool ibSheetFormatXlsx::Read(const wxString& fileName, ibSpreadsheetDescription& sheet) const
{
	ibPackage parts;
	if (!ReadPackage(fileName, parts))
		return false;

	std::vector<wxString> sheetParts;
	ReadSheetOrder(parts, sheetParts);
	if (sheetParts.empty())
		return false;   // nothing in this file is a worksheet

	std::vector<wxString> strings;
	ReadSharedStrings(parts, strings);

	// ⚠ FILLED INTO A DOCUMENT OF ITS OWN and handed over only once it is whole: a
	// caller that gets false must be free to keep the document it already had, and
	// a half-read workbook is worse than none.
	ibSpreadsheetDescription read;

	int topRow = 0;
	for (size_t at = 0; at < sheetParts.size(); at++) {
		const auto part = parts.find(sheetParts[at]);
		if (part == parts.end())
			continue;

		// ⭐ THE BREAK GOES BEFORE EVERY SHEET BUT THE FIRST — that is what makes the
		// workbook's tabs into this document's pages.
		if (at > 0 && topRow > 0)
			read.AddRowBrake(topRow - 1);

		const int used = ReadSheet(part->second, strings, read, topRow);
		topRow += wxMax(used, 1);
	}

	sheet = read;
	return true;
}
