"""A table's footer, its column groups, what the cursor stands on, and the cell's editor."""
import unittest

from harness import NAME, STOCK, PageTest, column, group, row, tab

SIDE_BY_SIDE, ONE_UNDER_ANOTHER, IN_CELL = 0, 1, 2
RIGHT = 0x200
ROW, CHANGE, SELECT, OPEN = 12, 5, 6, 7

FOOTERS = [column(NAME, 'Name', FooterAlign=0, state={'FooterText': 'Total'}),
           column(STOCK, 'Stock', FooterAlign=RIGHT, state={'FooterText': '150'})]


def footers(page):
    return [text.strip() for text in page.locator('.tabulator-calcs .column-footer').all_inner_texts()]


class Footer(PageTest):
    def test_shows_each_column_s_text(self):
        page = self.start({'tabs': [tab(columns=FOOTERS, footer=True)]})
        self.row('Item 1').wait_for()
        page.wait_for_selector('.column-footer')
        self.assertEqual(footers(page), ['Total', '150'])

    def test_stands_under_the_rows_and_where_its_alignment_puts_it(self):
        page = self.start({'tabs': [tab(columns=FOOTERS, footer=True)]})
        self.row('Item 5').wait_for()
        page.wait_for_selector('.column-footer')
        self.assertEqual(page.locator('.column-footer').nth(1).evaluate('(one) => one.style.justifyContent'), 'flex-end')
        under = page.locator('.tabulator-calcs').first.bounding_box()
        last = self.row('Item 5').bounding_box()
        self.assertGreaterEqual(under['y'], last['y'] + last['height'] - 1)

    def test_is_none_when_the_table_shows_none(self):
        page = self.start({'tabs': [tab(columns=FOOTERS, footer=False)]})
        self.row('Item 1').wait_for()
        self.assertEqual(page.locator('.column-footer').count(), 0)

    def test_a_picture_only_representation_shows_no_text(self):
        columns = [column(NAME, 'Name', state={'FooterText': 'Total', 'Representation': 2})]
        page = self.start({'tabs': [tab(columns=columns, footer=True)]})
        self.row('Item 1').wait_for()
        page.wait_for_selector('.column-footer', state='attached')
        self.assertEqual(footers(page), [''])

    def test_a_group_drawn_as_one_keeps_its_columns_footers(self):
        columns = [group(9, 'Both', FOOTERS, ONE_UNDER_ANOTHER)]
        page = self.start({'tabs': [tab(columns=columns, footer=True)]})
        self.row('Item 1').wait_for()
        page.wait_for_selector('.column-footer')
        self.assertEqual(footers(page), ['Total', '150'])


