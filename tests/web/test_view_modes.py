"""A table's three ways of showing its rows, as the thin client has them."""
import unittest

from harness import NAME, STOCK, PageTest, column, row, tab

TREE, HIERARCHY, LIST = 0, 1, 2
OPEN = 7

ROWS = [
    row(1, 'Fasteners', container=True),
    row(2, 'Tools', container=True),
    row(3, 'Loose item', '5'),
    row(10, 'Bolts', container=True, parent=1),
    row(11, 'Nut', '7', parent=1),
    row(20, 'Hammer', '3', parent=2),
    row(100, 'Bolt M8', '9', parent=10),
]


def scenario(view_mode, **extra):
    return {'tabs': [tab(view_mode=view_mode, rows=ROWS, **extra)]}


def crumbs(page):
    return [text.replace('\n', '').strip() for text in page.locator('.crumb').all_inner_texts()]


class TheQuestion(PageTest):
    def ask(self, mode):
        page = self.start(scenario(mode))
        self.row('Fasteners').wait_for()
        page.locator('.command-bar wa-button', has_text='View mode').click()
        page.wait_for_selector('wa-dialog .view-modes')
        return page

    def test_offers_the_three_the_one_in_force_chosen(self):
        page = self.ask(HIERARCHY)
        self.assertEqual(page.locator('.view-modes label').all_inner_texts(), ['Tree', 'Hierarchy', 'List'])
        self.assertEqual(page.locator('.view-modes input:checked').get_attribute('value'), '1')

    def test_ok_answers_with_the_one_chosen(self):
        page = self.ask(HIERARCHY)
        page.locator('.view-modes label', has_text='List').click()
        page.locator('wa-dialog wa-button', has_text='OK').click()
        page.wait_for_function("window.fake.calls.some((call) => call.method === 'respond')")
        self.assertEqual(page.evaluate("window.fake.calls.find((call) => call.method === 'respond').params.Response"), {'Mode': 2})

    def test_cancel_answers_with_none(self):
        page = self.ask(HIERARCHY)
        page.locator('.view-modes label', has_text='List').click()
        page.locator('wa-dialog wa-button', has_text='Cancel').click()
        page.wait_for_function("window.fake.calls.some((call) => call.method === 'respond')")
        self.assertEqual(page.evaluate("window.fake.calls.find((call) => call.method === 'respond').params.Response ?? {}"), {})

    def test_the_rows_come_in_the_mode_chosen(self):
        page = self.ask(HIERARCHY)
        page.locator('.view-modes label', has_text='List').click()
        page.locator('wa-dialog wa-button', has_text='OK').click()
        self.row('Bolt M8').wait_for(timeout=5000)
        self.assertEqual(len(self.rows_text()), len(ROWS))


class ListView(PageTest):
    def test_every_row_flat(self):
        self.start(scenario(LIST))
        self.row('Bolt M8').wait_for()
        self.assertEqual(len(self.rows_text()), len(ROWS))
        self.assertEqual(self.page.locator('.expander:visible').count(), 0)
        self.assertEqual(self.page.locator('.crumb').count(), 0)


