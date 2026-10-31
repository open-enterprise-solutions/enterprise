"""A button as a tile - the picture large over the caption and a note under them - and a grid whose cells are one
size: what a panel for a finger is made of."""
import struct
import unittest
import zlib

from harness import PageTest, table

# What a control shows (ibState::ibRepresentation).
BOTH, TILE = 3, 4
# An item's Stretch (wxStretch): it fills its box across.
EXPAND = 0x2000
PRESS = 3


def png():
    """One PNG of one grey pixel, made here: the page is handed bytes, not a file."""
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff)
    header = struct.pack('>IIBBBBB', 1, 1, 8, 0, 0, 0, 0)
    return list(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header) + chunk(b'IDAT', zlib.compress(b'\x00\x80')) + chunk(b'IEND', b''))


def item(item_id, control):
    return {'NodeType': 'SizerItem', 'ControlId': item_id, 'Proportion': 0, 'Stretch': EXPAND, 'NodeChildren': [control]}


def button(button_id, caption, representation, note=None, picture=None, members=None):
    state = {'Caption': caption, 'Representation': representation, 'Visible': True, 'Enabled': True}
    if members is not None:
        state['Members'] = {'NodeChildren': [{'Caption': name, 'Member': place} for place, name in enumerate(members)]}
    if note is not None:
        state['Note'] = note
    if picture is not None:
        state['Picture'] = picture
    return {'NodeType': 'Button', 'ControlId': button_id, 'State': state}


def scenario(**extra):
    grid = {'NodeType': 'Gridsizer', 'ControlId': 701, 'Cols': 2, 'NodeChildren': [
        item(702, button(710, 'Tea', TILE, note='12 pcs', picture='7')),
        item(703, button(711, 'Coffee of a long name that wraps', TILE, picture='7')),
        item(704, button(712, 'Plain', BOTH, picture='7')),
        item(705, button(713, 'Note elsewhere', BOTH, note='not shown')),
    ]}
    return {'tabs': [{'title': 'Panel', 'controls': [item(700, grid)], 'table': table()}], 'pictures': [{'id': 7, 'png': png()}],
            **extra}


# The height of a button's own face - the part inside the element, which is what fills the tile.
FACE_HEIGHT = "(button) => button.shadowRoot.querySelector('[part~=base]').getBoundingClientRect().height"


class ATile(PageTest):
    def test_stacks_the_picture_the_caption_and_the_note_and_takes_no_small_size(self):
        page = self.start(scenario())
        tiles = page.locator('oes-button.tile')
        self.assertEqual(tiles.count(), 2)
        tea = tiles.first
        self.assertEqual(tea.locator('.tile-caption').inner_text(), 'Tea')
        self.assertEqual(tea.locator('.tile-note').inner_text(), '12 pcs')
        self.assertEqual(tea.locator('img').count(), 1)
        self.assertNotEqual(tea.locator('wa-button').get_attribute('size'), 's')   # the button's own default, not the bar's small
        # The second tile has no note, and no empty line for one.
        self.assertEqual(tiles.nth(1).locator('.tile-note').count(), 0)
        # A plain button is what it was: small, its picture before its text - and a note it was given is not shown.
        plain = page.locator('oes-button:not(.tile)')
        self.assertEqual(plain.count(), 2)
        self.assertEqual(plain.first.locator('wa-button').get_attribute('size'), 's')
        self.assertEqual(plain.first.locator('img[slot=start]').count(), 1)
        self.assertEqual(page.locator('oes-button:not(.tile) .tile-note').count(), 0)
        self.assertNotIn('not shown', page.locator('oes-button').nth(3).inner_text())

    def test_draws_its_picture_large_and_a_plain_buttons_small(self):
        page = self.start(scenario())
        page.wait_for_selector('oes-button.tile img')
        tile_width = page.locator('oes-button.tile img').first.evaluate('(img) => img.getBoundingClientRect().width')
        plain_width = page.locator('oes-button:not(.tile) img').first.evaluate('(img) => img.getBoundingClientRect().width')
        self.assertEqual(plain_width, 16)
        self.assertGreaterEqual(tile_width, 32)

    def test_is_pressed_as_a_button_is(self):
        page = self.start(scenario())
        page.locator('oes-button.tile wa-button').first.click()
        page.wait_for_function('window.fake.events(3).length === 1')
        self.assertEqual(self.sent(PRESS)[0]['Control'], 710)

    def test_shows_the_note_the_server_changes_it_to(self):
        # The press is answered with the same button, its note alone changed - as a handler counting stock would.
        page = self.start(scenario(onPress={'710': {'Note': '11 pcs'}}))
        page.locator('oes-button.tile wa-button').first.click()
        page.wait_for_function("document.querySelector('oes-button.tile .tile-note')?.textContent === '11 pcs'")


