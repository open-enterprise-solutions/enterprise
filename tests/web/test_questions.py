"""The questions the application asks, drawn as the thin client asks them — what a new object is based on, a file to save
or to send — and one the page cannot ask yet, answered as cancelled."""
import unittest

from harness import PageTest, tab

# The questions (ibRequest::ibKind).
FILE, GENERATION = 5, 6


def generation(*items):
    return {'Id': 50, 'Kind': GENERATION, 'NodeChildren': [{'Id': id, 'Caption': caption} for id, caption in items]}


class Questions(PageTest):
    def answered(self):
        """What each question was answered."""
        self.page.wait_for_function("window.fake.calls.some((call) => call.method === 'respond')")
        return self.page.evaluate(
            "window.fake.calls.filter((call) => call.method === 'respond').map((call) => call.params.Response)")

    def press(self, caption):
        self.page.locator('wa-dialog wa-button', has_text=caption).click()

    def test_generation_is_answered_by_the_one_pressed(self):
        page = self.start({'tabs': [tab()], 'request': generation((11, 'Invoice'), (12, 'Receipt'))})
        page.locator('.choice-list button', has_text='Receipt').click()
        self.assertEqual(self.answered(), [{'Id': 12}])

    def test_generation_ok_takes_the_one_the_keys_stand_on(self):
        page = self.start({'tabs': [tab()], 'request': generation((11, 'Invoice'), (12, 'Receipt'))})
        page.locator('.choice-list button', has_text='Invoice').focus()
        self.press('Ok')
        self.assertEqual(self.answered(), [{'Id': 11}])

    def test_generation_cancelled_names_none(self):
        self.start({'tabs': [tab()], 'request': generation((11, 'Invoice'))})
        self.press('Cancel')
        self.assertEqual(self.answered(), [{}])

    def test_a_question_the_page_cannot_ask_is_answered_as_cancelled(self):
        page = self.start({'tabs': [tab()], 'request': {'Id': 50, 'Kind': 99}})
        page.get_by_text('Not in the browser yet').wait_for()
        self.press('OK')
        self.assertEqual(self.answered(), [{}])

    def test_a_file_of_the_server_s_is_saved_where_the_person_keeps_it(self):
        def no_save_picker(page):
            # The browser's own download, not its save dialog, which a test cannot answer.
            page.add_init_script("Object.defineProperty(window, 'showSaveFilePicker', { value: undefined })")
        page = self.start({'tabs': [tab()], 'request': {'Id': 50, 'Kind': FILE, 'File': 'tmp7'},
                           'files': {'tmp7': {'name': 'report.txt', 'bytes': list(b'figures')}}}, before=no_save_picker)
        page.get_by_text('report.txt').wait_for()
        with page.expect_download() as download:
            self.press('Save')
        self.assertEqual(download.value.suggested_filename, 'report.txt')
        with open(download.value.path(), 'rb') as saved:
            self.assertEqual(saved.read(), b'figures')
        self.assertEqual(self.answered(), [{'Done': True}])

    def test_a_file_of_the_person_s_goes_up_and_is_answered_by_its_id(self):
        page = self.start({'tabs': [tab()], 'request': {'Id': 50, 'Kind': FILE, 'NodeChildren': [
            {'Title': 'Text', 'Mask': '*.txt'}]}})
        page.get_by_text('Text (*.txt)').wait_for()
        self.assertEqual(page.locator('wa-dialog .file-choice input').get_attribute('accept'), '.txt')
        page.locator('wa-dialog .file-choice input').set_input_files(
            {'name': 'notes.txt', 'mimeType': 'text/plain', 'buffer': b'hello'})
        self.press('OK')
        self.assertEqual(self.answered(), [{'File': 'upload1'}])
        sent = page.evaluate("""window.fake.calls.filter((call) => call.method === 'upload')
            .map((call) => [call.params.Name, Array.from(call.params.Data)])""")
        self.assertEqual(sent, [['notes.txt', list(b'hello')]])


class Files(PageTest):
    def test_file_open_sends_the_file_and_opens_it_there(self):
        page = self.start({'tabs': [tab()], 'templates': [{'Title': 'Text', 'Extension': 'txt', 'Mask': '*.txt'}]})
        with page.expect_file_chooser() as chooser:
            self.menu_item('File', 'Open...').click()
        chooser.value.set_files({'name': 'notes.txt', 'mimeType': 'text/plain', 'buffer': b'hello'})
        page.wait_for_function("window.fake.calls.some((call) => call.method === 'open')")
        opened = page.evaluate("window.fake.calls.filter((call) => call.method === 'open').map((call) => call.params.File)")
        self.assertEqual(opened, ['upload1'])

    def test_masks_become_what_the_browser_s_file_input_takes(self):
        page = self.start({'tabs': [tab()]})
        accepted = page.evaluate("""import('/pub/src/window/files.js').then(({ acceptOf }) =>
            [acceptOf(['*.xlsx;*.xls', '*.txt']), acceptOf(['*.txt', '*.*'])])""")
        self.assertEqual(accepted, ['.xlsx,.xls,.txt', ''])


if __name__ == '__main__':
    unittest.main()