class Groups(PageTest):
    def columns(self, grouping, show_title=False):
        return [group(9, 'Goods', [column(NAME, 'Name'), column(STOCK, 'Stock')], grouping, show_title)]

    def headers(self):
        return [text.strip() for text in self.page.locator('.tabulator-header .tabulator-col-title').all_text_contents()]

    def test_side_by_side_is_a_column_each_the_title_over_them(self):
        page = self.start({'tabs': [tab(columns=self.columns(SIDE_BY_SIDE, True))]})
        self.row('Item 1').wait_for()
        self.assertIn('Goods', self.headers())
        self.assertIn('Name', self.headers())
        title = page.locator('.tabulator-col-group', has_text='Goods').bounding_box()
        name = page.locator('.tabulator-col', has_text='Name').last.bounding_box()
        self.assertLessEqual(title['y'], name['y'])
        self.assertGreaterEqual(title['width'], name['width'])
        self.assertEqual(self.rows_text()[0], 'Item 1|10')

    def test_side_by_side_with_no_title_shows_none(self):
        self.start({'tabs': [tab(columns=self.columns(SIDE_BY_SIDE))]})
        self.row('Item 1').wait_for()
        self.assertNotIn('Goods', self.headers())

    def test_one_under_another_stacks_them_in_a_taller_row(self):
        page = self.start({'tabs': [tab(columns=self.columns(ONE_UNDER_ANOTHER))]})
        self.row('Item 1').wait_for()
        lines = page.locator('.tabulator-tableholder .joined.one-under-another').first.locator('[data-column]')
        self.assertEqual(lines.all_text_contents(), ['Item 1', '10'])
        first, second = lines.nth(0).bounding_box(), lines.nth(1).bounding_box()
        self.assertGreater(second['y'], first['y'])
        whole = self.row('Item 1').bounding_box()
        self.assertLessEqual(second['y'] + second['height'], whole['y'] + whole['height'] + 1, 'the row cuts its second line')

    def test_in_cell_lays_them_on_one_line(self):
        page = self.start({'tabs': [tab(columns=self.columns(IN_CELL))]})
        self.row('Item 1').wait_for()
        lines = page.locator('.tabulator-tableholder .joined.in-cell').first.locator('[data-column]')
        self.assertEqual(lines.all_text_contents(), ['Item 1', '10'])
        self.assertEqual(lines.nth(1).bounding_box()['y'], lines.nth(0).bounding_box()['y'])

    def test_a_hidden_column_of_a_group_is_not_drawn(self):
        hidden = column(STOCK, 'Stock', state={'Visible': False})
        columns = [group(9, 'Goods', [column(NAME, 'Name'), hidden, column(4, 'Extra')], ONE_UNDER_ANOTHER)]
        page = self.start({'tabs': [tab(columns=columns)]})
        self.row('Item 1').wait_for()
        self.assertEqual(page.locator('.tabulator-tableholder .joined').first.locator(f'[data-column="{STOCK}"]').count(), 0)

    def test_a_hidden_group_is_not_drawn(self):
        columns = [column(NAME, 'Name'), {**group(9, 'Goods', [column(STOCK, 'Stock')], SIDE_BY_SIDE),
                                          'State': {'Caption': 'Goods', 'Visible': False}}]
        self.start({'tabs': [tab(columns=columns)]})
        self.row('Item 1').wait_for()
        self.assertNotIn('Stock', self.headers())


