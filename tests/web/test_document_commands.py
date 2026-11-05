"""A tab's document takes its commands: the client's own File and Edit, and their keys, as its tab's Commands say."""
import unittest

from harness import PageTest, tab

# The commands a tab's document takes (ibFrame::ibCommand).
UNDO, REDO, SAVE = 1, 2, 3


def form(**extra):
    return {'title': 'Document', 'fields': [{'id': 50, 'caption': 'Number', 'text': '1'}], **tab('Document'), **extra}


class DocumentCommands(PageTest):
    def commands(self):
        return self.page.evaluate(
            "window.fake.calls.filter((call) => call.method === 'command').map((call) => call.params.Command)")

    def sent(self):
        self.page.wait_for_function("window.fake.calls.some((call) => call.method === 'command')")
        return self.commands()

    def stays(self):
        """Nothing went to the server, given the time an answer takes."""
        self.page.wait_for_timeout(200)
        self.assertEqual(self.commands(), [])

    def item(self, menu, title):
        self.page.locator('.menu wa-button', has_text=menu).click()
        item = self.page.locator('wa-dropdown-item', has_text=title)
        item.wait_for()
        return item

    def disabled(self, item):
        return item.evaluate("(one) => one.hasAttribute('disabled')")

    def test_edit_is_enabled_by_the_tab_commands(self):
        self.start({'tabs': [form(commands=[UNDO])]})
        self.row('Item 1').wait_for()
        undo = self.item('Edit', 'Undo')
        self.assertFalse(self.disabled(undo))
        self.assertTrue(self.disabled(self.page.locator('wa-dropdown-item', has_text='Redo')))
        undo.click()
        self.assertEqual(self.sent(), [UNDO])

    def test_save_is_not_offered_where_the_document_has_nothing_to_save(self):
        self.start({'tabs': [form()]})
        self.row('Item 1').wait_for()
        self.assertTrue(self.disabled(self.item('File', 'Save')))

    def test_ctrl_s_saves_the_document(self):
        page = self.start({'tabs': [form(commands=[SAVE])]})
        self.row('Item 1').wait_for()
        page.keyboard.press('Control+s')
        self.assertEqual(self.sent(), [SAVE])

    def test_text_typed_reaches_the_server_before_the_save(self):
        page = self.start({'tabs': [form(commands=[SAVE])]})
        self.row('Item 1').wait_for()
        page.locator('oes-textctrl wa-input input').fill('42')
        page.keyboard.press('Control+s')
        self.sent()
        methods = page.evaluate('window.fake.calls.map((call) => call.method)')
        self.assertIn('event', methods[:methods.index('command')])

    def test_a_key_the_document_does_not_take_sends_nothing(self):
        page = self.start({'tabs': [form()]})
        self.row('Item 1').wait_for()
        page.keyboard.press('Control+z')
        self.stays()

    def test_undo_in_a_text_field_is_the_fields_own(self):
        page = self.start({'tabs': [form(commands=[UNDO])]})
        self.row('Item 1').wait_for()
        page.locator('oes-textctrl wa-input input').fill('42')
        page.keyboard.press('Control+z')
        self.stays()

    def test_file_close_closes_the_active_tab(self):
        page = self.start({'tabs': [form()]})
        self.row('Item 1').wait_for()
        self.item('File', 'Close').click()
        page.wait_for_function("document.querySelectorAll('.tab').length === 0")

    def test_a_locked_tab_is_not_closed_from_the_menu(self):
        self.start({'tabs': [form(locked=True)]})
        self.row('Item 1').wait_for()
        self.assertTrue(self.disabled(self.item('File', 'Close')))


if __name__ == '__main__':
    unittest.main()