class AGrid(PageTest):
    def test_gives_every_cell_the_same_size(self):
        page = self.start(scenario())
        page.wait_for_selector('oes-gridsizer')
        columns = page.locator('oes-gridsizer').evaluate('(grid) => getComputedStyle(grid).gridTemplateColumns')
        widths = [float(width[:-2]) for width in columns.split()]
        self.assertEqual(len(widths), 2)
        self.assertLess(abs(widths[0] - widths[1]), 1, columns)
        # Every cell the same height too: the row of plain buttons as tall as the row of tiles.
        cells = page.locator('oes-gridsizer > oes-sizeritem').evaluate_all('(cells) => cells.map((cell) => cell.getBoundingClientRect().height)')
        self.assertEqual(len(cells), 4)
        self.assertLess(max(cells) - min(cells), 1, cells)
        self.assertGreater(cells[0], 40)
        # ...and no lower than what it holds: a row grows rather than a cell spilling into the next one.
        self.assertEqual(page.locator('oes-gridsizer > oes-sizeritem').first.evaluate('(cell) => getComputedStyle(cell).minHeight'), 'auto')
        # The tiles fill their cells, face and all: the one with the short caption is no shorter than its neighbour.
        faces = page.locator('oes-button.tile wa-button').evaluate_all(f'(buttons) => buttons.map({FACE_HEIGHT})')
        self.assertLess(abs(faces[0] - faces[1]), 1, faces)
        self.assertLess(abs(faces[0] - cells[0]), 2, (faces, cells))


class AGroupTile(PageTest):
    """A tile bound to a group of commands: it fills its cell as any tile, and opens the group's commands."""

    def scenario(self):
        grid = {'NodeType': 'Gridsizer', 'ControlId': 701, 'Cols': 2, 'NodeChildren': [
            item(702, button(710, 'Drinks', TILE, note='3 kinds', picture='7', members=['Tea', 'Coffee', 'Juice'])),
            item(703, button(711, 'Tea', TILE, picture='7')),
        ]}
        return {'tabs': [{'title': 'Panel', 'controls': [item(700, grid)], 'table': table()}], 'pictures': [{'id': 7, 'png': png()}]}

    def test_fills_its_cell_as_a_plain_tile_does(self):
        page = self.start(self.scenario())
        page.wait_for_selector('oes-button.tile wa-dropdown')
        cells = page.locator('oes-gridsizer > oes-sizeritem').evaluate_all('(cells) => cells.map((cell) => cell.getBoundingClientRect().height)')
        faces = page.locator('oes-button.tile wa-button').evaluate_all(f'(buttons) => buttons.map({FACE_HEIGHT})')
        widths = page.locator('oes-button.tile wa-button').evaluate_all("(buttons) => buttons.map((button) => button.shadowRoot.querySelector('[part~=base]').getBoundingClientRect().width)")
        self.assertLess(abs(faces[0] - faces[1]), 1, faces)
        self.assertLess(abs(faces[0] - cells[0]), 2, (faces, cells))
        self.assertLess(abs(widths[0] - widths[1]), 1, widths)
        group = page.locator('oes-button.tile').first
        self.assertEqual(group.locator('.tile-note').inner_text(), '3 kinds')

    def test_opens_its_commands_and_sends_the_one_picked(self):
        page = self.start(self.scenario())
        page.locator('oes-button.tile wa-dropdown wa-button').first.click()
        page.locator('wa-dropdown-item', has_text='Coffee').click()
        page.wait_for_function('window.fake.events(3).length === 1')
        press = self.sent(PRESS)[0]
        self.assertEqual(press['Control'], 710)
        self.assertEqual(press['Args']['Member'], 1)


if __name__ == '__main__':
    unittest.main()
