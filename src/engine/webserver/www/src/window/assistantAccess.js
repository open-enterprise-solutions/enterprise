// THE ASSISTANT ACCESS (schema 7, McpServer) — the browser's page of the thin client's (frmclient/mainFrame/settings/
// mcpsettingspanel.cpp). The browser has no port of its own: the person's server is raised at a web address of the
// session's instead, published for as long as it runs on the web server the page came from — mcp {Command: Register}
// (protocol.h, ibMcp), <publication>/mcp/<…>, a request presenting the token.
//
// AS THE THIN CLIENT'S. "Let an assistant reach this session" is the setting: on, the access starts by itself at the
// next login. Start now and Stop now act at once. What the page says is what the server answers (Running), and the
// address and the key are handed out only while it runs. The setting, the address and the key are the person's, kept
// in this browser under their name, so an assistant set up once keeps working.

import { _, format, labelText } from '../localization.js';

/** @typedef {import('../protocol/session.js').Session} Session */
/** @typedef {{On: boolean, Path: string, Token: string}} Kept */

// The schema's number (ibSchema::ibKind::McpServer).
export const assistantSchema = 7;
// ibMcp::ibCommand.
const Command = Object.freeze({ Register: 4, Unregister: 5 });

// The address the page raised the server at, and its key — none while it is down.
/** @type {{Path: string, Token: string} | null} */
let registered = null;
// Why the access did not start by itself at login — said by the page until it is started or stopped.
let refusedAtLogin = '';

/** @param {Session} session @returns {Kept | null} */
function readKept(session) {
	try {
		const text = localStorage.getItem(keptKey(session));
		return text ? JSON.parse(text) : null;
	}
	catch {
		return null;
	}
}

/** @param {Session} session @param {Kept} kept */
function writeKept(session, kept) {
	try {
		localStorage.setItem(keptKey(session), JSON.stringify(kept));
	}
	catch {
		// Not kept: the access runs while the session does, and the next login does not start it.
	}
}

/** The publication's and the person's: another base, or another person in this browser, has its own. @param {Session} session */
function keptKey(session) {
	return `assistant ${new URL('.', location.href).pathname} ${session.user}`;
}

/**
 * The server raised at the address kept, when there is one; what is drawn is kept for next time.
 * @param {Session} session
 * @param {boolean} on the setting, kept with them
 */
async function start(session, on) {
	const kept = readKept(session);
	const answer = await session.call('mcp', { Command: Command.Register, Path: kept?.Path ?? '', Token: kept?.Token ?? '' });
	registered = { Path: String(answer.Path ?? ''), Token: String(answer.Token ?? '') };
	writeKept(session, { On: on, ...registered });
	refusedAtLogin = '';
}

/** @param {Session} session */
async function stop(session) {
	await session.call('mcp', { Command: Command.Unregister });
	registered = null;
	refusedAtLogin = '';
}

/**
 * At login: started by itself when the person left it on in this browser, as the thin client starts its port. A refusal
 * — their roles no longer give the assistant, the address taken by another of their windows — is the page's to say.
 * @param {Session} session
 */
export async function startAssistant(session) {
	if (readKept(session)?.On !== true)
		return;
	try {
		await start(session, true);
	}
	catch (error) {
		refusedAtLogin = /** @type {Error} */ (error).message;
	}
}

/**
 * Tools → Options...: the setting, the access started or stopped now, and — while it runs — what an assistant's
 * configuration takes. The schema asked first: refused to a person whose roles do not give the assistant, the refusal is
 * the menu's to show.
 * @param {Session} session
 */
