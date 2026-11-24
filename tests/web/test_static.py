"""A static, one control whose View says what it shows: a Text, its caption, its lines broken where the text breaks them;
a Line, one pixel across the width it is given - or down, one pixel wide."""
import unittest

from harness import PageTest, table

# A control's Stretch (wxStretch): it fills its box across.
EXPAND = 0x2000
# What a static shows (ibProtocol::ibForm::ibProperty::ibStaticView).
TEXT, LINE = 0, 1
# A line's orientation (wxHORIZONTAL, wxVERTICAL).
HORIZONTAL, VERTICAL = 4, 8


def placed(control, stretch=EXPAND):
    """A control at its place in the sizer above - its own node says it, as the server writes it."""
    return {**control, 'Proportion': 0, 'Stretch': stretch}


def scenario(*controls):
    return {'tabs': [{'title': 'Panel', 'controls': [placed(control) for control in controls], 'table': table()}]}


BOX = "(element) => { const box = element.getBoundingClientRect(); return [box.width, box.height]; }"


class AStatic(PageTest):
    def test_a_text_shows_its_caption_line_by_line(self):
        page = self.start(scenario({'NodeType': 'Static', 'View': TEXT, 'ControlId': 901,
                                    'State': {'Caption': 'Totals\nper month', 'Visible': True}}))
        page.wait_for_selector('oes-static[view="text"]')
        self.assertEqual(page.locator('oes-static[view="text"]').inner_text(), 'Totals\nper month')

    def test_a_line_runs_across_the_width_it_is_given(self):
        page = self.start(scenario({'NodeType': 'Static', 'View': LINE, 'ControlId': 902, 'Orient': HORIZONTAL,
                                    'State': {'Visible': True}}))
        page.wait_for_selector('oes-static[view="line"]')
        line = page.locator('oes-static[view="line"]')
        self.assertEqual(line.get_attribute('orient'), 'horizontal')
        width, height = line.evaluate(BOX)
        self.assertEqual(height, 1)
        self.assertGreater(width, 100)
        self.assertEqual(line.inner_text(), '')

    def test_a_vertical_line_is_one_pixel_wide(self):
        page = self.start(scenario({'NodeType': 'Static', 'View': LINE, 'ControlId': 903, 'Orient': VERTICAL,
                                    'State': {'Visible': True}}))
        page.wait_for_selector('oes-static[view="line"]')
        line = page.locator('oes-static[view="line"]')
        self.assertEqual(line.get_attribute('orient'), 'vertical')
        width, _ = line.evaluate(BOX)
        self.assertEqual(width, 1)


if __name__ == '__main__':
    unittest.main()
