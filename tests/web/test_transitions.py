"""A form comes whole: nothing on the screen is drawn in steps, and what did not change is not drawn again."""
import unittest

from harness import PageTest, row, tab, table

# Written down on every frame the browser paints: the rows of the form on show, how many forms are on show, the row marked.
WATCH = """() => {
  window.samples = [];
  const sample = () => {
    const forms = [...document.querySelectorAll('.views > oes-clientform')].filter((form) =>
      !form.hidden && getComputedStyle(form).visibility !== 'hidden');
    window.samples.push({
      forms: forms.length,
      rows: forms[0]?.querySelectorAll('.tabulator-tableholder .tabulator-row').length ?? 0,
      marked: forms[0]?.querySelectorAll('.current-row').length ?? 0,
      title: forms[0]?.dataset.control,
    });
    window.sampling = requestAnimationFrame(sample);
  };
  sample();
}"""


class FormArrives(PageTest):
    def test_a_new_form_is_never_on_show_without_its_rows(self):
        opened = {'title': 'Second', 'table': table(rows=[row(n, f'Other {n}') for n in range(1, 4)])}
        page = self.start({'tabs': [tab('First')], 'fetchLatency': 200})
        page.evaluate(WATCH)
        page.evaluate('(tab) => window.fake.open(tab)', opened)
        self.wait_rows(3)
        page.wait_for_timeout(100)
        samples = page.evaluate('window.samples')
        self.assertTrue(samples, 'nothing was sampled')
        self.assertEqual([one for one in samples if one['forms'] == 1 and one['rows'] == 0], [], 'an empty table was on show')
        self.assertEqual([one for one in samples if one['forms'] != 1], [], 'not exactly one form was on show')
        self.assertEqual({one['title'] for one in samples}, {'100', '200'}, 'the first stays, then the second')

    def test_a_new_form_is_never_on_show_before_its_fields_can_be_seen(self):
        # A component is loaded the first time it is met: the fields of the first form with any came after the form.
        self.serve_late('components/input/input.js', 0.3)
        page = self.start({'tabs': [tab('First')]})
        page.evaluate("""() => {
          window.bare = 0;
          const look = () => {
            for (const form of document.querySelectorAll('.views > oes-clientform:not([hidden])')) {
              const field = form.querySelector('wa-input');
              if (field !== null && getComputedStyle(form).visibility !== 'hidden' && !field.shadowRoot?.querySelector('input'))
                window.bare++;
            }
            requestAnimationFrame(look);
          };
          look();
        }""")
        opened = {'title': 'Second', 'fields': [{'id': 50, 'caption': 'Number', 'text': '1'}], 'table': table()}
        page.evaluate('(tab) => window.fake.open(tab)', opened)
        page.wait_for_function("document.querySelector('.views > oes-clientform:not([hidden]) wa-input')?.shadowRoot?.querySelector('input')")
        page.wait_for_timeout(100)
        self.assertEqual(page.evaluate('window.bare'), 0, 'a form was on show with its fields not drawn')

    def test_a_form_whose_rows_do_not_come_still_comes(self):
        opened = {'title': 'Second', 'table': table(rows=[row(1, 'Slow')])}
        page = self.start({'tabs': [tab('First')]})
        page.evaluate('window.fake.fetchLatency = 3000')
        page.evaluate('(tab) => window.fake.open(tab)', opened)
        page.wait_for_function("document.querySelector('.views > oes-clientform:last-of-type:not(.arriving)')", timeout=2000)

    def test_a_tab_gone_back_to_comes_at_once(self):
        page = self.start({'tabs': [tab('First')]})
        page.evaluate("(tab) => window.fake.open(tab)", {'title': 'Second', 'table': table()})
        self.wait_rows(5)
        page.wait_for_function("document.querySelectorAll('.tab').length === 2")
        page.evaluate('window.fake.fetchLatency = 3000')
        page.locator('.tab', has_text='First').click()
        page.wait_for_function("document.querySelector('.views > oes-clientform:not([hidden])').dataset.control === '100'",
                               timeout=500)


class WhatDidNotChange(PageTest):
    def test_the_tabs_are_not_drawn_anew_by_a_frame(self):
        page = self.start({'tabs': [tab('First')]})
        page.evaluate("window.firstTab = document.querySelector('.tab')")
        page.evaluate('window.fake.push()')
        page.wait_for_timeout(100)
        self.assertTrue(page.evaluate("window.firstTab === document.querySelector('.tab') && window.firstTab.isConnected"))

    def test_a_new_tab_leaves_the_buttons_of_the_others(self):
        page = self.start({'tabs': [tab('First')]})
        page.evaluate("window.firstTab = document.querySelector('.tab')")
        page.evaluate("(tab) => window.fake.open(tab)", {'title': 'Second', 'table': table()})
        page.wait_for_function("document.querySelectorAll('.tab').length === 2")
        self.assertTrue(page.evaluate("window.firstTab === document.querySelector('.tab')"))
        self.assertEqual(page.locator('.tab[aria-selected="true"]').inner_text().replace('×', '').strip(), 'Second')

    def test_rows_read_anew_keep_the_cursor_on_every_frame(self):
        page = self.start({'tabs': [tab('First', currentRow=2)]})
        self.row('Item 2').wait_for()
        page.wait_for_function("document.querySelectorAll('.current-row').length === 1")
        page.evaluate(WATCH)
        page.evaluate("""() => { const table = window.fake.tab().table; table.version = 2; table.rows[0].cells['2'] = 'Changed'; window.fake.push(); }""")
        page.wait_for_function("document.querySelector('.tabulator-row').textContent.includes('Changed')", timeout=3000)
        page.wait_for_timeout(100)
        samples = page.evaluate('window.samples')
        self.assertEqual([one for one in samples if one['marked'] != 1], [], 'a frame was painted with no row under the cursor')
        self.assertEqual([one for one in samples if one['rows'] == 0], [], 'a frame was painted with no rows')


if __name__ == '__main__':
    unittest.main()
