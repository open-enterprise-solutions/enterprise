"""A text box — a text document's view, or a text area on a form — as the thin client's text editor: what is typed goes
back whole as the box is left, Tab indents by the editor's steps, paper takes the whole text; and Find over it."""
import unittest

from harness import PageTest, tab

# The event a text box sends its text back by (ibEvent::ibKind::Change).
CHANGE = 5
BOX = 70


def text_tab(text, **state):
    return {**tab('Text'), 'controls': [{'NodeType': 'Textbox', 'ControlId': BOX, 'State': {'Text': text, **state}}]}


class Textbox(PageTest):
    def editor(self):
        return self.page.locator('oes-textbox textarea')

    def selection(self):
        return self.editor().evaluate('(text) => [text.selectionStart, text.selectionEnd]')

    def test_shows_the_text_the_server_sent(self):
        self.start({'tabs': [text_tab('one\ntwo')]})
        self.assertEqual(self.editor().input_value(), 'one\ntwo')

    def test_what_is_typed_goes_back_whole_as_the_box_is_left(self):
        page = self.start({'tabs': [text_tab('one')]})
        self.editor().fill('one and two')
        page.wait_for_timeout(200)
        self.assertEqual(self.sent(CHANGE), [], 'nothing while it is typed')
        self.editor().evaluate('(text) => text.blur()')
        page.wait_for_function('(kind) => window.fake.events(kind).length === 1', arg=CHANGE)
        self.assertEqual(self.sent(CHANGE)[0]['Args'], {'Text': 'one and two'})

    def test_a_box_left_unchanged_sends_nothing(self):
        page = self.start({'tabs': [text_tab('one')]})
        self.editor().focus()
        self.editor().evaluate('(text) => text.blur()')
        page.wait_for_timeout(200)
        self.assertEqual(self.sent(CHANGE), [])

    def test_tab_puts_spaces_up_to_the_next_stop(self):
        page = self.start({'tabs': [text_tab('ab')]})
        self.editor().focus()
        self.editor().evaluate('(text) => text.setSelectionRange(2, 2)')
        page.keyboard.press('Tab')
        self.assertEqual(self.editor().input_value(), 'ab  ', 'two spaces to the fourth column, not a tab')

    def test_tab_and_shift_tab_move_the_lines_a_selection_spans(self):
        page = self.start({'tabs': [text_tab('one\n\ntwo')]})
        self.editor().focus()
        self.editor().evaluate('(text) => text.setSelectionRange(0, text.value.length)')
        page.keyboard.press('Tab')
        self.assertEqual(self.editor().input_value(), '    one\n\n    two', 'an empty line is not indented')
        page.keyboard.press('Shift+Tab')
        self.assertEqual(self.editor().input_value(), 'one\n\ntwo')

    def test_a_read_only_box_is_read_only(self):
        self.start({'tabs': [text_tab('help', ReadOnly=True)]})
        self.assertTrue(self.editor().evaluate('(text) => text.readOnly'))

    def test_on_paper_the_whole_text_and_after_it_none(self):
        page = self.start({'tabs': [text_tab('a long text')]})
        printout = page.locator('oes-textbox .printout')
        page.evaluate("dispatchEvent(new Event('beforeprint'))")
        self.assertEqual(printout.text_content(), 'a long text')
        page.evaluate("dispatchEvent(new Event('afterprint'))")
        self.assertEqual(printout.text_content(), '')

    def find(self, sought):
        self.menu_item('Edit', 'Find...').click()
        field = self.page.locator('.find-dialog input[type=text]')
        field.fill(sought)
        return field

    def test_find_takes_the_next_one_down_and_says_when_there_is_none(self):
        page = self.start({'tabs': [text_tab('cat dog cat')]})
        field = self.find('cat')
        field.press('Enter')
        self.assertEqual(self.selection(), [0, 3])
        field.press('Enter')
        self.assertEqual(self.selection(), [8, 11])
        field.press('Enter')
        page.get_by_text('"cat" not found!').wait_for()

    def test_find_a_whole_word_skips_one_inside_a_longer(self):
        self.start({'tabs': [text_tab('category cat')]})
        field = self.find('cat')
        self.page.locator('.find-dialog label', has_text='Whole word').locator('input').check()
        field.press('Enter')
        self.assertEqual(self.selection(), [9, 12])


if __name__ == '__main__':
    unittest.main()