class Hierarchy(PageTest):
    def enter(self, name):
        self.row(name).dblclick()

    def test_shows_one_level(self):
        self.start(scenario(HIERARCHY))
        self.row('Fasteners').wait_for()
        self.assertEqual(self.rows_text(), ['▸Fasteners', '▸Tools', 'Loose item|5'])

    def test_a_folder_s_mark_is_no_button(self):
        page = self.start(scenario(HIERARCHY))
        self.row('Fasteners').wait_for()
        self.assertEqual(page.locator('button.expander').count(), 0)

    def test_a_folder_is_entered_not_opened_by_the_server(self):
        page = self.start(scenario(HIERARCHY))
        self.row('Fasteners').wait_for()
        self.enter('Fasteners')
        self.row('Nut').wait_for()
        self.assertEqual(self.sent(OPEN), [])
        self.assertEqual(self.reads()[-1]['Parent'], 1)
        self.assertEqual(self.rows_text(), ['▸Bolts', 'Nut|7'])
        self.assertEqual(crumbs(page), ['▾Fasteners'])

    def test_a_row_that_is_no_folder_goes_to_the_server(self):
        self.start(scenario(HIERARCHY))
        self.row('Loose item').wait_for()
        self.enter('Loose item')
        self.page.wait_for_function('window.fake.events(7).length === 1')
        self.assertEqual(self.sent(OPEN)[0]['Args']['Row'], 3)

    def test_the_crumbs_stand_over_the_level_outermost_first(self):
        page = self.start(scenario(HIERARCHY))
        self.row('Fasteners').wait_for()
        self.enter('Fasteners')
        self.row('Nut').wait_for()
        self.enter('Bolts')
        self.row('Bolt M8').wait_for()
        self.assertEqual(crumbs(page), ['▾Fasteners', '▾Bolts'])
        self.assertEqual(self.rows_text(), ['Bolt M8|9'])
        crumb = page.locator('.crumb').last.bounding_box()
        first = self.row('Bolt M8').bounding_box()
        self.assertLessEqual(crumb['y'] + crumb['height'], first['y'] + 1)

    def test_a_crumb_goes_back_to_the_level_it_stands_in(self):
        page = self.start(scenario(HIERARCHY))
        self.row('Fasteners').wait_for()
        self.enter('Fasteners')
        self.row('Nut').wait_for()
        self.enter('Bolts')
        self.row('Bolt M8').wait_for()
        page.locator('.crumb', has_text='Bolts').dblclick()
        self.row('Nut').wait_for()
        self.assertEqual(self.reads()[-1]['Parent'], 1)
        self.assertEqual(crumbs(page), ['▾Fasteners'])
        page.locator('.crumb', has_text='Fasteners').dblclick()
        self.row('Tools').wait_for()
        self.assertEqual(self.reads()[-1]['Parent'], 0)
        self.assertEqual(page.locator('.crumb').count(), 0)
        self.assertEqual(self.rows_text(), ['▸Fasteners', '▸Tools', 'Loose item|5'])
        self.assertEqual(self.sent(OPEN), [])

    def test_a_list_read_again_inside_a_folder_reads_that_folder(self):
        page = self.start(scenario(HIERARCHY))
        self.row('Fasteners').wait_for()
        self.enter('Fasteners')
        self.row('Nut').wait_for()
        page.evaluate("""() => { const table = window.fake.tab().table; table.rows.push({id: 12, parent: 1, cells: {'2': 'Washer', '3': '1'}});
            table.version = 2; window.fake.push(); }""")
        self.row('Washer').wait_for(timeout=5000)
        self.assertEqual(self.reads()[-1]['Parent'], 1)
        self.assertEqual(page.locator('.crumb').count(), 1)

    def test_the_cursor_stands_in_the_level_entered(self):
        page = self.start(scenario(HIERARCHY))
        self.row('Fasteners').wait_for()
        self.enter('Fasteners')
        self.row('Nut').wait_for()
        page.wait_for_function("document.querySelector('.current-row')?.textContent.includes('Bolts')", timeout=3000)
        self.assertEqual(self.page.evaluate('window.fake.tab().table.answerCurrent ?? null'), None)


