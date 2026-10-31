"""The theme - light, dark, or as the machine is set: the person's, read as the window comes up, and chosen anew from
Tools → Theme..."""
import unittest

from harness import PageTest, tab, table

STYLE = 20
SAVE = 4
SYSTEM, LIGHT, DARK = 0, 1, 2
MENU = {'NodeChildren': [{'Title': 'Tools', 'NodeChildren': [{'Title': 'Theme...', 'Schema': STYLE}]}]}


def scenario(theme, offered=True):
    return {'tabs': [tab()], 'menu': MENU, 'schemas': {str(STYLE): {'Theme': theme}},
            **({'schemaKinds': [STYLE]} if offered else {})}


class TheTheme(PageTest):
    def painted(self, page):
        return page.evaluate("""() => ({ theme: document.documentElement.dataset.theme,
            dark: document.documentElement.classList.contains('wa-dark'),
            window: getComputedStyle(document.documentElement).getPropertyValue('--oes-window-bg').trim() })""")

    def test_is_the_persons_dark(self):
        page = self.start(scenario(DARK))
        self.assertEqual(self.painted(page), {'theme': 'dark', 'dark': True, 'window': '#1b1d21'})

    def test_is_the_persons_light_on_a_dark_machine(self):
        page = self.start(scenario(LIGHT), before=lambda page: page.emulate_media(color_scheme='dark'))
        self.assertEqual(self.painted(page)['theme'], 'light')
        self.assertFalse(self.painted(page)['dark'])
        self.assertEqual(self.painted(page)['window'], '#f4f5f7')

    def test_the_systems_follows_the_machine_as_it_changes(self):
        page = self.start(scenario(SYSTEM), before=lambda page: page.emulate_media(color_scheme='dark'))
        self.assertEqual(self.painted(page)['theme'], 'dark')
        page.emulate_media(color_scheme='light')
        page.wait_for_function("document.documentElement.dataset.theme === 'light'")

    def test_a_server_that_offers_no_style_leaves_the_machines(self):
        page = self.start(scenario(DARK, offered=False), before=lambda page: page.emulate_media(color_scheme='light'))
        self.assertEqual(self.painted(page)['theme'], 'light')
        self.assertEqual([call for call in page.evaluate('window.fake.calls') if call['method'] == 'schema'], [])

    def test_is_chosen_from_tools_kept_and_painted_at_once(self):
        page = self.start(scenario(LIGHT))
        page.locator('.menu wa-button', has_text='Tools').click()
        page.locator('wa-dropdown-item', has_text='Theme').click()
        page.wait_for_selector('wa-dialog.theme .themes')
        self.assertEqual(page.locator('.themes input:checked').get_attribute('value'), str(LIGHT))
        page.locator('.themes label', has_text='Dark').click()
        page.locator('wa-dialog.theme wa-button', has_text='OK').click()
        page.wait_for_function("document.documentElement.dataset.theme === 'dark'")
        saved = [call['params'] for call in page.evaluate('window.fake.calls')
                 if call['method'] == 'schema' and call['params'].get('Command') == SAVE]
        self.assertEqual(len(saved), 1)
        self.assertEqual(saved[0]['Schema'], STYLE)
        self.assertEqual(saved[0]['Args'], {'Theme': DARK})

    def test_a_refused_save_is_said_and_paints_nothing(self):
        page = self.start({**scenario(LIGHT), 'schemaRefusal': 'The theme could not be kept'})
        page.locator('.menu wa-button', has_text='Tools').click()
        page.locator('wa-dropdown-item', has_text='Theme').click()
        page.wait_for_selector('wa-dialog.theme .themes')
        page.locator('.themes label', has_text='Dark').click()
        page.locator('wa-dialog.theme wa-button', has_text='OK').click()
        page.get_by_text('The theme could not be kept').wait_for()
        self.assertEqual(self.painted(page)['theme'], 'light')
        self.assertEqual(page.locator('wa-dialog.theme').count(), 0)

    def test_escape_while_the_save_is_on_its_way_waits_for_its_answer(self):
        page = self.start({**scenario(LIGHT), 'schemaRefusal': 'The theme could not be kept', 'commandLatency': 1000})
        page.locator('.menu wa-button', has_text='Tools').click()
        page.locator('wa-dropdown-item', has_text='Theme').click()
        page.wait_for_selector('wa-dialog.theme .themes')
        page.locator('.themes label', has_text='Dark').click()
        page.locator('wa-dialog.theme wa-button', has_text='OK').click()
        page.keyboard.press('Escape')
        page.wait_for_timeout(300)
        self.assertTrue(page.evaluate("document.querySelector('wa-dialog.theme')?.open === true"))
        page.get_by_text('The theme could not be kept').wait_for()
        self.assertEqual(self.painted(page)['theme'], 'light')

    def test_a_printout_is_light(self):
        page = self.start(scenario(DARK))
        page.emulate_media(media='print')
        page.wait_for_function("!document.documentElement.classList.contains('wa-dark')")
        self.assertEqual(self.painted(page)['window'], '#f4f5f7')
        self.assertTrue(page.evaluate("document.documentElement.classList.contains('wa-light')"))
        page.emulate_media(media='screen')
        page.wait_for_function("document.documentElement.classList.contains('wa-dark')")
        self.assertEqual(self.painted(page)['window'], '#1b1d21')

    def test_a_tables_text_is_light_in_the_dark(self):
        page = self.start(scenario(DARK))
        page.wait_for_selector('oes-tablebox .tabulator-cell')
        look = page.evaluate("""() => {
            const cell = document.querySelector('oes-tablebox .tabulator-row .tabulator-cell');
            const holder = document.querySelector('oes-tablebox .tabulator-tableholder .tabulator-table');
            return [getComputedStyle(cell).color, getComputedStyle(holder).backgroundColor]; }""")
        self.assertEqual(look, ['rgb(228, 230, 234)', 'rgb(36, 39, 44)'])

    def test_the_splash_is_the_machines_before_the_script(self):
        page = self.start(scenario(SYSTEM), before=lambda page: page.emulate_media(color_scheme='dark'))
        # The inline script in the head has said it before any module ran - the page's own source carries it.
        self.assertIn("matchMedia('(prefers-color-scheme: dark)')", page.evaluate("document.head.innerHTML"))
        self.assertEqual(self.painted(page)['theme'], 'dark')

    def test_ok_on_the_kept_one_paints_it(self):
        # The page shows another than the one kept - a read at the start that failed: OK on the kept one paints it.
        page = self.start(scenario(LIGHT))
        page.evaluate("async () => (await import('/pub/src/window/theme.js')).applyTheme(2)")
        self.assertEqual(self.painted(page)['theme'], 'dark')
        page.locator('.menu wa-button', has_text='Tools').click()
        page.locator('wa-dropdown-item', has_text='Theme').click()
        page.wait_for_selector('wa-dialog.theme .themes')
        page.locator('wa-dialog.theme wa-button', has_text='OK').click()
        page.wait_for_function("document.documentElement.dataset.theme === 'light'")
        self.assertFalse(any(call['params'].get('Command') == SAVE for call in page.evaluate('window.fake.calls')
                             if call['method'] == 'schema'))

    def test_cancel_keeps_nothing(self):
        page = self.start(scenario(LIGHT))
        page.locator('.menu wa-button', has_text='Tools').click()
        page.locator('wa-dropdown-item', has_text='Theme').click()
        page.wait_for_selector('wa-dialog.theme .themes')
        page.locator('.themes label', has_text='Dark').click()
        page.locator('wa-dialog.theme wa-button', has_text='Cancel').click()
        page.wait_for_timeout(200)
        self.assertEqual(self.painted(page)['theme'], 'light')
        self.assertFalse(any(call['params'].get('Command') == SAVE for call in page.evaluate('window.fake.calls')
                             if call['method'] == 'schema'))


