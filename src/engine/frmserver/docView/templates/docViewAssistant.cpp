#include "docViewAssistant.h"

#include "core/fdatetime.h"                  // ibDateTime — a line's time
#include "core/serialize/dataBuilder.h"
#include "frmserver/client/clientFrame.h"   // the client told its frame changed (RefreshFrame)

wxIMPLEMENT_DYNAMIC_CLASS(ibAssistantTranscriptDocument, ibDocument);
wxIMPLEMENT_DYNAMIC_CLASS(ibAssistantTranscriptView, ibView);

ibMcpServer* ibAssistantTranscriptView::GetMcpServer() const
{
	const ibAssistantTranscriptDocument* const document = wxDynamicCast(GetDocument(), ibAssistantTranscriptDocument);
	return document != nullptr ? document->GetMcpServer() : nullptr;
}

bool ibAssistantTranscriptView::OnCreate(ibDocument* doc, long flags)
{
	// The client whose tab this is — the current session's, as the view is made in its call.
	m_frame = ibClientFrame::GetFrame();

	if (ibMcpServer* const server = GetMcpServer()) {

		// ⭐ WHAT WAS ALREADY SAID, BEFORE ANYTHING NEW — restored before the view is attached, so a message arriving
		// mid-restore lands after the history rather than in the middle of it (the desktop's OnCreate).
		{
			std::lock_guard<std::mutex> lock(m_transcriptMutex);
			for (const ibMcpServer::Turn& turn : server->GetConversation())
				m_transcript.push_back(ibString::Format(
					turn.fromPerson ? ibT("## %s\n\n%s\n\n") : ibT("%s%s\n\n"),
					turn.fromPerson ? ibString(_("You")) : ibString(), turn.text));
		}

		server->AddNotifier(this);

		// ⭐ THE STATE, WHICHEVER IT IS — and from the server, which is the only thing that knows (Greeting carries the
		// not-running sentence too).
		std::lock_guard<std::mutex> lock(m_transcriptMutex);
		m_transcript.push_back(server->Greeting() + ibT("\n\n"));
	}

	return ibView::OnCreate(doc, flags);
}

void ibAssistantTranscriptView::OnDraw(ibDataNode& frame)
{
	const ibMcpServer* const server = GetMcpServer();

	std::lock_guard<std::mutex> lock(m_transcriptMutex);

	// ⚠ TWO SILENCES, TOLD APART BY ASKING. A message still sitting in the queue has been collected by nobody — the
	// client says "waiting for an assistant" then, and "processing" once it is taken (the desktop's RefreshWaiting).
	ibDataNode& state = frame.Child(ibProtocol::ibView::State);
	state.SetValue(ibProtocol::ibView::ibAssistant::Waiting, m_waiting);
	state.SetValue(ibProtocol::ibView::ibAssistant::Pending, static_cast<std::int32_t>(server != nullptr ? server->PendingCount() : 0));

	// An entry a row, named by its place: the transcript only grows, so what reaches a client is the new entries.
	ibDataNode& rows = frame.Child(ibProtocol::ibView::ibAssistant::Rows);
	for (std::size_t i = 0; i < m_transcript.size(); ++i)
		rows.AddChild(0, static_cast<ibMetaID>(i + 1)).SetValue(ibProtocol::ibView::ibAssistant::Text, m_transcript[i]);
}

bool ibAssistantTranscriptView::OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args)
{
	if (event != ibProtocol::ibEvent::ibKind::Change)
		return false;

	const ibString text = args.GetValue<ibString>(ibProtocol::ibView::ibAssistant::Text);
	if (text.IsEmpty())
		return false;

	ibMcpServer* const server = GetMcpServer();
	if (server == nullptr)
		return false;

	// ⭐ AND THE WAITING BECOMES VISIBLE — set before the message goes: an answer asked of the connected model may land
	// on the server's thread before the asking returns, and it is the answer that ends the wait.
	{
		std::lock_guard<std::mutex> lock(m_transcriptMutex);
		m_waiting = true;
	}

	// ⭐ ASKED IF IT CAN BE ASKED, LEFT WHERE IT CAN BE FOUND IF NOT — the desktop's OnSend. The person sees the same
	// thing either way; only the waiting differs.
	ibString refusal;
	if (!server->CanAskModel() || !server->AskModel(text, refusal))
		server->Say(text);
	return true;
}

bool ibAssistantTranscriptView::OnClose(bool deleteWindow)
{
	// BEFORE THE VIEW GOES — once RemoveNotifier returns, no call to it is in flight (ibMcpServer::Publish).
	if (ibMcpServer* const server = GetMcpServer())
		server->RemoveNotifier(this);

	return ibView::OnClose(deleteWindow);
}

void ibAssistantTranscriptView::OnMcpExchange(const ibString& method, const ibString& request, const ibString& answer)
{
	// A message collected — not a line, but the client's "waiting for an assistant" is over (State Pending).
	if (method != ibT("taken")) {

		// A TRANSCRIPT, NOT A WIRE LOG — the desktop's notifier's three shapes: what a person said, what the assistant
		// answered, and a single line for anything the assistant DID along the way, each with the time.
		const ibString at = ibDateTime::Now().Format(ibT("%H:%M"));

		// The dot after the time, by its code point: this file has no BOM, and MSVC reads such a file in the system
		// codepage — the desktop's literal (its file has one) arrived here as two letters.
		const ibString dot = ibString(ibT(" ")) + ibChar(0x00B7) + ibT(" ");

		ibString line;

		if (method == ibT("you")) {
			line << ibT("## ") << at << dot << _("You") << ibT("\n\n");
			line << request << ibT("\n\n");
		}
		else if (method == ibT("assistant")) {
			line << ibT("## ") << at << dot << _("Assistant") << ibT("\n\n");
			line << answer << ibT("\n\n");
		}
		else if (method == ibT("did")) {
			// ⭐ A HEADLINE, AND UNDER IT WHAT THERE IS TO READ — the tool decides which of its arguments is worth
			// showing; most have nothing and stay one line. Still not the JSON.
			line << ibT("> ") << at << dot << request << ibT("\n\n");

			if (!answer.IsEmpty())
				line << answer << ibT("\n");
		}
		else {
			line << ibT("> ") << at << dot << method << ibT("\n\n");
		}

		std::lock_guard<std::mutex> lock(m_transcriptMutex);
		m_transcript.push_back(line);
		// The arrival of the answer is what ends the wait (the desktop's StopWaiting).
		if (method == ibT("assistant"))
			m_waiting = false;
	}

	if (m_frame != nullptr)
		m_frame->RefreshFrame();
}
