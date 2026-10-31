"""A dialog moves by its title, as a window on a desktop."""
import unittest

from harness import PageTest, tab

PANEL = 'wa-dialog [part~="dialog"]'
TITLE = 'wa-dialog [part~="title"]'


class Moving(PageTest):
    def ask(self):
        page = self.start({'tabs': [tab()]})
        self.row('Item 1').wait_for()
        page.locator('.command-bar wa-button', has_text='View mode').click()
        page.wait_for_selector('wa-dialog .view-modes')
        page.wait_for_timeout(300)
        return page

    def press_and_drag(self, page, selector, dx, dy):
        at = page.locator(selector).bounding_box()
        x, y = at['x'] + at['width'] / 2, at['y'] + at['height'] / 2
        page.mouse.move(x, y)
        page.mouse.down()
        page.mouse.move(x + dx, y + dy, steps=4)
        page.mouse.up()

    def test_follows_its_title(self):
        page = self.ask()
        before = page.locator(PANEL).bounding_box()
        self.press_and_drag(page, TITLE, -120, 60)
        after = page.locator(PANEL).bounding_box()
        self.assertAlmostEqual(after['x'], before['x'] - 120, delta=1)
        self.assertAlmostEqual(after['y'], before['y'] + 60, delta=1)

    def test_a_second_drag_goes_on_from_where_it_stands(self):
        page = self.ask()
        before = page.locator(PANEL).bounding_box()
        self.press_and_drag(page, TITLE, -50, 0)
        self.press_and_drag(page, TITLE, -50, 20)
        after = page.locator(PANEL).bounding_box()
        self.assertAlmostEqual(after['x'], before['x'] - 100, delta=1)
        self.assertAlmostEqual(after['y'], before['y'] + 20, delta=1)

    def test_stays_inside_the_window(self):
        page = self.ask()
        self.press_and_drag(page, TITLE, -3000, -3000)
        after = page.locator(PANEL).bounding_box()
        self.assertGreaterEqual(after['x'], 0)
        self.assertGreaterEqual(after['y'], 0)
        self.press_and_drag(page, TITLE, 3000, 3000)
        after = page.locator(PANEL).bounding_box()
        self.assertLessEqual(after['x'] + after['width'], 1280)
        self.assertLessEqual(after['y'] + after['height'], 800)

    def test_its_content_does_not_take_it(self):
        page = self.ask()
        before = page.locator(PANEL).bounding_box()
        self.press_and_drag(page, 'wa-dialog .view-modes', 80, 80)
        self.assertEqual(page.locator(PANEL).bounding_box(), before)

    def test_it_still_answers_where_it_was_moved(self):
        page = self.ask()
        self.press_and_drag(page, TITLE, -120, 60)
        page.locator('.view-modes label', has_text='List').click()
        page.locator('wa-dialog wa-button', has_text='OK').click()
        page.wait_for_function("window.fake.calls.some((call) => call.method === 'respond')")


if __name__ == '__main__':
    unittest.main()