class AControlsOwnLook(PageTest):
    """The platform's own look of a control - what a form saves when its author picks no colour - is painted with the
    theme's; a colour the author chose stays theirs."""

    def look(self, theme):
        fields = [{'id': 21, 'caption': 'Platform', 'node': {'BackgroundColour': '250,247,240', 'ForegroundColour': '63,92,119'}},
                  {'id': 22, 'caption': 'Chosen fill', 'node': {'BackgroundColour': '200,30,30', 'ForegroundColour': '63,92,119'}},
                  {'id': 23, 'caption': 'Chosen ink', 'node': {'BackgroundColour': '250,247,240', 'ForegroundColour': '0,0,128'}},
                  {'id': 24, 'caption': 'Fill alone', 'node': {'BackgroundColour': '255,255,0'}}]
        page = self.start({'tabs': [{'title': 'List', 'fields': fields, 'table': table()}], 'schemas': {str(STYLE): {'Theme': theme}}, 'schemaKinds': [STYLE]})
        return page.evaluate("""() => [...document.querySelectorAll('oes-textctrl')].map((field) => {
            const look = getComputedStyle(field.lookTarget ?? field); return [look.color, look.backgroundColor]; })""")

    def test_the_platforms_is_dark_in_the_dark(self):
        platform, fill, ink, alone = self.look(DARK)
        self.assertEqual(platform, ['rgb(201, 214, 226)', 'rgb(34, 38, 43)'])
        # One side the author's: the other is the light theme's, so the pair reads as they chose it.
        self.assertEqual(fill, ['rgb(63, 92, 119)', 'rgb(200, 30, 30)'])
        self.assertEqual(ink, ['rgb(0, 0, 128)', 'rgb(250, 247, 240)'])
        self.assertEqual(alone, ['rgb(34, 34, 34)', 'rgb(255, 255, 0)'])

    def test_the_field_itself_is_the_themes(self):
        # What a person types into is Web Awesome's own part, painted by its class - the form's colours stand on the
        # label and behind it, as they did before the theme.
        for theme, wanted in ((DARK, 'rgb(16, 18, 25)'), (LIGHT, 'rgb(255, 255, 255)')):
            page = self.start({'tabs': [{'title': 'List', 'fields': [{'id': 21, 'caption': 'Platform'}], 'table': table()}],
                               'schemas': {str(STYLE): {'Theme': theme}}, 'schemaKinds': [STYLE]})
            self.assertEqual(page.evaluate("""() => getComputedStyle(document.querySelector('oes-textctrl').lookTarget
                .shadowRoot.querySelector('[part~="base"]')).backgroundColor"""), wanted)
            page.close()

    def test_the_platforms_is_itself_in_the_light(self):
        platform, fill, ink, alone = self.look(LIGHT)
        self.assertEqual(platform, ['rgb(63, 92, 119)', 'rgb(250, 247, 240)'])
        self.assertEqual(fill, ['rgb(63, 92, 119)', 'rgb(200, 30, 30)'])
        self.assertEqual(ink, ['rgb(0, 0, 128)', 'rgb(250, 247, 240)'])


