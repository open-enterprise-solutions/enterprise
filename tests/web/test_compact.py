"""A form is as dense as the thin client's on a desktop; a finger keeps its touch targets."""
import unittest

from harness import PageTest, tab, table

FORM = {'tabs': [{'title': 'Document', 'fields': [{'id': 50, 'caption': 'Number', 'text': '1'}], 'table': table()}]}


def heights(page):
    return page.evaluate("""() => Object.fromEntries(Object.entries({
        field: 'oes-textctrl wa-input', row: '.tabulator-tableholder .tabulator-row', header: '.tabulator-header',
        tool: 'oes-tablebox .command-bar wa-button', bar: 'oes-tablebox .command-bar', tab: '.tab', top: '.client-window > header',
      }).map(([name, selector]) => [name, Math.round(document.querySelector(selector).getBoundingClientRect().height)]))""")


class Desktop(PageTest):
    def test_the_controls_take_a_desktop_form_s_height(self):
        page = self.start(FORM)
        self.row('Item 1').wait_for()
        size = heights(page)
        limits = {'field': 26, 'row': 27, 'header': 22, 'tool': 28, 'bar': 34, 'tab': 26, 'top': 28}
        self.assertEqual({name: size[name] for name in limits if size[name] > limits[name]}, {}, size)

    def test_a_field_is_not_cut(self):
        page = self.start(FORM)
        page.wait_for_function("document.querySelector('oes-textctrl wa-input').value === '1'")
        fits = page.evaluate("""() => {
            const input = document.querySelector('oes-textctrl wa-input').shadowRoot.querySelector('input');
            return input.scrollHeight <= input.clientHeight + 1 && input.clientHeight >= 14; }""")
        self.assertTrue(fits)


class Touch(PageTest):
    def test_a_finger_keeps_its_targets(self):
        page = self.start(FORM, touch=True)
        self.row('Item 1').wait_for()
        size = heights(page)
        self.assertGreaterEqual(size['row'], 44)
        self.assertGreaterEqual(size['tool'], 44)
        self.assertGreaterEqual(size['tab'], 44)


if __name__ == '__main__':
    unittest.main()
