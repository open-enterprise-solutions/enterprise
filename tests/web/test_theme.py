"""The theme - light, dark, or as the machine is set: the client's own, kept in this browser and applied as the page
starts; chosen anew from Tools → Theme..., which asks the server nothing (it keeps nothing of it)."""
import unittest

from harness import PageTest, tab, table

STYLE = 20
SYSTEM, LIGHT, DARK = 0, 1, 2
MENU = {'NodeChildren': [{'Title': 'Tools', 'NodeChildren': [{'Title': 'Theme...', 'Schema': STYLE, 'Enabled': True}]}]}


def kept(theme, scheme=None):
    """The page's start with a theme kept in the browser before it - and the machine set to a scheme, if one is named."""
    def before(page):
        page.add_init_script(f"localStorage.setItem('theme', '{theme}');")
        if scheme is not None:
            page.emulate_media(color_scheme=scheme)
    return before


def scenario():
    return {'tabs': [tab()], 'menu': MENU}


class TheTheme(PageTest):
    def painted(self, page):
        return page.evaluate("""() => ({ theme: document.documentElement.dataset.theme,
            dark: document.documentElement.classList.contains('wa-dark'),
            window: getComputedStyle(document.documentElement).getPropertyValue('--oes-window-bg').trim() })""")

    def schema_calls(self, page):
        return [call for call in page.evaluate('window.fake.calls') if call['method'] == 'schema']

    def choose(self, page, caption, button='OK'):
        page.locator('.menu wa-button', has_text='Tools').click()
        page.locator('wa-dropdown-item', has_text='Theme').click()
        page.wait_for_selector('wa-dialog.theme .themes')
        page.locator('.themes label', has_text=caption).click()
        page.locator('wa-dialog.theme wa-button', has_text=button).click()

    def test_is_the_kept_dark(self):
        page = self.start(scenario(), before=kept(DARK))
        self.assertEqual(self.painted(page), {'theme': 'dark', 'dark': True, 'window': '#1b1d21'})

    def test_is_the_kept_light_on_a_dark_machine(self):
        page = self.start(scenario(), before=kept(LIGHT, 'dark'))
        self.assertEqual(self.painted(page)['theme'], 'light')
        self.assertFalse(self.painted(page)['dark'])
        self.assertEqual(self.painted(page)['window'], '#f4f5f7')

    def test_none_kept_is_the_machines(self):
        page = self.start(scenario(), before=lambda page: page.emulate_media(color_scheme='dark'))
        self.assertEqual(self.painted(page)['theme'], 'dark')

    def test_the_systems_follows_the_machine_as_it_changes(self):
        page = self.start(scenario(), before=kept(SYSTEM, 'dark'))
        self.assertEqual(self.painted(page)['theme'], 'dark')
        page.emulate_media(color_scheme='light')
        page.wait_for_function("document.documentElement.dataset.theme === 'light'")

    def test_the_server_is_asked_nothing_of_it(self):
        page = self.start(scenario(), before=kept(DARK))
        self.choose(page, 'Light')
        page.wait_for_function("document.documentElement.dataset.theme === 'light'")
        self.assertEqual(self.schema_calls(page), [])

    def test_is_chosen_from_tools_kept_and_painted_at_once(self):
        page = self.start(scenario(), before=kept(LIGHT))
        page.locator('.menu wa-button', has_text='Tools').click()
        page.locator('wa-dropdown-item', has_text='Theme').click()
        page.wait_for_selector('wa-dialog.theme .themes')
        self.assertEqual(page.locator('.themes input:checked').get_attribute('value'), str(LIGHT))
        page.locator('.themes label', has_text='Dark').click()
        page.locator('wa-dialog.theme wa-button', has_text='OK').click()
        page.wait_for_function("document.documentElement.dataset.theme === 'dark'")
        self.assertEqual(page.evaluate("localStorage.getItem('theme')"), str(DARK))

    def test_cancel_keeps_nothing(self):
        page = self.start(scenario(), before=kept(LIGHT))
        self.choose(page, 'Dark', 'Cancel')
        page.wait_for_timeout(200)
        self.assertEqual(self.painted(page)['theme'], 'light')
        self.assertEqual(page.evaluate("localStorage.getItem('theme')"), str(LIGHT))

    def test_a_printout_is_light(self):
        page = self.start(scenario(), before=kept(DARK))
        page.emulate_media(media='print')
        page.wait_for_function("!document.documentElement.classList.contains('wa-dark')")
        self.assertEqual(self.painted(page)['window'], '#f4f5f7')
        self.assertTrue(page.evaluate("document.documentElement.classList.contains('wa-light')"))
        page.emulate_media(media='screen')
        page.wait_for_function("document.documentElement.classList.contains('wa-dark')")
        self.assertEqual(self.painted(page)['window'], '#1b1d21')

    def test_a_tables_text_is_light_in_the_dark(self):
        page = self.start(scenario(), before=kept(DARK))
        page.wait_for_selector('oes-table .tabulator-cell')
        look = page.evaluate("""() => {
            const cell = document.querySelector('oes-table .tabulator-row .tabulator-cell');
            const holder = document.querySelector('oes-table .tabulator-tableholder .tabulator-table');
            return [getComputedStyle(cell).color, getComputedStyle(holder).backgroundColor]; }""")
        self.assertEqual(look, ['rgb(228, 230, 234)', 'rgb(36, 39, 44)'])

    def test_the_splash_is_the_kept_one(self):
        page = self.start(scenario(), wait=False, before=kept(DARK))
        page.wait_for_function("document.documentElement.dataset.theme === 'dark'")


