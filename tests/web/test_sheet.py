"""A sheet's text wider than its cell, placed as the desktop's grid places it (ibSpreadsheetDescription::IsCellWrapped):
Auto runs on over the empty cell after it, and where it has no room to - the cell after it taken, the text not aligned
left, or the sheet's end reached with the text still wider - breaks onto further lines in a row whose height follows its
text, the row growing to them; in a row of its own height it is cut. Wrap breaks always, Clip never runs on, Mask shows a
text too wide as `#`."""
import unittest

from harness import PageTest, tab

LONG = 'Cable, copper, three cores of two and a half square millimetres, a drum of one hundred metres'

# A cell's placement (ibSpreadsheetCellDescription::ibFitMode) and wx's alignment flag, as a cell keeps them.
CLIP, WRAP, MASK = 5, 6, 7
RIGHT = 0x200

# Three columns of the default width, so a text in the first has a cell after it.
COLS = {'sizes': [{'at': col, 'size': 70} for col in range(3)]}

PLACED = """async (sheet) => {
	const { drawSheet } = await import('/pub/src/spreadsheet/sheet.js');
	const table = drawSheet(sheet, { gridLines: true, labels: false, onCell: () => {} });
	document.body.append(table);
	// A masked text is measured once the sheet is laid out (sheet.js, maskText).
	await new Promise(requestAnimationFrame);
	await new Promise(requestAnimationFrame);
	const cell = table.querySelector('td[data-row="0"][data-col="0"]');
	return { spills: cell.classList.contains('spills'), wraps: cell.classList.contains('wraps'),
		masked: cell.classList.contains('masked'), mask: cell.dataset.mask ?? '', text: cell.textContent,
		height: cell.parentElement.getBoundingClientRect().height };
}"""


def sheet(cell, after=None, rows=None, last=None, text=LONG):
    """The long text in the first cell of the first row; `after`, a text in the cell after it; `last`, in the sheet's last
    column."""
    cells = [{'row': 0, 'col': 0, 'value': text, **cell}]
    if after is not None:
        cells.append({'row': 0, 'col': 1, 'value': after})
    if last is not None:
        cells.append({'row': 0, 'col': 2, 'value': last})
    return {'cells': cells, 'cols': COLS, **({'rows': rows} if rows else {})}


class ASheetCell(PageTest):
    def placed(self, node):
        page = self.start({'tabs': [tab()]})
        return page.evaluate(PLACED, node)

    def test_auto_runs_on_over_the_empty_cell_after_it(self):
        placed = self.placed(sheet({}, last='100'))
        self.assertTrue(placed['spills'])
        self.assertFalse(placed['wraps'])
        self.assertLess(placed['height'], 20)

    def test_auto_that_fits_before_the_sheets_end_runs_on(self):
        placed = self.placed(sheet({}, text='Sales (New York)'))
        self.assertTrue(placed['spills'])
        self.assertFalse(placed['wraps'])
        self.assertLess(placed['height'], 20)

    def test_auto_still_wider_at_the_sheets_end_breaks_into_lines_and_the_row_grows(self):
        placed = self.placed(sheet({}))
        self.assertFalse(placed['spills'])
        self.assertTrue(placed['wraps'])
        self.assertGreater(placed['height'], 30)

    def test_auto_breaks_into_lines_where_the_cell_after_it_is_taken_and_the_row_grows(self):
        placed = self.placed(sheet({}, after='100'))
        self.assertFalse(placed['spills'])
        self.assertTrue(placed['wraps'])
        self.assertGreater(placed['height'], 30)

    def test_auto_aligned_right_breaks_into_lines_rather_than_run_on(self):
        placed = self.placed(sheet({'alignHorz': RIGHT}))
        self.assertFalse(placed['spills'])
        self.assertTrue(placed['wraps'])

    def test_auto_in_a_row_of_its_own_height_is_cut(self):
        placed = self.placed(sheet({}, after='100', rows={'sizes': [{'at': 0, 'size': 15}]}))
        self.assertFalse(placed['spills'])
        self.assertFalse(placed['wraps'])
        self.assertLess(placed['height'], 20)

    def test_wrap_breaks_into_lines_with_the_cell_after_it_empty(self):
        placed = self.placed(sheet({'fit': WRAP}))
        self.assertFalse(placed['spills'])
        self.assertTrue(placed['wraps'])

    def test_clip_neither_runs_on_nor_breaks(self):
        placed = self.placed(sheet({'fit': CLIP}))
        self.assertFalse(placed['spills'])
        self.assertFalse(placed['wraps'])

    def test_mask_shows_a_text_too_wide_as_hashes_and_keeps_it(self):
        placed = self.placed(sheet({'fit': MASK}))
        self.assertFalse(placed['spills'])
        self.assertFalse(placed['wraps'])
        self.assertTrue(placed['masked'])
        self.assertRegex(placed['mask'], r'^#+$')
        self.assertEqual(placed['text'], LONG)

    def test_mask_shows_a_text_that_fits_as_it_is(self):
        placed = self.placed({'cells': [{'row': 0, 'col': 0, 'value': '12.50', 'fit': MASK}], 'cols': COLS})
        self.assertFalse(placed['masked'])
        self.assertEqual(placed['text'], '12.50')


if __name__ == '__main__':
    unittest.main()
