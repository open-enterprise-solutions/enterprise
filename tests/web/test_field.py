"""The field — one control, drawn with the widget its View is (docs/private/form-field.md): typed into, on or off, shown,
one of its items picked, how far a number has come, a number moved between two ends. A new View draws the field anew;
what was being typed is let go with the old one."""
import unittest

from harness import PageTest, tab

# What a field is for (ibProtocol::ibForm::ibProperty::ibFieldView) and the events it sends (ibEvent::ibKind).
INPUT, FLAG, LABEL, SWITCH, GAUGE, SLIDER = 0, 1, 2, 3, 4, 5
CHANGE, TOGGLE = 5, 9
FIELD = 80


def field(view, control=FIELD, **state):
    return {'NodeType': 'Field', 'ControlId': control, 'View': view, 'TitleLocation': 1,
            'State': {'Caption': 'Counterparty', **state}}


def items(*captions, selected=None):
    """A switch's State.Choices: an item each, its Caption, Selected on the chosen one."""
    return {'NodeChildren': [{'Caption': caption, **({'Selected': True} if place == selected else {})}
                             for place, caption in enumerate(captions)]}


def field_tab(*controls):
    return {**tab('Form'), 'controls': list(controls)}


class Field(PageTest):
    def at(self, control, inside=''):
        return self.page.locator(f'oes-field[data-control="{control}"] {inside}'.strip())

    def switch_view(self, view, **state):
        """The server's field turned into another view — its module set View — and the page told."""
        self.page.evaluate("""([view, state]) => { const control = window.fake.tab().controls[0];
            control.View = view; control.State = {Caption: 'Counterparty', ...state}; window.fake.push(); }""", [view, state])

    def test_each_view_draws_its_widget(self):
        self.start({'tabs': [field_tab(field(INPUT, 81, Text='Acme'), field(FLAG, 82, Checked=True),
                                       field(LABEL, 83, Text='Paid'),
                                       field(SWITCH, 84, Choices=items('Cash', 'Card', selected=1)))]})
        self.assertEqual(self.at(81, 'wa-input input').input_value(), 'Acme')
        self.assertTrue(self.at(82, 'wa-checkbox').evaluate('(box) => box.checked'))
        self.assertEqual(self.at(83, '.text').text_content(), 'Paid')
        self.assertEqual(self.at(84, 'wa-radio').count(), 2)
        self.assertEqual(self.at(84, 'wa-radio-group').evaluate('(group) => group.value'), '1')
        for control in (81, 82, 83, 84):
            self.assertEqual(self.at(control, '.caption').text_content(), 'Counterparty', 'the caption is the field\'s')

    def test_a_new_view_builds_the_field_anew(self):
        self.start({'tabs': [field_tab(field(INPUT, Text='Acme'))]})
        self.at(FIELD, 'wa-input').wait_for()
        self.switch_view(FLAG, Checked=True)
        self.at(FIELD, 'wa-checkbox').wait_for()
        self.assertEqual(self.at(FIELD, 'wa-input').count(), 0, 'the input went with its view')

    def test_a_switch_sends_change_with_the_items_place(self):
        page = self.start({'tabs': [field_tab(field(SWITCH, Choices=items('Cash', 'Card', 'Credit')))]})
        self.at(FIELD, 'wa-radio').nth(2).click()
        page.wait_for_function('(kind) => window.fake.events(kind).length === 1', arg=CHANGE)
        self.assertEqual(self.sent(CHANGE)[0]['Args'], {'Member': 2})

    def test_a_flag_sends_toggle(self):
        page = self.start({'tabs': [field_tab(field(FLAG, Checked=False))]})
        self.at(FIELD, 'wa-checkbox').click()
        page.wait_for_function('(kind) => window.fake.events(kind).length === 1', arg=TOGGLE)
        self.assertEqual(self.sent(TOGGLE)[0]['Args'], {'Checked': True})

    def test_a_gauge_fills_to_its_share_of_the_range(self):
        self.start({'tabs': [field_tab({**field(GAUGE, Position=30), 'Range': 60})]})
        fill = self.at(FIELD, '.gauge .fill')
        fill.wait_for()
        self.assertEqual(fill.evaluate("(fill) => fill.style.getPropertyValue('--oes-gauge-share')"), '50%')

    def test_a_slider_sends_change_where_it_is_let_go(self):
        page = self.start({'tabs': [field_tab({**field(SLIDER, Position=10), 'MinValue': 0, 'MaxValue': 40})]})
        slider = self.at(FIELD, 'wa-slider')
        slider.wait_for()
        self.assertEqual(slider.evaluate('(slider) => slider.value'), 10)
        # The slider's own focus(), as Tab gives it: Playwright's locator.focus() lands outside it, and the key goes
        # nowhere.
        slider.evaluate('(slider) => slider.focus()')
        page.keyboard.press('End')
        page.wait_for_function('(kind) => window.fake.events(kind).length === 1', arg=CHANGE)
        self.assertEqual(self.sent(CHANGE)[0]['Args'], {'Position': 40})

    def test_an_input_keeps_its_mask(self):
        page = self.start({'tabs': [field_tab(field(INPUT, Text='', Mask='999'))]})
        typed = self.at(FIELD, 'wa-input input')
        typed.focus()
        page.keyboard.type('12a3')
        self.assertEqual(typed.input_value(), '123', 'a letter where a digit stands is not taken')

    def test_view_change_while_typing_sends_nothing(self):
        page = self.start({'tabs': [field_tab(field(INPUT, Text='Acme'))]})
        typed = self.at(FIELD, 'wa-input input')
        typed.focus()
        page.keyboard.press('End')
        page.keyboard.type(' Ltd')
        self.assertEqual(typed.input_value(), 'Acme Ltd')
        self.switch_view(LABEL, Text='Acme')
        self.at(FIELD, '.text').wait_for()
        page.wait_for_timeout(200)
        self.assertEqual(self.sent(CHANGE), [], 'what was typed went with the input, uncommitted')


if __name__ == '__main__':
    unittest.main()