class AControlsOwnLook(PageTest):
    """The platform's own look of a control - a colour the form leaves empty, its author picked none - is painted with
    the theme's; a colour the author chose is theirs in the light theme, and in the dark carried there."""

    def look(self, theme):
        # Empty - the platform's: a field's own fill is white in the light theme.
        fields = [{'id': 21, 'caption': 'Platform', 'node': {'BackgroundColour': '', 'ForegroundColour': ''}},
                  {'id': 22, 'caption': 'Chosen fill', 'node': {'BackgroundColour': '200,30,30', 'ForegroundColour': ''}},
                  {'id': 23, 'caption': 'Chosen ink', 'node': {'BackgroundColour': '', 'ForegroundColour': '0,0,128'}}]
        page = self.start({'tabs': [{'title': 'List', 'fields': fields, 'table': table()}]}, before=kept(theme))
        return page.evaluate("""() => [...document.querySelectorAll('oes-field')].map((field) => {
            const look = getComputedStyle(field.lookTarget ?? field); return [look.color, look.backgroundColor]; })""")

    def test_the_platforms_is_dark_in_the_dark(self):
        platform, fill, ink = self.look(DARK)
        self.assertEqual(platform, ['rgb(201, 214, 226)', 'rgb(36, 39, 44)'])
        # One side the author's: the other is the light theme's, and the pair is carried into the dark whole - each
        # side's lightness turned over, its hue kept (the thin client's ibStyle::Get): the red fill a light red under
        # the light ink carried, the navy ink a pale blue on the field's white carried.
        self.assertEqual(fill, ['rgb(184, 216, 248)', 'rgb(255, 151, 139)'])
        self.assertEqual(ink, ['rgb(218, 230, 255)', 'rgb(26, 26, 26)'])

    def test_the_field_itself_is_the_themes(self):
        # What a person types into is Web Awesome's own part, painted by its class - the form's colours stand on the
        # label and behind it, as they did before the theme.
        for theme, wanted in ((DARK, 'rgb(16, 18, 25)'), (LIGHT, 'rgb(255, 255, 255)')):
            page = self.start({'tabs': [{'title': 'List', 'fields': [{'id': 21, 'caption': 'Platform'}], 'table': table()}]},
                              before=kept(theme))
            self.assertEqual(page.evaluate("""() => getComputedStyle(document.querySelector('oes-field').lookTarget
                .shadowRoot.querySelector('[part~="base"]')).backgroundColor"""), wanted)
            page.close()

    def test_the_platforms_is_itself_in_the_light(self):
        platform, fill, ink = self.look(LIGHT)
        self.assertEqual(platform, ['rgb(63, 92, 119)', 'rgb(255, 255, 255)'])
        self.assertEqual(fill, ['rgb(63, 92, 119)', 'rgb(200, 30, 30)'])
        self.assertEqual(ink, ['rgb(0, 0, 128)', 'rgb(255, 255, 255)'])

    def table_fill(self, theme):
        # A table's own fill, left empty, is white in the light theme: its command bar stands on it.
        page = self.start({'tabs': [tab(node={'BackgroundColour': '', 'ForegroundColour': ''})]}, before=kept(theme))
        return page.evaluate("() => getComputedStyle(document.querySelector('oes-table')).backgroundColor")

    def test_a_tables_own_is_the_themes_table_in_the_dark(self):
        self.assertEqual(self.table_fill(DARK), 'rgb(36, 39, 44)')

    def test_a_tables_own_is_itself_in_the_light(self):
        self.assertEqual(self.table_fill(LIGHT), 'rgb(255, 255, 255)')


class ACellsOwnColour(PageTest):
    """A table's cell paints the colours its row names as the author chose them in the light theme, and in the dark
    carried there - the lightness turned over, the hue kept (control.js themed). One named alone stays alone: a fill
    takes the theme's ink, ink the row's own fill - current, hovered, alternate."""

    def paint(self, theme):
        page = self.start(scenario(), before=kept(theme))
        return page.evaluate("""async () => {
            const { recordOf } = await import('/pub/src/table/rows.js');
            const filled = recordOf({ NodeId: 1, NodeChildren: [{ NodeId: 7, Text: 'x', BackgroundColour: '255,255,0' }] });
            const inked = recordOf({ NodeId: 1, NodeChildren: [{ NodeId: 7, Text: 'x', TextColour: '200,0,0' }] });
            const painted = (colour) => {
                const probe = document.body.appendChild(document.createElement('i'));
                probe.style.color = colour;
                const rgb = getComputedStyle(probe).color;
                probe.remove();
                return rgb; };
            return [painted(filled.s7.backgroundColor), filled.s7.color ?? null,
                    painted(inked.s7.color), inked.s7.backgroundColor ?? null]; }""")

    def test_is_the_authors_in_the_light(self):
        self.assertEqual(self.paint(LIGHT), ['rgb(255, 255, 0)', None, 'rgb(200, 0, 0)', None])

    def test_is_carried_into_the_dark(self):
        # The yellow fill a dark olive under the theme's light ink, the red ink a light red.
        self.assertEqual(self.paint(DARK), ['rgb(37, 37, 0)', None, 'rgb(255, 160, 145)', None])


if __name__ == '__main__':
    unittest.main()
