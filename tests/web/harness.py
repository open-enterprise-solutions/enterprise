"""The browser client's tests: the real page, its real modules, in a real browser, the server a fake in the page
(fakeServer.js). No build and no Node: Python with Playwright (pip install playwright, playwright install chromium).

    python -m unittest discover -s tests/web -v

A browser already on the machine instead of Playwright's own: OES_WEB_BROWSER=msedge.
"""
import functools
import http.server
import json
import os
import threading
import time
import unittest
from pathlib import Path

from playwright.sync_api import sync_playwright

HERE = Path(__file__).parent
WWW = HERE.parent.parent / 'src' / 'engine' / 'webserver' / 'www'

# The columns of a plain table: Name (2), Stock (3).
NAME = 2
STOCK = 3


def column(column_id, caption, **extra):
    state = {'Caption': caption, 'Visible': True, 'ReadOnly': extra.pop('read_only', False), 'Sortable': False}
    state.update(extra.pop('state', {}))
    return {'NodeType': 'TableboxColumn', 'ControlId': column_id, 'Width': 80, 'Visible': True, 'ButtonSelect': True,
            'ButtonOpen': False, 'ButtonClear': True, 'State': state, **extra}


def group(group_id, caption, columns, grouping, show_title=False):
    return {'NodeType': 'TableboxColumnGroup', 'ControlId': group_id, 'Grouping': grouping, 'ShowTitle': show_title,
            'State': {'Caption': caption, 'Visible': True}, 'NodeChildren': columns}


def row(row_id, name, stock='', parent=0, container=False, read_only=True, caption=''):
    return {'id': row_id, 'parent': parent, 'container': container, 'readOnly': read_only, 'group': caption,
            'cells': {str(NAME): name, str(STOCK): stock}}


def table(rows=None, columns=None, view_mode=2, selection=1, **extra):
    return {'id': 1, 'viewMode': view_mode, 'selection': selection,
            'columns': columns if columns is not None else [column(NAME, 'Name'), column(STOCK, 'Stock')],
            'rows': rows if rows is not None else [row(n, f'Item {n}', str(n * 10)) for n in range(1, 6)], **extra}


def tab(title='List', **extra):
    return {'title': title, 'table': table(**extra)}


class _Quiet(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, '.js': 'text/javascript',
                      '.mjs': 'text/javascript', '.css': 'text/css', '.png': 'image/png'}
    # What is served late, as a slow line would: the end of a path → seconds.
    late = {}

    def do_GET(self):
        for ending, seconds in self.late.items():
            if self.path.endswith(ending):
                time.sleep(seconds)
        super().do_GET()

    def translate_path(self, path):
        # The page at /pub/, as a publication's: everything under it is the site's folder.
        return super().translate_path(path[len('/pub'):] if path.startswith('/pub') else path)

    def log_message(self, *args):
        pass


class _Server(http.server.ThreadingHTTPServer):
    # A page asks for its modules all at once: one refused by a short queue and the page never starts.
    request_queue_size = 128

    def handle_error(self, request, client_address):
        # A page closed with a file still on its way: nothing went wrong.
        pass


class PageTest(unittest.TestCase):
    """One browser for the file, one page for each test."""

    @classmethod
    def setUpClass(cls):
        handler = functools.partial(_Quiet, directory=str(WWW))
        cls.server = _Server(('127.0.0.1', 0), handler)
        threading.Thread(target=cls.server.serve_forever, daemon=True).start()
        cls.address = f'http://127.0.0.1:{cls.server.server_address[1]}/pub/'
        cls.playwright = sync_playwright().start()
        channel = os.environ.get('OES_WEB_BROWSER')
        cls.browser = cls.playwright.chromium.launch(**({'channel': channel} if channel else {}))

    @classmethod
    def tearDownClass(cls):
        cls.browser.close()
        cls.playwright.stop()
        cls.server.shutdown()

    def setUp(self):
        self.context = None
        self.errors = []
        _Quiet.late = {}

    def tearDown(self):
        if self.context is not None:
            self.context.close()
        self.assertEqual(self.errors, [], 'the page said an error')

    def start(self, scenario=None, wait=True, touch=False, before=None):
        """The page opened on a scenario (see fakeServer.js); by default until its window is on the screen."""
        options = {'viewport': {'width': 1280, 'height': 800}}
        if touch:
            options.update(viewport={'width': 390, 'height': 844}, is_mobile=True, has_touch=True)
        self.context = self.browser.new_context(**options)
        # Everything here answers at once: what has not come in this time will not come, and a whole run should not wait
        # thirty seconds for each thing a broken page does not draw.
        self.context.set_default_timeout(5000)
        page = self.context.new_page()
        page.on('pageerror', lambda error: self.errors.append(str(error)))
        page.on('requestfailed', lambda request: self.errors.append(f'{request.url}: {request.failure}')
                if request.failure != 'net::ERR_ABORTED' else None)
        page.add_init_script(f'window.fakeScenario = {json.dumps(scenario or {})};')
        page.add_init_script(path=str(HERE / 'fakeServer.js'))
        if before is not None:
            before(page)
        page.goto(self.address, wait_until='commit')
        if wait:
            page.wait_for_selector('.client-window', timeout=15000)
            page.wait_for_function("document.getElementById('splash').hidden", timeout=15000)
        self.page = page
        return page

    def serve_late(self, ending, seconds):
        """A file of the site served late, as a slow line would."""
        _Quiet.late = {ending: seconds}

    def sent(self, kind=None):
        """The events the page sent, of one kind."""
        return self.page.evaluate('(kind) => window.fake.events(kind ?? undefined)', kind)

    def reads(self):
        return self.page.evaluate('window.fake.reads()')

    def rows_text(self):
        """Each row of the grid as its cells read."""
        self.page.wait_for_timeout(50)
        return self.page.evaluate("""[...document.querySelectorAll('oes-clientform:not([hidden]) .tabulator-tableholder .tabulator-row')]
            .map((row) => [...row.querySelectorAll('.tabulator-cell')].map((cell) => cell.textContent.trim()).filter(Boolean).join('|'))""")

    def row(self, text):
        return self.page.locator('oes-clientform:not([hidden]) .tabulator-row', has_text=text).first

    def wait_rows(self, count):
        self.page.wait_for_function(
            """(count) => document.querySelectorAll("oes-clientform:not([hidden]) .tabulator-tableholder .tabulator-row").length === count""",
            arg=count, timeout=5000)
