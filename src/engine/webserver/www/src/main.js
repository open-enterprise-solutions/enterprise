// THE PAGE — a publication's client in the browser. The base is the address's first segment; its socket stands beside
// the page. The page logs in, then draws the window the server answers with, and keeps drawing it as the answers come.

import './controls/index.js';
import './docView/index.js';
// The application's property cells — a designer's page would import the designer's beside them (properties/index.js).
import './properties/runtime.js';
import { _, readDictionary } from './localization.js';
import { startAssistant } from './window/assistantAccess.js';
import { readLanguages } from './properties/translate.js';
import { Connection } from './protocol/connection.js';
import { Mode, Session } from './protocol/session.js';
import { Pictures } from './protocol/pictures.js';
import { logIn } from './window/login.js';
import { hideSplash, showSplash } from './window/splash.js';
import { ClientWindow } from './window/window.js';
import { moveDialogs } from './window/dialogMove.js';
import { Theme, applyTheme, readTheme } from './window/theme.js';

const root = /** @type {HTMLElement} */ (document.getElementById('client'));
const publication = location.pathname.split('/').filter(Boolean)[0] ?? '';

async function start() {
	// Before the login the person is not known: the page speaks the browser's language and is painted as the machine is
	// set, the splash too — as the thin client speaks the machine's before it has a base.
	await readDictionary(navigator.language);
	if (publication === '') {
		root.textContent = _('No publication in the address: open /<publication>/.');
		hideSplash();
		return;
	}
	document.title = publication;
	applyTheme(Theme.System);
	moveDialogs();
	showSplash(_('Connecting to the base'));
	const connection = Connection.beside(location);
	try {
		await connection.open();
	}
	catch (error) {
		root.textContent = `${publication}: ${/** @type {Error} */ (error).message}`;
		hideSplash();
		return;
	}
	// The application's client — appworkspace's in the browser: its documents are those docView/index.js registers.
	const session = new Session(connection, Mode.Runtime);
	const pictures = new Pictures(session);
	showSplash(_('Logging in'));
	await logIn(root, session, publication);
	addEventListener('pagehide', () => session.logout());
	showSplash(_('Opening the window'));
	// The person's theme and language, before anything of theirs is drawn.
	await readTheme(session, session.frame);
	await readDictionary(session.locale);
	await readLanguages(session, session.frame);
	// The login's answer is the first frame: the session holds it, and the window draws it as it comes up.
	const clientWindow = new ClientWindow(root, session, pictures);
	if (session.frame !== null)
		await clientWindow.draw(session.frame);
	await clientWindow.settled();
	hideSplash();
	// The person's assistant access, started by itself when they left it on — as the thin client starts its port at login.
	await startAssistant(session);
}

start();