class RowSelection(PageTest):
    def test_the_row_is_marked_and_no_cell(self):
        page = self.start({'tabs': [tab(selection=1)]})
        self.row('Item 2').click()
        page.wait_for_function("document.querySelector('.current-row')?.textContent.includes('Item 2')")
        self.assertEqual(page.locator('.current-cell').count(), 0)

    def test_a_second_click_on_the_row_leaves_it_marked(self):
        page = self.start({'tabs': [tab(selection=1)]})
        self.row('Item 2').click()
        self.row('Item 2').click()
        page.wait_for_timeout(100)
        self.assertEqual(page.locator('.current-row').count(), 1)

    def test_the_keys_work_after_a_click_with_nothing_else_focused(self):
        page = self.start({'tabs': [tab(selection=1)]})
        self.row('Item 2').click()
        page.keyboard.press('ArrowDown')
        page.wait_for_function("document.querySelector('.current-row')?.textContent.includes('Item 3')")

    def test_the_table_takes_the_focus_as_its_form_opens(self):
        page = self.start({'tabs': [tab(selection=1, currentRow=2)]})
        self.row('Item 2').wait_for()
        page.wait_for_function("document.querySelector('oes-tablebox .grid') === document.activeElement")
        page.keyboard.press('ArrowDown')
        page.wait_for_function("document.querySelector('.current-row')?.textContent.includes('Item 3')")

    def test_the_table_takes_the_focus_from_the_button_that_opened_its_form(self):
        page = self.start({'tabs': [tab(selection=1)]})
        self.row('Item 2').wait_for()
        page.evaluate("""() => { const button = document.createElement('button'); document.querySelector('.client-window header').append(button);
            button.focus(); window.fake.open(window.fakeSecond); }""".replace('window.fakeSecond', "{title: 'Second', table: window.fake.tab().table}"))
        page.wait_for_function("document.activeElement === document.querySelector('oes-clientform:not([hidden]) oes-tablebox .grid')")

    def test_the_ends_and_a_page(self):
        page = self.start({'tabs': [tab(selection=1, currentRow=2)]})
        self.row('Item 2').wait_for()
        page.locator('oes-tablebox .grid').focus()
        for key, name in (('End', 'Item 5'), ('Home', 'Item 1'), ('PageDown', 'Item 5'), ('PageUp', 'Item 1')):
            page.keyboard.press(key)
            page.wait_for_function("(name) => document.querySelector('.current-row')?.textContent.includes(name)", arg=name)

    def test_the_arrows_sideways_mark_a_cell_and_back_past_it_the_row_is_whole(self):
        page = self.start({'tabs': [tab(selection=1)]})
        self.row('Item 2').click()
        page.keyboard.press('ArrowRight')
        page.wait_for_function("document.querySelector('.current-cell')?.textContent.trim() === 'Item 2'")
        page.keyboard.press('ArrowRight')
        page.wait_for_function("document.querySelector('.current-cell')?.textContent.trim() === '20'")
        page.keyboard.press('ArrowLeft')
        page.keyboard.press('ArrowLeft')
        page.wait_for_function("document.querySelector('.current-cell') === null")

    def test_the_keys_outlive_the_row_clicked_being_drawn_anew(self):
        page = self.start({'tabs': [tab(selection=1)]})
        self.row('Item 2').click()
        page.evaluate("""() => { const box = document.querySelector('oes-tablebox');
            box.table.getRows().forEach((one) => one.reformat()); }""")
        page.keyboard.press('ArrowDown')
        page.wait_for_function("document.querySelector('.current-row')?.textContent.includes('Item 3')")

    def test_a_cell_is_no_stop_of_tab(self):
        page = self.start({'tabs': [tab(selection=1)]})
        self.row('Item 2').wait_for()
        self.assertEqual(page.locator('.tabulator-cell[tabindex]').count(), 0)

    def test_tab_goes_on_to_the_next_row(self):
        page = self.start({'tabs': [tab(selection=0)]})
        self.row('Item 2').locator('.tabulator-cell', has_text='20').click()
        page.keyboard.press('Tab')
        page.wait_for_function("document.querySelector('.current-cell')?.textContent.trim() === 'Item 3'")

    def test_a_column_dragged_says_where_it_stands(self):
        page = self.start({'tabs': [tab()]})
        self.row('Item 1').wait_for()
        name = page.locator('.tabulator-col', has_text='Name').bounding_box()
        stock = page.locator('.tabulator-col', has_text='Stock').bounding_box()
        page.mouse.move(name['x'] + 20, name['y'] + name['height'] / 2)
        page.mouse.down()
        page.wait_for_timeout(300)
        page.mouse.move(stock['x'] + 200, stock['y'] + stock['height'] / 2, steps=8)
        page.mouse.move(stock['x'] + stock['width'] - 4, stock['y'] + stock['height'] / 2, steps=8)
        page.mouse.up()
        page.wait_for_function("window.fake.calls.flatMap((call) => [call.params, ...(call.params?.Pending ?? [])]).some((one) => one?.Event === 11)"
                               " || document.querySelector('.tabulator-col').textContent.includes('Stock')")
        page.locator('.command-bar wa-button', has_text='View mode').click()
        page.wait_for_function("window.fake.calls.flatMap((call) => [call.params, ...(call.params?.Pending ?? [])]).some((one) => one?.Event === 11)")
        moved = page.evaluate("window.fake.calls.flatMap((call) => [call.params, ...(call.params?.Pending ?? [])]).find((one) => one?.Event === 11)")
        self.assertEqual(moved['Args'], {'Column': NAME, 'Holder': 0, 'Position': 1})

    def test_a_group_s_columns_are_not_dragged(self):
        columns = [group(9, 'Goods', [column(NAME, 'Name'), column(STOCK, 'Stock')], SIDE_BY_SIDE)]
        page = self.start({'tabs': [tab(columns=columns)]})
        self.row('Item 1').wait_for()
        self.assertEqual(page.locator('.tabulator-moving').count(), 0)
        self.assertFalse(page.evaluate("document.querySelector('.tabulator-col').hasAttribute('draggable')"))


