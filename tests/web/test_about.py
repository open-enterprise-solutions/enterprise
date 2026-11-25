"""The window About: the program beside the logo, the session's facts the server answers, what it is built on."""
import unittest

from harness import PageTest, tab

ABOUT = 4
ANSWER = {'Version': '2.0.0', 'Build': 1234, 'Database': 'file base', 'Program': 'File base', 'User': 'Admin',
          'Locale': 'en_US'}
MENU = {'NodeChildren': [{'Title': 'Help', 'NodeChildren': [{'Title': 'About', 'Schema': ABOUT}]}]}


class About(PageTest):
    def show(self, answer=None):
        page = self.start({'tabs': [tab()], 'menu': MENU, 'schemas': {str(ABOUT): answer if answer is not None else ANSWER}})
        page.locator('.menu wa-button', has_text='Help').click()
        page.locator('wa-dropdown-item', has_text='About').click()
        page.wait_for_selector('wa-dialog.about .program')
        return page

    def facts(self, page):
        """The facts as label, value, label, value… - the system's value is the browser's, so only its label."""
        return page.locator('.about dl').evaluate('(one) => [...one.children].map((part) => part.textContent)')

    def test_says_the_program_its_version_and_build(self):
        page = self.show()
        self.assertEqual(page.locator('.about .program').inner_text(), 'OES Application Workspace')
        self.assertEqual(page.locator('.about .version').inner_text(), 'Version 2.0.0, build 1234')

    def test_says_what_the_session_runs_on(self):
        facts = self.facts(self.show())
        self.assertEqual(facts[:8], ['Database', 'file base', 'Connection', 'File base', 'User', 'Admin',
                                     'Language', 'en_US'])
        self.assertEqual(facts[8], 'System')

    def test_no_base_no_line_for_it(self):
        self.assertNotIn('Database', self.facts(self.show({**ANSWER, 'Database': ''})))

    def test_an_empty_fact_is_a_dash(self):
        facts = self.facts(self.show({**ANSWER, 'User': ''}))
        self.assertEqual(facts[facts.index('User') + 1], '—')

    def test_the_plugins_only_if_any(self):
        page = self.show()
        self.assertNotIn('Plugins', self.facts(page))
        page.context.close()
        self.context = None
        facts = self.facts(self.show({**ANSWER, 'Plugins': {'NodeChildren': [{'Name': 'Sample', 'Version': '1.2'}]}}))
        self.assertEqual(facts[-2:], ['Plugins', 'Sample 1.2'])

    def test_says_what_it_is_built_on(self):
        credits = self.show().locator('.about .credits').inner_text()
        self.assertIn('Built on', credits)
        self.assertIn('PolyForm Noncommercial 1.0.0', credits)

    def test_ok_closes_it(self):
        page = self.show()
        page.locator('.about wa-button', has_text='OK').click()
        page.wait_for_selector('wa-dialog.about', state='detached')


if __name__ == '__main__':
    unittest.main()
