"""Escape closes what stands on top: an editor, a menu, a dialog, and with none of them up the active tab."""
import unittest

from harness import PageTest, row, tab

MENU = {'NodeChildren': [{'Title': 'File', 'NodeChildren': [{'Title': 'Save', 'Command': 1}]}]}
DIALOG = 'wa-dialog[open]'


def form(title='Document', **extra):
    return {'title': title, 'fields': [{'id': 50, 'caption': 'Number', 'text': '1'}], **tab(title), **extra}


class Escape(PageTest):
    def tabs(self):
        return self.page.locator('.tab').count()

    def closes(self):
        return self.page.evaluate("window.fake.calls.filter((call) => call.method === 'close').length")

    def wait_for_tabs(self, count):
        self.page.wait_for_function('(count) => document.querySelectorAll(".tab").length === count', arg=count)

    def stays(self):
        """Nothing went to the server to close, given the time an answer takes."""
        self.page.wait_for_timeout(200)
        self.assertEqual(self.closes(), 0)

    def test_closes_the_active_tab(self):
        page = self.start({'tabs': [tab('One'), tab('Two')]})
        self.row('Item 1').wait_for()
        page.keyboard.press('Escape')
        self.wait_for_tabs(1)
        self.assertIn('Two', page.locator('.tab').inner_text())

    def test_each_press_closes_one(self):
        page = self.start({'tabs': [tab('One'), tab('Two'), tab('Three')]})
        self.row('Item 1').wait_for()
        page.keyboard.press('Escape')
        self.wait_for_tabs(2)
        page.keyboard.press('Escape')
        self.wait_for_tabs(1)

    def test_a_key_held_closes_one(self):
        page = self.start({'tabs': [tab('One'), tab('Two'), tab('Three')]})
        self.row('Item 1').wait_for()
        page.keyboard.down('Escape')
        page.keyboard.down('Escape')
        page.keyboard.down('Escape')
        page.keyboard.up('Escape')
        self.wait_for_tabs(2)
        page.wait_for_timeout(200)
        self.assertEqual(self.closes(), 1)

    def test_a_press_while_the_server_answers_closes_nothing_more(self):
        page = self.start({'tabs': [tab('One'), tab('Two'), tab('Three')], 'latency': 100})
        self.row('Item 1').wait_for()
        page.keyboard.press('Escape')
        page.keyboard.press('Escape')
        self.wait_for_tabs(2)
        page.wait_for_timeout(300)
        self.assertEqual(self.closes(), 1)

    def test_with_a_modifier_it_is_not_taken(self):
        page = self.start({'tabs': [tab()]})
        self.row('Item 1').wait_for()
        page.keyboard.press('Shift+Escape')
        self.stays()

    def test_a_locked_tab_stays(self):
        page = self.start({'tabs': [{**tab('Start'), 'locked': True}]})
        self.row('Item 1').wait_for()
        page.keyboard.press('Escape')
        self.stays()

    def test_the_table_of_the_tab_left_takes_the_keys(self):
        page = self.start({'tabs': [tab('One'), tab('Two')]})
        self.row('Item 1').wait_for()
        page.locator('.tab', has_text='Two').click()
        page.wait_for_function("window.fake.active === 2 && document.querySelector('.tab[aria-selected=\"true\"]')?.textContent.includes('Two')")
        page.wait_for_timeout(300)
        page.keyboard.press('Escape')
        self.wait_for_tabs(1)
        page.wait_for_function("document.activeElement?.closest('oes-tablebox') !== null")

    def test_a_dialog_goes_and_the_tab_stays(self):
        page = self.start({'tabs': [tab()]})
        self.row('Item 1').wait_for()
        page.locator('.command-bar wa-button', has_text='View mode').click()
        page.wait_for_selector('wa-dialog .view-modes')
        page.keyboard.press('Escape')
        page.wait_for_function("window.fake.calls.some((call) => call.method === 'respond')")
        page.wait_for_selector(DIALOG, state='detached')
        self.stays()
        page.keyboard.press('Escape')
        self.wait_for_tabs(0)

    def test_a_window_menu_goes_and_the_tab_stays(self):
        page = self.start({'tabs': [tab()], 'menu': MENU})
        self.row('Item 1').wait_for()
        page.locator('.menu wa-button', has_text='File').click()
        item = page.locator('wa-dropdown-item', has_text='Save')
        item.wait_for()
        page.keyboard.press('Escape')
        item.wait_for(state='hidden')
        self.stays()

    def test_a_table_menu_goes_and_the_tab_stays(self):
        page = self.start({'tabs': [tab(contextMenu=[{'Id': 1, 'Caption': 'Open'}])]})
        self.row('Item 1').click(button='right')
        page.wait_for_selector('.popup-menu button')
        page.keyboard.press('Escape')
        page.wait_for_selector('.popup-menu', state='detached')
        self.stays()

    def test_a_cell_editor_goes_and_the_tab_stays(self):
        page = self.start({'tabs': [tab(rows=[row(1, 'Item 1', read_only=False)])]})
        self.row('Item 1').locator('.tabulator-cell').first.dblclick()
        page.wait_for_selector('wa-input.cell-editor')
        page.wait_for_function("document.activeElement?.localName === 'wa-input'")
        page.keyboard.press('Escape')
        page.wait_for_selector('wa-input.cell-editor', state='detached')
        self.stays()

    def test_text_typed_reaches_the_server_before_the_close(self):
        page = self.start({'tabs': [form()]})
        self.row('Item 1').wait_for()
        page.locator('oes-textctrl wa-input input').fill('42')
        page.keyboard.press('Escape')
        self.wait_for_tabs(0)
        methods = page.evaluate('window.fake.calls.map((call) => call.method)')
        self.assertIn('event', methods[:methods.index('close')])

    def test_what_is_unsaved_is_asked_about(self):
        page = self.start({'tabs': [form(unsaved=True)]})
        self.row('Item 1').wait_for()
        page.keyboard.press('Escape')
        page.wait_for_selector('wa-dialog wa-button:has-text("Cancel")')
        page.wait_for_timeout(300)
        # Escape on the question is its Cancel: the form stays, as it was.
        page.keyboard.press('Escape')
        page.wait_for_function(
            "window.fake.calls.some((call) => call.method === 'respond' && call.params.Response?.Button === 16)")
        self.assertEqual(self.tabs(), 1)
        page.wait_for_selector(DIALOG, state='detached')
        page.keyboard.press('Escape')
        page.locator('wa-dialog wa-button', has_text='No').click()
        self.wait_for_tabs(0)

    def test_the_table_takes_the_keys_after_the_question(self):
        page = self.start({'tabs': [tab('One'), form('Two', unsaved=True)]})
        self.row('Item 1').wait_for()
        page.locator('.tab', has_text='Two').click()
        page.wait_for_function("document.querySelector('.tab[aria-selected=\"true\"]')?.textContent.includes('Two')")
        page.wait_for_timeout(300)
        page.keyboard.press('Escape')
        page.locator('wa-dialog wa-button', has_text='No').click()
        self.wait_for_tabs(1)
        page.wait_for_selector(DIALOG, state='detached')
        page.wait_for_function("document.activeElement?.closest('oes-tablebox') !== null")


if __name__ == '__main__':
    unittest.main()
