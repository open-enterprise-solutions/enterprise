"""The toolbar under the menu, as the thin client's: New, Open, Save, Save as | Find | Redo, Undo — the engine's stock
pictures by their ids, each button done and enabled as its File or Edit item; the items carry the same pictures."""
import unittest

from harness import PageTest, tab
from test_button import png

# The commands a tab's document takes (ibFrame::ibCommand).
SAVE = 3

# The engine's stock pictures (core/picturePredefined.h), as the wire says them.
NEW, SAVE_PICTURE, FIND, UNDO_PICTURE = '645455583498007629', '639324940303605204', '623288159307697294', '647948470173557467'


def form(**extra):
    return {'title': 'Document', 'fields': [{'id': 50, 'caption': 'Number', 'text': '1'}], **tab('Document'), **extra}


class Toolbar(PageTest):
    def buttons(self):
        self.page.wait_for_function("document.querySelectorAll('.toolbar wa-button').length === 7")
        return self.page.locator('.toolbar wa-button')

    def test_the_buttons_stand_as_the_thin_clients(self):
        self.start({'tabs': [form()]})
        titles = self.buttons().evaluate_all("(all) => all.map((one) => one.title)")
        self.assertEqual(titles, ['New', 'Open', 'Save', 'Save as', 'Find', 'Redo', 'Undo'])
        shape = self.page.evaluate(
            "[...document.querySelector('.toolbar').children].map((one) => one.classList.contains('separator') ? '|' : 'b')")
        self.assertEqual(shape, ['b', 'b', 'b', 'b', '|', 'b', '|', 'b', 'b'])

    def test_the_pictures_are_asked_for_by_their_ids_and_drawn(self):
        self.start({'tabs': [form()], 'pictures': [{'id': one, 'png': png()} for one in (NEW, SAVE_PICTURE, FIND)]})
        self.buttons()
        asked = self.page.evaluate(
            "window.fake.calls.filter((call) => call.method === 'picture').flatMap((call) => call.params.Pictures)")
        for one in (NEW, SAVE_PICTURE, FIND, UNDO_PICTURE):
            self.assertIn(one, asked)
        self.page.wait_for_function("document.querySelector('.toolbar wa-button[title=\"Save\"] img')?.src.startsWith('blob:')")

    def test_save_is_enabled_and_done_as_files_save(self):
        self.start({'tabs': [form(commands=[SAVE])]})
        self.row('Item 1').wait_for()
        save = self.page.locator('.toolbar wa-button[title="Save"]')
        self.page.wait_for_function("!document.querySelector('.toolbar wa-button[title=\"Save\"]').hasAttribute('disabled')")
        save.click()
        self.page.wait_for_function("window.fake.calls.some((call) => call.method === 'command')")
        self.assertEqual(self.page.evaluate(
            "window.fake.calls.filter((call) => call.method === 'command').map((call) => call.params.Command)"), [SAVE])

    def test_save_is_off_where_the_document_has_nothing_to_save(self):
        self.start({'tabs': [form()]})
        self.row('Item 1').wait_for()
        self.assertTrue(self.page.locator('.toolbar wa-button[title="Save"]').evaluate("(one) => one.hasAttribute('disabled')"))

    def test_the_menu_items_carry_the_same_pictures(self):
        self.start({'tabs': [form()], 'pictures': [{'id': SAVE_PICTURE, 'png': png()}]})
        self.row('Item 1').wait_for()
        item = self.menu_item('File', 'Save')
        self.assertTrue(item.evaluate("(one) => one.querySelector('img[slot=\"icon\"]')?.src.startsWith('blob:') ?? false"))


if __name__ == '__main__':
    unittest.main()
