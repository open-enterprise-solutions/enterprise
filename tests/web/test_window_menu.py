"""The client's own menus, as the thin client's: File's New and Quit, the tabbed frame's Window — in the person's language,
read from the thin client's dictionary."""
import struct
import unittest

from harness import PageTest, tab

# The frame's commands (ibFrame::ibCommand).
EXIT, CLOSE_ALL = 6, 13


def catalogue(words):
    """A GNU catalogue (.mo) of the pairs, laid out as msgfmt lays it: the head, the two tables, the strings."""
    keys = sorted(words)
    originals = 28
    translations = originals + len(keys) * 8
    strings = translations + len(keys) * 8
    tables, texts = [b'', b''], b''
    for table, side in ((0, keys), (1, [words[key] for key in keys])):
        for text in side:
            encoded = text.encode()
            tables[table] += struct.pack('<II', len(encoded), strings + len(texts))
            texts += encoded + b'\0'
    return struct.pack('<7I', 0x950412de, 0, len(keys), originals, translations, 0, strings) + tables[0] + tables[1] + texts


class WindowMenu(PageTest):
    def tops(self):
        """The menu's titles, left to right."""
        self.page.wait_for_function("document.querySelectorAll('.menu wa-dropdown').length >= 3")
        return [title.strip() for title in self.page.locator('.menu wa-dropdown > wa-button').all_text_contents()]

    def disabled(self, item):
        return item.evaluate("(one) => one.hasAttribute('disabled')")

    def calls(self, method):
        return self.page.evaluate("(method) => window.fake.calls.filter((call) => call.method === method).map((call) => call.params)",
                                  method)

    def called(self, method):
        self.page.wait_for_function("(method) => window.fake.calls.some((call) => call.method === method)", arg=method)
        return self.calls(method)

    def test_window_stands_before_help(self):
        self.start({'tabs': [tab()], 'menu': {'NodeChildren': [
            {'Title': 'Operations', 'NodeChildren': [{'Title': 'Post', 'Command': 100}]},
            {'Title': 'Help', 'NodeChildren': [{'Title': 'About', 'Command': 101}]}]}})
        self.assertEqual(self.tops(), ['File', 'Edit', 'Operations', 'Window', 'Help'])

    def test_window_is_last_where_the_server_has_no_help(self):
        self.start({'tabs': [tab()], 'menu': {'NodeChildren': [
            {'Title': 'Operations', 'NodeChildren': [{'Title': 'Post', 'Command': 100}]}]}})
        self.assertEqual(self.tops(), ['File', 'Edit', 'Operations', 'Window'])

    def test_next_and_previous_go_round_the_strip(self):
        page = self.start({'tabs': [tab('A'), tab('B'), tab('C')]})
        self.menu_item('Window', 'Previous').click()
        self.assertEqual(self.called('activate')[-1]['Tab'], 3, 'the first tab\'s previous is the last')
        page.wait_for_function("document.querySelector('.tab[aria-selected=\"true\"]')?.title === 'C'")
        self.menu_item('Window', 'Next').click()
        page.wait_for_function("window.fake.calls.filter((call) => call.method === 'activate').length === 2")
        self.assertEqual(self.calls('activate')[-1]['Tab'], 1, 'and the last tab\'s next is the first')

    def test_close_all_is_one_call_and_the_locked_tab_stays(self):
        page = self.start({'tabs': [{**tab('Home'), 'locked': True}, tab('B'), tab('C')]})
        self.menu_item('Window', 'Close All').click()
        self.assertEqual([call['Command'] for call in self.called('command')], [CLOSE_ALL])
        page.wait_for_function("document.querySelectorAll('.tab').length === 1")
        self.assertEqual(page.locator('.tab').first.get_attribute('title'), 'Home')

    def test_a_lone_locked_tab_is_neither_closed_nor_left(self):
        self.start({'tabs': [{**tab('Home'), 'locked': True}]})
        self.assertTrue(self.disabled(self.menu_item('Window', 'Close All')))
        for title in ('Close', 'Next', 'Previous'):
            self.assertTrue(self.disabled(self.menu_entry('Window', title)), title)

    def test_quit_lets_the_client_go_and_logs_out(self):
        page = self.start({'tabs': [tab()]})
        self.menu_item('File', 'Quit').click()
        self.assertEqual([call['Command'] for call in self.called('command')], [EXIT])
        page.locator('.ended').wait_for()
        self.assertIn('Exit', page.locator('.ended p').text_content())
        self.called('logout')

    def test_new_with_one_template_makes_it_at_once(self):
        self.start({'tabs': [tab()], 'templates': [{'Title': 'Text', 'Extension': 'txt'},
                                                   {'Title': 'Data tool', 'Extension': 'epf', 'OnlyOpen': True}]})
        self.menu_item('File', 'New').click()
        self.assertEqual([call.get('Extension') for call in self.called('open')], ['txt'])

    def test_new_asks_which_of_several_templates(self):
        page = self.start({'tabs': [tab()], 'templates': [{'Title': 'Text', 'Extension': 'txt'},
                                                          {'Title': 'Spreadsheet', 'Extension': 'mxl'}]})
        self.menu_item('File', 'New').click()
        page.locator('.choice-list button', has_text='Spreadsheet').click()
        self.assertEqual([call.get('Extension') for call in self.called('open')], ['mxl'])

    def test_new_and_open_are_off_with_no_templates(self):
        self.start({'tabs': [tab()]})
        self.assertTrue(self.disabled(self.menu_item('File', 'New')))
        self.assertTrue(self.disabled(self.menu_entry('File', 'Open...')))

    def test_the_menus_speak_the_language_the_login_names(self):
        words = catalogue({'&File': '&Файл', '&Window': '&Вікно', 'Close All': 'Закрити всі'})
        self.start({'tabs': [tab()], 'locale': 'uk_UA'},
                   before=lambda page: page.route('**/lang/uk/open_es.mo', lambda route: route.fulfill(body=words)))
        self.assertEqual(self.tops(), ['Файл', 'Edit', 'Вікно'], 'a word the dictionary does not have stays as written')
        self.menu_item('Вікно', 'Закрити всі').wait_for()

    def test_a_label_loses_its_mnemonic_and_a_format_takes_its_values_in_order(self):
        page = self.start({'tabs': [tab()]})
        said = page.evaluate("""import('/pub/src/localization.js').then(({ labelText, format }) =>
            [labelText('Save &As...'), labelText('R&&D'), format('%s of %d', 'Page 2', 7)])""")
        self.assertEqual(said, ['Save As...', 'R&D', 'Page 2 of 7'])


if __name__ == '__main__':
    unittest.main()
