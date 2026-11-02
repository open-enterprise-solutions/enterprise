"""A grid sizer: every cell one size, as the desktop's grid gives them - a column's share of the width, not its widest
content, and every row as tall as the tallest - an item that does not stretch keeping its own width in its cell, and
the columns counted from the rows when only the rows are given."""
import unittest

from harness import PageTest, table

# What a button shows (ibState::ibRepresentation): its caption alone.
TEXT = 1
# An item's Stretch (wxStretch): it fills its box across.
EXPAND = 0x2000


def item(item_id, control, stretch=EXPAND):
    return {'NodeType': 'SizerItem', 'ControlId': item_id, 'Proportion': 0, 'Stretch': stretch, 'NodeChildren': [control]}


def button(button_id, caption, minimum=None):
    node = {'NodeType': 'Button', 'ControlId': button_id,
            'State': {'Caption': caption, 'Representation': TEXT, 'Visible': True, 'Enabled': True}}
    if minimum is not None:
        node['MinimumSize'] = minimum
    return node


def scenario(captions, tall=None, stretch=EXPAND, cols=2, rows=0):
    grid = {'NodeType': 'Gridsizer', 'ControlId': 701, 'Cols': cols, 'Rows': rows, 'NodeChildren': [
        item(702 + place, button(710 + place, caption, tall if place == 0 else None), stretch) for place, caption in enumerate(captions)]}
    return {'tabs': [{'title': 'Panel', 'controls': [item(700, grid)], 'table': table()}]}


CELLS = "(cells) => cells.map((cell) => { const box = cell.getBoundingClientRect(); return [box.width, box.height]; })"


class AGrid(PageTest):
    def test_gives_every_column_the_same_width_whatever_it_holds(self):
        page = self.start(scenario(['A', 'A caption much longer than the one beside it', 'B', 'C']))
        page.wait_for_selector('oes-gridsizer oes-button')
        columns = page.locator('oes-gridsizer').evaluate('(grid) => getComputedStyle(grid).gridTemplateColumns')
        widths = [float(width[:-2]) for width in columns.split()]
        self.assertEqual(len(widths), 2)
        self.assertLess(abs(widths[0] - widths[1]), 1, columns)
        cells = page.locator('oes-gridsizer > oes-sizeritem').evaluate_all(CELLS)
        self.assertLess(abs(cells[0][0] - cells[1][0]), 1, cells)

    def test_gives_every_row_the_tallest_rows_height(self):
        # A button of a taller minimum makes its row taller; the other row is as tall.
        page = self.start(scenario(['A', 'B', 'C', 'D'], tall='0,120'))
        page.wait_for_selector('oes-gridsizer oes-button')
        cells = page.locator('oes-gridsizer > oes-sizeritem').evaluate_all(CELLS)
        heights = [height for _, height in cells]
        self.assertLess(max(heights) - min(heights), 1, heights)

    def test_a_column_is_never_narrower_than_what_it_holds(self):
        # A form too narrow for two of the widest: the wide column takes what it holds, and nothing is drawn over its
        # neighbour.
        # Wider than the narrow form's fold (40rem), narrower than two of the long caption.
        long = 'A caption much longer than the one beside it, and longer still, and longer again, and once more'
        page = self.start(scenario([long, 'OK', 'B', 'C'], stretch=0))
        page.set_viewport_size({'width': 800, 'height': 800})
        page.wait_for_selector('oes-gridsizer oes-button')
        page.wait_for_timeout(300)
        boxes = page.locator('oes-gridsizer oes-button').evaluate_all(
            '(buttons) => buttons.map((b) => { const box = b.getBoundingClientRect(); return [box.left, box.right]; })')
        cells = page.locator('oes-gridsizer > oes-sizeritem').evaluate_all(CELLS)
        self.assertLessEqual(boxes[0][1], boxes[1][0] + 1, ('the long button ends before the next begins', boxes))
        columns = [float(width[:-2]) for width in
                   page.locator('oes-gridsizer').evaluate('(grid) => getComputedStyle(grid).gridTemplateColumns').split()]
        self.assertGreaterEqual(columns[0] + 1, cells[0][0], (columns, cells))

    def test_rows_given_keep_their_place_empty_or_not(self):
        page = self.start(scenario(['A', 'B'], cols=2, rows=3))
        page.wait_for_selector('oes-gridsizer oes-button')
        rows = page.locator('oes-gridsizer').evaluate('(grid) => getComputedStyle(grid).gridTemplateRows')
        self.assertEqual(len(rows.split()), 3, rows)

    def test_a_shrink_item_keeps_its_own_width_at_the_cells_start(self):
        # Shrink (a sizer item's default): the button as wide as its caption, not its column, as the desktop places it.
        # On the harness's wide page, where a column is far wider than "OK".
        page = self.start(scenario(['OK', 'A caption much longer than the one beside it', 'B', 'C'], stretch=0))
        page.wait_for_selector('oes-gridsizer oes-button')
        grid = page.locator('oes-gridsizer').evaluate('(grid) => grid.getBoundingClientRect().left')
        column = page.locator('oes-gridsizer').evaluate('(grid) => parseFloat(getComputedStyle(grid).gridTemplateColumns)')
        ok = page.locator('oes-gridsizer oes-button').first.evaluate('(b) => { const box = b.getBoundingClientRect(); return [box.left, box.width]; }')
        self.assertLess(ok[1], column / 2, (ok, column))
        self.assertLess(abs(ok[0] - grid), 1, (ok, grid))

    def test_no_cols_and_rows_given_counts_the_columns_from_the_rows(self):
        page = self.start(scenario(['A', 'B', 'C', 'D', 'E'], cols=0, rows=2))
        page.wait_for_selector('oes-gridsizer oes-button')
        columns = page.locator('oes-gridsizer').evaluate('(grid) => getComputedStyle(grid).gridTemplateColumns')
        self.assertEqual(len(columns.split()), 3, columns)


if __name__ == '__main__':
    unittest.main()