class ContextMenu(PageTest):
    MENU = [{'Id': 11, 'Caption': 'Add'}, {'Id': 12, 'Caption': 'Delete', 'Enabled': False}]

    def open(self, **extra):
        page = self.start({'tabs': [tab(contextMenu=self.MENU, **extra)]})
        self.row('Item 1').wait_for()
        return page

    def test_on_a_row_it_moves_the_cursor_and_offers_the_commands(self):
        page = self.open()
        self.row('Item 2').click(button='right')
        page.wait_for_selector('.popup-menu:popover-open')
        self.assertEqual(page.locator('.popup-menu button').all_inner_texts(), ['Add', 'Delete'])
        page.wait_for_function("document.querySelector('.current-row')?.textContent.includes('Item 2')")

    def test_past_the_last_row_it_offers_them_too(self):
        page = self.open()
        last = self.row('Item 5').bounding_box()
        page.mouse.click(last['x'] + 100, last['y'] + last['height'] + 60, button='right')
        page.wait_for_selector('.popup-menu:popover-open')
        self.assertEqual(page.locator('.popup-menu button').count(), 2)

    def test_in_an_empty_table_too(self):
        page = self.start({'tabs': [tab(contextMenu=self.MENU, rows=[])]})
        frame = page.locator('oes-tablebox .grid-frame').bounding_box()
        page.mouse.click(frame['x'] + 100, frame['y'] + frame['height'] - 20, button='right')
        page.wait_for_selector('.popup-menu:popover-open')

    def test_a_text_too_long_loses_its_middle(self):
        text = 'Limited liability company of fasteners and bolts'
        page = self.start({'tabs': [tab(rows=[row(1, text)], columns=[column(NAME, 'A caption that does not fit into the width'), column(STOCK, 'Stock', Width=1100)])]})
        cell = page.locator('.tabulator-row .cut').first
        cell.wait_for()
        self.assertEqual(cell.evaluate('(one) => one.textContent'), text, 'the whole text stands behind the cut')
        self.assertEqual(cell.get_attribute('title'), text)
        widths = cell.evaluate("""(one) => {
            const start = one.querySelector('.cut-start'), end = one.querySelector('.cut-end');
            return { cell: one.closest('.tabulator-cell').clientWidth, whole: one.getBoundingClientRect().width,
                     start: start.clientWidth, end: end.clientWidth,
                     startCut: start.scrollWidth > start.clientWidth, endCut: end.firstChild.getBoundingClientRect().width > end.clientWidth,
                     endAtItsEnd: end.firstChild.getBoundingClientRect().right === end.getBoundingClientRect().right };
        }""")
        self.assertLessEqual(widths['whole'], widths['cell'])
        self.assertTrue(widths['startCut'] and widths['endCut'], widths)
        self.assertTrue(widths['endAtItsEnd'], widths)
        self.assertAlmostEqual(widths['start'], widths['end'], delta=widths['cell'] * 0.15)
        caption = page.locator('.tabulator-col .cut').first
        self.assertLessEqual(caption.bounding_box()['width'], page.locator('.tabulator-col').first.bounding_box()['width'])

    def test_a_text_that_fits_reads_as_one(self):
        page = self.start({'tabs': [tab(rows=[row(1, 'Bolt M8')])]})
        self.row('Bolt M8').wait_for()
        box = page.locator('.tabulator-row .cut').first.bounding_box()
        probe = page.evaluate("""() => { const one = document.createElement('span'); one.textContent = 'Bolt M8';
            one.style.whiteSpace = 'pre'; document.querySelector('.tabulator-row .first-text').append(one);
            const width = one.getBoundingClientRect().width; one.remove(); return width; }""")
        self.assertAlmostEqual(box['width'], probe, delta=2)

    def test_asked_at_the_press_it_opens_at_the_release(self):
        # Linux and macOS ask for the menu as the button goes down, Windows as it comes up: here the first, on any OS.
        page = self.open()
        at = self.row('Item 2').bounding_box()
        x, y = at['x'] + 100, at['y'] + at['height'] / 2
        # The browser's own is not the one asked here.
        page.evaluate("""() => addEventListener('contextmenu', (event) => {
            if (event.isTrusted)
                event.stopImmediatePropagation();
        }, true)""")
        page.mouse.move(x, y)
        page.mouse.down(button='right')
        page.evaluate("""([x, y]) => document.elementFromPoint(x, y).dispatchEvent(new MouseEvent('contextmenu',
            { bubbles: true, cancelable: true, button: 2, buttons: 2, clientX: x, clientY: y }))""", [x, y])
        self.assertEqual(page.locator('.popup-menu').count(), 0)
        page.mouse.up(button='right')
        page.wait_for_selector('.popup-menu:popover-open')
        page.wait_for_timeout(200)
        self.assertEqual(page.locator('.popup-menu:popover-open').count(), 1)

    def test_the_one_picked_goes_to_the_server(self):
        page = self.open()
        last = self.row('Item 5').bounding_box()
        page.mouse.click(last['x'] + 100, last['y'] + last['height'] + 60, button='right')
        page.locator('.popup-menu button', has_text='Add').click()
        page.wait_for_function("window.fake.events(16).length === 1")
        self.assertEqual(self.sent(16)[0]['Args'], {'Id': 11})

    def test_with_none_offered_it_stays_the_browser_s(self):
        page = self.start({'tabs': [tab()]})
        self.row('Item 1').click(button='right')
        page.wait_for_timeout(100)
        self.assertEqual(page.locator('.popup-menu').count(), 0)


