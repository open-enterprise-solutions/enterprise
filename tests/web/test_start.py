"""The start: the splash with its stages, the login when there is one, then the window with the logo beside the name."""
import unittest

from harness import PageTest, tab


class Splash(PageTest):
    def test_is_on_the_screen_while_a_slow_stylesheet_is_on_its_way(self):
        self.serve_late('webawesome.css', 2)
        page = self.start({'tabs': [tab()]}, wait=False)
        page.wait_for_function("performance.getEntriesByName('first-contentful-paint').length > 0", timeout=1500)
        self.assertEqual(page.evaluate("[...document.styleSheets].some((sheet) => sheet.href?.endsWith('webawesome.css'))"), False,
                         'the slow stylesheet had come: this tells nothing')
        self.assertTrue(page.locator('#splash img').is_visible())
        self.assertEqual(page.locator('#splash .arrows i').count(), 3)

    def test_says_the_stages_of_the_start(self):
        page = self.start({'tabs': [tab()], 'connectLatency': 300, 'loginLatency': 300, 'fetchLatency': 300}, wait=False)
        for stage in ('Connecting to the base', 'Logging in', 'Opening the window'):
            page.wait_for_function('(stage) => document.querySelector("#splash .stage").textContent === stage', arg=stage,
                                   timeout=5000)

    def test_goes_once_the_form_has_its_rows(self):
        page = self.start({'tabs': [tab()], 'fetchLatency': 300}, wait=False)
        page.wait_for_selector('.client-window', state='attached', timeout=15000)
        page.wait_for_function("document.getElementById('splash').hidden", timeout=5000)
        self.assertGreater(page.locator('.tabulator-row').count(), 0, 'the splash went before the rows came')

    def test_has_the_desktop_picture(self):
        page = self.start({}, wait=False)
        page.wait_for_function("document.querySelector('#splash img').naturalWidth > 0")
        self.assertEqual(page.evaluate("document.querySelector('#splash img').naturalWidth"), 434)


class Login(PageTest):
    def test_a_base_with_no_users_asks_nothing(self):
        page = self.start({'tabs': [tab()]})
        self.assertEqual(page.locator('form.login').count(), 0)

    def test_a_base_with_users_asks_and_the_splash_comes_back(self):
        page = self.start({'users': ['Admin'], 'password': 'secret', 'tabs': [tab()], 'loginLatency': 200}, wait=False)
        page.wait_for_selector('form.login', timeout=15000)
        page.wait_for_function("document.getElementById('splash').hidden", timeout=5000)
        page.locator('form.login wa-input input').first.fill('secret')
        page.click('form.login wa-button[type=submit]')
        page.wait_for_function("document.querySelector('#splash .stage').textContent === 'Opening the window' "
                               "&& !document.getElementById('splash').hidden", timeout=5000)
        page.wait_for_selector('.client-window', timeout=5000)
        page.wait_for_function("document.getElementById('splash').hidden", timeout=5000)

    def test_a_wrong_password_is_said_in_the_form(self):
        page = self.start({'users': ['Admin'], 'password': 'secret'}, wait=False)
        page.wait_for_selector('form.login', timeout=15000)
        page.locator('form.login wa-input input').first.fill('nope')
        page.click('form.login wa-button[type=submit]')
        page.wait_for_function("document.querySelector('form.login .refusal').textContent.includes('wrong')", timeout=5000)
        self.assertEqual(page.locator('.client-window').count(), 0)


class Header(PageTest):
    def test_the_logo_stands_beside_the_name_of_the_configuration(self):
        page = self.start({'tabs': [tab()], 'title': 'Trade'})
        page.wait_for_function("document.querySelector('header .logo').naturalWidth > 0")
        self.assertEqual(page.locator('header h1').inner_text(), 'Trade')
        logo = page.locator('header .logo').bounding_box()
        name = page.locator('header h1').bounding_box()
        self.assertLessEqual(logo['x'] + logo['width'], name['x'])
        self.assertLess(name['x'] - (logo['x'] + logo['width']), 16)


if __name__ == '__main__':
    unittest.main()
