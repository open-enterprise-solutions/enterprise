"""The window About: what the server answers, in the thin client's blocks."""
import unittest

from harness import PageTest, tab

ABOUT = 4
ANSWER = {'Build': 1234, 'Database': 'file base', 'Application': 'Thin client', 'User': 'Admin', 'Locale': 'en_US'}
MENU = {'NodeChildren': [{'Title': 'Help', 'NodeChildren': [{'Title': 'About', 'Schema': ABOUT}]}]}


class About(PageTest):
    def show(self, answer=None):
        page = self.start({'tabs': [tab()], 'menu': MENU, 'schemas': {str(ABOUT): answer if answer is not None else ANSWER}})
        page.locator('.menu wa-button', has_text='Help').click()
        page.locator('wa-dropdown-item', has_text='About').click()
        page.wait_for_selector('wa-dialog.about .platform')
        return page

    def info(self, page):
        return page.locator('.about dl').evaluate('(one) => [...one.children].map((part) => part.textContent)')

    def test_says_the_build_and_what_the_session_runs_on(self):
        page = self.show()
        self.assertIn('build 1234', page.locator('.about .platform').inner_text())
        self.assertEqual(self.info(page), ['Database:', 'file base', 'Application:', 'Thin client', 'User:', 'Admin', 'Locale:', 'en_US'])

    def test_no_base_no_line_for_it(self):
        page = self.show({**ANSWER, 'Database': ''})
        self.assertNotIn('Database:', self.info(page))

    def test_the_plugins_only_if_any(self):
        page = self.show()
        self.assertEqual(page.locator('.about legend').all_inner_texts(), ['Info', 'Thanks'])
        page.context.close()
        self.context = None
        page = self.show({**ANSWER, 'Plugins': {'NodeChildren': [{'Name': 'Sample', 'Version': '1.2'}]}})
        self.assertEqual(page.locator('.about legend').all_inner_texts(), ['Info', 'Plugins', 'Thanks'])
        self.assertIn('Sample  1.2', page.locator('.about fieldset', has_text='Plugins').evaluate('(one) => one.textContent'))

    def test_ok_closes_it(self):
        page = self.show()
        page.locator('.about wa-button', has_text='OK').click()
        page.wait_for_selector('wa-dialog.about', state='detached')


if __name__ == '__main__':
    unittest.main()