class Lines(PageTest):
    def test_the_header_takes_the_lines_its_table_says(self):
        page = self.start({'tabs': [tab(headerHeight=3)]})
        self.row('Item 1').wait_for()
        high = page.locator('.tabulator-header').bounding_box()['height']
        self.assertGreaterEqual(high, 3 * 16)

    def test_the_footer_takes_the_lines_its_table_says(self):
        page = self.start({'tabs': [tab(columns=FOOTERS, footer=True, footerHeight=2)]})
        self.row('Item 1').wait_for()
        page.wait_for_selector('.column-footer')
        self.assertGreaterEqual(page.locator('.column-footer').first.bounding_box()['height'], 2 * 16)


class CellSelection(PageTest):
    def test_the_cell_is_marked_and_its_row_tinted(self):
        page = self.start({'tabs': [tab(selection=0)]})
        self.row('Item 2').locator('.tabulator-cell', has_text='20').click()
        page.wait_for_function("document.querySelector('.current-cell')?.textContent.trim() === '20'")
        self.assertEqual(page.locator('.current-cell').count(), 1)
        self.assertEqual(page.locator('.current-row').count(), 1)
        tinted = page.locator('.current-row').evaluate('(one) => getComputedStyle(one).backgroundColor')
        marked = page.locator('.current-cell').evaluate('(one) => getComputedStyle(one).backgroundColor')
        self.assertNotEqual(tinted, marked)

    def test_a_click_in_the_same_row_moves_the_cell_and_says_the_column(self):
        page = self.start({'tabs': [tab(selection=0)]})
        self.row('Item 2').locator('.tabulator-cell', has_text='Item 2').click()
        self.row('Item 2').locator('.tabulator-cell', has_text='20').click()
        page.wait_for_function("document.querySelector('.current-cell')?.textContent.trim() === '20'")
        self.assertEqual(page.locator('.current-row').count(), 1)
        page.locator('.command-bar wa-button', has_text='View mode').click()
        page.wait_for_function("window.fake.calls.some((call) => (call.params?.Pending ?? []).some((held) => held.Event === 12))")
        held = page.evaluate("window.fake.calls.flatMap((call) => call.params?.Pending ?? []).filter((one) => one.Event === 12).at(-1)")
        self.assertEqual(held['Args'], {'Row': 2, 'Column': STOCK})

    def test_the_arrows_move_the_cell_and_the_row(self):
        page = self.start({'tabs': [tab(selection=0)]})
        self.row('Item 2').locator('.tabulator-cell', has_text='Item 2').click()
        page.locator('oes-tablebox .grid').focus()
        page.keyboard.press('ArrowRight')
        page.wait_for_function("document.querySelector('.current-cell')?.textContent.trim() === '20'")
        page.keyboard.press('ArrowDown')
        page.wait_for_function("document.querySelector('.current-cell')?.textContent.trim() === '30'")

    def test_the_server_s_column_is_marked(self):
        page = self.start({'tabs': [tab(selection=0, currentRow=3, currentColumn=STOCK)]})
        page.wait_for_function("document.querySelector('.current-cell')?.textContent.trim() === '30'", timeout=3000)
        self.assertEqual(page.locator('.current-row').count(), 1)

    def test_on_a_group_drawn_as_one_the_part_is_marked(self):
        columns = [group(9, 'Goods', [column(NAME, 'Name'), column(STOCK, 'Stock')], ONE_UNDER_ANOTHER)]
        page = self.start({'tabs': [tab(selection=0, columns=columns)]})
        self.row('Item 2').locator(f'[data-column="{STOCK}"]').click()
        page.wait_for_function("document.querySelector('.current-cell')?.textContent.trim() === '20'")


