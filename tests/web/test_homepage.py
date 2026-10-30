"""The start page: a container of live forms in columns, with sashes between them; each form goes back by its own Key."""
import unittest

from harness import PageTest, row, table

KEY_READS = "window.fake.calls.filter((call) => call.method === 'fetch').map((call) => call.params.Form)"


def cell(title, name, **extra):
    return {'title': title, 'table': table(rows=[row(1, name)], bar=False), **extra}


def page_tab(columns, **extra):
    return {'title': 'Home page', 'locked': True, 'columns': columns, **extra}


class HomePage(PageTest):
    def sizes(self, selector, side):
        return self.page.evaluate('([selector, side]) => [...document.querySelectorAll(selector)]'
                                  '.map((one) => one.getBoundingClientRect()[side])', [selector, side])

    def drag(self, sash, dx, dy):
        box = self.page.locator(sash).first.bounding_box()
        self.page.mouse.move(box['x'] + box['width'] / 2, box['y'] + box['height'] / 2)
        self.page.mouse.down()
        self.page.mouse.move(box['x'] + box['width'] / 2 + dx, box['y'] + box['height'] / 2 + dy, steps=4)
        self.page.mouse.up()

    def two_columns(self, **extra):
        return self.start({'tabs': [page_tab([[cell('Goods', 'Bolt'), cell('Stock', 'Nut', height=3)], [cell('Receipts', 'First')]], **extra)]})

    def test_draws_each_form_under_its_strip(self):
        page = self.two_columns()
        self.row('Bolt').wait_for()
        self.row('Nut').wait_for()
        self.row('First').wait_for()
        self.assertEqual(page.locator('.home-strip').all_inner_texts(), ['Goods', 'Stock', 'Receipts'])
        self.assertEqual(page.locator('.view-placeholder').count(), 0)

    def test_each_form_reads_by_its_own_key(self):
        page = self.two_columns()
        self.row('First').wait_for()
        keys = page.evaluate(KEY_READS)
        self.assertEqual(len(set(keys)), 3)

    def test_the_shares_are_the_servers(self):
        self.two_columns(gravity=0.25)
        self.row('First').wait_for()
        left, right = self.sizes('.home-column', 'width')
        self.assertAlmostEqual(left / (left + right), 0.25, places=1)
        goods, stock = self.sizes('.home-column:first-child .home-cell', 'height')
        self.assertAlmostEqual(stock / (goods + stock), 0.75, places=1)

    def test_a_sash_gives_one_pane_what_the_other_loses(self):
        self.two_columns()
        self.row('First').wait_for()
        left, right = self.sizes('.home-column', 'width')
        self.drag('.home-sash.horizontal', -100, 0)
        moved_left, moved_right = self.sizes('.home-column', 'width')
        self.assertAlmostEqual(moved_left, left - 100, delta=2)
        self.assertAlmostEqual(moved_right, right + 100, delta=2)

    def test_a_sash_leaves_a_pane_its_least(self):
        self.two_columns()
        self.row('First').wait_for()
        self.drag('.home-sash.horizontal', -2000, 0)
        self.assertAlmostEqual(self.sizes('.home-column', 'width')[0], 80, delta=2)

    def test_a_cell_sash_moves_the_cells_of_its_column(self):
        self.two_columns()
        self.row('First').wait_for()
        goods, stock = self.sizes('.home-column:first-child .home-cell', 'height')
        self.drag('.home-sash.vertical', 0, 50)
        moved_goods, moved_stock = self.sizes('.home-column:first-child .home-cell', 'height')
        self.assertAlmostEqual(moved_goods, goods + 50, delta=2)
        self.assertAlmostEqual(moved_stock, stock - 50, delta=2)

    def test_one_column_takes_the_width_and_has_no_sash(self):
        page = self.start({'tabs': [page_tab([[cell('Goods', 'Bolt')]])]})
        self.row('Bolt').wait_for()
        self.assertEqual(page.locator('.home-sash').count(), 0)

    def test_a_form_that_would_not_open_is_said_in_its_cell(self):
        page = self.start({'tabs': [page_tab([[cell('Goods', 'Bolt')], [{'title': 'Gone', 'unavailable': 'Form is not available'}]])]})
        self.row('Bolt').wait_for()
        self.assertEqual(page.locator('.home-unavailable:visible').inner_text(), 'Form is not available')

    def test_a_form_on_it_is_not_folded_while_the_window_is_wide(self):
        page = self.start({'tabs': [page_tab([[cell('Goods', 'Bolt')], [cell('Receipts', 'First')]], gravity=0.2)]})
        self.row('Bolt').wait_for()
        min_height = page.evaluate("getComputedStyle(document.querySelector('oes-homepage oes-tablebox')).minHeight")
        self.assertNotIn('dvh', min_height)
        self.assertLess(float(min_height.removesuffix('px')), page.viewport_size['height'] * 0.5)
        self.assertEqual(page.locator('.tabulator-responsive-collapse').count(), 0)

    def test_escape_leaves_it_open(self):
        page = self.two_columns()
        self.row('First').wait_for()
        page.keyboard.press('Escape')
        page.wait_for_timeout(200)
        self.assertEqual(page.evaluate("window.fake.calls.filter((call) => call.method === 'close').length"), 0)


if __name__ == '__main__':
    unittest.main()