class Tree(PageTest):
    def expander(self, name):
        return self.row(name).locator('.expander')

    def test_shows_the_top_a_folder_with_an_expander(self):
        page = self.start(scenario(TREE))
        self.row('Fasteners').wait_for()
        self.assertEqual(self.rows_text(), ['▸Fasteners', '▸Tools', 'Loose item|5'])
        self.assertEqual(page.locator('.expander:visible').count(), 2)

    def test_an_expander_opens_the_folder_in_place_a_level_in(self):
        self.start(scenario(TREE))
        self.row('Fasteners').wait_for()
        self.expander('Fasteners').click()
        self.row('Nut').wait_for(timeout=5000)
        self.assertEqual(self.rows_text(), ['▾Fasteners', '▸Bolts', 'Nut|7', '▸Tools', 'Loose item|5'])
        self.assertEqual(self.reads()[-1]['Parent'], 1)
        step = self.page.evaluate("""() => { const left = (text) => [...document.querySelectorAll('.tabulator-row')].find((row) =>
            row.textContent.includes(text)).querySelector('.first-cell').style.paddingInlineStart; return [left('Fasteners'), left('Nut')]; }""")
        self.assertEqual(step, ['0px', '16px'])

    def test_an_expander_is_neither_a_move_nor_an_activation(self):
        self.start(scenario(TREE))
        self.row('Fasteners').wait_for()
        self.expander('Tools').click()
        self.row('Hammer').wait_for(timeout=5000)
        self.assertEqual(self.sent(OPEN), [])
        self.assertEqual(self.sent(12), [])

    def test_closed_again_what_it_holds_goes_at_every_depth(self):
        self.start(scenario(TREE))
        self.row('Fasteners').wait_for()
        self.expander('Fasteners').click()
        self.row('Bolts').wait_for(timeout=5000)
        self.expander('Bolts').click()
        self.row('Bolt M8').wait_for(timeout=5000)
        self.expander('Fasteners').click()
        self.page.wait_for_function("!document.querySelector('.tabulator-tableholder').textContent.includes('Nut')", timeout=3000)
        self.assertEqual(self.rows_text(), ['▸Fasteners', '▸Tools', 'Loose item|5'])

    def test_a_folder_activated_goes_to_the_server(self):
        self.start(scenario(TREE))
        self.row('Fasteners').wait_for()
        self.row('Tools').locator('.first-text').dblclick()
        self.page.wait_for_function('window.fake.events(7).length === 1')
        self.assertEqual(self.sent(OPEN)[0]['Args']['Row'], 2)

    def test_a_folder_stays_open_when_the_rows_are_read_again(self):
        page = self.start(scenario(TREE))
        self.row('Fasteners').wait_for()
        self.expander('Fasteners').click()
        self.row('Nut').wait_for(timeout=5000)
        page.evaluate("""() => { const table = window.fake.tab().table; table.rows.find((one) => one.id === 11).cells['3'] = '8';
            table.version = 2; window.fake.push(); }""")
        self.row('Nut').locator('text=8').wait_for(timeout=5000)
        self.assertEqual(self.rows_text(), ['▾Fasteners', '▸Bolts', 'Nut|8', '▸Tools', 'Loose item|5'])

    def test_the_mode_switched_leaves_no_folder_and_no_expander(self):
        page = self.start(scenario(TREE))
        self.row('Fasteners').wait_for()
        self.expander('Fasteners').click()
        self.row('Nut').wait_for(timeout=5000)
        page.locator('.command-bar wa-button', has_text='View mode').click()
        page.wait_for_selector('wa-dialog .view-modes')
        page.locator('.view-modes label', has_text='List').click()
        page.locator('wa-dialog wa-button', has_text='OK').click()
        self.row('Bolt M8').wait_for(timeout=5000)
        self.assertEqual(page.locator('.expander:visible').count(), 0)
        self.assertEqual(len(self.rows_text()), len(ROWS))

    def test_the_keys_open_close_and_go_to_the_parent(self):
        page = self.start(scenario(TREE))
        self.row('Fasteners').click()
        page.keyboard.press('ArrowRight')
        self.row('Nut').wait_for(timeout=5000)
        page.keyboard.press('ArrowRight')
        page.wait_for_function("document.querySelector('.current-row')?.textContent.includes('Bolts')")
        page.keyboard.press('ArrowLeft')
        page.wait_for_function("document.querySelector('.current-row')?.textContent.includes('Fasteners')")
        page.keyboard.press('ArrowLeft')
        page.wait_for_function("!document.querySelector('.tabulator-tableholder').textContent.includes('Nut')", timeout=3000)

    def test_a_star_opens_the_folder_whole(self):
        page = self.start(scenario(TREE))
        self.row('Fasteners').click()
        page.keyboard.press('*')
        self.row('Bolt M8').wait_for(timeout=5000)
        page.keyboard.press('-')
        page.wait_for_function("!document.querySelector('.tabulator-tableholder').textContent.includes('Nut')", timeout=3000)


class TheFirstCell(PageTest):
    def test_the_row_s_place_is_in_the_first_column_in_every_view(self):
        for mode in (TREE, HIERARCHY, LIST):
            with self.subTest(mode=mode):
                page = self.start(scenario(mode))
                self.row('Loose item').wait_for()
                self.assertEqual(page.locator('.tabulator-header .tabulator-col').count(), 2)
                self.assertEqual(self.row('Loose item').locator('.first-cell').count(), 1)
                self.context.close()
                self.context = None

    def test_a_group_s_row_is_its_caption_across_the_line(self):
        caption = 'The whole caption ' + 'of this group ' * 12 + 'to its end'
        rows = [row(1, 'Goods', 'stands under', caption=caption), row(2, 'Nut', '7')]
        columns = [column(NAME, 'Name'), column(STOCK, 'Stock')]
        page = self.start({'tabs': [tab(view_mode=LIST, rows=rows, columns=columns)]})
        self.row('The whole caption').wait_for()
        self.assertEqual(self.rows_text()[0], caption)
        text = page.locator('.group-row .first-text')
        self.assertGreater(text.evaluate('(one) => one.scrollWidth'), page.locator('.group-row .tabulator-cell').first.evaluate('(one) => one.clientWidth'))
        self.assertEqual(text.evaluate('(one) => getComputedStyle(one).overflow'), 'visible')


class AColumnOfOneName(PageTest):
    def test_a_tree_of_one_column(self):
        self.start({'tabs': [tab(view_mode=TREE, rows=ROWS, columns=[column(NAME, 'Name')])]})
        self.row('Fasteners').wait_for()
        self.assertEqual(self.page.locator('.expander:visible').count(), 2)


if __name__ == '__main__':
    unittest.main()