export async function showAssistant(session) {
	const shown = await session.call('schema', { Schema: assistantSchema });
	let running = shown.Running === true;
	if (!running)
		registered = null;
	// The page's copy of the setting: kept on OK, and by Start now — it starts on what the page says.
	let on = readKept(session)?.On === true;
	let said = running ? '' : refusedAtLogin;

	const dialog = /** @type {any} */ (document.createElement('wa-dialog'));
	dialog.label = _('Assistant access (MCP)');
	dialog.className = 'assistant';

	const enabled = /** @type {any} */ (document.createElement('wa-checkbox'));
	enabled.textContent = _('Let an assistant reach this session');
	enabled.checked = on;
	const what = document.createElement('p');
	what.textContent = _('A connected assistant works as you, with your rights: it can do here what your roles allow you. '
		+ 'It runs only while your session is open.');
	const state = document.createElement('p');
	state.className = 'state';
	const run = /** @type {any} */ (document.createElement('wa-button'));
	run.setAttribute('size', 's');

	const howTo = document.createElement('p');
	howTo.textContent = _('Any assistant that speaks MCP can connect at this address. Put this into its server '
		+ 'configuration - it is the standard form and needs nothing else:');
	const block = handout(10);
	const orCommand = document.createElement('p');
	orCommand.textContent = _('Using Claude Code? This one line does the same thing:');
	const command = handout(3);
	command.className = 'wrapped';
	const warning = document.createElement('p');
	warning.textContent = _('Both contain your access key. Whoever holds it can do here everything you can - put it '
		+ 'into your own assistant and nowhere else.');
	const given = document.createElement('fieldset');
	const legend = document.createElement('legend');
	legend.textContent = _('Give this to your assistant');
	given.append(legend, howTo, block, copyButton(_('Copy the configuration'), block), orCommand, command,
		copyButton(_('Copy the command'), command), warning);

	/** What is true right now, in one place — the button's label and the sentence beside it cannot disagree. */
	const refresh = () => {
		run.textContent = running ? _('Stop now') : _('Start now');
		const url = registered !== null ? new URL(registered.Path, location.href).href : '';
		state.textContent = said !== '' ? said
			: running ? format(_('Running at %s'), url)
			: on ? _('Not running. It starts by itself next time you log in.')
			: _('Not running, and switched off above - it will not start by itself.');
		// Not running is an answer, and a better one than an address that would be refused.
		given.hidden = !running || registered === null;
		if (registered === null)
			return;
		block.value = JSON.stringify({ mcpServers: { oes: { type: 'http', url,
			headers: { Authorization: `Bearer ${registered.Token}` } } } }, null, 2);
		command.value = `claude mcp add --transport http oes ${url} --header "Authorization: Bearer ${registered.Token}"`;
	};

	enabled.addEventListener('change', () => {
		on = enabled.checked === true;
		said = '';
		refresh();
	});

	// Started on what the page says, or stopped; a refusal stays on the page, beside what fixes it.
	run.addEventListener('click', async () => {
		if (run.hasAttribute('loading'))
			return;
		said = '';
		if (!running && !on) {
			said = _('Assistant access is switched off.\n\nTurn it on in the assistant access settings.');
			refresh();
			return;
		}
		run.setAttribute('loading', '');
		try {
			if (running)
				await stop(session);
			else
				await start(session, on);
			running = !running;
		}
		catch (error) {
			said = /** @type {Error} */ (error).message;
		}
		run.removeAttribute('loading');
		refresh();
	});

	const ok = document.createElement('wa-button');
	ok.slot = 'footer';
	ok.setAttribute('size', 's');
	ok.setAttribute('variant', 'brand');
	ok.textContent = labelText('&OK');
	ok.addEventListener('click', () => {
		const kept = readKept(session);
		writeKept(session, { On: on, Path: kept?.Path ?? '', Token: kept?.Token ?? '' });
		dialog.open = false;
	});
	const cancel = document.createElement('wa-button');
	cancel.slot = 'footer';
	cancel.setAttribute('size', 's');
	cancel.textContent = _('Cancel');
	cancel.addEventListener('click', () => { dialog.open = false; });

	const runRow = document.createElement('div');
	runRow.className = 'run';
	runRow.append(run, state);
	dialog.append(enabled, what, runRow, given, ok, cancel);
	dialog.addEventListener('wa-after-hide', (/** @type {Event} */ event) => {
		if (event.target === dialog)
			dialog.remove();
	});
	refresh();
	document.body.append(dialog);
	dialog.open = true;
}

/** A text to be copied whole, read-only. @param {number} rows */
function handout(rows) {
	const text = document.createElement('textarea');
	text.readOnly = true;
	text.rows = rows;
	text.spellcheck = false;
	return text;
}

/**
 * A button that copies the text whole — selecting a key by hand is where it gets truncated. The page's own copy, not the
 * clipboard's promise: that one asks for a secure origin, and a publication may be plain http.
 * @param {string} caption
 * @param {HTMLTextAreaElement} from
 */
function copyButton(caption, from) {
	const button = document.createElement('wa-button');
	button.setAttribute('size', 's');
	button.textContent = caption;
	button.addEventListener('click', () => {
		from.select();
		document.execCommand('copy');
	});
	return button;
}