class ACellsOwnColour(PageTest):
    """A sheet's or a table's cell that names one colour gets the light theme's other side, in any theme."""

    def test_one_side_named_is_paired_with_the_paper(self):
        page = self.start(scenario(DARK))
        pairs = page.evaluate("""async () => {
            const { onFill } = await import('/pub/src/controls/control.js');
            const { recordOf } = await import('/pub/src/tablebox/rows.js');
            const row = recordOf({ NodeId: 1, NodeChildren: [{ NodeId: 7, Text: 'x', BackgroundColour: '255,255,0' }] });
            const inked = recordOf({ NodeId: 1, NodeChildren: [{ NodeId: 7, Text: 'x', TextColour: '200,0,0' }] });
            return [onFill('', 'rgb(255,255,0)'), onFill('rgb(0,0,128)', ''), onFill('', ''),
                    onFill('rgb(1,2,3)', 'rgb(4,5,6)'), [row.s7.color, row.s7.backgroundColor],
                    [inked.s7.color, inked.s7.backgroundColor ?? null]]; }""")
        self.assertEqual(pairs[4], ['var(--oes-paper-text)', 'rgb(255,255,0)'])
        # Ink alone stays alone: the row's own fill - current, hovered, alternate - shows under it.
        self.assertEqual(pairs[5], ['rgb(200,0,0)', None])
        self.assertEqual(pairs[:4], [['var(--oes-paper-text)', 'rgb(255,255,0)'], ['rgb(0,0,128)', ''],
                                 ['', ''], ['rgb(1,2,3)', 'rgb(4,5,6)']])
        self.assertEqual(page.evaluate("getComputedStyle(document.documentElement).getPropertyValue('--oes-paper-text').trim()"),
                         '#222222')


if __name__ == '__main__':
    unittest.main()
