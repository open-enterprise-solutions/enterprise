// THE PAGE — a publication's client in the browser. The base is the address's first segment; its socket stands beside
// the page. The page logs in, then draws the window the server answers with, and keeps drawing it as the answers come.

import './controls/index.js';
import { Connection } from './protocol/connection.js';
import { Session } from './protocol/session.js';
import { Pictures } from './protocol/pictures.js';
import { logIn } from './window/login.js';
import { ClientWindow } from './window/window.js';

const root = /** @type {HTMLElement} */ (document.getElementById('client'));
const publication = location.pathname.split('/').filter(Boolean)[0] ?? '';

async function start() {
	if (publication === '') {
		root.textContent = 'No publication in the address: open /<publication>/.';
		return;
	}
	document.title = publication;
	const connection = Connection.beside(location);
	try {
		await connection.open();
	}
	catch (error) {
		root.textContent = `${publication}: ${/** @type {Error} */ (error).message}`;
		return;
	}
	const session = new Session(connection);
	const pictures = new Pictures(session);
	await logIn(root, session, publication);
	addEventListener('pagehide', () => session.logout());
	// The login's answer is the first frame: the session holds it, and the window draws it as it comes up.
	const clientWindow = new ClientWindow(root, session, pictures);
	if (session.frame !== null)
		clientWindow.draw(session.frame);
}

start();