class CellEditor(PageTest):
    ROWS = [row(1, 'Bolt', '5', read_only=False), row(2, 'Nut', '7', read_only=False)]

    def open(self, text='Bolt', **extra):
        page = self.start({'tabs': [{'title': 'Document', 'fields': [{'id': 50, 'caption': 'Number', 'text': '1'}],
                                     'table': {'id': 1, 'viewMode': 2, 'selection': 1, 'rows': self.ROWS,
                                               'columns': [column(NAME, 'Name', **extra), column(STOCK, 'Stock')]}}]})
        self.row('Nut').wait_for()
        self.row('Bolt').locator('.tabulator-cell', has_text=text).dblclick()
        page.wait_for_selector('.tabulator-cell wa-input.cell-editor')
        page.wait_for_function("document.activeElement?.localName === 'wa-input'", timeout=3000)
        return page

    def test_is_the_field_s_editor_and_looks_it(self):
        page = self.open()
        look = """(field) => { const style = getComputedStyle(field); const base = field.shadowRoot.querySelector('[part~="base"]');
            const inner = getComputedStyle(base);
            return [field.getAttribute('size'), style.fontSize, inner.borderTopWidth, inner.borderTopStyle]; }"""
        self.assertEqual(page.locator('.tabulator-cell wa-input').evaluate(look), page.locator('oes-textctrl wa-input').evaluate(look))

    def test_stands_inside_its_row(self):
        page = self.open()
        field = page.locator('.tabulator-cell wa-input').bounding_box()
        whole = self.row('Nut').bounding_box()
        self.assertLessEqual(field['height'], whole['height'])

    def test_has_the_column_s_buttons_as_a_field_has_them(self):
        page = self.open(ButtonOpen=True)
        shown = page.locator('.tabulator-cell wa-input .side-button:not([hidden])').all_inner_texts()
        self.assertEqual(shown, ['…', '▷', '×'])

    def test_a_column_that_takes_no_typing_still_has_its_buttons(self):
        page = self.open(TexteditMode=False)
        self.assertEqual(page.locator('.tabulator-cell wa-input').evaluate('(field) => field.readonly'), True)
        self.assertEqual(page.locator('.tabulator-cell wa-input .side-button:not([hidden])').all_inner_texts(), ['…', '×'])

    def test_a_second_click_on_the_cursor_s_cell_opens_it(self):
        page = self.start({'tabs': [tab(selection=0, rows=self.ROWS)]})
        cell = self.row('Bolt').locator('.tabulator-cell', has_text='Bolt')
        cell.click()
        page.wait_for_timeout(400)
        self.assertEqual(page.locator('.tabulator-cell wa-input').count(), 0)
        cell.click()
        page.wait_for_selector('.tabulator-cell wa-input.cell-editor')
        self.assertEqual(self.sent(OPEN), [])

    def test_a_click_in_the_editor_leaves_it_open(self):
        page = self.open()
        page.locator('.tabulator-cell wa-input').click()
        page.wait_for_timeout(250)
        page.locator('.tabulator-cell wa-input').dblclick()
        page.wait_for_timeout(250)
        self.assertEqual(page.locator('.tabulator-cell wa-input').count(), 1)
        self.assertEqual(self.sent(OPEN)[1:], [])

    def test_f2_opens_it_and_enter_gives_the_keys_back(self):
        page = self.start({'tabs': [tab(selection=1, rows=self.ROWS)]})
        self.row('Bolt').click()
        page.keyboard.press('F2')
        page.wait_for_selector('.tabulator-cell wa-input.cell-editor')
        page.wait_for_function("document.activeElement?.localName === 'wa-input'", timeout=3000)
        page.keyboard.press('Escape')
        page.wait_for_function("document.querySelector('.tabulator-cell wa-input') === null")
        page.keyboard.press('ArrowDown')
        page.wait_for_function("document.querySelector('.current-row')?.textContent.includes('Nut')")

    def test_enter_on_a_row_opens_its_editor_and_activates_it(self):
        page = self.start({'tabs': [tab(selection=1, rows=self.ROWS)]})
        self.row('Bolt').click()
        page.keyboard.press('Enter')
        page.wait_for_selector('.tabulator-cell wa-input.cell-editor')
        page.wait_for_function('window.fake.events(7).length === 1')
        self.assertEqual(self.sent(OPEN)[0]['Args'], {'Row': 1})

    def test_a_button_saved_off_is_not_shown(self):
        page = self.open(ButtonClear=False, ButtonSelect=False)
        self.assertEqual(page.locator('.tabulator-cell wa-input .side-button:not([hidden])').count(), 0)

    def test_enter_commits_what_was_typed(self):
        page = self.open()
        page.keyboard.type('Screw')
        page.keyboard.press('Enter')
        page.wait_for_function('window.fake.events(5).length === 1')
        self.assertEqual(self.sent(CHANGE)[0]['Args'], {'Row': 1, 'Column': NAME, 'Text': 'Screw'})
        self.assertEqual(page.locator('.tabulator-cell wa-input').count(), 0)

    def test_escape_lets_it_go(self):
        page = self.open()
        page.keyboard.type('Screw')
        page.keyboard.press('Escape')
        page.wait_for_function("document.querySelector('.tabulator-cell wa-input') === null")
        page.wait_for_timeout(100)
        self.assertEqual(self.sent(CHANGE), [])

    def test_leaving_it_commits(self):
        page = self.open()
        page.keyboard.type('Screw')
        self.row('Nut').locator('.tabulator-cell', has_text='7').click()
        page.wait_for_function('window.fake.events(5).length === 1')

    def test_a_button_lets_the_typing_go_and_asks_the_server(self):
        page = self.open()
        page.keyboard.type('Screw')
        page.locator('.tabulator-cell wa-input .side-button[title="Select"]').click()
        page.wait_for_function('window.fake.events(6).length === 1')
        self.assertEqual(self.sent(SELECT)[0]['Args'], {'Row': 1, 'Column': NAME})
        self.assertEqual(self.sent(CHANGE), [])


if __name__ == '__main__':
    unittest.main()
