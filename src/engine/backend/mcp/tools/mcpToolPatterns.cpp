///////////////////////////////////////////////////////////////////////////////
//	Copyright : Maxim Kornienko / Open Enterprise Solutions
//	Name      : mcpToolPatterns.cpp
//	Purpose   : `pattern_read` — how to read a request and which shape it asks for.
//
//	⭐⭐ WHY THIS IS A TOOL AND NOT MORE `instructions`.
//
//	The connection's `instructions` are read in full by every client, every time, and they had
//	grown past seven kilobytes. Everything put there is paid for on every connection whether it is
//	needed or not — and this material is exactly the kind that keeps growing: every conversation
//	with someone who knows the domain adds another "and when they say THIS, it means THAT".
//
//	The dividing line is not size, it is whether a model can know to ask:
//	  · a DISCIPLINE — write notes on the object, name things in English, link by id — is something
//	    nobody asks for, because not knowing it feels like nothing. It stays in `instructions`.
//	  · a PATTERN is consulted at a moment the model can recognise: it is about to choose a
//	    metatype, and it knows it is choosing. Told that these exist, it will come and read one.
//
//	And these are RECOMMENDATIONS, deliberately: "where it is better to look", not obligations.
//	A rule that cannot be followed sensibly in some case gets ignored wholesale; advice that says
//	what it is FOR survives being disagreed with.
//
//	⭐⭐ WHAT THIS CORPUS IS, so that what gets added to it stays the same kind of thing.
//
//	A KNOWLEDGE BASE, not advice addressed to an assistant. It is written for whoever is reading —
//	a model connected over MCP today, a person learning the platform tomorrow — and this tool is
//	one door onto it rather than its purpose. Nothing in the entries should assume its reader.
//
//	What it holds is a TRANSLATOR — from the words of the people who ask for the work into the
//	shapes this platform has. An accountant does not say "accumulation register with RegisterType
//	= turnovers"; they say "I want to see the sales by month", and that sentence names the shape
//	precisely. The entries record which sentence means which shape, and why.
//
//	So it is NOT documentation of the platform: what a register IS belongs to the docs and to the
//	help corpus, and repeating it here would only give the two a chance to disagree. What lives
//	here is the step BEFORE that — hearing the request correctly — which is nowhere else, because
//	it is not a property of the platform at all. It is experience, one person's, written down
//	deliberately as such: it will be extended as more of it is put into words, and an entry may be
//	argued with, which is why each says what it is FOR rather than only what to do.
///////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"

#include <algorithm>   // the addresses are ranked, and the bar is the best score there was

#include <vector>

namespace {

using ibArg = ibMcpTool::ibMcpArgument;
const ibArg& ArgName() { static const ibArg a(wxT("name"), ibArg::Kind::Text, ibMcpText("Which pattern to read. Alone it answers the summary and the list of topics inside; with `topic` it answers that part in full. Omit it to list what there is.")); return a; }

const ibArg& ArgTopic()
{
	static const ibArg a(wxT("topic"), ibArg::Kind::Text,
		ibMcpText("Which part of that pattern, by any words of its heading as the listing shows them. "
		  "Omit for the table of contents; pass `all` for the whole entry."));
	return a;
}

// ⭐⭐ THE WAY IN WHEN THE PATTERN'S NAME IS NOT KNOWN - and it usually is not. A caller has the
// WORDS OF A PROBLEM ("stock will not write off", "spread the overheads", "the shops serve each
// other") and no idea which of thirty entries holds the answer, or whether one does at all.
//
// Searching the corpus answers with ADDRESSES: which pattern, which topic inside it, and the line
// that matched. Then one more call reads that topic. Nothing else is fetched, and an entry that
// does not match is never opened - which is the whole point at a hundred kilobytes.
const ibArg& ArgQuery()
{
	static const ibArg a(wxT("query"), ibArg::Kind::Text,
		ibMcpText("Words of the problem, in any language of the trade - 'will not write off', 'close the "
		  "month', 'column per warehouse'. Answers WHERE the corpus speaks about it: the pattern, "
		  "the topic inside it, and the matching line. Ranked, not filtered: what matched every "
		  "word if anything did, otherwise the best there was, marked '3 of your 5 words' - so a "
		  "word too many narrows the answer instead of erasing it. A REGULAR EXPRESSION works too "
		  "and is read as one whenever it carries | \\ [ ] ^ $ .* - 'lot|batch|fifo', "
		  "'advance.*offset'."));
	return a;
}

// ONE ENTRY. `name` is the address, `summary` is what the listing shows, `text` is the whole of it.
struct ibMcpPattern {
	const wxChar* m_name;
	wxString      m_summary;
	wxString      m_text;
};

// ⚠ HELD AS A FUNCTION, not a file-scope table: a static built before the locale is set would
// freeze whatever the strings were at that moment for the life of the process.
//
// 🛑⭐ AND THE ENTRIES ARE `wxT`, NOT `_()`, WHICH IS NOT A STYLE CHOICE. The translation macro
// resolves to `wxASCII_STR` here, so every character outside ASCII in a translatable literal comes
// back as a question mark - and this corpus is written with ⭐ / 🛑 / ⚠ opening its headings. The
// result was `??? FIXED OVERHEADS…`: the marks were gone from the text, from the table of contents
// built out of those headings, and from every search hit quoting them.
//
// ⚠ Seen only by READING AN ANSWER, never by anything failing: the tool worked, the topics split
// correctly, and the noise was three characters wide (2026-09-02, on the first live check of the
// layered reading). The entries are English by rule and are not translated, so `wxT` costs nothing
// and keeps the marks - but any NEW string here that a person is meant to read in their own
// language has to stay `_()` and stay ASCII.
const std::vector<ibMcpPattern>& Patterns()
{
	static const std::vector<ibMcpPattern> s_patterns = {

	{ wxT("shapes"),
	  ibMcpText("Which metatype a described need asks for - and the words that give it away."),
	  ibMcpText("READING A REQUEST FOR ITS SHAPE.\n"
		"\n"
		"What someone describes tells you which metatype it is. Listen for the QUESTION behind the\n"
		"words, not for the nouns - the same noun lands in different shapes depending on what is\n"
		"being asked of it.\n"
		"\n"
		"\"It changes over time and I need it as of a date\" - a price, a rate, who is responsible\n"
		"for something - is an InformationRegister with `Periodicity` set. The period is part of\n"
		"the key: you are keeping a value that was true for a stretch of time, not one that was\n"
		"overwritten. And the granularity is dictated too - \"once a month\", \"I want to see it\n"
		"monthly\" means `Periodicity` = Month, not a date field someone rounds by hand.\n"
		"\n"
		"\"I just need somewhere to keep this, nothing produces it\" is an InformationRegister with\n"
		"`WriteMode` = Independent. Nothing posts it, it is written directly. Reach for this before\n"
		"inventing a document whose only purpose is to carry data.\n"
		"\n"
		"SETTINGS LIVE THERE TOO, and this is the case most often missed. Anything kept PER\n"
		"something - per user, per user and warehouse, per user and report - is an independent\n"
		"InformationRegister keyed by exactly that. The key is the whole design: \"whose setting is\n"
		"this\" answered as dimensions. A setting that is the same for everybody is not this - see\n"
		"`constants`.\n"
		"\n"
		"\"How much happened over a period\" - sales, where a return is a reversing movement against\n"
		"the same register rather than a second register - is an AccumulationRegister with\n"
		"`RegisterType` = turnovers. It answers totals for an interval and nothing else, which is\n"
		"the point: a narrow question, answered cheaply.\n"
		"\n"
		"\"What is left\" - stock on hand, how much is reserved, lot accounting, opening and closing\n"
		"figures - is an AccumulationRegister with `RegisterType` = balances. Read it through\n"
		"`<Register>.Balance` and `<Register>.Turnovers`; never compute a balance by summing\n"
		"movements yourself.\n"
		"\n"
		"The moment the words are an accountant's - an account, a correspondence, debit against\n"
		"credit - it is an AccountingRegister over a ChartOfAccounts with `Correspondence` on.\n"
		"Nothing else carries double entry, and imitating it with a pair of accumulation registers\n"
		"loses the one property that makes it accounting.\n"
		"\n"
		"KEY: THE WORDS THEY USE ALREADY CONTAIN A DESIGN - AND IT IS NOT THEIR DECISION TO HAVE MADE.\n"
		"An accountant saying \"lots of goods in warehouses\", \"write off by FIFO\", \"the receipt\n"
		"document of the batch\" is not describing a requirement: they are repeating the shape of a\n"
		"system they have used, and they will keep repeating it because it is the only one they have\n"
		"seen. Take it as a strong signal about their HABITS and a weak one about their NEEDS.\n"
		"\n"
		"STOP: SO DO NOT SILENTLY DECIDE EITHER WAY. Two mistakes, and the second is the common one:\n"
		" building what their words imply, because they said it - and inheriting a mechanism they\n"
		" never needed;\n"
		" building the cheaper design because it is better - and being wrong at the audit, when\n"
		" the policy really did say FIFO.\n"
		"\n"
		"* PUT THE FORK TO THEM, IN THEIR LANGUAGE, WITH THE CONSEQUENCE OF EACH. Not \"lots or\n"
		"average cost?\" - that asks them to know what you know - but \"do you ever need to say which\n"
		"delivery a particular item came from? if not, cost can be an average and the whole system\n"
		"is simpler; if yes, we keep the deliveries apart and it costs this much more\". People\n"
		"answer that immediately and correctly, because it is a question about their work rather\n"
		"than about the software.\n"
		"NOTE: AND SAY WHICH ONE YOU RECOMMEND. A fork offered without a recommendation is work handed\n"
		"back to somebody who came for an answer.\n"
		"\n"
		"KEY: AND A PROPERTY THAT CHANGES OVER TIME DOES NOT BELONG ON THE CATALOGUE ITEM AT ALL. This\n"
		"is the same rule as the first one above, applied where people least expect it: a fixed\n"
		"asset does not carry its cost, its useful life, its depreciation method, its state and its\n"
		"\"charge depreciation or not\" as attributes. Every one of those changes - modernisation\n"
		"raises the cost, a decision changes the method, the asset is put into service and later\n"
		"withdrawn - and every one of them is needed AS IT WAS at the moment being computed.\n"
		"\n"
		"So they live in information registers with periodicity, one per family of facts, and any\n"
		"calculation reads the LATEST ENTRY AS OF ITS OWN DATE. The item itself keeps only what\n"
		"never changes: what it is, its inventory number, what it is called.\n"
		"NOTE: THE TEST: \"if I recompute last year, must this value be last year's?\" If yes, it is a\n"
		"register entry, not an attribute - and putting it on the item means last year can never be\n"
		"recomputed, only remembered.\n"
		"* The same reasoning covers a person's position and salary, an item's tax rate, a\n"
		"warehouse's responsible person, an agreement's settlement rules. A catalogue is what a\n"
		"thing IS; a periodic register is what was TRUE about it at a time.\n"
		"\n"
		"AND WHEN THE REQUEST IS A VERB RATHER THAN A THING - \"load the statement\", \"upload to the\n"
		"portal\", \"recalculate the cost\", \"find and replace\", \"close the month\" - it is a DATA\n"
		"PROCESSOR. Told apart from its two neighbours by what it leaves behind:\n"
		" A DOCUMENT records that something HAPPENED and stays in the base as evidence of it,\n"
		" with movements behind it. If the event has a date, a number and consequences, it is a\n"
		" document however much work the entry takes.\n"
		" A REPORT only READS. It answers and changes nothing.\n"
		" A PROCESSOR ACTS and keeps nothing of its own: it creates documents, rewrites values,\n"
		" sends a file. Afterwards there is no trace of the processor itself, only of what it\n"
		" did - which is why what it did has to be visible before it does it (see\n"
		" `external-data` for the shape of a loading screen, and note the rule that nothing is\n"
		" written before the command).\n"
		"NOTE: THE MISTAKE IN BOTH DIRECTIONS: a \"month-end closing\" built as a processor leaves nobody\n"
		"able to say when the month was closed or to undo it - that is a document. And a document\n"
		"invented to carry the parameters of an import, posted for no reason and never looked at\n"
		"again, is a processor that was afraid to be one.\n"
		"* AND A TINY ONE NEEDS NO METAOBJECT AT ALL: \"recalculate this document's lines\", \"fill\n"
		"the table from the order\" is a COMMAND on the object it acts upon, living in its module.\n"
		"A processor earns its own existence when it works across many objects, or has parameters\n"
		"and a screen of its own.\n"
		"\n"
		"A CLOSED SET THE CODE ITSELF BRANCHES ON IS AN ENUMERATION - VAT rate, direction of a\n"
		"movement, state of an order. The test is not how many values there are but WHO MAY ADD\n"
		"ONE: nobody, because the code names them (`Enums.VatRates.Vat20`) and a value added by a\n"
		"user would be a value nothing knows what to do with. The moment somebody says \"and we will\n"
		"add our own kinds later\", it is a catalogue instead, and the code stops branching on\n"
		"identity - it reads an attribute of the row.\n"
		"NOTE: AND THE ENUMERATION HOLDS THE NAME, NOT THE NUMBER. The member is `Vat20`; that this\n"
		"currently means twenty percent is a fact of law, and it belongs in a function that maps the\n"
		"member to a figure - or, once the figure has to be right for OLD documents too, in an\n"
		"information register with periodicity (see `shapes` above and `printing`). Writing the\n"
		"number into the member's name is why systems end up with `Vat20` meaning eighteen.\n"
		"\n"
		"NOTE: AND THE GENERAL FORM OF ALL OF THESE: \"I want to see it by month\", \"per warehouse\",\n"
		"\"as of a date\" is a request for a REGISTER of some kind. A catalogue or a document that\n"
		"someone reports over is the shape that has to be unpicked later, once the data is real.") },

	{ wxT("predefined"),
	  ibMcpText("A particular item named in conversation has to exist as a predefined one."),
	  ibMcpText("A NAMED INSTANCE IS A PREDEFINED ITEM.\n"
		"\n"
		"When someone says \"for the Central warehouse\", \"the VAT rate\", \"the main cash desk\" -\n"
		"naming a PARTICULAR item rather than a kind of item - that item has to exist as predefined.\n"
		"\n"
		"The reason is identity. Code has no other stable handle on a single row: without a\n"
		"predefined name it has to go and find the row by name or by code, and then a rename, a\n"
		"typo, or a second row that happens to share the name turns the lookup into a silent wrong\n"
		"answer rather than an error. A predefined item is named in the metadata and reached by that\n"
		"name, and the name is checked when the configuration is applied.\n"
		"\n"
		"The signal is grammatical: a definite article, a proper name, \"the\" rather than \"a\". If\n"
		"the sentence would still be true of any row of that catalogue, it is not this pattern.\n"
		"\n"
		"KEY: A PREDEFINED ITEM LIVES IN TWO PLACES AT ONCE, and everything awkward about it follows\n"
		"from that. It is declared in the CONFIGURATION and it exists as a ROW IN THE DATA, and the\n"
		"two are tied by a NAME the row carries in a standard attribute of its own. There is a\n"
		"second standard attribute beside it - a flag saying the row is predefined at all.\n"
		"\n"
		"WHAT THAT BUYS: code names it directly, with no search - and a query filters on that same\n"
		"name attribute rather than on code or description, which is the whole point (a query\n"
		"matching on a description is the silent wrong answer this pattern exists to prevent).\n"
		"\n"
		"STOP: AND WHAT IT COSTS, in the order people meet it:\n"
		" THE ROW CAN BE MARKED FOR DELETION BY A PERSON, unless the right to do so is withheld.\n"
		" The declaration stays; the row goes to the bin. Decide who may, rather than assuming\n"
		" nobody will.\n"
		" REMOVING THE DECLARATION DOES NOT REMOVE THE ROW. The row survives as an ordinary item\n"
		" with its tie broken - and every reference in every document still points at it. That is\n"
		" the correct behaviour (references must not evaporate) and it surprises everybody once.\n"
		" REFERENCING A PREDEFINED ITEM THAT IS NOT THERE RAISES. Code that names one assumes it\n"
		" exists, and in a base restored from before it was added, it does not. Where that is\n"
		" possible, ask through something that answers \"nothing\" instead of throwing.\n"
		" THE TIE IS THE NAME, AND THE NAME CAN BE MOVED. Writing the name onto another row hands\n"
		" the identity over - which is how a predefined item is re-pointed at a row a person\n"
		" already created, instead of ending up with two.\n"
		"\n"
		"NOTE: WHERE IT DOES NOT BELONG: a catalogue whose items belong to an OWNER (which item of which\n"
		"owner would the name mean?), and any set that changes with the business - a predefined item\n"
		"is a thing the CODE knows, so adding one is a configuration change, and a set that grows\n"
		"monthly does not want to be one. Also not needed where a lookup by a stable attribute\n"
		"already answers: this pattern buys identity, not convenience.\n"
		"\n"
		"NOTE: AND IN A BASE THAT IS SPLIT - copies exchanging data, or one base serving several tenants\n"
		"- the rows are per copy and per tenant, so the same predefined name is a DIFFERENT\n"
		"reference in each. Anything comparing references across the boundary has to compare the\n"
		"names instead.") },

	{ wxT("recipes"),
	  ibMcpText("Building a whole area from nothing: what to create, in what order, for each classic subsystem."),
	  ibMcpText("RECIPES FOR THE AREAS THAT ARE SOLD ELSEWHERE AS SEPARATE PRODUCTS.\n"
		"\n"
		"STOP: FIRST, A CORRECTION OF LANGUAGE, because it changes the answer. Large systems sell\n"
		"currency accounting, settlements, treasury, asset accounting and controlling as MODULES,\n"
		"and it is easy to repeat that framing and say a platform \"does not have currency\n"
		"accounting\". It is the wrong sentence. None of these is a feature a platform either has or\n"
		"lacks: each is an APPLICATION built out of registers, documents and reports - a fortnight\n"
		"or two of work apiece, on primitives this platform already has.\n"
		"\n"
		"So the honest answer to \"do you support multi-currency?\" is not \"no\" and not \"yes\" - it is\n"
		"THIS RECIPE: here is what gets created, in what order, and how you know it works. The\n"
		"things genuinely absent are few and named where they arise (`payroll` is the clear one -\n"
		"no calculation register, so its machinery is yours to build).\n"
		"\n"
		"WHAT THE BIG SYSTEMS SELL AS MODULES, AND WHERE IT IS HERE - so the answer to \"where is\n"
		"your module for this\" is an address rather than an apology:\n"
		" materials, warehousing, procurement the STOCK and PURCHASING recipes below,\n"
		" `lot-accounting`, `stock-control`\n"
		" sales and distribution the SALES recipe, `pricing`\n"
		" financial accounting, the general ledger the FINANCIAL RESULT recipe,\n"
		" `ledger-reports`, `parallel-accounting`\n"
		" controlling - cost centres, cost objects, allocation `production`, `allocation`\n"
		" treasury and cash management the MONEY recipe, `cash-flow`\n"
		" asset accounting the FIXED ASSETS recipe\n"
		" production planning and execution `production`, and the graph it is built on\n"
		" taxation `vat`\n"
		" consolidation and multiple entities the SEVERAL LEGAL ENTITIES recipe\n"
		" human resources and payroll `payroll`, WITH the missing pieces named there\n"
		"Every one of those is an application written on registers, documents and reports. What is\n"
		"bought elsewhere as a module is here a fortnight of building - which is a different\n"
		"proposition, not a smaller one, and worth stating in exactly those terms.\n"
		"\n"
		"Each recipe has a settled order. Do them in the order given: every step is needed by the\n"
		"one after it, and building them the other way round means rebuilding.\n"
		"\n"
		" CURRENCY (read `currency`) \n"
		" 1. Catalogue of currencies. 2. Rates: an information register, periodicity by day,\n"
		" currency as the dimension, TWO resources - rate and multiplicity.\n"
		" 3. Give every document that can be in currency its own currency, rate and multiplicity\n"
		" ATTRIBUTES - copied at posting, never re-read afterwards.\n"
		" 4. Registers that hold money in currency get a currency dimension AND a second resource\n"
		" for the currency amount beside the base-currency one.\n"
		" 5. Revaluation at period end, per kind of currency holding.\n"
		" DONE WHEN: re-opening last quarter's document shows the rate it was posted with, and\n"
		" the revaluation reproduces the same difference twice.\n"
		"\n"
		" SETTLEMENTS (read `settlements`) \n"
		" 1. Counterparties, then AGREEMENTS under them - the agreement carries the currency of\n"
		" settlement and the level of detail (as a whole / per order / per invoice / per\n"
		" settlement document).\n"
		" 2. The settlement register: dimensions counterparty, agreement, and the detail key;\n"
		" resources the amount and, from day one, the currency amount.\n"
		" 3. Posting on both sides - what creates debt (sale, receipt) and what settles it\n"
		" (payment) - through ONE offsetting routine, never per document.\n"
		" 4. The offset itself: split each payment into what closes debt and what becomes an\n"
		" advance; advances on their own account.\n"
		" 5. The reconciliation act, and a screen to pick documents by hand.\n"
		" DONE WHEN: a prepayment followed by a partial delivery leaves the right advance and the\n"
		" right debt, and the balance by agreement equals the balance by documents.\n"
		"\n"
		" STOCK (read `shapes`, `lot-accounting`, `stock-control`) \n"
		" 1. Items, warehouses, units - and the DECISION about lots before anything is built.\n"
		" 2. The balance register: warehouse, item, whatever else is genuinely kept (series,\n"
		" characteristic); resources quantity and amount.\n"
		" 3. Receipt and issue documents writing into it, through one write-off routine.\n"
		" 4. Control of free balance at posting, with the override as a property of the warehouse.\n"
		" 5. Reserves, if orders exist: a second register, and the control reads both.\n"
		" 6. Stocktake, transfer, write-off - and only then reports.\n"
		" DONE WHEN: an issue of more than is free is refused with the line named, and the\n"
		" balance report agrees with the sum of movements for any period.\n"
		"\n"
		" MONEY (read `cash-flow`) \n"
		" 1. Bank accounts and tills; the register of actual funds.\n"
		" 2. Incoming and outgoing documents, each recording what it settles (link to\n"
		" `settlements`).\n"
		" 3. The cash-flow classification - the item of the movement - on every document, because\n"
		" retrofitting it means revisiting history.\n"
		" 4. Requests to spend, with approval and CLOSING; the reserved-money register.\n"
		" 5. Expected receipts and payments, for the payment calendar.\n"
		" DONE WHEN: the calendar for next week is produced without anybody typing a figure\n"
		" twice, and an approved request reduces what may still be committed.\n"
		"\n"
		" FINANCIAL RESULT (read `ledger-reports`, `parallel-accounting`) \n"
		" 1. Chart of accounts, then the accounting register over it with correspondence on.\n"
		" 2. Decide HOW entries arise: written by each document, or DERIVED later from the\n"
		" operational registers - that choice shapes everything after it.\n"
		" 3. Rules of reflection as data (which account for which operation), never as branches\n"
		" inside documents.\n"
		" 4. The standard ledger reports, all of them - they are what an accountant judges the\n"
		" system by.\n"
		" 5. Closing the period: what may still be posted into it, and by whom.\n"
		" DONE WHEN: the trial balance balances, and every figure in it drills down to the\n"
		" documents that made it.\n"
		"\n"
		" PURCHASING AND SALES - THE TWO CYCLES (read `settlements`, `stock-control`, `pricing`) \n"
		" Both are the same three-step chain in opposite directions: an INTENT, a FACT, and MONEY.\n"
		" 1. The order - what was agreed. It reserves nothing by itself; it is a promise with a\n"
		" date and lines.\n"
		" 2. A register of orders with what is still OUTSTANDING per line - ordered less delivered.\n"
		" This is the whole mechanism: everything else reads it.\n"
		" 3. The delivery or receipt, ENTERED FROM the order so nothing is retyped, closing the\n"
		" outstanding quantity and moving stock.\n"
		" 4. Payment against it, settling the debt (`settlements`), with the order as the detail\n"
		" key if the agreement says so.\n"
		" 5. CLOSING an order that will never be completed - a deliberate document, never a status\n"
		" somebody edits, because \"why is this still open\" must have an answer.\n"
		" DONE WHEN: partial delivery leaves the right remainder, over-delivery is refused or\n"
		" permitted by right, and \"what is owed to us and by when\" is one report.\n"
		"\n"
		" FIXED ASSETS (read `shapes` on time-varying properties, `parallel-accounting`) \n"
		" 1. The asset catalogue holds only what never changes - what it is, its inventory number.\n"
		" 2. EVERYTHING ELSE IS A PERIODIC REGISTER, one per family of facts: initial value and\n"
		" useful life, depreciation method and parameters, current state (in service,\n"
		" withdrawn), where it is and whose cost centre it belongs to, whether depreciation is\n"
		" charged at all - each of them separately for each set of books.\n"
		" 3. Documents are EVENTS that write those registers: accepted, put into service,\n"
		" modernised, moved, state changed, written off.\n"
		" 4. Monthly depreciation reads the registers AS OF ITS OWN MONTH and writes the charge to\n"
		" the cost account the reflection register names.\n"
		" 5. Revaluation, and the companion entry it drags into the other books.\n"
		" DONE WHEN: recomputing an old month reproduces the old figure exactly, after a\n"
		" modernisation and a method change have both happened since.\n"
		"\n"
		" SEVERAL LEGAL ENTITIES IN ONE BASE \n"
		" 1. The organisation is a DIMENSION of every register that holds value, from the first\n"
		" day - retrofitting it means re-posting everything.\n"
		" 2. Documents carry it, and posting refuses when the agreement, the warehouse or the\n"
		" account belongs to another one.\n"
		" 3. Transfers BETWEEN organisations are sales and purchases, not movements: one entity's\n"
		" stock leaves and another's arrives, with a debt between them.\n"
		" 4. Access by organisation is row-level (`roles`), and the exemption \"sees every\n"
		" organisation\" is a role.\n"
		" 5. Reports take it as a parameter and default to one, because a total across legal\n"
		" entities is meaningless in most questions and is a consolidation in the rest.\n"
		" DONE WHEN: a document naming another entity's warehouse is refused, and every balance\n"
		" report is right for one organisation and for all of them.\n"
		"\n"
		"* AND THE ORDER BETWEEN THE AREAS is the one in `where-to-start`: goods move first, money\n"
		"and who owes it second, production third, payroll last. Currency runs alongside whichever\n"
		"of them meets a foreign currency first.\n"
		"NOTE: Each recipe ends with a CHECK rather than a list of objects, on purpose: an area is not\n"
		"finished when its metaobjects exist, it is finished when a question it was built for gets\n"
		"the right answer twice.") },

	{ wxT("where-to-start"),
	  ibMcpText("The order configurations actually grow in - useful for knowing what comes next."),
	  ibMcpText("WHAT APPEARS, AND ROUGHLY IN WHAT ORDER.\n"
		"\n"
		"Almost every configuration grows the same way, and knowing the sequence is worth more\n"
		"than it sounds: it tells you what is coming, so that what you build today does not have\n"
		"to be unpicked to make room for it.\n"
		"\n"
		" 1. GOODS MOVING. Receipt, transfer between warehouses, write-off, sale. This is the\n"
		" spine, and almost everything later hangs off it. Get the stock registers right here\n"
		" - see `shapes` for balances against turnovers - because everything downstream reads\n"
		" them.\n"
		" 2. MONEY AND WHO OWES IT. Settlements with counterparties, contracts, currency (see\n"
		" `settlements`, `currency`). Arrives as soon as the goods do, and often the same\n"
		" week.\n"
		" 3. PRODUCTION, if there is any. The shift production report, transport documents,\n"
		" material consumption - and with it costs, lots and the month-end close (see\n"
		" `production`, `lot-accounting`).\n"
		" 4. PAYROLL, if there is any. Work orders and piece records, accrual, deductions. It is\n"
		" its own world with its own periodicity and its own rules, and it touches the cost\n"
		" side at the close.\n"
		"\n"
		"* THE USE OF KNOWING THIS is not to build it all. It is to ask the one question that\n"
		"stops a design being cornered: is there production here? is there payroll? A stock model\n"
		"laid out without knowing that costs will have to be allocated over it is the model that\n"
		"gets rebuilt in month four - and the answer costs one question at the start.") },

	{ wxT("production"),
	  ibMcpText("Costs, month-end close and work in progress - and why the close is several documents."),
	  ibMcpText("PRODUCTION, COSTS AND CLOSING THE MONTH.\n"
		"\n"
		"OVERHEADS ARE KEPT APART. General production costs do not belong to any one item, so they\n"
		"are collected in a COST register of their own and distributed afterwards. Which register\n"
		"depends on the strategy (see `parallel-accounting`): for management figures an\n"
		"AccumulationRegister; where the statutory books lead, turnovers and the accounting\n"
		"register itself, accumulating against the particular account.\n"
		"\n"
		"One large register holding costs AND lots together is also a real design - everything in\n"
		"one place, one pass to resolve it - and so is keeping them separate. The trade is the\n"
		"usual one: one register means one mechanism and one order of resolution; separate ones\n"
		"are simpler each but must then be reconciled with each other.\n"
		"\n"
		"KEY: AND THAT IS THE SECOND FORK, WORTH SEEING WHOLE. The traditional shape gives every\n"
		"accounting subject its own register - lots, costs, work in progress, scrap in production,\n"
		"output, and each of them again for the second set of books. It is how large configurations\n"
		"are built, and it is why they have fifty accumulation registers: each is simple, each has\n"
		"exactly the dimensions its subject needs, and the virtual balance and turnover tables\n"
		"answer its questions directly.\n"
		"\n"
		"The other shape is ONE register of movements, with the subject as a dimension. Everything\n"
		"lands in one place and one resolution pass walks it all. What it saves is the multiplication\n"
		"- one mechanism instead of ten, one order to get right, one place where a new kind of\n"
		"movement is added.\n"
		"NOTE: What it costs is that the dimensions become a UNION: a column that only work in progress\n"
		"needs is empty on every stock row, and the queries grow conditions that the specialised\n"
		"registers did not need. It works while the subjects share most of their key - item,\n"
		"warehouse, department, order - and stops working when one of them is keyed by something\n"
		"nothing else has.\n"
		"\n"
		"NOTE: THE HONEST TEST is not elegance but the QUESTIONS: if each subject is asked about\n"
		"separately, in its own words, by different people, separate registers keep those questions\n"
		"cheap. If everything is really one question - \"what is the value sitting in production\n"
		"right now\" - one register is the shape, and ten registers are ten reconciliations.\n"
		"\n"
		"CLOSING THE MONTH IS NOT ONE OPERATION. It is a pile of regulated ones - distributing\n"
		"overheads, valuing output, adjusting actual cost, writing off what was consumed - and\n"
		"they run in an order that matters. Two ways to carry it:\n"
		" ONE document that performs all of them. Simple, and the whole month closes or does\n"
		" not.\n"
		" A DOCUMENT PER AREA, each closing its own section. More pieces, and much better in\n"
		" practice: rights can be granted per area, so the person who closes payroll is not the\n"
		" person who closes stock; a failure is localised to its own step; and a step can be\n"
		" re-run without redoing the rest. Prefer this once there is more than one person\n"
		" involved - the roles are the reason, not the tidiness.\n"
		"\n"
		"WORK IN PROGRESS IS THE PENNIES AGAIN. What is left unfinished at the close has to take\n"
		"its share of the costs, and that share is a distribution - the same arithmetic, the same\n"
		"remainder, the same decision about where the odd penny lands (see `allocation`). It is\n"
		"the part people find hardest, and the reason is not conceptual: it is that the same\n"
		"rounding problem is met for the third time, now with the numbers mattering to a tax\n"
		"authority.\n"
		"\n"
		"NOTE: THE ORDER OF THE CLOSE IS PART OF THE DESIGN, not an implementation detail. Write it\n"
		"down - in the notes of whatever performs it - because it is invisible in the code and the\n"
		"next person will reorder it while making something else work.\n"
		"\n"
		"KEY: WHAT PEOPLE SAY, AND WHAT THEY ARE ASKING FOR - because nobody dictates this in steps.\n"
		" \"CLOSE THE MONTH\" - the whole list below, in order. Ask which parts they actually run:\n"
		" a trading company skips the processing stages entirely.\n"
		" \"SPREAD THE OVERHEADS\" - compute a BASE first, then distribute in proportion to it.\n"
		" Two steps, and the base is the one they will argue about.\n"
		" \"WORK OUT THE COST\" - the graph adjustment, then the cost of what was produced. Not a\n"
		" formula, and not something a document can do at posting time.\n"
		" \"WORK IN PROGRESS DOES NOT ADD UP\" - the reconciliation step between loading costs onto\n"
		" the account and carrying them off was skipped, or ran before something it depends on.\n"
		" \"WRITE OFF THE DEFERRED EXPENSES\" - this month's slice, by days when the period is\n"
		" partial.\n"
		" \"REVALUE THE CURRENCY\" - exchange differences on what is held in foreign currency.\n"
		" \"THE SHOPS SERVE EACH OTHER\" - the mutual-output cycle below. This sentence is the\n"
		" warning that no closing order exists yet.\n"
		"\n"
		"WHAT A FULL CLOSE ACTUALLY CONTAINS, in the order it runs - useful as a checklist, because\n"
		"a close missing a step produces figures that balance and are wrong:\n"
		" 1. DEPRECIATION of fixed and intangible assets.\n"
		" 2. REVALUATION of currency holdings - the exchange differences for the month.\n"
		" 3. DEFERRED EXPENSES written off for their share of the month (a start date, an end date\n"
		" and a method: the month's slice of a yearly insurance, computed by days rather than\n"
		" by twelfths when the period does not start on the first).\n"
		" 4. INDIRECT COSTS DISTRIBUTED - which needs a BASE first: the coefficients each\n"
		" receiver's share is computed from, calculated as its own step over its own list of\n"
		" cost items.\n"
		" 5. SCRAP AND REWORK distributed the same way, on its own base.\n"
		"\n"
		"KEY: HOW INDIRECT COSTS ARE ACTUALLY SPREAD, in three facts:\n"
		" THE BASE IS CHOSEN PER COST ITEM, and the choices are few and standard: by materials,\n"
		" by wages, by output volume, by planned cost, by all direct costs, or by a NAMED LIST of\n"
		" cost items. So the base is computed once, for every one of those measures at the same\n"
		" time, keyed by cost account, department, product group - and each item then picks the\n"
		" column it distributes by. One query, six answers, no re-reading per item.\n"
		" A SHARE IS THE ORDINARY PROPORTION - this receiver's base over the total base - and the\n"
		" pennies problem is the one in `allocation`. A total base of ZERO is the case to decide\n"
		" deliberately: there is a cost and nothing to spread it over, and silently dropping it\n"
		" is how costs disappear.\n"
		" AND EVERY FIGURE EXISTS IN PARALLEL - accounting, tax, and the tax-designation split -\n"
		" so the base carries a column per measure PER SET OF BOOKS. That is why the table looks\n"
		" wide: it is six measures times the books being kept.\n"
		"\n"
		"STOP: FIXED OVERHEADS ARE NOT SPREAD LIKE THE REST - THEY GO BY NORMAL CAPACITY. Variable\n"
		"overheads follow the actual base. Fixed ones are spread against the NORMAL level of\n"
		"activity the department was planned for, and when the month produced less than that, the\n"
		"unabsorbed remainder is NOT loaded onto the goods: it goes straight to cost of sales as a\n"
		"cost of the period. Loading it onto a small output would inflate the value of stock with\n"
		"the cost of idleness, which is exactly what the rule exists to prevent.\n"
		"NOTE: So the design needs, per department: the normal capacity figure, the split of overheads\n"
		"into fixed and variable, and somewhere for the unabsorbed part to land. Missing any of the\n"
		"three produces figures that look right and overstate inventory in every bad month.\n"
		"\n"
		"SCRAP IS COST THAT NEVER BECOMES PRODUCT. It is collected on its own, and then either\n"
		"charged to whoever is responsible, or spread over the good output of the same product group\n"
		"- by the same base machinery as the overheads, on its own base. What must not happen is\n"
		"scrap quietly sitting in work in progress: it is not unfinished, it is lost, and leaving it\n"
		"there overstates what the department still holds.\n"
		"\n"
		"AND WORK IN PROGRESS IS WHAT IS LEFT AFTER ALL OF THAT - not a figure computed, but the\n"
		"residue: everything loaded onto the department minus what was carried onto finished output\n"
		"and minus scrap. Which is why the reconciliation step above matters so much: an error\n"
		"anywhere upstream lands in work in progress, where it looks like an ordinary balance.\n"
		" 6. DIRECT COSTS, stage by stage through processing.\n"
		" 7. ACTUAL COST ADJUSTED (the graph above), then the cost of finished output computed.\n"
		"\n"
		"KEY: AND THE ORDER OF THE STAGES IS COMPUTED, NOT DECLARED. Which department closes before\n"
		"which, and which processing stage before which, follows from what was actually produced\n"
		"this month - department A's output fed department B, so A closes first. So the close begins\n"
		"by ANALYSING the month's production and deriving that order, and then CHECKS it before\n"
		"using it. A hand-written order is right until somebody adds a department.\n"
		"\n"
		"* HOW THE ORDER IS DERIVED, and it is short: take the month's production as pairs of what\n"
		"WENT IN against what CAME OUT. Anything consumed that nobody produced is bought from\n"
		"outside - those lines are stage ONE. Then repeat: a line whose input is produced only by\n"
		"stages already numbered becomes the next stage. Sweep after sweep, until nothing new gets\n"
		"a number.\n"
		"STOP: AND THE STOPPING CONDITION IS THE DIAGNOSIS. If a sweep numbers NOTHING while unnumbered\n"
		"lines remain, there is no order - the remaining lines feed each other. That is the mutual\n"
		"output below, detected exactly here, and it must be reported as that rather than as a\n"
		"failure to close: the person needs to know which two departments are in the knot.\n"
		"\n"
		"STOP: AND THE CHECK EXISTS BECAUSE OF MUTUAL OUTPUT: two departments serve each other in the\n"
		"same month - the boiler house heats the workshop, the workshop repairs the boiler house -\n"
		"and no order exists at all. It is the cycle from the cost graph wearing different clothes,\n"
		"and it is resolved the same way: detect it, break it deliberately (net the mutual volumes,\n"
		"or price one side at a planned rate), and say in the result that it was broken. A close\n"
		"that loops or silently drops one side of the exchange is the classic production defect.\n"
		"\n"
		"KEY: ADJUSTING ACTUAL COST IS A GRAPH PROBLEM, and knowing that in advance is the difference\n"
		"between a week and a quarter. It is not a loop over documents in date order.\n"
		" A STATE is one place value can sit: the combination of warehouse, department, account\n"
		" and analytics that identifies it. Number them - the algorithm works on integers, not\n"
		" on structures, or every comparison costs a dictionary lookup.\n"
		" A MOVEMENT between two states is an EDGE. One query builds the whole table of them for\n"
		" the period; nothing is read document by document.\n"
		" THE COST OF A STATE is opening balance + arrivals from OUTSIDE + everything arriving\n"
		" along its incoming edges. Which means a state can be computed only when EVERY incoming\n"
		" edge already is - a topological order, and the whole calculation is that order.\n"
		" STOP: AND THE ORDER DOES NOT ALWAYS EXIST. Goods move A to B and B back to A within the\n"
		" month, and the graph has a CYCLE: neither state can be computed first, and a naive\n"
		" pass either loops forever or silently leaves cost behind.\n"
		"\n"
		" * BREAKING ONE IS A SMALL, EXACT ALGORITHM, and it is worth knowing rather than\n"
		" re-deriving: walk the graph depth-first, CARRYING THE PATH - the vertices visited and,\n"
		" for each, the edge that got there. When the next vertex is already ON the path, the\n"
		" cycle is the stretch of path from its first appearance plus the edge that closes it.\n"
		" Find the SMALLEST quantity among that cycle's edges, and subtract it from EVERY edge of\n"
		" the cycle. At least one edge becomes zero - so the cycle is gone - and the arithmetic\n"
		" is untouched, because going round the loop the same amount was removed from what came\n"
		" in and from what went out. Then mark the vertex as already explored so the same cycle\n"
		" is not taken apart twice, and carry on.\n"
		" NOTE: Two details that are easy to lose: the path has to be COPIED at each branch (a shared\n"
		" one leaks vertices between sibling branches), and the closing edge must be included in\n"
		" the minimum - it is often the small one.\n"
		"\n"
		" Nothing in the accounting says this is needed; it emerges from the data, and it is the\n"
		" single hardest part of the whole close.\n"
		"\n"
		"KEY: AND THERE IS A COMPLETELY DIFFERENT WAY TO SOLVE THE SAME GRAPH - WORTH KNOWING BEFORE\n"
		"CHOOSING THE ONE ABOVE. Instead of walking it in order, write it as SIMULTANEOUS EQUATIONS\n"
		"and solve them all at once:\n"
		" A COST CENTRE is the key cost is computed for - organisation, warehouse, kind of stock,\n"
		" item, characteristic, accounting section. Each centre's unit cost is one UNKNOWN.\n"
		" EACH CENTRE GETS A BALANCE EQUATION: opening value + everything that arrived (from\n"
		" outside, and from other centres at THEIR unknown costs) = what left (quantity times\n"
		" THIS centre's unknown cost) + closing value.\n"
		" Arcs that cannot exist are struck out - televisions moved within a warehouse do not\n"
		" become coffee makers - and what remains is a sparse system with one equation per\n"
		" centre.\n"
		" SOLVE IT. Every cost comes out together.\n"
		"\n"
		"STOP: AND THIS IS WHY IT MATTERS: A CYCLE IS NO LONGER A PROBLEM. Mutual transfers, departments\n"
		"serving each other, goods going back and forth - all of it is just coefficients in the\n"
		"matrix. No topological order, no cycle detection, no breaking. The hardest part of the\n"
		"traversal approach simply does not arise.\n"
		"\n"
		"THE METHOD IS CHOSEN BY THE LEFT-HAND SIDE, which is a neat property: value the CLOSING\n"
		"stock as quantity times the unknown, and the system yields the monthly weighted average;\n"
		"value it by FIFO from the actual receipts - a known number - and the same system yields\n"
		"FIFO. One mechanism, two accounting policies, differing in one term.\n"
		"\n"
		"NOTE: THE COSTS: it needs a solver and it is a MONTHLY, batch answer - there is no meaningful\n"
		"cost for a single document mid-month, only a provisional one. And a badly formed system\n"
		"(a centre with no incoming value, a quantity that went out where nothing came in) has to be\n"
		"diagnosed as a system, which is harder to explain to an accountant than \"lot 17 ran out\".\n"
		"NOTE: AND KEEP LAST MONTH'S LOTS APART. Rolling the opening balance into one averaged lot makes\n"
		"FIFO impossible from that point on and quietly changes the figures - the older editions of\n"
		"one well-known system did exactly this and it was treated as a defect.\n"
		" TWO SETS OF BOOKS SHARE THE ALGORITHM AND NOT THE QUERIES: management and statutory\n"
		" figures are read by their own queries and then walked by the same graph code. Writing\n"
		" the traversal twice is how the two quietly start disagreeing.\n"
		"\n"
		"AND IT IS A DOCUMENT, NOT A PROCESSOR (see `shapes`): it produces movements that somebody\n"
		"has to be able to see, reverse and re-run for a period, and \"when was the cost adjusted\"\n"
		"is a question with an answer.\n"
		"\n"
		"KEY: AND THIS MACHINE IS NOT ABOUT COST. What it actually does is carry a VALUE ALONG A PATH\n"
		"through states, splitting and merging it as the path branches - and that shape turns up all\n"
		"over an accounting system:\n"
		" overheads pushed down through a hierarchy of departments, each passing on what it\n"
		" received plus its own;\n"
		" services re-invoiced along a chain of parties, where the cost has to arrive at whoever\n"
		" finally consumed it;\n"
		" a mark-up applied stage after stage through processing, so the last stage knows what\n"
		" the first one contributed;\n"
		" any \"distribute this in proportion, then distribute the result again\".\n"
		"Recognise it by the sentence \"the amount has to travel\" - the moment a distribution feeds\n"
		"ANOTHER distribution, it is this algorithm and not a formula. Build it once, keep the\n"
		"traversal separate from the queries that fill the graph, and the second use costs a query\n"
		"instead of a rewrite. Production is simply where it is met first and where it is\n"
		"unavoidable.\n"
		"\n"
		"HOW WORK IN PROGRESS ACTUALLY MOVES, because \"it takes its share\" hides the mechanism.\n"
		"Costs are loaded onto the work-in-progress account as they are incurred - debited to it,\n"
		"again and again, from stock and from everywhere else. At the close the account is\n"
		"credited off: the accumulated amount is carried onto the cost accounts it belongs to.\n"
		"Then the close has to LOOK at what it has done - the loading and the carrying off do not\n"
		"land equal, deltas appear between the accounts - and level them. Only after that does the\n"
		"per-item distribution of actual cost run, position by position.\n"
		"\n"
		"So the sequence is: accumulate, carry off, RECONCILE, then distribute. The reconciliation\n"
		"step is the one that gets left out, because nothing demands it until the balance does not\n"
		"balance - by which time the distribution has already run on figures that were not final.\n"
		"\n"
		"NOTE: NAME ACCOUNTS BY WHAT THEY ARE, NEVER BY THEIR NUMBER. A chart of accounts is\n"
		"legislation: the numbering differs by country and changes with the law, while the ROLES\n"
		"are the same everywhere - the account work in progress accumulates on, the account stock\n"
		"is held on, the accounts production costs are gathered into. Think and write in those\n"
		"terms, and take the actual accounts from the chart the configuration has (a predefined\n"
		"account is exactly how code holds onto one - see `predefined`). A mechanism with numbers\n"
		"written into it is wrong the first time it meets another chart, and wrong silently: the\n"
		"posting still goes somewhere.\n"
		"\n"
		"* AND THE CHAIN ENDS AT THE FINANCIAL RESULT. The last document of the period closes the\n"
		"settlements and determines what was earned - everything above exists to make that figure\n"
		"correct. It is called different things in different jurisdictions and the concept is\n"
		"everywhere; if a design has no such terminal step, the question to ask is not \"do you\n"
		"need one\" but \"where do you close the period today\".") },

	{ wxT("user-formulas"),
	  ibMcpText("Letting people write the rule: indicators as data, expression evaluated at run time."),
	  ibMcpText("WHEN THE RULE BELONGS TO THE PERSON, NOT TO THE PROGRAMMER.\n"
		"\n"
		"THE SIGNAL: \"the bonus is worked out differently for each scheme\", \"the discount depends\n"
		"on the manager's own conditions\", \"we change how this is calculated a few times a year\".\n"
		"Written as code, every change is a developer, a release and a wait - and the rule is not\n"
		"actually the developer's to know.\n"
		"\n"
		"THE SHAPE IS THREE PARTS, and it is the same in payroll, in pricing, in bonuses, in\n"
		"scoring and in cost allocation:\n"
		" INDICATORS - a catalogue of the things a formula may refer to, each with a name and a\n"
		" type: hours worked, revenue of the department, plan fulfilment, tariff, coefficient.\n"
		" They are DATA, so a new one is added without touching anything.\n"
		" VALUES OF INDICATORS - a register keyed by indicator plus whoever it is about plus the\n"
		" period it holds for. This is where the numbers come from when a formula runs.\n"
		" THE FORMULA ITSELF - a text stored beside the scheme, written in the platform's own\n"
		" script and EVALUATED at run time with the indicators bound as variables. The engine\n"
		" that runs modules is the same engine; there is no second expression language, and no\n"
		" parser to write.\n"
		"\n"
		"* WHAT THIS BUYS is that the rule stops being a release. A scheme is a row, its formula is\n"
		"a string, and the person who owns the rule edits it.\n"
		"\n"
		"NOTE: AND WHAT IT COSTS, said before it is built: a formula written by a person can be WRONG -\n"
		"a name that does not exist, a division by zero, a reference to an indicator nobody filled\n"
		"in. So it needs the same three things any code needs, exposed to a non-programmer: a CHECK\n"
		"when it is saved (compile it, do not wait for the payroll run), a clear error naming the\n"
		"scheme and the indicator rather than a stack, and a TEST - let them run it against one\n"
		"person for one month and see the number.\n"
		"NOTE: Do not let a formula reach the database or change anything. It computes a value from the\n"
		"indicators it was given, and nothing else - which is also what makes it safe to run\n"
		"thousands of times in a loop.\n"
		"\n"
		"NOTE: AND KEEP THE INDICATORS COARSE. The temptation is to expose everything so any formula is\n"
		"possible; the result is a language nobody can use and every formula reads the whole\n"
		"database. Publish the dozen quantities people actually name when they describe the rule out\n"
		"loud - that list is short, and it is the real specification.") },

	{ wxT("payroll"),
	  ibMcpText("What payroll needs to work - and which of it this platform does not have."),
	  ibMcpText("PAYROLL IS ITS OWN WORLD, and the first honest thing to say is what is missing here.\n"
		"\n"
		"STOP: THIS PLATFORM HAS NO CALCULATION REGISTER AND NO CHART OF CALCULATION TYPES. In systems\n"
		"built for payroll those two metatypes carry the whole machinery below - period of\n"
		"validity, displacement, base, recalculation - and it is done FOR you. Here they do not\n"
		"exist (`metadata_accepts` on the configuration is the check). So payroll is buildable, but\n"
		"every mechanism named below is yours to build out of registers and documents. Say that\n"
		"plainly to whoever asks before agreeing a date.\n"
		"\n"
		"WHAT THE MACHINERY ACTUALLY IS, in case it is being built or judged:\n"
		" A KIND OF ACCRUAL is a thing in its own right - salary, bonus, sick leave, holiday,\n"
		" each with its own formula. They are data, not code branches: somebody adds one.\n"
		" TWO PERIODS, ALWAYS. The period the money is FOR (a holiday covering the last week of\n"
		" March) and the period it is REGISTERED in (the March payroll run). Every figure needs\n"
		" both, and reports ask for one or the other depending on the question - taxes by\n"
		" registration, averages by validity.\n"
		" DISPLACEMENT: some accruals push others out for the days they cover. Holiday displaces\n"
		" salary for those days - the salary is not reduced by hand, it is CROWDED OUT by a\n"
		" period that overlaps it. Without this, a month with any absence is wrong by\n"
		" construction.\n"
		" BASE: some accruals are computed FROM others. A contribution is a percentage of a set\n"
		" of accruals; average earnings are computed over a window of previous ones. So a kind\n"
		" of accrual declares WHICH kinds form its base, and the order of calculation follows\n"
		" from that - the same dependency ordering as the cost graph in `production`.\n"
		" REVERSAL: backdated changes do not edit old records, they STORNO them - a negative\n"
		" entry in the new period cancelling the old figure, and a fresh entry replacing it. The\n"
		" filed reports stay filed and the correction is visible as a correction.\n"
		"\n"
		"* AND WHAT IS DECIDED BY LAW IS DECIDED BY DATE, more than anywhere else in accounting.\n"
		"Contribution rates, tax thresholds, indexation rules, minimum wage - each changed on a\n"
		"named day, and last year's payroll must still recompute as it was filed. Two habits from\n"
		"systems that survive this: every legislative boundary is a NAMED constant (a function or a\n"
		"register entry called \"the day rule N came in\"), never a date literal inside a formula;\n"
		"and where the law itself was ambiguous, the CHOICE of interpretation is stored as a\n"
		"setting, so an organisation that read it differently is not a fork of the code.\n"
		"\n"
		"NOTE: AND THE SCHEDULE IS A SEPARATE MECHANISM AGAIN: working-time norms per calendar per\n"
		"month, before any of the above can compute a day. It is the first thing to build and the\n"
		"one most often left until last.") },

	{ wxT("vat"),
	  ibMcpText("Indirect tax: the designation that follows every line, and the earlier of two events."),
	  ibMcpText("VAT IS TWO MECHANISMS, AND BOTH ARE EASY TO MISS UNTIL AN AUDIT.\n"
		"\n"
		"KEY: FIRST, A TAX DESIGNATION TRAVELS WITH EVERY LINE. Not the rate - the PURPOSE: is this\n"
		"purchase for taxable activity, for exempt activity, for something outside the tax\n"
		"altogether, for non-business use. It decides whether the input tax may be recovered at all,\n"
		"and it has to be carried on the movement, through stock, through cost, through the\n"
		"write-off - because goods bought for one purpose get used for another, and the correction\n"
		"is computed from what they were bought AS.\n"
		"NOTE: Bolting it on later means every register gains a dimension and every historic row has an\n"
		"empty one. This is the dimension worth having from the first day in any base that will\n"
		"ever see indirect tax.\n"
		"\n"
		"KEY: SECOND, THE LIABILITY ARISES ON THE EARLIER OF TWO EVENTS - the shipment or the money -\n"
		"and which came first is knowable only by looking at both. So the shape is: each side\n"
		"records what it caused as EXPECTED, and a later step decides which event was first and\n"
		"CONFIRMS one of them. Hence the pairs of registers - expected against confirmed, for sales\n"
		"and for purchases - and the reconciliation between them, which is where the tax invoices\n"
		"are matched up.\n"
		"NOTE: Computing tax at the moment of the sale alone is right in the simple case and silently\n"
		"wrong the first time somebody prepays.\n"
		"\n"
		"THE RATE IS A KIND, NOT A NUMBER (`shapes`): an enumeration member whose current percentage\n"
		"comes from a function or a dated register, so old documents keep old figures. And the tax's\n"
		"own parameters - registration status, special regimes, thresholds - are read AS OF THE\n"
		"DOCUMENT'S DATE for the same reason.\n"
		"\n"
		"NOTE: AND THE ARITHMETIC HAS TWO DIRECTIONS: tax INCLUDED in the price is extracted from it,\n"
		"tax ON TOP is added to it, and the two produce different totals from the same numbers\n"
		"(`printing`). Which one applies is a property of the agreement or the price type - never a\n"
		"constant in the calculation.\n"
		"\n"
		"NOTE: THE PRINTED TAX DOCUMENT IS ITS OWN OBJECT, with its own number, its own state (issued,\n"
		"registered, rejected) and its own reporting. It is created FROM the sale, and it is not the\n"
		"sale - which matters the moment one invoice covers several deliveries, or one delivery is\n"
		"invoiced in parts.") },

	{ wxT("cash-flow"),
	  ibMcpText("Money: what is planned, what is promised and what actually moved."),
	  ibMcpText("PLANNED MONEY AND REAL MONEY ARE DIFFERENT REGISTERS.\n"
		"\n"
		"Everybody starts with one balance of cash and finds out within a month that the questions\n"
		"are three, not one:\n"
		" WHAT WE HAVE - the actual balance of each account and till. Moved only by payments that\n"
		" happened.\n"
		" WHAT IS COMING AND GOING - expected receipts and expected payments, each with a DATE\n"
		" somebody promised. This is the cash-flow forecast, and it is what a director asks for\n"
		" when they say \"will we cover the salaries on Friday\".\n"
		" WHAT IS SPOKEN FOR - money reserved against approved requests to spend, which is not\n"
		" the same as spent. A request approved today reduces what may be committed tomorrow\n"
		" while the balance is untouched.\n"
		"Three questions, three registers - and a fourth for the CASH-FLOW STATEMENT, which\n"
		"classifies real movements by activity and item, because \"where did the money go\" is asked\n"
		"of history rather than of balances.\n"
		"\n"
		"* THE REQUEST TO SPEND IS A DOCUMENT, and its life is the mechanism: raised, approved,\n"
		"paid, and CLOSED - closed being the state that releases what was reserved and is never\n"
		"automatic. Requests that are neither paid nor closed accumulate and quietly eat the\n"
		"available balance, so the closing document (or the closing job) is part of the design, not\n"
		"an afterthought.\n"
		"\n"
		"NOTE: AND EXPECTED MONEY HAS TWO DATES: when it was promised and when it is expected. Reports\n"
		"are built on the second, disputes on the first, and a register carrying only one of them\n"
		"answers half the questions asked of it.\n"
		"\n"
		"NOTE: THE SIGNAL THAT ANY OF THIS IS WANTED: \"payment calendar\", \"cash gap\", \"who approves the\n"
		"payment\", \"we must not pay more than we planned\". None of them is answered by a balance of\n"
		"cash, and all of them are cheap while the money registers are being designed.") },

	{ wxT("roles"),
	  ibMcpText("Cutting roles: a job is not a role, and rights combine rather than nest."),
	  ibMcpText("HOW RIGHTS ARE ACTUALLY CUT UP.\n"
		"\n"
		"STOP: THE BEGINNER'S SHAPE IS ONE ROLE PER PERSON - \"Ivanov's rights\" - and it collapses the\n"
		"first time two people do overlapping jobs. The working shape is TWO KINDS OF ROLE, handed\n"
		"out together:\n"
		" A JOB ROLE, named after the work: storekeeper, cashier, buyer, salesperson, accountant,\n"
		" payroll clerk, treasurer. It carries what that job needs all day and nothing else. This\n"
		" is the same cut as a section (`sections`) - and that is not a coincidence: what a\n"
		" person may do and where they work are the same question asked twice.\n"
		" A PERMISSION ROLE, named after ONE capability: may edit items, may edit counterparties,\n"
		" may edit the organisation, may change a document's status, may run external processing,\n"
		" may see everything. Small, additive, granted on top.\n"
		"A person then holds a job plus the handful of permissions they were trusted with, and the\n"
		"question \"who may edit a counterparty\" has an answer that is a list rather than an audit.\n"
		"\n"
		"* AND ONE BASE ROLE EVERYBODY HAS - the right to start the application at all, to read the\n"
		"common classifiers, to open their own settings. Without it every job role repeats the same\n"
		"twenty entries, and the twenty-first is forgotten in one of them.\n"
		"\n"
		"NOTE: EDITING REFERENCE DATA IS ITS OWN PERMISSION, separately from using it. Everybody selects\n"
		"items and counterparties; almost nobody should create them, because a duplicate\n"
		"counterparty splits a balance that then has to be merged by hand. This single split -\n"
		"choose freely, create by permission - prevents more mess than any other rule here.\n"
		"\n"
		"* AN ATOMIC ROLE POINTS EITHER WAY, and both are ordinary:\n"
		" GRANTING - \"may edit items\", \"may change a status\", \"may run external processing\".\n"
		" EXEMPTING - \"do not apply the automatic filter\", \"see every organisation\", \"post into\n"
		" a closed period\". The restriction is the normal state and the role lifts it. Better\n"
		" than a flag on the user record, because it reads correctly and can be listed: who is\n"
		" exempt is a query, not an inspection.\n"
		"\n"
		"STOP: AND DO NOT SHRED ACCESS BEFORE SOMEBODY ASKS. Atomic roles are for a division that\n"
		"actually exists in the conversation - \"the sales people must not create counterparties\",\n"
		"\"only the chief accountant closes a period\". When nothing in what the person describes\n"
		"suggests they want to split a right, the job roles alone are correct and complete. Every\n"
		"speculative atomic role is another tick box on every user, which somebody will forget to\n"
		"set - and the symptom is not a refusal, it is a person quietly unable to do their job.\n"
		"Listen for the division; build it then.\n"
		"\n"
		"NOTE: AND ADMINISTRATION IS NOT A JOB. \"Full rights\" exists for one or two people and is not\n"
		"how a head of department is described - they are a job role plus permissions, or the system\n"
		"has no answer to what anybody may actually do.") },

	{ wxT("pricing"),
	  ibMcpText("Prices, discounts and the four things a price is measured in."),
	  ibMcpText("A PRICE IS NEVER JUST A NUMBER.\n"
		"\n"
		"It is a number PLUS four facts, and every one of them can differ from the document it is\n"
		"about to land in: the PRICE TYPE it belongs to (wholesale, retail, planned cost), the\n"
		"CURRENCY, the UNIT it is quoted per, and whether it INCLUDES TAX. Store all four beside the\n"
		"figure - a price register keyed by item and price type, kept with periodicity so yesterday's\n"
		"price is still answerable (`shapes`).\n"
		"\n"
		"KEY: AND EVERY ONE OF THE FOUR MEANS A RECALCULATION, not a relabelling. Change the document\n"
		"to another currency and the price converts by rate AND multiplicity; change the unit and it\n"
		"scales by the packing coefficient; switch \"price includes tax\" and it is extracted or added\n"
		"(`printing` has the arithmetic, and the two directions do not produce the same number).\n"
		"Leaving the figure and changing the label is how a base ends up selling at a tenth of its\n"
		"price.\n"
		"\n"
		"* ROUNDING IS TO A STEP, NOT TO PENNIES. Retail prices are wanted at 0.05, at 0.10, at the\n"
		"whole unit, at fifty - so rounding takes an ORDER: divide by the step, take whole steps,\n"
		"multiply back. And it takes a direction: ALWAYS UP is a separate switch, because a shop\n"
		"that rounds its sale prices down loses the difference on every line. The step and the\n"
		"direction belong to the price type, not to the code.\n"
		"\n"
		"DISCOUNTS COME FROM SEVERAL PLACES AT ONCE, and that is the whole difficulty: the\n"
		"counterparty's agreement, a discount card, a promotion running on a date, a volume\n"
		"threshold on this line, a sales condition attached to the item. Build the list of\n"
		"POSSIBLE GRANTERS for the document first, then ask each what it offers, then decide -\n"
		"largest wins, or they add up, or one excludes the others. That rule is a decision to be\n"
		"asked about, and it is the first thing that will be changed after go-live.\n"
		"\n"
		"NOTE: A PROMOTION IS DATED AND A CARD IS NOT. \"Special offer\" means \"on this date, for this\n"
		"item, for this buyer\", so it is read as of the document's date like a rate is - never as\n"
		"of today, or last month's invoices reprice themselves when the promotion ends.\n"
		"\n"
		"NOTE: AND SELLING BELOW COST IS A QUESTION SOMEBODY WILL ASK. Keeping a planned cost per item\n"
		"lets the document compare its total against it and warn - or refuse, by a right rather\n"
		"than by a constant. Cheap to add while the price mechanism is being built, awkward to\n"
		"retrofit into every sales document afterwards.") },

	{ wxT("stock-control"),
	  ibMcpText("Refusing to go negative: free balance, reserves, and who may override it."),
	  ibMcpText("CHECKING THERE IS ENOUGH BEFORE WRITING THE MOVEMENT.\n"
		"\n"
		"KEY: THE FIGURE TO CHECK IS THE FREE BALANCE, NOT THE BALANCE. What is on the shelf minus\n"
		"what is already promised: reserved for orders, allocated to production, being shipped.\n"
		"Checking the raw balance lets a document consume goods that another document has already\n"
		"spoken for, and the shortage surfaces later - at the far end, where nobody can tell which\n"
		"of the two took it.\n"
		"\n"
		"STOP: THE CHECK RUNS PRIVILEGED, and this catches everyone once. The person posting may be\n"
		"allowed to see only their own warehouse, or only their own department's rows - so a check\n"
		"run with their rights reads a SMALLER balance than exists and refuses a perfectly good\n"
		"document. Worse, in the other direction: a filter that hides other people's reserves lets\n"
		"the same goods be promised twice. The control must see everything; the person must not.\n"
		"\n"
		"* WHETHER IT REFUSES IS A PROPERTY OF THE PLACE, NOT OF THE CODE. Some warehouses may not\n"
		"go negative; some may, deliberately - a shop counting stock once a week, a production line\n"
		"where the paperwork lags the physical movement. So the switch lives on the WAREHOUSE (or on\n"
		"the organisation), and where it is off the shortage is still SAID and no longer refuses.\n"
		"Reporting and refusing are two decisions, and only one of them is configurable.\n"
		"\n"
		"AND THE MESSAGE HAS TO BE ACTIONABLE - what was asked for, what is available, HOW MUCH IS\n"
		"MISSING, and the item named with its characteristic and unit. \"Insufficient stock\" tells a\n"
		"person to go and find out; \"needed 10 pcs, free 6 pcs, short 4 pcs of <item>\" tells them\n"
		"what to do. Every shortage in the document is reported, not the first one.\n"
		"\n"
		"NOTE: SERIES AND CHARACTERISTICS MAKE IT TWO CHECKS. Enough of the item overall does not mean\n"
		"enough of THAT series, and a reserve made without naming a series cannot be matched against\n"
		"an issue that names one. Decide which of the two the base actually keeps - and check at\n"
		"exactly that level, no finer (`posting`: a dimension filled where it should be empty splits\n"
		"the balance).\n"
		"\n"
		"NOTE: AND THE SAME MECHANISM ANSWERS \"MAY I PROMISE THIS\": an order reserving goods is checked\n"
		"against the free balance, and an order exceeding what was ordered upstream is checked\n"
		"against the order. Control of quantity is one idea applied at several moments, not a\n"
		"feature of the shipping document.") },

	{ wxT("not-mine"),
	  ibMcpText("Goods that are not yours, and yours that are not with you - commission and safekeeping."),
	  ibMcpText("SOMEBODY ELSE'S GOODS IN YOUR WAREHOUSE, AND YOURS IN THEIRS.\n"
		"\n"
		"THE SIGNAL: \"we take it on commission\", \"we hand it over for sale\", \"it is stored with us\n"
		"but it is not ours\", \"we sent materials out for processing\". Four situations, one\n"
		"question: WHOSE is it, and WHERE is it. Those are two different facts and both have to be\n"
		"kept.\n"
		"\n"
		"STOP: THE MISTAKE IS TO MIX IT WITH YOUR OWN STOCK. Goods that are not yours must not enter\n"
		"your balances, your cost calculation or your valuation - they are not an asset of yours,\n"
		"and averaging them into your cost quietly corrupts every figure downstream. In the books\n"
		"they belong off-balance; in the operational registers they are told apart by a STATUS on\n"
		"the lot - bought, taken on commission, held for safekeeping - or by a register of their\n"
		"own.\n"
		"\n"
		"AND THE TWO AXES ARE INDEPENDENT, which is what decides the design:\n"
		" YOURS, WITH YOU - ordinary stock.\n"
		" YOURS, ELSEWHERE - handed to an agent to sell, sent out for processing, in transit.\n"
		" Still your asset, still in your cost, but not available to sell from the shelf. A\n"
		" register of TRANSFERRED lots, separate from the warehouse one.\n"
		" NOT YOURS, WITH YOU - taken on commission, accepted for safekeeping. In your building,\n"
		" absent from your assets.\n"
		" NOT YOURS, ELSEWHERE - not your problem, and worth saying so when somebody asks for it.\n"
		"\n"
		"* SELLING SOMEBODY ELSE'S GOODS CREATES AN OBLIGATION, not a profit. The movement that\n"
		"matters is not \"stock went down\" but \"we now owe the owner for what was sold\" - a separate\n"
		"record, written at the moment of sale, from which the report to the owner and the\n"
		"settlement are later built. And it has to be written for goods sold BOTH from your\n"
		"warehouse and from wherever else they were sitting, which is the case that gets forgotten.\n"
		"\n"
		"NOTE: THE OWNER'S REPORT IS ITS OWN DOCUMENT, in both directions: the one you send for what you\n"
		"sold of theirs, and the one you receive telling you what an agent sold of yours. The second\n"
		"is what finally turns your transferred lot into a sale - until it arrives, the goods are\n"
		"still yours and still unsold, however long ago they physically left.\n"
		"\n"
		"NOTE: AND THE RETURN PATHS ARE REAL: goods come back from an agent unsold, and goods you took\n"
		"on commission go back to their owner. Neither is a sale in reverse - both are movements\n"
		"between the states above, and each needs its own operation kind so the write-off knows\n"
		"which register it is taking from (`lot-accounting`).") },

	{ wxT("posting"),
	  ibMcpText("Writing a document's movements: sets, partial replacement, locks and refusals."),
	  ibMcpText("HOW MOVEMENTS ARE ACTUALLY WRITTEN.\n"
		"\n"
		"A RECORD SET IS THE UNIT, NOT A RECORD. Movements are collected into a table while the\n"
		"posting works, and written ONCE per register at the end. Writing them one at a time turns\n"
		"a document of two hundred lines into two hundred round trips, and leaves the register half\n"
		"filled if something fails in the middle.\n"
		"\n"
		"AND WRITING A SET REPLACES EVERYTHING THAT RECORDER HAD. That is what makes re-posting\n"
		"safe - and it is also the trap:\n"
		"\n"
		"STOP: TWO MECHANISMS WRITING TO ONE REGISTER FROM ONE DOCUMENT WILL ERASE EACH OTHER. The\n"
		"document posts its own movements; later the cost adjustment writes its own into the same\n"
		"register under the same recorder - and replaces the lot. Nothing raises; the first set is\n"
		"simply gone.\n"
		"* THE CURE IS A MARK ON THE RECORD: an attribute saying which mechanism made it. Then a\n"
		"mechanism re-posting itself READS the set, removes only the rows carrying ITS mark, adds\n"
		"its new rows with the mark set, and writes. Each half owns its own records, and the two\n"
		"stop overwriting each other. Worth deciding the day a second writer appears - the symptom\n"
		"otherwise is movements that vanish depending on the order things were run in.\n"
		"\n"
		"LOCK BEFORE YOU READ WHAT YOU ARE ABOUT TO CHANGE. Balances read for a decision - are there\n"
		"enough lots, what is the remaining amount - can change between the reading and the write,\n"
		"and the document then posts on figures that were true a second ago. Take the lock over the\n"
		"table and the values being touched, then read. This is invisible in testing, where nobody\n"
		"else is working, and shows up as impossible balances on a busy day.\n"
		"\n"
		"A PROBLEM IS A REFUSAL PLUS A MESSAGE, NOT AN EXCEPTION. Posting carries a refusal flag\n"
		"through the whole routine: each check that fails says WHAT is wrong, in which LINE, with\n"
		"the item named, and sets the flag. At the end the document does not post - but the person\n"
		"has every problem at once rather than the first one, fixed, then the second one. An\n"
		"exception thrown from the middle also leaves the write transaction to be unwound by\n"
		"somebody else, which is its own class of trouble.\n"
		"\n"
		"NOTE: AND CHECK THE HEADER BEFORE DOING ANY WORK. Organisation, warehouse, date, currency -\n"
		"the handful of values everything downstream depends on. Discovering on line 180 that the\n"
		"warehouse is empty wastes the work and produces a message about the wrong thing.\n"
		"\n"
		"KEY: THE ORDER OF A REAL POSTING, and it is the same in every document worth copying:\n"
		" 1. CLEAR THE OLD MOVEMENTS when re-posting.\n"
		" 2. GATHER, in queries: the header's context, then ONE query per tabular section bringing\n"
		" everything those lines need - accounts, policies, item settings, whatever is reached\n"
		" through a reference. Nothing is fetched inside the loops that follow.\n"
		" 3. CHECK EVERYTHING - the header, each section, the agreement against the currency, the\n"
		" balances, the rights - accumulating refusals and messages rather than stopping at the\n"
		" first.\n"
		" 4. ONLY THEN WRITE, in one block, guarded by \"if nothing refused\". A document that has\n"
		" written half its registers and then found a problem has to unwind them, and the\n"
		" unwinding is where the defects live.\n"
		"\n"
		"* AND SOME CHECKS ARE ABOUT THE PERSON, NOT THE DATA: may this user sell at this price,\n"
		"give this discount, post into a closed period, ship beyond what was ordered. They belong\n"
		"with the other checks in step 3 - as RIGHTS, so the answer can differ per role, rather than\n"
		"as a constant that has to be edited when the sales manager is allowed a bigger discount.\n"
		"\n"
		"NOTE: WHAT TRAVELS BETWEEN THE PARTS: one structure carrying the posting's context - the\n"
		"document, its header values, the policy read for that date, the tables being filled, the\n"
		"registers being written. Passing eleven arguments through six routines is how the seventh\n"
		"ends up with a stale copy of one of them.\n"
		"\n"
		"NOTE: ONE DOCUMENT WRITES INTO MANY REGISTERS, and that is normal rather than a smell. A single\n"
		"sale moves stock, consumes lots, changes what the counterparty owes, records the sale for\n"
		"analysis, notes the tax event, writes the double entry, and leaves an index for later\n"
		"returns. Each of those answers a DIFFERENT question, and folding them into one register to\n"
		"be tidy produces something that answers none of them well.\n"
		"* What keeps it manageable is that the document does not know how: it hands its prepared\n"
		"tables to the mechanism that owns each register - stock, settlements, tax, lots - and each\n"
		"writes its own. A document containing the write-off rule, the settlement rule and the tax\n"
		"rule inline is the same code copied into the next document a month later.\n"
		"\n"
		"STOP: AND FILLING A DIMENSION \"JUST IN CASE\" IS A DEFECT, not caution. A dimension is filled\n"
		"only when accounting is actually KEPT by it: the series only where lots are tracked by\n"
		"series, the warehouse only where lots are tracked per warehouse, the order only for work\n"
		"done to order. Otherwise it is left EMPTY - and empty is a legitimate, meaningful value:\n"
		"the balance held without a series.\n"
		"\n"
		"Why it matters more than it looks: a dimension filled where it should be empty SPLITS the\n"
		"balance. The receipt lands under one key and the write-off looks under another, so the\n"
		"lots are \"not there\" while the register plainly shows them - and the report totals\n"
		"correctly, because the sum is right and only the breakdown is wrong. It is the single most\n"
		"common cause of \"there is stock but it will not write off\".\n"
		"\n"
		"* WHICH MEANS THE SET OF DIMENSIONS TO FILL IS DECIDED BY THE POLICY AND THE OPERATION\n"
		"KIND, read for the document's own date - not written as a fixed list in each posting\n"
		"routine. The same document posts different keys in a base that keeps lots per warehouse and\n"
		"in one that does not, and both are correct.") },

	{ wxT("lot-accounting"),
	  ibMcpText("Lots and cost: one shared write-off, and cost resolved over a graph of movements."),
	  ibMcpText("LOT ACCOUNTING.\n"
		"\n"
		"STOP: FIRST DECIDE WHETHER YOU NEED LOTS AT ALL - there is a second design, and it is often\n"
		"the right one. Two ways to answer \"what did this cost\":\n"
		" KEEP THE LOTS. Every receipt is a lot, the write-off chooses which lots a quantity\n"
		" comes out of, and cost follows the individual lot. Necessary when the ANSWER has to\n"
		" name a source: FIFO required by the accounting policy, expiry dates, serial numbers,\n"
		" goods that are somebody else's, a return that must go back at the price it left at.\n"
		" KEEP QUANTITY AND AMOUNT ONLY. The register carries how much and how much it is worth;\n"
		" a write-off takes the AVERAGE - amount divided by quantity - and no lot is ever\n"
		" identified. \"Which delivery was this from\" becomes a question with no answer, and for\n"
		" most trading and manufacturing bases nobody ever asks it.\n"
		"\n"
		"* THE SECOND IS DRAMATICALLY CHEAPER, and not only in code: no lot dimension, no tree of\n"
		"lots to resolve per document, a fraction of the movements, and backdated entry costs a\n"
		"RECALCULATION rather than a re-selection of which lots each later document consumed. The\n"
		"whole apparatus below - the repayment loop, the shortage messages, the ordering - exists\n"
		"only in the first design.\n"
		"NOTE: Its own difficulty is real but smaller: the average is only final once the month's\n"
		"receipts are all in, so a figure shown during the month is provisional and the close\n"
		"restates it. That is the same provisional-figure problem as below, without the graph.\n"
		"\n"
		"NOTE: SO ASK, BEFORE ANYTHING: \"do you need to know which delivery a particular item came\n"
		"from?\" and \"does your policy say FIFO?\". Two questions, and they decide a month of work.\n"
		"Building lots because they sound more accurate, into a base that only ever reports\n"
		"averages, is the most expensive default in this whole corpus.\n"
		"\n"
		"WHAT FOLLOWS IS THE FIRST DESIGN.\n"
		"\n"
		"IT IS ONE MODULE, NOT A HABIT REPEATED IN EVERY DOCUMENT. The write-off - choosing which\n"
		"lots a quantity comes out of, in which order, at what cost - is written ONCE in a common\n"
		"module, and documents reach it from there (directly, or through a subscription so that\n"
		"posting any of them goes through the same code). Every document that implements its own\n"
		"write-off is another place where the rule can differ, and they will differ: the second\n"
		"one is written by copying the first and then quietly diverging.\n"
		"\n"
		"THE WRITE-OFF IS WHERE THE PENNIES APPEAR. Splitting a cost across the lots a quantity is\n"
		"taken from is exactly the distribution problem in `allocation` - read it before writing\n"
		"the arithmetic, and decide there whether the remainder is placed or absorbed.\n"
		"\n"
		"KEY: AND COST IS NOT KNOWN WHEN THE MOVEMENT HAPPENS. This is the part that surprises\n"
		"people: the actual cost of what left the warehouse today may depend on an invoice that\n"
		"arrives next week, on freight allocated afterwards, on a transfer that has not been\n"
		"priced yet. So the adjustment of actual cost is not a per-document calculation at all -\n"
		"it is resolved over the WHOLE CHAIN, afterwards.\n"
		"\n"
		"The shape to think in is a GRAPH. The vertices are the states goods pass through, the\n"
		"edges are the MOVEMENTS between them - receipt, transfer, assembly, return - and cost\n"
		"flows along that graph. A write-off (a sale, an issue) is a TERMINAL vertex: it is where\n"
		"the route ends, the culmination of everything upstream, and it is priced only once the\n"
		"route leading to it is priced.\n"
		"\n"
		"NOTE: THE CONSEQUENCES ARE PRACTICAL, and they decide the design:\n"
		" the ORDER of resolution matters, and it is the order of the graph, not of document\n"
		" numbers or of dates alone;\n"
		" a cycle in it (goods returned back up the chain, mutual transfers) is a real\n"
		" possibility and has to be recognised rather than looped over forever;\n"
		" the figures a document showed at posting time are PROVISIONAL, and something has to\n"
		" say so - to the user, and in the notes of whatever stores them.\n"
		"\n"
		"Building this per-document, at posting, produces numbers that look right on the day and\n"
		"are never right afterwards.\n"
		"\n"
		"KEY: AND A DOCUMENT ENTERED BACKDATED INVALIDATES EVERYTHING AFTER IT. A sale inserted into\n"
		"last month changes which lots every later sale consumed, and those documents are already\n"
		"posted with their own figures. Nothing about them looks wrong.\n"
		"\n"
		"So there has to be a BOUNDARY OF VALIDITY - the point up to which lot calculations are known\n"
		"good. Documents register themselves against it as they post; writing before the boundary\n"
		"moves it back, and everything after it is marked as needing to be re-posted. Then a person\n"
		"or a scheduled job restores it, in order, and the boundary walks forward again.\n"
		"NOTE: THIS PLATFORM HAS NO METATYPE FOR IT - there is no ready-made sequence object. Build it:\n"
		"an information register holding the boundary per key (organisation, and whatever else the\n"
		"cost is computed within), written by the posting, and a scheduled job that re-posts what\n"
		"lies beyond it. Saying so plainly matters more than the design - a pattern that assumes a\n"
		"mechanism the platform does not have sends somebody looking for a checkbox that is not\n"
		"there.\n"
		"NOTE: WITHOUT ONE, backdated entry silently corrupts cost - and it is not detectable\n"
		"afterwards, because every document individually is consistent with what it saw when it was\n"
		"posted. This is the mechanism people mean when they say the base \"has to be re-posted in\n"
		"sequence\", and it is worth building before the first backdated document rather than after.\n"
		"\n"
		"KEY: AND THE WRITE-OFF ITSELF IS A REPAYMENT LOOP, which is worth writing down because every\n"
		"implementation rediscovers it:\n"
		" A QUANTITY STILL TO SETTLE starts at what the line asks for and goes down lot by lot,\n"
		" in the order the policy dictates. The loop ends when it reaches zero - or when the lots\n"
		" run out, which is the interesting case.\n"
		" EACH LOT IS TAKEN WITH A COEFFICIENT - the share of that lot being consumed - and every\n"
		" amount attached to the lot (cost, currency amount, the debt behind a return to a\n"
		" supplier) is carried across IN THAT PROPORTION. One coefficient, applied to all of\n"
		" them, is what keeps quantity and money in step; computing each amount separately is how\n"
		" they drift apart by a penny.\n"
		" THE LOTS ARE READ ONCE FOR THE WHOLE DOCUMENT, into a structure the lines then draw\n"
		" from - not a query per line. A document with two hundred lines otherwise asks two\n"
		" hundred times for overlapping answers.\n"
		" RUNNING OUT IS A MESSAGE, NOT AN EXCEPTION - naming the line, the item and how much\n"
		" could not be settled. \"Not enough lots\" as a bare refusal leaves a person to find which\n"
		" of two hundred lines caused it. And taking a NAMED lot partially is its own message:\n"
		" the person asked for that lot specifically, and got some of it.\n"
		"\n"
		"WHERE IT COMES FROM AND WHERE IT GOES IS A TABLE, NOT A CHAIN OF IFS. Each kind of\n"
		"operation - sale, transfer, write-off to costs, return to supplier, transfer to a\n"
		"commission agent - names the register it takes from and the one it puts into. Kept as data\n"
		"keyed by the operation, a new kind is a row; written as branches inside each document, it\n"
		"is a change in every one of them.\n"
		"\n"
		"KEY: A RETURN HAS TO GO BACK INTO THE LOT IT CAME OUT OF, and nothing in the return document\n"
		"says which one that was. A retail sale names an item and a quantity; the lot it consumed is\n"
		"buried in the movements. Returning it at today's cost quietly changes the margin of a sale\n"
		"that already happened.\n"
		"* THE ANSWER IS A HELPER REGISTER WRITTEN AS THINGS ARE WRITTEN OFF - item, characteristic,\n"
		"series, warehouse, quantity, operation kind, and the DOCUMENT that did it. A return then\n"
		"looks backwards through it for the most recent sale of the same thing, from the same\n"
		"warehouse, of at least this quantity, and takes its lots and its cost.\n"
		"NOTE: It is an index, not a truth: it duplicates what the movements already imply, and it\n"
		"exists because the movements cannot be asked that question cheaply. Write it in the same\n"
		"posting that writes the movements - a helper filled by a separate pass is a helper that\n"
		"will one day disagree with what it indexes.\n"
		"\n"
		"NOTE: AND THE POLICY IS NOT A CONSTANT. Valuation method, whether lots are kept per warehouse,\n"
		"whether empty series may be written off - these are per ORGANISATION and true AS OF A DATE\n"
		"(`shapes`: an information register with periodicity). Read them for the moment being\n"
		"posted, not for today: re-posting last year's document must use last year's policy.\n"
		"* AND THE POLICY HOLDS FOR A WHOLE MONTH, so it is read AS OF THE END OF THE MONTH the\n"
		"document falls in - not as of the document's own second. Otherwise two documents of the\n"
		"same month, either side of the day somebody changed the setting, post under different\n"
		"rules and their figures cannot be reconciled with each other.\n"
		"* It is also asked for on every line of every document, so it is worth CACHING for the\n"
		"call: the same organisation and month answer the same thing, and the reading is a query.\n"
		"* A related habit: what a thing IS can often be read from the account it sits on - goods\n"
		"held on commission against goods owned outright. One fact, kept once, rather than a flag\n"
		"beside it that can disagree.") },

	{ wxT("settlements"),
	  ibMcpText("Settlements with counterparties: contracts, and which currency the balance is in."),
	  ibMcpText("MUTUAL SETTLEMENTS.\n"
		"\n"
		"THE TWO LEVELS, and it is worth knowing which one is being asked for.\n"
		" By COUNTERPARTY alone - one running balance per partner. The primitive level: it\n"
		" works, and it cannot answer \"which of the two agreements is this against\".\n"
		" By CONTRACT, which is what people usually mean. A Contracts catalogue whose OWNER is\n"
		" the counterparty - subordinate to it, so a contract cannot exist without one and the\n"
		" choice lists are narrowed by construction. The contract carries its period of\n"
		" validity and its CURRENCY.\n"
		"\n"
		"THE CONTRACT'S CURRENCY DECIDES THE BEHAVIOUR, which is why it belongs to the contract\n"
		"and not to the document. Settlements are then kept in a register whose dimensions include\n"
		"the counterparty, the contract, and the SETTLEMENT CURRENCY - the contract's, the one the\n"
		"debt is actually owed in.\n"
		"\n"
		"AND THE AMOUNT IS USUALLY MORE THAN ONE NUMBER. The same debt is looked at in the\n"
		"settlement currency (what is owed), and in whatever currency the books are kept in -\n"
		"management, regulated, both. Where those are genuinely separate kinds of accounting they\n"
		"are separate resources on the register, converted at the document's own frozen rate (see\n"
		"`currency`).\n"
		"\n"
		"* AND WHEN THE KINDS ARE NOT YET SEPARATED, carry them all from the start anyway. Adding\n"
		"a second amount later means revisiting every posting, every report and every balance ever\n"
		"computed - while an unused resource costs a column. This is one of the few places where\n"
		"building for a distinction nobody has asked for yet is the cheaper mistake.\n"
		"\n"
		"NOTE: THE CURRENCY IS NOT DERIVABLE LATER. A balance in a register with no currency dimension\n"
		"cannot be told apart afterwards - the numbers are already added together. If there is any\n"
		"chance of a second currency, the dimension goes in now.\n"
		"\n"
		"KEY: OFFSETTING AN ADVANCE IS THE MECHANISM AT THE HEART OF ALL THIS, and it has one shape:\n"
		" 1. EVERY PAYMENT IS SPLIT IN TWO - what it CLOSES of an existing debt, and what is left\n"
		" over, which becomes an ADVANCE. Never one or the other by assumption: a payment of a\n"
		" hundred against a debt of sixty is sixty settled and forty prepaid, and both halves\n"
		" have to be written.\n"
		" 2. DEBTS ARE CLOSED OLDEST FIRST unless somebody says otherwise - FIFO over the\n"
		" outstanding documents. \"Otherwise\" is a person picking the documents by hand, and that\n"
		" screen is worth having: it is how disputes get settled.\n"
		" 3. AN ADVANCE LIVES ON A DIFFERENT ACCOUNT from a debt - money owed TO you and money held\n"
		" FOR you are opposite things, and netting them into one balance hides both. So the\n"
		" offset itself is a TRANSFER between the advance account and the settlement account,\n"
		" which is what the accountant will look for.\n"
		"\n"
		"STOP: AND THE KEY THE DEBT IS SEARCHED BY IS A SETTING OF THE AGREEMENT, not a constant of the\n"
		"system: settled as a whole, per order, per invoice, or per individual settlement document.\n"
		"Same code, four levels of detail, and the level is read from the agreement. Hard-coding the\n"
		"finest one looks safest and is not - it demands that every payment name an order, which\n"
		"nobody does when the agreement is settled as a whole.\n"
		"\n"
		"NOTE: A RETURN IS NOT A NEGATIVE SALE HERE EITHER. It moves the debt the other way and has its\n"
		"own operation kind; writing it as a minus makes turnover reports understate both sides.\n"
		"\n"
		"NOTE: AND IN A FOREIGN CURRENCY THE OFFSET ITSELF CREATES AN EXCHANGE DIFFERENCE: the advance\n"
		"came in at one rate, the debt arose at another, and closing one against the other realises\n"
		"the gap. It is computed at the moment of offset, distributed over the lines being closed,\n"
		"and it is the reason a settlement in currency is not the same routine with a rate applied\n"
		"at the end.") },

	{ wxT("currency"),
	  ibMcpText("Currencies, rates with multiplicity, and why a document keeps its own copy."),
	  ibMcpText("KEEPING ACCOUNTS IN MORE THAN ONE CURRENCY.\n"
		"\n"
		"THE SHAPE. A catalogue of Currencies, and beside it the rates: an InformationRegister with\n"
		"`Periodicity` = Day, the currency as its dimension, and TWO resources - the rate and the\n"
		"MULTIPLICITY. (Periodicity is what makes this an information register rather than an\n"
		"accumulation one: a rate is a value that was true for a stretch of time, which is the\n"
		"definition in `shapes`.)\n"
		"\n"
		"MULTIPLICITY IS NOT OPTIONAL. Some currencies are quoted per 10, per 100, per 1000 units,\n"
		"and a rate on its own is then simply wrong by two orders of magnitude - not visibly, just\n"
		"wrong. The pair is what converts: amount * rate / multiplicity. Store both from the start,\n"
		"even where every rate is currently quoted per one; adding it later means revisiting every\n"
		"line of arithmetic that was written without it.\n"
		"\n"
		"* THE DOCUMENT KEEPS ITS OWN COPY. Give documents their own Rate and Multiplicity\n"
		"attributes, filled from the register at the moment the document is made, and do the\n"
		"document's arithmetic from THOSE - not from the register.\n"
		"\n"
		"The reason is that a document must not change behind the person who signed it. Read the\n"
		"rate live and re-opening last quarter's invoice re-prices it at today's rate: the printed\n"
		"paper and the screen disagree, and nothing is broken anywhere to point at. With the rate\n"
		"copied in, the document is a record of what was agreed - and the copy can be EDITED,\n"
		"which is what people mean by a management rate: this deal was done at this rate, whatever\n"
		"the bank said that morning.\n"
		"\n"
		"This is the legitimate exception in `showing-a-value` in its purest form: not a duplicate\n"
		"of a current fact, but a fact of its own with its own date. Say so in the object's notes,\n"
		"so the next reader does not \"fix\" it into a dot-walk.") },

	{ wxT("parallel-accounting"),
	  ibMcpText("A second set of books - IFRS, budgeting - and the forks worth knowing before choosing."),
	  ibMcpText("KEEPING A SECOND SET OF BOOKS.\n"
		"\n"
		"Sooner or later someone asks for accounting alongside the accounting they already have -\n"
		"IFRS beside the statutory books, management figures beside both, a budget. There are\n"
		"several ways to carry it and they are not equally good; the choice is worth making\n"
		"deliberately, because it is the one that cannot be changed once there is a year of data.\n"
		"\n"
		" AN ACCUMULATION REGISTER WITH A CATALOGUE STANDING IN FOR ACCOUNTS. It works, and it\n"
		" is the crudest of them: you have re-implemented a chart of accounts as ordinary data,\n"
		" with no correspondence, no double entry, and nothing checking that the two sides\n"
		" agree. Everything the accounting register does for free becomes code somebody\n"
		" maintains. Reach for it only when what is wanted is genuinely not accounting.\n"
		" A CHART OF ACCOUNTS AND AN ACCOUNTING REGISTER OF ITS OWN. The straightforward\n"
		" answer: a second set of books IS a second chart and a second register, and they cost\n"
		" nothing to have side by side. If the words being used are an accountant's, this is\n"
		" where to start.\n"
		" OPERATIONAL REGISTERS THAT FEED IT. Keep the working detail - lots, stock, movement -\n"
		" on accumulation registers where it is cheap to read, and TRANSLATE from them into the\n"
		" accounting register as postings. The operational side answers \"what is where\" fast;\n"
		" the accounting side stays the record that balances. This is the usual shape of a\n"
		" grown-up configuration, and it is worth designing towards even when starting small.\n"
		" NOTE: The translation is then a mechanism with a moment and a direction: decide WHEN it\n"
		" runs and what happens when the source is edited afterwards, and write that in the\n"
		" notes - it is the part that rots quietly.\n"
		"\n"
		"BUDGETING IS THE SAME QUESTION WITH A DIFFERENT ANSWER. A budget is not usually\n"
		"correspondence at all - nothing is debited against anything. It is turnovers: amounts\n"
		"planned per period, per item, per SCENARIO, with the scenario as a dimension of its own\n"
		"so that plan, fact and a revision can sit in one register and be compared. Turnovers give\n"
		"the movement and balances give the standing figure, which together is what people mean by\n"
		"reading a budget - so an AccumulationRegister carries it without a chart of accounts\n"
		"anywhere in sight.\n"
		"\n"
		"* THE QUESTION THAT DECIDES: does this have to BALANCE - two sides that must agree, an\n"
		"account that something comes from and one it goes to? Yes, it is a chart of accounts and\n"
		"an accounting register, and imitating it elsewhere throws away the only property that\n"
		"makes it worth having. No, it is accumulation, and reaching for accounts adds ceremony to\n"
		"something that never needed it.\n"
		"\n"
		"---\n"
		"\n"
		"AND ONE LEVEL UP: WHICH STRATEGY THE WHOLE CONFIGURATION FOLLOWS. This is decided once,\n"
		"early, and everything else leans on it.\n"
		"\n"
		" PARALLEL. Management and statutory accounting are kept side by side, and documents\n"
		" carry the flags that say which of them this one is reflected in. Honest when the two\n"
		" genuinely diverge - different valuation, different periods, things recognised in one\n"
		" and not the other - and a real cost everywhere: every document, every report and\n"
		" every user has to know which set they are looking at.\n"
		" FUSED. No flags at all: one truth, recorded once. Far simpler, and correct whenever\n"
		" the two would only ever say the same thing. Do not build the parallel machinery on\n"
		" the chance that they might diverge one day - that chance is paid for daily.\n"
		" TRANSLATED. Operational documents are what people actually work with; the accounting\n"
		" postings are produced FROM them, typically by a scheduled job at month end. The\n"
		" accountant receives a summary and works with that. Good when accounting is a\n"
		" reporting obligation rather than the daily instrument.\n"
		" ACCOUNTING-LED. The accounting register is the primary record: the finance people\n"
		" live in it, the balance is closed there, and everything else is derived. Right when\n"
		" the business IS run in accounting terms.\n"
		"\n"
		"KEY: AND YOU CANNOT READ THIS OFF THE REQUEST - ASK. Two questions settle it: \"will you\n"
		"keep management accounting?\" and \"will you keep statutory accounting?\". People answer\n"
		"them immediately, because they know their own work; guessing produces either machinery\n"
		"nobody wanted or a rebuild once the second kind of accounting turns up. It is one of the\n"
		"few questions worth asking before writing anything at all.\n"
		"\n"
		"KEY: AND THE STRUCTURAL CHOICE IS: ONE REGISTER WITH A \"WHICH BOOKS\" DIMENSION, OR TWO\n"
		"REGISTERS SIDE BY SIDE. Configurations that carry both kinds of accounting for years end up\n"
		"at TWO - stock lots, costs, work in progress, scrap, output, each existing twice, one for\n"
		"management and one for the statutory books. The reasons are worth knowing before choosing:\n"
		" THE DIMENSIONS DIFFER. The statutory side needs the account, the tax designation, the\n"
		" organisation as a legal entity; the management side needs none of them and wants\n"
		" dimensions of its own. Forced into one register, every row carries the other side's\n"
		" empty columns.\n"
		" THE VOLUMES DIFFER, and so does what may be recalculated. Re-posting a quarter of\n"
		" management figures must not touch what has been filed.\n"
		" THE RIGHTS DIFFER. Two registers can be granted separately; one register with a\n"
		" dimension cannot, short of row-level rules.\n"
		"NOTE: THE PRICE IS DUPLICATION - two sets of movements, two write-off routines to keep in step.\n"
		"Pay it with a SHARED ALGORITHM and separate queries (`production`): the traversal is\n"
		"written once, the reading of each side is its own. A single register with a dimension is\n"
		"the right answer only while the two sides genuinely hold the same facts - which stops being\n"
		"true the day the accountant asks for one more account.\n"
		"\n"
		"KEY: AND THERE IS A THIRD SHAPE, WORTH REACHING FOR BEFORE THE OTHER TWO: ONE OPERATIONAL\n"
		"REGISTER, AND ENTRIES DERIVED FROM IT LATER. Documents write their movements once - stock,\n"
		"costs, whatever the operation actually is - and know nothing about accounts. Then a\n"
		"scheduled job walks what was written since it last ran and produces the double entry: at\n"
		"the end of the day, at the end of the month, whenever the accountant needs it.\n"
		"\n"
		"WHAT IT BUYS is the thing the other two shapes pay for: one road for writing, one mechanism\n"
		"to get right, no second set of movements to keep in step - and the accounting rules live in\n"
		"ONE place instead of inside forty documents. Changing how something is reflected in the\n"
		"books becomes an edit to the derivation, not a re-posting campaign.\n"
		"\n"
		"NOTE: WHAT IT COSTS, said plainly: the books LAG. A balance asked for mid-day answers as of the\n"
		"last derivation, and \"why is yesterday's sale not in the ledger\" becomes a question with a\n"
		"legitimate answer that still has to be explained. The derivation needs a boundary of its\n"
		"own - what has already been reflected - and re-posting a document has to invalidate the\n"
		"entries derived from it (`lot-accounting` has the same boundary for cost, and it is the\n"
		"same idea).\n"
		"\n"
		"NOTE: WHICH TO CHOOSE: if the accountant lives IN the ledger all day and posts corrections\n"
		"there, derive nothing - the ledger is primary and the operational side follows it. If the\n"
		"business runs on operational figures and accounting is a monthly obligation, deriving is\n"
		"the cheapest correct design there is, and it is the one people reach for last because the\n"
		"documents look incomplete without their entries.\n"
		"\n"
		"STOP: THE SECOND SET IS NOT A CONVERSION OF THE FIRST. Depreciation is the standing example:\n"
		"the same asset has one life for the books and another for tax, so the month's charge is\n"
		"TWO DIFFERENT NUMBERS computed twice, not one number copied across. The same is true of\n"
		"valuation, of what may be expensed and of when revenue is recognised. Build the parallel\n"
		"figure as a figure in its own right - carried in its own resource, side by side with the\n"
		"first - and a routine that computes \"the amount\" and multiplies it by something to get the\n"
		"other one is a shortcut that will be unpicked.\n"
		"\n"
		"NOTE: AND THE RULES ARE DATED. Tax law changes on a day, and documents before and after that\n"
		"day are computed differently - forever, because re-posting an old period must reproduce\n"
		"what was filed. So the branch is on the DOCUMENT'S date against the law's date, never on\n"
		"today's, and the dates themselves belong in data rather than in the code that reads them.\n"
		"\n"
		"* ONE MORE THING THAT ONLY SHOWS UP IN PRACTICE: an operation in one set of books can drag\n"
		"a companion entry with it in the other. Charging depreciation on an asset that was revalued\n"
		"also moves the revaluation surplus, in proportion to the depreciation, into retained\n"
		"earnings - an entry nobody asks for and everybody's auditor expects. When a mechanism is\n"
		"built for one set of books, ask what it obliges in the other.") },

	{ wxT("constants"),
	  ibMcpText("One value for the whole application, the same for everyone - and when it is not."),
	  ibMcpText("WHAT A CONSTANT IS FOR.\n"
		"\n"
		"A Constant holds ONE value for the entire application: the same for every user, every\n"
		"company, every day. The configuration's own version; a global switch that changes how the\n"
		"whole program behaves; the organisation the base is kept for. If you can point at the one\n"
		"value and say \"this is simply what it is here\", it is a constant.\n"
		"\n"
		"THREE QUESTIONS SEND IT SOMEWHERE ELSE, and each of them is asked by someone eventually:\n"
		" \"What was it before?\" - a constant has no history. Writing it overwrites what was\n"
		" there and the previous value is gone, with nothing recording when it changed. If the\n"
		" old value will ever be needed - a rate, a limit, a responsible person - this is an\n"
		" InformationRegister with `Periodicity` (see `shapes`), not a constant.\n"
		" \"Whose?\" - if the answer differs per company, per warehouse, per user, it is not one\n"
		" value. It is a register keyed by whatever it differs by. A constant that grows a\n"
		" second meaning (\"the main warehouse - well, for the main company\") has already\n"
		" stopped being one.\n"
		" \"Which one?\" - if it names a particular item rather than holding a setting, look at\n"
		" `predefined` first: a predefined item is reachable by name from code without a\n"
		" constant standing in front of it, and it cannot be left empty by accident.\n"
		"\n"
		"NOTE: AND A CONSTANT CAN BE EMPTY. Nothing forces it to be filled before something reads it,\n"
		"so code that assumes a value will one day run on a fresh base where nobody set it. Decide\n"
		"what the empty case means - a sensible default, or a refusal that says which constant is\n"
		"missing - rather than letting it read as zero or as an empty reference deep inside a\n"
		"calculation.") },

	{ wxT("allocation"),
	  ibMcpText("Spreading an amount over lines so the parts still add up to the whole."),
	  ibMcpText("DISTRIBUTING AN AMOUNT ACROSS LINES - AND THE LEFTOVER PENNIES.\n"
		"\n"
		"THE SIGNAL, in the words people actually use: \"spread this amount over those lines\",\n"
		"\"split one sum across another\", \"I do not want any pennies left over\". A discount, a\n"
		"freight charge, a total to be divided by weight - the amount is known, the lines are\n"
		"known, and the parts must add back to exactly the amount.\n"
		"\n"
		"Rounding each line on its own does not do that: round twelve lines to two decimals and\n"
		"the sum lands a few pennies off, every time, in a document that has to balance. This is\n"
		"not an edge case to be discovered later - it happens on almost every real document, and\n"
		"it is the single most common thing to get quietly wrong.\n"
		"\n"
		"METHOD 1 - BY WEIGHT, WITH THE REMAINDER PLACED. Compute each line's weight from its\n"
		"base, take the share, round it, and add up what you actually handed out; the difference\n"
		"between that and the total goes onto ONE chosen line.\n"
		" weight_i = base_i / SUM(base)\n"
		" part_i = round(total * weight_i)\n"
		" residue = total - SUM(part_i) -> onto the last line (or the largest)\n"
		"This is the common one and it is honest as long as the residue is placed DELIBERATELY.\n"
		"Which line receives it is a decision - the last is simplest, the largest hides it best -\n"
		"and it belongs written in the object's notes, because the next reader will find a line\n"
		"that is a penny off its own weight and wonder whether it is a bug.\n"
		"\n"
		"METHOD 2 - CUMULATIVE, WITH NOTHING LEFT OVER. Round the RUNNING TOTAL rather than each\n"
		"share, and take each line as the difference between two rounded running totals:\n"
		" C_i = round(total * SUM(base_1..base_i) / SUM(base)), C_0 = 0\n"
		" part_i = C_i - C_(i-1)\n"
		"The parts then add to C_n, which IS the total, by construction - there is no residue to\n"
		"place, because the error never accumulates: each line silently absorbs the previous\n"
		"line's rounding, and no line is ever more than one unit from its ideal share. This is the\n"
		"one to use when the requirement is \"it must come out even\" rather than \"each line must\n"
		"match its own weight exactly\".\n"
		"\n"
		"NOTE: ZERO AND NEGATIVE BASES BREAK BOTH if not thought about: a line with base 0 must get 0\n"
		"and must not be the one carrying the residue, and SUM(base) = 0 has no answer at all -\n"
		"refuse rather than divide. Decide what a line of zero means before writing the loop.\n"
		"\n"
		"* ASK WHICH FAILURE IS ACCEPTABLE, because one of them must be. Either a line differs\n"
		"slightly from its own weight (method 1's chosen line, method 2's absorption) or the\n"
		"parts do not sum to the whole. There is no third outcome; the arithmetic does not allow\n"
		"it. What people mean by \"I do not want any pennies left\" is almost always method 2.\n"
		"\n"
		"KEY: SPREADING OVER TIME IS THE SAME PROBLEM, and it has one form that gets it right - the\n"
		"monthly write-off of something paid for a period (insurance, a licence, deferred expenses):\n"
		"\n"
		" share = round(REMAINING_AMOUNT / days_from_this_month's_start_to_the_END_of_the_term\n"
		" * days_of_the_term_falling_INSIDE_this_month, 2)\n"
		"\n"
		"STOP: THE DIVISOR IS THE DAYS THAT REMAIN, NOT THE DAYS OF THE WHOLE TERM, and it is computed\n"
		"from the REMAINING amount rather than the original. That is the entire trick: rounding\n"
		"never accumulates, because every month re-divides what is actually left over what is\n"
		"actually left. Dividing the original sum by the total days leaves a residue that has to be\n"
		"chased in the final month - and somebody always forgets to chase it.\n"
		"\n"
		"And the last month is an explicit case, not an accident: when the days remaining in the\n"
		"term equal the days falling inside this month, write off the WHOLE remainder without\n"
		"dividing at all. That is what closes the account to exactly zero.\n"
		"\n"
		"NOTE: PARTIAL MONTHS AT BOTH ENDS are the ordinary case - a term starting on the 17th, ending\n"
		"on the 8th - so the month's slice is bounded on both sides: start of the slice is the LATER\n"
		"of the month's start and the term's start, end of it the EARLIER of the month's end and the\n"
		"term's end, and days are counted inclusively at both ends. The by-months variant (equal\n"
		"twelfths) is a different request and a different answer; ask which one is meant, because\n"
		"the two differ by real money in the first and last months.\n"
		"\n"
		"KEY: WHERE THIS MEETS TAX, which is where it is met most often. Computing VAT on each line\n"
		"and adding the results up gives a DIFFERENT number from computing it once on the\n"
		"document's total - each line rounded a fraction of a penny, and twelve of them add up to\n"
		"something visible. Neither is wrong arithmetic; they answer different questions, and one\n"
		"of them has to be declared authoritative:\n"
		" per-line tax is authoritative when each line has to stand on its own - different\n"
		" rates, a line reprinted or reported separately;\n"
		" document-level tax is authoritative when the printed total is what must match, and\n"
		" then the lines are ALLOCATED from it by the methods above rather than summed into it.\n"
		"Decide which, say so in the notes, and never let one road compute it both ways - that is\n"
		"the reconciliation that never converges. See `printing` for the two VAT schemes\n"
		"themselves, which is a separate choice on top of this one.") },

	{ wxT("printing"),
	  ibMcpText("Where a print command belongs, and what a commercial blank is made of."),
	  ibMcpText("BUILDING A PRINTED FORM.\n"
		"\n"
		"WHERE THE COMMAND LIVES - the choice made first and regretted later.\n"
		" A command ON THE FORM that prints inline is the crudest of them. It works, and it\n"
		" means the printout exists only where that form is open: not from a list, not from a\n"
		" job, not from another form. Putting the call behind a procedure changes nothing -\n"
		" it is the same option written more tidily.\n"
		" A command ON THE OBJECT is the one to reach for. It is invoked in its own right, is\n"
		" handed what it needs, and produces the document; the form becomes one caller among\n"
		" several rather than the only door.\n"
		" Or put the printing in the OBJECT or MANAGER module and let the command delegate to\n"
		" it. Older in style, and entirely sound: what matters is that the printout is reachable\n"
		" without a form being open, not which of these two carries it.\n"
		"The test is a question: if this had to be printed for two hundred documents overnight,\n"
		"with nobody at a screen, would it still work? If not, it is on the form.\n"
		"\n"
		"WHAT A COMMERCIAL BLANK IS MADE OF. Nearly all of them are four things, in this order:\n"
		"a HEADER, a TITLE line, the repeating DETAIL row, and a FOOTER. See `form-to-areas` for\n"
		"how to cut them; what follows is what each usually holds.\n"
		"\n"
		"The HEADER identifies the two parties - supplier and buyer: full legal name, legal\n"
		"address, tax and registration numbers, the contract number and date. These read as\n"
		"sentences one after another, so they are usually TEMPLATES rather than bare parameters -\n"
		"`[FullName], [LegalAddress]` sits inside a line of text and stays readable. Where there\n"
		"are genuinely only one or two, filling them as parameters is fine; the template earns its\n"
		"place when the line is prose.\n"
		"\n"
		"The DETAIL row is the goods - product, quantity, price, amount - drawn once per line, and\n"
		"it is the area that repeats. It may be collapsed or expanded differently from one form to\n"
		"another; that is a question about which lines are output, not about a second area.\n"
		"\n"
		"The FOOTER carries the totals, and they sit UNDER THE COLUMNS they total: quantity under\n"
		"quantity, amount under amount. VAT is shown the same way, in its own row beside them.\n"
		"\n"
		"VAT IS WHERE THE VARIATION LIVES, and people will ask about it every time. Two shapes,\n"
		"and they are not the same arithmetic:\n"
		" VAT INCLUDED - the line's amount already contains the tax, and the tax is EXTRACTED\n"
		" from it.\n"
		" VAT ON TOP - the line's amount is the base, and the tax is ADDED to it.\n"
		"Ask which one before writing either: the two produce different totals from the same\n"
		"numbers, and a form built for one silently mis-states the other.\n"
		"\n"
		"NOTE: AND THE RATE IS NOT A CONSTANT IN THE CODE. It is set by legislation and it changes -\n"
		"which makes it the textbook case for an InformationRegister with `Periodicity` (see\n"
		"`shapes`): a value that was true for a stretch of time. A rate written into a formula\n"
		"re-prints last year's documents at this year's rate, and nothing anywhere says so.") },

	{ wxT("query-craft"),
	  ibMcpText("How accounting queries are actually written - the constructs and why those and not others."),
	  ibMcpText("THE HABITS OF A WORKING QUERY, counted in a real configuration rather than guessed.\n"
		"\n"
		"KEY: LEFT JOIN IS THE DEFAULT, AND BY A LONG WAY - seven of them for every inner one. The\n"
		"reason is not taste: in accounting the LINE must survive even when what it is joined to is\n"
		"absent. An item with no price, a document with no agreement, a lot with no cost yet - an\n"
		"inner join silently DROPS those rows, the totals come out lower, and nothing anywhere says\n"
		"a row was lost. Use inner only when the absence genuinely means the row should not be\n"
		"there, and say to yourself which case you are in.\n"
		"\n"
		"STOP: AND EVERY LEFT JOIN OBLIGES A NULL GUARD. A missing row gives NULL, and NULL poisons\n"
		"arithmetic - added to a number it yields NULL, and the sum of a column with one NULL in it\n"
		"is NULL, not the sum of the rest. So numeric fields coming from the joined side are wrapped\n"
		"the moment they are selected. In the configuration measured, null-guards appear about as\n"
		"often as left joins do, which is the correct ratio: one per join, not one per bug found.\n"
		"\n"
		"* CASE EXPRESSIONS OUTNUMBER EVERYTHING - more than one per query on average. They do two\n"
		"jobs: CLASSIFY (what kind of movement is this, which column does this amount belong in) and\n"
		"PROTECT (a divisor that might be zero, a rate that might be absent). Both belong in the\n"
		"query rather than in the loop that reads it - the database decides once per row, the loop\n"
		"decides once per row per reader.\n"
		"\n"
		"FILTER BEFORE GROUPING, NOT AFTER. Conditions on the source rows go in the WHERE; the\n"
		"configuration measured uses HAVING exactly zero times in a megabyte of queries. Filtering\n"
		"after aggregation means aggregating what will be thrown away, and it reads as if the filter\n"
		"were about the totals when it is about the rows.\n"
		"\n"
		"UNION ALL, NOT UNION - forty-seven to seven. Combining movements from several sources does\n"
		"not want duplicate elimination: it costs a sort over everything, and worse, it MERGES rows\n"
		"that were legitimately identical - two receipts of the same item, same quantity, same day\n"
		"are two facts, and plain UNION quietly makes them one.\n"
		"\n"
		"STOP: A CONDITION OF A JOIN IS NOT A CONDITION OF THE QUERY, and moving one into the WHERE is\n"
		"the mistake that looks like tidying. `LEFT JOIN Prices ON ... AND Prices.Kind = &Kind` keeps\n"
		"every row of the left side and joins only the matching price. Move that last test into the\n"
		"WHERE and rows whose price did not match now have NULL there, the test rejects them, and\n"
		"the left join has silently become an inner one - fewer rows, smaller totals, nothing\n"
		"reported. Conditions ABOUT THE JOIN stay in the join; conditions about which rows the query\n"
		"is interested in go in the WHERE.\n"
		"\n"
		"* REACHING THROUGH A DOT IN A QUERY IS A JOIN THE ENGINE WRITES FOR YOU. `Line.Item.Unit`\n"
		"is convenient and honest in a small selection; in a WHERE, or three dots deep, or over a\n"
		"large table, it becomes joins nobody chose and nobody sees in the text. Where it matters,\n"
		"write the join yourself - then the kind of join is a decision (`LEFT` keeps the row when\n"
		"the reference is empty, which the implicit one may not) and the reader can see what the\n"
		"query actually touches.\n"
		"NOTE: The related habit: a dotted path in a CONDITION is the expensive one. Selecting it for\n"
		"display is usually fine; filtering by it makes the engine join before it can filter.\n"
		"\n"
		"* PARAMETERS ARE ALSO WHAT MAKES A QUERY REUSABLE TO THE DATABASE. The same text with a\n"
		"parameter is one prepared plan reused for every value; the same text with the value glued\n"
		"in is a NEW query each time, planned from scratch and filling the cache with single-use\n"
		"entries. That is on top of the type-safety and injection reasons in `query-by-name`.\n"
		"\n"
		"* AND SEVERAL RELATED SELECTIONS TRAVEL AS ONE BATCH - the steps of a calculation sent in a\n"
		"single call rather than one round trip apiece. Same reasoning as the loop rule above, one\n"
		"level up: the cost is in the trips, not in the reading.\n"
		"\n"
		"NOTE: AND A QUERY ONLY READS. It cannot write, and anything that changes data is a separate\n"
		"act afterwards - which is why the shape of a posting is \"read everything, decide, then\n"
		"write\" (`posting`) rather than a query that does the work.\n"
		"\n"
		"KEY: NAME THE TABLES BY THE ROLE THEY PLAY IN THIS QUERY, not by what they are. A register\n"
		"joined to itself is the clearest case: the two copies are `Accruals` and `Endings`, not\n"
		"`T1` and `T2` - one is the record, the other is what comes after it, and the condition\n"
		"between them then reads as a sentence. The same everywhere else: `Balances`, `Movement`,\n"
		"`Salaries`, `TimeNorms`, `FullDistributionBase`. A query with `Doc`, `Doc1`, `Doc2` in it\n"
		"cannot be reviewed - every reader has to reconstruct which is which before they can look\n"
		"for the mistake.\n"
		"* And name a temporary table after its CONTENT for the same reason, since it becomes a\n"
		"source in the next step: `LotsAvailable`, `EmployeesAndAssignments`. The name is what the\n"
		"step after it reads.\n"
		"\n"
		"KEY: ASK THE REGISTER, NOT THE MOVEMENTS. Virtual tables - balances, turnovers, both at once,\n"
		"the latest values as of a date - are used constantly, and the combined balance-and-turnover\n"
		"table more than the separate ones: opening, movement and closing in ONE pass, already\n"
		"aggregated by the engine, instead of two queries whose results have to agree. Summing the\n"
		"movement table by hand to get a balance is slower, longer, and wrong the day the register\n"
		"keeps totals.\n"
		"* For anything periodic - prices, rates, policies, states - the latest-as-of-a-date table is\n"
		"the whole answer.\n"
		"\n"
		"STOP: AND THE PARAMETERS GO INSIDE THE TABLE'S OWN BRACKETS, NEVER INTO THE WHERE. This is the\n"
		"single most expensive habit to get wrong, and both versions look correct:\n"
		"\n"
		" RIGHT: FROM AccumulationRegister.Stock.Balance(&AsOf, Warehouse = &Warehouse) AS B\n"
		" WRONG: FROM AccumulationRegister.Stock.Balance() AS B WHERE B.Warehouse = &Warehouse\n"
		"\n"
		"A virtual table is not a table - it is a CALCULATION the engine performs, and its\n"
		"parameters are the terms of that calculation. Put the date and the condition inside, and\n"
		"the engine folds the register up to that moment for that warehouse alone. Put them in the\n"
		"WHERE, and it computes balances for EVERY warehouse over the whole history, and then throws\n"
		"away all but one. On a small base both answer the same; on a real one the second is the\n"
		"reason a report takes four minutes.\n"
		"NOTE: AND SOMETIMES IT IS NOT EVEN THE SAME ANSWER. A period is a parameter of the calculation,\n"
		"not a column of the result: a balance table has no period to compare in a WHERE at all, and\n"
		"a turnover table's period means something different from the boundary that produced it.\n"
		"Filtering afterwards on what looks like the same field can silently give figures nobody\n"
		"ordered.\n"
		"\n"
		"WHICH VIRTUAL TABLE TO ASK FOR is the question before that one:\n"
		" BALANCE - \"what is left\", as of a moment. One row per key.\n"
		" TURNOVER - \"how much moved\", over a period, optionally broken by month or day.\n"
		" BALANCE AND TURNOVER - opening, movement, closing in ONE read. Ask for this whenever a\n"
		" report shows all three: two separate queries over the same register cost twice and can\n"
		" disagree at the boundary.\n"
		" LATEST AS OF A DATE - for a periodic information register: the value in force.\n"
		" THE MOVEMENT TABLE ITSELF - only when the individual records are the subject: an audit\n"
		" of who wrote what, a card of one account. Never for computing a balance.\n"
		"\n"
		"NOTE: AND A TEMPORARY TABLE IS THE OTHER THING ENTIRELY, though both are \"tables that do not\n"
		"exist\". A virtual table is the ENGINE'S answer about a register; a temporary table is YOUR\n"
		"intermediate result, built by a step of your own query and indexed by you. Use the virtual\n"
		"one to ask, the temporary one to keep - and pass the set you already hold into the\n"
		"temporary one rather than into a loop (see `query-by-name`).\n"
		"\n"
		"TEMPORARY TABLES ARE THE UNIT OF A BIG QUERY (see `query-by-name`), indexed on their join\n"
		"fields and DROPPED when the calculation no longer needs them - a long posting holds several\n"
		"at once, and they cost memory on the server for as long as the manager lives.\n"
		"\n"
		"KEY: HOW LONG WAS THAT VALUE IN FORCE - the standing trick, and the one worth memorising. A\n"
		"periodic register stores WHEN a value started and never when it ended: the rate of the 3rd\n"
		"holds until the next rate appears. To get the interval - and from it the number of days -\n"
		"JOIN THE REGISTER TO ITSELF:\n"
		" the left copy is the record itself, and its period is the start;\n"
		" the right copy is joined on the SAME KEY - every dimension, the currency, the employee,\n"
		" the item - with the one extra condition that its period is LATER;\n"
		" take the MINIMUM of those later periods, and that is the end of the interval: the\n"
		" moment the next value took over.\n"
		"\n"
		"STOP: AND IT MUST BE A LEFT JOIN. The newest record has no successor, so an inner join drops\n"
		"exactly the row that matters most - the value in force TODAY. Its end comes back empty,\n"
		"which is the correct answer and reads as \"still current\"; substitute the end of the period\n"
		"being reported when a number is needed.\n"
		"NOTE: AND THE KEY MUST BE COMPLETE. Join on fewer dimensions than the register has and the next\n"
		"record of ANOTHER currency ends this one's interval - the query still runs, the durations\n"
		"are simply wrong, and they are wrong only where the data is dense enough to overlap.\n"
		"\n"
		"Days in force is then the difference between the two dates, and \"which value applied on\n"
		"date X\" is the interval containing it. Where the platform offers window functions the same\n"
		"answer is one line - the next period over a partition - and the self-join is what to write\n"
		"when it does not.\n"
		"* THE SAME SHAPE ANSWERS MORE THAN RATES: how long an employee held a position, how long a\n"
		"price stood, how long an asset was in one department, when a status ended. Any register\n"
		"that records starts and expects the reader to infer ends.\n"
		"\n"
		"THREE SMALLER ONES THAT COME UP CONSTANTLY:\n"
		" TOTALS IN THE QUERY when the result is going to be walked as a hierarchy - the engine\n"
		" produces the group rows, so the reading code never accumulates by hand and never\n"
		" disagrees with the report.\n"
		" IN HIERARCHY for a filter by a folder: \"everything under this product group\" is one\n"
		" condition, not a recursive walk.\n"
		" CAST a composite field before reaching through it - a field that may hold several types\n"
		" has no attributes until it is narrowed to one, and the message when it is not narrowed\n"
		" names the field rather than the reason.\n"
		"\n"
		"NOTE: AND THE ONE THAT SAVES THE MOST TIME: run the query alone, on real data, before it is\n"
		"embedded in anything (`query_check` resolves the names; a debugger stop shows the rows).\n"
		"Most \"the posting is wrong\" turns out to be \"the query returned rows nobody looked at\".") },

	{ wxT("query-by-name"),
	  ibMcpText("Assembling a query text so one routine serves many kinds - and where that stops."),
	  ibMcpText("ONE QUERY INSTEAD OF TEN NEARLY IDENTICAL ONES.\n"
		"\n"
		"\"Find the document of kind X that was created from this one\" is the same query for every\n"
		"kind - only the TABLE NAME differs. Written out per kind it is ten copies to keep in step,\n"
		"and the eleventh kind arrives without one. Written once, with the kind's name assembled\n"
		"into the text, it is a routine that works for a kind added tomorrow:\n"
		"\n"
		" query.SetParameter(\"Base\", reference);\n"
		" query.Text = \"SELECT ALLOWED Ref FROM Document.\" + kind + \" WHERE Base = &Base\";\n"
		"\n"
		"STOP: AND HERE IS THE LINE, WHICH IS NOT A STYLE PREFERENCE. What may be glued into the text\n"
		"is STRUCTURE - a table name, a field name - and it comes from the METADATA. What must never\n"
		"be glued in is a VALUE: a reference, a date, anything a person typed. Values go through\n"
		"parameters, always, and not only because of injection - a parameter carries its TYPE, so a\n"
		"reference stays a reference and a date does not become a string that happens to look like\n"
		"one.\n"
		"\n"
		"NOTE: A NAME ASSEMBLED IS A NAME UNCHECKED. The compiler cannot see inside a string, so a\n"
		"misspelled kind fails when the routine runs - and only for that kind. Two habits pay for\n"
		"themselves: take the name FROM the metadata rather than from a caller's spelling, and put\n"
		"the finished text through the query checker before it is stored (`query_check` reads it the\n"
		"way the engine will, resolving every name against this configuration).\n"
		"\n"
		"AND SAY ALLOWED. A query in code that ignores access rights either shows a person rows they\n"
		"may not see or fails outright when a narrower role runs it; ALLOWED drops what the reader\n"
		"may not have instead.\n"
		"\n"
		"NOTE: WHEN NOT TO. If the branches differ in more than a name - different joins, different\n"
		"conditions, different fields - one assembled text becomes a query nobody can read and the\n"
		"compiler cannot help with. Then they really are several queries, and saying so is cheaper\n"
		"than a string built out of ifs.\n"
		"\n"
		"KEY: A BIG QUERY IS BUILT IN STEPS, NOT WRITTEN IN ONE BREATH. Any real calculation - the\n"
		"month's payroll, a cost distribution, a stock report with reserves - reads five or six\n"
		"different things and combines them. Written as one text it becomes a page of joins nobody\n"
		"can change safely; written as TEMPORARY TABLES it becomes a sequence:\n"
		" one step per subject, each a small query with a name that says what it selected -\n"
		" the salaries, the time norms, the balances, the reserves;\n"
		" later steps join the earlier ones instead of the raw tables;\n"
		" the final step selects the answer.\n"
		"\n"
		"* AND THE MANAGER OF THOSE TABLES IS PASSED AROUND, which is the part worth copying: one\n"
		"routine prepares a temporary table, another consumes it, and neither has to know how the\n"
		"other got its data. It is how a calculation is split across procedures without passing\n"
		"tables of values through memory.\n"
		"\n"
		"NOTE: Each step is also separately CHECKABLE - run it alone and look at what it returned, which\n"
		"is the only practical way to debug a calculation that reads six sources. And name the\n"
		"steps after the subject, never after the order: `Salaries`, not `Query2`.\n"
		"\n"
		"STOP: AND THIS IS HOW LARGE VOLUMES ARE HANDLED AT ALL - THE SET GOES INTO THE QUERY, not the\n"
		"query into a loop. The list you have in memory - the document's lines, the employees being\n"
		"paid, the items being written off - is passed AS A PARAMETER, selected into a temporary\n"
		"table, and everything downstream joins against it. One query answers for two thousand rows.\n"
		"\n"
		"The shape of that first step is always the same: select the columns of the value table into\n"
		"a temporary table, and INDEX IT BY the fields the later joins use. The indexing is not\n"
		"optional - a temporary table without one is scanned once per row of whatever it is joined\n"
		"to, and that is precisely where a calculation that was fine on ten rows dies on ten\n"
		"thousand.\n"
		"\n"
		"STOP:STOP: A QUERY INSIDE A LOOP IS HOW A SYSTEM IS BROUGHT DOWN, and that is not a figure of\n"
		"speech. It is correct, it is readable, it passes review, and it is one round trip per\n"
		"iteration - so a thousand-line document is a thousand queries, and thirty people posting\n"
		"such documents at once is thirty thousand. The server does not slow down gracefully: it\n"
		"holds connections, locks pile up behind them, and everybody stops. It is the single most\n"
		"common way an application that worked in testing dies in production.\n"
		"\n"
		"* THE FIX IS ALWAYS THE SAME THREE STEPS, and it is worth writing out because it is\n"
		"mechanical:\n"
		" 1. BEFORE the loop, collect the keys you are about to ask about - the items, the\n"
		" employees, the documents - into an array or a value table.\n"
		" 2. RUN ONE QUERY over that set: pass it as a parameter and either match with `IN\n"
		" (&Keys)` or select it into an indexed temporary table and join. One trip, everything\n"
		" needed for every iteration.\n"
		" 3. INSIDE the loop, work only with what came back. Walk the selection, or build a map\n"
		" from the result once and look up by key - and touch the database not at all.\n"
		"\n"
		"NOTE: THE TELL THAT IT IS HAPPENING: a query object, or a reference reached through a dot, on\n"
		"a line that is indented inside a loop. Both are reads. If either is there, the loop is\n"
		"inside out and the three steps above are the answer - not caching, not a faster query, not\n"
		"a bigger server.\n"
		"\n"
		"STOP:STOP: AND THE TELL IS NOT ENOUGH, BECAUSE THE QUERY HIDES BEHIND A CALL. `GetPrice(item)` on\n"
		"a line inside a loop looks like arithmetic; the query is one level down, in a routine\n"
		"somebody else wrote, and the reviewer who was watching for the word 'query' sees nothing.\n"
		"This is not hypothetical - it is a known way to walk a defect straight through an\n"
		"interview and a code review alike (Max, 2026-09-02, having done exactly that to see who\n"
		"would notice).\n"
		"\n"
		"* SO THE RULE IS ABOUT THE FUNCTION, NOT ABOUT THE LOOP: anything called from inside a\n"
		"loop must be MUTE - it may compute, it may not read. Three habits keep that true:\n"
		" A ROUTINE THAT READS THE DATABASE TAKES A SET AND RETURNS A MAP. `GetPrices(items)` can\n"
		" only be called once; `GetPrice(item)` invites the loop and always gets it. Shape the\n"
		" signature so the wrong use is awkward.\n"
		" SAY SO IN THE NAME OR THE NOTE when a routine goes to the database, so a reader one\n"
		" level up knows without opening it.\n"
		" AND MEASURE RATHER THAN READ: count the queries a posting actually made. Reading code\n"
		" finds the loops you can see; counting finds the ones behind three calls, which are the\n"
		" ones still in production.\n"
		"NOTE: And say ALLOWED in these queries too (`query-by-name` above), and be careful with\n"
		"DISTINCT: it is needed after joins that multiply rows, and it also HIDES duplicates that\n"
		"were a mistake. Add it because the join legitimately fans out, not to make a symptom go\n"
		"away.\n"
		"\n"
		"KEY: THE OTHER USE IS READING SEVERAL ATTRIBUTES AT ONCE, and it is a performance habit worth\n"
		"having from the first day. Reaching through a reference - `line.Item.Article`, then\n"
		"`line.Item.Unit`, then `line.Item.VatRate` - fetches the object once PER ATTRIBUTE, and\n"
		"inside a loop over document lines that is thousands of reads nobody sees.\n"
		"\n"
		"Ask for them together: one query over the table the reference points at, selecting the\n"
		"fields wanted, filtered by the reference. The table's name comes from the reference's TYPE\n"
		"through the metadata, so the same routine serves catalogues and documents alike - and the\n"
		"field list is assembled from what the caller asked for, which is structure again and not\n"
		"data.\n"
		"NOTE: The rule underneath: inside a loop, reaching through a reference is a database read. One\n"
		"query before the loop beats a hundred lookups inside it, and the difference is invisible\n"
		"until the document has two hundred lines.") },

	{ wxT("look-first"),
	  ibMcpText("Before building a mechanism, find out whether this base already has one."),
	  ibMcpText("A REQUEST FOR A MECHANISM IS NOT EVIDENCE THAT IT IS MISSING.\n"
		"\n"
		"People describe what they NEED, not what their configuration already contains - they are\n"
		"not holding an inventory of it, and often nobody is. So a request to \"add reservation\"\n"
		"regularly arrives at a base that already reserves, somewhere, written by someone else and\n"
		"working. Build the second one and there are now two answers to \"how much is reserved\",\n"
		"nothing reconciling them, and no error anywhere - just two numbers that drift apart.\n"
		"\n"
		"So look first. `metadata_list` shows what exists by kind, `query_sources` shows what can\n"
		"be read, and the notes on the objects are where the previous builder said what theirs was\n"
		"for. It costs one or two calls, and it is the cheapest moment this question will ever be\n"
		"asked - after the second mechanism has data in it, the answer is a migration.\n"
		"\n"
		"Reservation is the standing example, and stock is full of relatives: goods reserved in\n"
		"warehouses, goods in transit, goods accepted for commission. Each pair looks like a\n"
		"duplicate and often is not - which is exactly why it has to be READ rather than guessed.\n"
		"\n"
		"* AND WHAT TO DO WITH THE FINDING: offer it, do not act on it silently. Say to the person\n"
		"that this already exists here and that it may be worth looking at that code before\n"
		"anything is added. They may know, and have a reason - a mechanism that does not fit, one\n"
		"they intend to retire. Deciding that for them is not yours to do; not telling them is\n"
		"how the duplicate gets built.") },

	{ wxT("naming"),
	  ibMcpText("Forming a name: say the whole thing, and only what is not already known."),
	  ibMcpText("NAMING WHAT YOU CREATE.\n"
		"\n"
		"Names are English - they travel into scripts, into query text and into the database\n"
		"schema. What the person actually said goes in the SYNONYM, which is where their language\n"
		"belongs and where it can be translated.\n"
		"\n"
		"A NAME IS THE SENTENCE, TRANSLATED. Say what is counted and where or whose it is, in that\n"
		"order, and read the result back: it should be the thing the person said.\n"
		" GoodsInWarehouses - goods, in warehouses\n"
		" GoodsOfCompany - the same goods, seen as the company's\n"
		" GoodsLotsInWarehouses - lots of goods, in warehouses\n"
		" GoodsTransferred - goods, once handed over\n"
		"These are four different registers because they answer four different questions, and the\n"
		"names are what make that visible at a glance. A name that could equally be any of them\n"
		"(`GoodsRegister`, `Stock2`) has thrown that away.\n"
		"\n"
		"CARRY THE OWNER ONLY WHEN THE OWNER IS PART OF THE CONCEPT. A characteristic is always a\n"
		"characteristic OF something, and a series belongs to what it is a series of - so\n"
		"`ProductCharacteristic` and `ProductSeries` say the whole thing and are right.\n"
		"\n"
		"But a concept that attaches to ANYTHING stands on its own: quality is just `Quality`.\n"
		"Quality can qualify a product, a lot, a characteristic, a service - gluing one of them\n"
		"onto the front states something untrue and makes the name wrong the first time it is\n"
		"used elsewhere. The test: does this idea only ever exist as part of that other thing? If\n"
		"yes, name them together. If it merely happens to sit there today, name it alone.\n"
		"\n"
		"AND DO NOT REPEAT WHAT THE PLACE ALREADY SAYS. An attribute on a product is reached as\n"
		"`Product.Name`; calling it `ProductName` says \"product\" twice and reads worse at every\n"
		"use. The context is already carrying it.\n"
		"\n"
		"KEY: EACH METATYPE HAS ITS OWN GRAMMAR, and following it is what makes a configuration read\n"
		"as one system rather than as a pile of objects:\n"
		" A DOCUMENT IS AN EVENT, so it is named as one - an action plus what it acts on:\n"
		" `GoodsReceipt`, `SalesOfGoodsAndServices`, `ReturnOfGoodsFromCustomer`,\n"
		" `WriteOffOfGoods`, `MonthEndClosing`. A document called `Invoice2` or `StockDoc`\n"
		" describes a piece of paper rather than the thing that happened.\n"
		" A CATALOGUE IS A THING, in the plural of what it holds: `Goods`, `Warehouses`,\n"
		" `Counterparties`, `Currencies`. Its items are things, not events.\n"
		" A REGISTER IS WHAT IT KEEPS PLUS THE CUT IT KEEPS IT BY: `GoodsInWarehouses`,\n"
		" `LotsOfGoodsInWarehouses`, `LotsTransferred`, `SettlementsWithCounterparties`,\n"
		" `SettlementsWithAccountablePersons`, `WorkInProgress`, `ScrapInProduction`. Read the\n"
		" name aloud and it should be the question the register answers.\n"
		" AN ENUMERATION IS A KIND, plural: `VatRates`, `OperationKinds`, `RoundingOrders`.\n"
		" A COMMON MODULE IS A SUBJECT, not a place: `StockManagement`,\n"
		" `SettlementsManagement`, `BalanceControl`, `Pricing`. `Utils`, `Common2` and\n"
		" `Helpers` are where code goes to be lost.\n"
		" A ROLE IS A JOB OR ONE PERMISSION (`roles`): `Storekeeper`, `Cashier` beside\n"
		" `EditItems`, `EditCounterparties`, `SeeEveryOrganisation`.\n"
		"\n"
		"* SUFFIXES CARRY THE VARIANT, AND ONLY THE VARIANT. When the same subject exists twice for\n"
		"a structural reason, the second is the first plus one word - `Costs` and\n"
		"`CostsAccounting`, `WorkInProgress` and `WorkInProgressAccounting` - so the pair is\n"
		"obvious at a glance and sorts together. The same for technical variants of a module: the\n"
		"cached one, the overridable one, the client-side one, the privileged one, each the base\n"
		"name plus its word. What a suffix must never carry is a VERSION or a date: `Sales2`,\n"
		"`SalesNew`, `SalesFinal` are a history nobody can read.\n"
		"\n"
		"KEY: AND MARK WHAT IS YOURS WITH A PREFIX, in any configuration that will be updated from\n"
		"somewhere else. Objects added by the implementer carry a short prefix of their own, and\n"
		"then \"what did we add\" is answerable by sorting the tree - which is exactly the question\n"
		"asked at every update, and unanswerable without it. It costs three characters and settles\n"
		"an argument that otherwise happens once a year.\n"
		"\n"
		"THE SYNONYM IS THE PERSON'S OWN WORDS, and it is not a translation of the name - it is\n"
		"what they would say out loud. `SettlementsWithAccountablePersons` is a name; the synonym\n"
		"is whatever the accountant calls that screen, in their language, with their spacing and\n"
		"their capitals. Two rules that save trouble later: keep it a NOUN PHRASE, since it appears\n"
		"as a heading, a menu entry and a column title; and do not put the metatype in it - a\n"
		"catalogue's synonym is \"Warehouses\", never \"Warehouses catalogue\", because the place it\n"
		"is shown already says what it is.\n"
		"NOTE: AND EVERY OBJECT A PERSON CAN SEE NEEDS ONE. A missing synonym falls back to the name -\n"
		"English, run together, technical - and that is what a user reads on the screen. It is the\n"
		"cheapest visible defect in a configuration and the most common.") },

	{ wxT("external-data"),
	  ibMcpText("Getting data out of another base or system - the door that already exists."),
	  ibMcpText("DATA FROM SOMEWHERE ELSE.\n"
		"\n"
		"When the task is to pull data out of another base or another application, the platform\n"
		"already has the door: `ComObject`. It is a real value type of the language - create one,\n"
		"call its methods, read its properties - so a scripted exchange with anything that exposes\n"
		"an automation interface is written, not invented.\n"
		"\n"
		"Reach for it before designing a file exchange. A folder of files to be dropped and picked\n"
		"up is a protocol somebody then has to own: a format, a naming scheme, what happens to a\n"
		"half-written file, who deletes what and when. If the far side can simply be ASKED, all of\n"
		"that disappears.\n"
		"\n"
		"NOTE: IT IS WINDOWS-ONLY, and that is the honest cost. A configuration that depends on it\n"
		"stops being portable to the platforms the engine itself runs on. Worth deciding\n"
		"deliberately rather than discovering later - and worth writing in the notes of whatever\n"
		"uses it, so the next reader knows the limit is chosen and not accidental.\n"
		"\n"
		"KEY: AND WHATEVER THE CHANNEL IS, THE LOADING SCREEN HAS ONE SHAPE - importing a bank\n"
		"statement is the case everybody meets first, and every other import is the same five steps:\n"
		" 1. THE SOURCE AND ITS CONTEXT, at the top: the file, and the few things the far side does\n"
		" not know - which organisation, which bank account, which account the entries land on.\n"
		" 2. READ, AND SHOW WHAT WAS READ - a table of what is in the file, one row per record,\n"
		" BEFORE anything is written. \"Refresh from file\" re-reads it, because the file gets\n"
		" replaced while the window is open.\n"
		" 3. THE MATCHING IS COLUMNS OF THAT TABLE: counterparty, contract, cash-flow item, the\n"
		" accounts. The import guesses them from what the file carries - an account number, a\n"
		" tax code - and a person corrects what it got wrong, in the row, before loading. An\n"
		" unmatched row is visible as an empty cell rather than as a message.\n"
		" 4. A TICK PER ROW, because half a statement is a normal thing to load: the ones already\n"
		" entered by hand are unticked, the rest go in. And a foot that counts what is about to\n"
		" happen - how many documents, incoming and outgoing totals - so the decision is made\n"
		" on numbers rather than on faith.\n"
		" 5. ONE LOADING COMMAND, and a REPORT OF WHAT HAPPENED afterwards: created, skipped,\n"
		" failed and why. Settings - the general ones and the ones specific to this bank or\n"
		" partner - live on their own buttons, away from the daily path.\n"
		"\n"
		"STOP: NOTHING IS WRITTEN BEFORE THE COMMAND. An import that creates documents while reading\n"
		"leaves a person no way to say no, and the mess it makes is theirs to unpick by hand.\n"
		"\n"
		"BOTH DIRECTIONS BELONG IN ONE PROCESSOR, on two pages: sending and receiving are the same\n"
		"agreement with the same far side, the same settings and the same file format. Two separate\n"
		"processors mean the settings are entered twice and drift.\n"
		"\n"
		"AND THREE THINGS THE OUTGOING HALF TEACHES:\n"
		" THE STEPS ARE WRITTEN ON THE FORM, in four short lines beside the fields: choose what\n"
		" to send, untick what should not go, check that the ticked ones are ready, send and\n"
		" then take a report. A screen used once a month by somebody who is not a specialist\n"
		" should not need a manual open beside it.\n"
		" A ROW THAT CANNOT GO IS MARKED IN THE ROW - shown in red, with the REASON stated\n"
		" underneath in the words of the far side's requirement: \"counterparty tax number is\n"
		" empty\". Not a dialog after the attempt, and not a count of errors: the line and the\n"
		" field that is missing.\n"
		" THE FOOTER COUNTS THE TICKED ONES ONLY - four documents, this much money - because\n"
		" that is what is about to leave. A total over everything read would be a number that\n"
		" describes nothing anybody is doing.\n"
		"\n"
		"DEBUGGING IT IS AWKWARD BUT NOT BLIND. The far side is not ours and will not stop for us,\n"
		"so do not try to debug THROUGH it: put the breakpoint in the script that drives it, on\n"
		"our side of the call, and step from there - inspecting what came back, one call at a\n"
		"time. Expect to learn the far side's shape by looking at real answers rather than by\n"
		"reading about it.") },

	{ wxT("showing-a-value"),
	  ibMcpText("Asked for a column: look for a path to it before adding a field."),
	  ibMcpText("A COLUMN ASKED FOR IS NOT AUTOMATICALLY A FIELD TO ADD.\n"
		"\n"
		"When someone asks to see something in a list - a dynamic list, a report, a printed form -\n"
		"the first question is not \"where do I put this attribute\". It is: can this value already\n"
		"be REACHED from what the list is over?\n"
		"\n"
		"A list of goods receipt lines already holds a reference to the product, and the product\n"
		"holds its group, its unit, its vendor; the line holds a reference to its document, and\n"
		"the document holds the date, the partner, the warehouse. All of that is reachable through\n"
		"the dot - `Product.Parent`, `Ref.Partner` - without storing anything twice.\n"
		"\n"
		"Adding an attribute instead creates a SECOND copy of a fact that already exists, and from\n"
		"that moment something has to keep the two equal. Nothing does. The copy is written once,\n"
		"at creation, and then the product is renamed or moved to another group and the list goes\n"
		"on showing what was true that day - not visibly wrong, which is what makes it expensive.\n"
		"\n"
		"NOTE: AND THE EXCEPTION THAT IS NOT ONE: sometimes the value MUST be frozen - the price at\n"
		"which this was actually sold, the address the parcel actually went to, the rate applied\n"
		"on the day. That is not a copy of a current fact, it is a fact of its own with its own\n"
		"date, and it belongs stored. The test is a question about the past: if the source later\n"
		"changes, should this line change with it? Yes - walk the dot. No - store it, and say in\n"
		"the object's notes that it is deliberately a snapshot.") },

	{ wxT("form-to-areas"),
	  ibMcpText("Taking a paper form apart: which parts repeat, which are said once."),
	  ibMcpText("READING A PRINTED FORM INTO AREAS.\n"
		"\n"
		"Handed a blank - an invoice, a delivery note, an act - do not lay it out cell by cell.\n"
		"Read it for what REPEATS, because that is the only structural question a spreadsheet\n"
		"template asks.\n"
		"\n"
		"A form is three kinds of thing, and the middle one is the whole design:\n"
		" the HEADER - said once per document: the number, the date, the parties, the contract.\n"
		" Everything here is a parameter of one document.\n"
		" the DETAIL band - said once per LINE, and drawn over and over from a single area. Its\n"
		" columns are the tabular section's columns. If you find yourself making a second area\n"
		" that differs only in which row it sits on, you have not found the band yet.\n"
		" the FOOTER - said once again at the end: totals, the amount in words, the signatures.\n"
		"\n"
		"COLLAPSE WHAT REPEATS INTO ONE PRIMITIVE. The blank is a SPREADSHEET DOCUMENT, and it is\n"
		"marked out into areas - lines and groups of lines. The whole job is recognising which of\n"
		"them are the SAME line drawn again: if rows differ only in their values, they are one\n"
		"area, output once per line, not five areas that happen to look alike.\n"
		"(Keep the two words apart: the spreadsheet document is the PAPER and is cut into areas;\n"
		"a document's tabular section is DATA, and its lines are what the repeating area is drawn\n"
		"for. One area, many lines - they are not the same thing and do not correspond one to one:\n"
		"a single tabular section can feed several areas, and an area can be fed by none.)\n"
		"\n"
		"AND THE REPETITION IS NOT ALWAYS DOWNWARDS. Sometimes it runs to the RIGHT: a column per\n"
		"warehouse, per month, per price type, with the heading repeating across the page. Then the\n"
		"piece that repeats horizontally is its own area too, output to the right rather than below.\n"
		"Recognising this early is what stops a template being rebuilt when a second warehouse\n"
		"appears - a form drawn column by column has the number of warehouses frozen into it.\n"
		"\n"
		"KEY: WHICH MEANS A TEMPLATE IS CUT BOTH WAYS, and the worked example is a price-setting\n"
		"document. Two areas are named down the ROWS - `TableHeader`, `Row` - and two more across\n"
		"the COLUMNS: `Goods` (the number and the product name) and `Price` (price, unit, currency,\n"
		"discount). Printing walks them as a GRID: for each line of the document, put out `Row` x\n"
		"`Goods` once, then `Row` x `Price` again for every price type there is - wholesale, small\n"
		"wholesale, retail, planned cost - and the same crossing on the heading line, where the\n"
		"price type's NAME becomes the heading above its four columns.\n"
		"\n"
		"The result has as many column groups as the data has price types, and the template says\n"
		"nothing about how many. Draw those four groups by hand instead and the document is correct\n"
		"until somebody adds a fifth price type - after which it is quietly wrong, because nothing\n"
		"in the layout knows it is missing one.\n"
		"\n"
		"NOTE: THE WORDS THAT GIVE IT AWAY are \"a column for each...\" and any heading that is itself a\n"
		"VALUE: a price type, a warehouse, a month. A heading that names data is a vertical area\n"
		"waiting to be recognised.\n"
		"\n"
		"KEY: A VERTICAL AREA IS ALSO HOW A BLOCK OF COLUMNS BECOMES OPTIONAL, which is the other half\n"
		"of why a template is cut sideways. VAT is the standing example: some counterparties are\n"
		"charged it and some are not, and the ones who are not must not be handed a form with two\n"
		"empty columns and a nil total. Cut the VAT columns out as their own vertical area, give it\n"
		"a NAME that says what it is, and the module puts it out or does not - one decision, taken\n"
		"where the fact is known. A discount block, a currency block and a storage place behave the\n"
		"same way.\n"
		"\n"
		"KEY: SO AN OPTIONAL COLUMN HAS TWO BUILDS, AND THEY COST DIFFERENT THINGS. Say the goods may\n"
		"or may not carry a CHARACTERISTIC, and the form must show it as its own column when they do:\n"
		" A VERTICAL AREA for that column, joined on when it is wanted. One heading, one detail\n"
		" band, one decision. Add a second optional block - a discount, a storage place - and it\n"
		" is one more area, not one more form.\n"
		" A SECOND PAIR of areas: `TableHeaderCharacteristic` + `RowCharacteristic`, drawn for\n"
		" exactly that case, beside the plain pair. The module picks a pair and prints it.\n"
		"\n"
		"NOTE: THE SECOND IS SIMPLER UNTIL THE VARIANTS MULTIPLY, and then it multiplies with them: two\n"
		"independent options are four pairs, three are eight. A real delivery note in a working\n"
		"configuration ends up with `Row`, `RowDiscount`, `RowPlace`, `RowCode`, `RowCodeDiscount`,\n"
		"`RowCodePlaceDiscount` - every one of them a full copy of the same line, and a column widened\n"
		"in one of them and not the others is a defect nobody sees until that combination prints.\n"
		"\n"
		"AND VERTICAL AREAS NEST, which is what makes them hold a real form. A till receipt cuts its\n"
		"line into `Number`, `CodeColumn`, `Data` (the product), `AmountBeforeDiscount`,\n"
		"`DiscountAmount` and `Amount` - and the middle two sit under one wider area, `Discount`,\n"
		"because they are switched on and off together and carry a heading spanning both. One pair\n"
		"of areas for the whole receipt, and every question the form asks - is there an article\n"
		"code, is there a discount - is one of these joined on or left out.\n"
		"\n"
		"STOP: A VERTICAL CUT GOES THROUGH EVERY HORIZONTAL BAND IT CROSSES - the heading, the detail\n"
		"line AND THE TOTALS. Left out, the receipt above prints without the discount columns and\n"
		"with ONE total instead of three; a template that cut only the heading and the row would\n"
		"print a footer still totalling columns that are not on the page. Whatever is optional is\n"
		"optional all the way down.\n"
		"\n"
		"* SO: PAIRS WHEN THE VARIANTS ARE FEW AND DIFFER IN LAYOUT, not merely in one column - when\n"
		"the whole line is re-proportioned, the pair is honest and the vertical cut would fight the\n"
		"widths. VERTICAL AREAS WHEN THE OPTIONS ARE INDEPENDENT and combine - that is precisely the\n"
		"case a pair per combination cannot survive. The question to ask is not which looks tidier\n"
		"but HOW MANY COMBINATIONS THERE WILL BE.\n"
		"\n"
		"NOTE: AND THAT IS THE CHOICE BEHIND \"two columns in one area or two areas side by side\". Both\n"
		"print the same page:\n"
		" ONE AREA holding both columns is simpler and says they always travel together. Nothing\n"
		" to sequence, nothing to forget.\n"
		" TWO AREAS make each one optional and reorderable - and hand you the ORDER as a\n"
		" responsibility: they come out left to right in the order the module puts them, so the\n"
		" heading line and the detail line must be walked the same way or the columns and their\n"
		" titles come apart. That is a real cost, and it buys a real thing.\n"
		"Split when a block is genuinely optional or genuinely repeated; keep it whole when it is\n"
		"neither. \"It might be optional one day\" is not a reason to pay the ordering cost today.\n"
		"\n"
		"KEY: AND THAT RECOGNITION IS A DOOR, NOT A DETAIL. The moment the columns come from the data,\n"
		"the sheet has stopped being a blank and become a CROSS TABLE - the same shape a report makes\n"
		"when a grouping is put ACROSS instead of down. Two ways to build it, and knowing which one\n"
		"you are in is the whole decision:\n"
		" A PRINTED FORM with a vertical area, output to the right once per value. Right when the\n"
		" paper is fixed by somebody else - a blank with its own layout, borders and captions -\n"
		" and the horizontal axis is the only thing that varies.\n"
		" A COMPOSER OUTPUT with a level declared across the columns, which IS a cross table and\n"
		" needs no template at all: state the row grouping, state the column grouping, state what\n"
		" is summed, and every heading, every column and every total comes out of the data. Right\n"
		" whenever the answer is a table rather than a document - stock by warehouse, sales by\n"
		" month, price by type.\n"
		"\n"
		"The mistake worth naming: drawing a cross table cell by cell into a template because it\n"
		"started life as a blank. If nothing on the page is fixed by regulation or by a counterparty,\n"
		"it is a report with a column grouping, and drawing it is work that the composer would have\n"
		"done from the data.\n"
		"\n"
		"A ROW IS BROKEN DOWN BY PARAMETERS, and that is usually as far as it needs to go. Each\n"
		"cell is one of three things, and choosing between them IS the decomposition:\n"
		" TEXT - a caption that is the same on every printout. It belongs in the template.\n"
		" PARAMETER - the value comes from outside and fills the cell whole. Name it after what\n"
		" it is, not after where it sits.\n"
		" TEMPLATE - text with `[Name]` in square brackets inside it, for when a value has to\n"
		" sit INSIDE a sentence (\"Received [Quantity] pieces of [Product]\") or when spelling it\n"
		" out reads better on the page than three cells in a row would.\n"
		"A cell holding \"Invoice No. 12 of 3 March\" as one string is three parameters that were\n"
		"never separated: it cannot be reused, translated, or filled from anything else.\n"
		"\n"
		"NOTE: THE HEADER TELLS YOU THE DOCUMENT. Look at it first: what a blank asks for at the top is\n"
		"what the document must already hold, and it is common to find the form asking for\n"
		"something the metadata has no attribute for. Better discovered while reading the blank\n"
		"than while filling it.\n"
		"\n"
		"NAME AREAS BY WHAT THEY MEAN, never by where they sit. `Header`, `Title`, `TableHeader`,\n"
		"`Row`, `Total`, `TotalVat`, `AmountInWords`, `PlaceOfIssue`, `Signatures` - a printing\n"
		"module reads as a sentence when they are named this way, and a name like `Rows12to14`\n"
		"is wrong the first time a line is inserted above it. An empty band between blocks is an\n"
		"area too (`Gap`): spacing that the module can choose to put out or leave alone.\n"
		"\n"
		"KEY: A VARIATION OF THE DETAIL ROW IS ANOTHER PAIR OF AREAS, NOT ANOTHER TEMPLATE. Real\n"
		"blanks differ by which COLUMNS they show - with a discount column, with a storage place,\n"
		"with the article code, with several of those at once - and each variation needs its column\n"
		"headings to match. So they come in PAIRS: `TableHeader` + `Row`, `TableHeaderDiscount` +\n"
		"`RowDiscount`, `TableHeaderPlace` + `RowPlace`, `TableHeaderCodeDiscount` +\n"
		"`RowCodeDiscount`. The module then CHOOSES A PAIR and puts the rows out through it -\n"
		"one decision, taken once, instead of a second template kept in step by hand.\n"
		"\n"
		"TWO BLANKS, SO THE DIFFERENCE IS VISIBLE.\n"
		"\n"
		" A DELIVERY NOTE, the simple case: `Header` (number and date, supplier and buyer with\n"
		" their legal details, the contract), `TableHeader`, `Row`, `Total` and `TotalVat` under\n"
		" their own columns, `AmountInWords`, `Signatures`. Seven areas, one repeating band, and\n"
		" the whole form is a straight run down the page.\n"
		"\n"
		" AN ACT OF WORK DONE, the same skeleton with more said once: an `Approval` block at the\n"
		" very top (two columns of signatures - supplier's side and buyer's side - printed before\n"
		" anything else), `Title`, two lines of ACT PROSE naming the parties in a sentence,\n"
		" `ExtraInfo` for named parameters, then the same table and totals, then `PlaceOfIssue`\n"
		" and `Signatures`. What differs is not the mechanism: the act simply says more before\n"
		" the table.\n"
		"\n"
		" AND THE SAME NOTE GROWN UP - the one a working configuration ends with. Five pairs of\n"
		" table header and row (plain, with discount, with place, with code, with code AND\n"
		" discount), a SECOND table for returnable packaging with its own header, row and total,\n"
		" totals in four flavours (`Total`, `TotalDiscount`, `TotalVat`, `TotalWithVat`), and two\n"
		" signature blocks - one plain, one for a signature by power of attorney. Twenty-odd\n"
		" areas, and still ONE template: every one of them is a band that is either put out or\n"
		" not, decided by the module from what the document actually holds.\n"
		"\n"
		"TWO TABLES ARE TWO SUBJECTS, and that is a different thing from two variations. An act for\n"
		"work done with materials used prints a table of SERVICES and a table of MATERIALS: each\n"
		"has its own heading (\"name of product\" against \"name of raw material\"), its own detail\n"
		"row with its own columns, and its own total. They are not a pair to choose between - both\n"
		"are printed, one after the other, because the document holds two tabular sections and the\n"
		"paper says both.\n"
		"\n"
		"AND THEN THE SIGNATURES FOLLOW THE SUBJECTS. The same act carries `SignaturesMaterials`\n"
		"and `SignaturesServices`: who accepted the materials is not who accepted the work, and the\n"
		"blank asks each to sign under their own part. A single `Signatures` area at the foot is the\n"
		"simple case, not the rule - count the things being agreed to, not the pages.\n"
		"\n"
		"* REPETITION HAPPENS OUTSIDE THE TABLE TOO, and it is the case most often missed. A\n"
		"stocktake or a write-off signed by a COMMISSION has a chairman said once and then MEMBERS -\n"
		"name, position, signature - repeated for as many as there are. That is a detail band in\n"
		"every sense, and it belongs to a list of people rather than to a list of goods. Drawing\n"
		"three member lines into the footer freezes the commission at three.\n"
		"\n"
		"* AT THE OTHER END OF THE SCALE, AN AREA IS ONE LINE - and for the same reason as the big\n"
		"ones: because that line can be absent. Printing an e-mail is the plain case - sender,\n"
		"recipients, copies, subject, responsible, the body, the date it was printed - and each is\n"
		"its own area because each is printed only when it has something in it. No copies, no\n"
		"`Copies` line; nobody responsible, no `Responsible` line. The alternative is a form with\n"
		"labelled blanks, which reads as missing data rather than as absent data.\n"
		"\n"
		"NOTE: THAT IS WHY SUCH TEMPLATES END UP WITH `Header1`, `Header2`, `Header3`: the header did\n"
		"not fall into three ideas, it fell into three PIECES with optional lines between them. It\n"
		"is an honest cost of making single lines optional - but name the pieces after what they\n"
		"hold whenever they hold anything nameable, because a number tells the next reader nothing\n"
		"and stops being true the moment a fourth piece appears.\n"
		"\n"
		"An empty band is worth an area of its own here too (`BlankLine`): spacing that is put out\n"
		"deliberately between blocks that were, or were not, printed - the only way to keep the gaps\n"
		"even when the middle of the form is missing.\n"
		"\n"
		"NOTE: AND SOME FORMS HAVE NO REPEATING BAND AT ALL - one area, and that is the correct answer\n"
		"rather than a lazy one. An order of dismissal (form P-4), an order of appointment, a\n"
		"certificate: every line is said exactly once, so the whole sheet is a single area filled\n"
		"with parameters. Splitting it into `Header` / `Body` / `Footer` invents a structure the\n"
		"paper does not have, and the module then puts out three areas in a fixed order for no\n"
		"reason. Ask what repeats FIRST; when the answer is nothing, stop.\n"
		"\n"
		"Such a form is still made of the same three cell kinds, and two details recur on every\n"
		"personnel blank:\n"
		" A TICK BOX IS A BORDERED CELL with a parameter in it - the mark goes in when the\n"
		" condition holds and the cell stays empty when it does not. It is not a picture and not\n"
		" a font trick.\n"
		" SPACE LEFT FOR A HAND is deliberate: `\"__\" ________ 20__` for a date somebody writes\n"
		" in, ruled cells with nothing behind them. A blank that will be completed by hand has to\n"
		" keep those gaps, and an assistant filling every cell it can find destroys the form.\n"
		"\n"
		"AN OFFICIAL BLANK IS A DIFFERENT ANIMAL FROM A COMMERCIAL ONE, and four of its habits have\n"
		"to be built in rather than discovered late (a payroll payment sheet is the textbook case).\n"
		" IT CARRIES ITS OWN IDENTITY AS TEXT: \"Appendix 1 to the Regulation on cash operations,\n"
		" clause 18 of section II\", printed top right. That is the FORM saying which form it is,\n"
		" not data - it belongs in the template and changes only when the law does.\n"
		" EVERY BLANK LINE IS ANNOTATED. Under each rule sits a small italic caption saying what\n"
		" goes on it - \"(name of the enterprise/institution/organisation)\", \"(signature,\n"
		" surname, initials)\". On an official form these are compulsory, and they are text in\n"
		" the template beneath a bordered cell.\n"
		" THE COLUMNS ARE NUMBERED, on their own line under the headings: 1, 2, 3, 4, 5, 6. The\n"
		" regulation refers to columns BY THOSE NUMBERS, so the line is part of the table\n"
		" heading area and not decoration.\n"
		" AND THERE IS A SECOND HEADER FOR THE CONTINUATION SHEET - \"insert sheet to appendix\n"
		" No...\" - printed at the top of every page after the first. Two header areas, chosen by\n"
		" which page is being started, which is the same pair rule as the detail bands.\n"
		"\n"
		"KEY: AND SOMETIMES THE ROWS ARE NUMBERED PARAMETERS, WHICH IS NOT THE MISTAKE IT LOOKS LIKE.\n"
		"An expense claim has a block of ten accounting-entry lines and three \"received from\" lines,\n"
		"and the blank has exactly that many - printed empty when there is nothing to put in them,\n"
		"because the form is defined that way. So the cells are `Account1`...`Account10`,\n"
		"`Amount1`...`Amount10`, filled by index, and NOT a repeating band: a band would print three\n"
		"lines when there are three, and the form would no longer be the form. Fixed count, fixed\n"
		"height, empty rows and all - the one case where numbering in a name is right.\n"
		"NOTE: The moment the count is NOT fixed by the blank, this becomes the worst way to build a\n"
		"table: eleven entries have nowhere to go, and nothing says so.\n"
		"\n"
		"TWO MORE FIXTURES OF SUCH FORMS: a code entered ONE CHARACTER PER CELL (the tax number, the\n"
		"registration code - a row of little boxes, one glyph each, because the regulator wants it\n"
		"legible digit by digit), and a VERTICAL STRIP down the side - a receipt or a counterfoil,\n"
		"text rotated ninety degrees, that gets torn off and kept. Both are layout, and both are\n"
		"invisible in any description of the data.\n"
		"\n"
		"A TOTAL PER PAGE IS NOT A TOTAL PER DOCUMENT. The same sheet carries \"total for this\n"
		"page\" under the rows, and it only means anything if the module knows where the page ended -\n"
		"so the page break is a decision the printing makes, not a property of the data. Design for\n"
		"it early: a footer that assumes one page has to be taken apart to add it.\n"
		"\n"
		"AND A HEADER CAN HOLD A SMALL FIXED TABLE - the corresponding account block (account,\n"
		"sub-account, analytics code, purpose code) is a three-by-three grid inside the header, said\n"
		"once, with no repetition at all. It is a table by its looks and a header by its behaviour;\n"
		"the second is what decides which area it belongs to.\n"
		"\n"
		"THREE THINGS A PRINTED SHEET SHOWS THAT A DESIGN ON PAPER DOES NOT.\n"
		" A RULED LINE WITH ITS CAPTION UNDER IT is one cell and one small italic line: the rule\n"
		" is the cell's bottom border, `(signature)` sits beneath it as TEXT. Not underscores,\n"
		" which the module would have to print around and which never line up.\n"
		" NOT EVERY COLUMN HAS A TOTAL. A stocktake totals the money columns and leaves the\n"
		" quantity column blank, because units of different goods do not add up to anything a\n"
		" person would read. A total belongs where the addition MEANS something.\n"
		" THE COUNT IS ITS OWN LINE. \"5 items, for 955.30\" and then the amount in words is what\n"
		" a foot of a commercial blank actually says - a sentence built from a row count and a\n"
		" sum, and a second one spelling the sum out. Both are parameters of the footer, not\n"
		" another table.\n"
		"\n"
		"NOTE: THE TEST FOR A NEW AREA is not \"does it look different\" but \"is it said a different\n"
		"number of times, or chosen instead of another one\". Said once per document - part of the\n"
		"header. Once per line - a detail band, and there is one per subject. Chosen between - a\n"
		"pair. Anything else is formatting inside an area that already exists.") },

	{ wxT("sections"),
	  ibMcpText("The command interface: how a person reaches anything at all."),
	  ibMcpText("SECTIONS ARE THE APPLICATION AS A PERSON MEETS IT.\n"
		"\n"
		"A catalogue, a document or a report that belongs to no section works perfectly and is\n"
		"INVISIBLE: there is no menu it appears in and no way to open it. Checking objects into\n"
		"sections is the last step of building anything, and the one that gets forgotten because\n"
		"everything the builder does goes through the designer, where the tree shows it regardless.\n"
		"\n"
		"* A SECTION IS A JOB, NOT A KIND OF OBJECT. Purchases, Sales, Stock, Finance, Production,\n"
		"Payroll, Administration - these are what people DO, and each maps to a person or a part of\n"
		"a day. Sections called Catalogues, Documents and Reports are the metadata tree wearing a\n"
		"costume: they answer \"what kind of thing is it\", which nobody wonders, instead of \"where\n"
		"do I do my work\", which is the only question being asked.\n"
		"\n"
		"THE SAME OBJECT BELONGS IN SEVERAL. A goods catalogue is opened from Purchases, from Sales\n"
		"and from Stock, and putting it in one of them and expecting people to remember which is a\n"
		"decision they will pay for daily. Membership is cheap; hunting is not.\n"
		"\n"
		"WHAT A SECTION HOLDS, in the order a person needs it: the LISTS they live in (documents\n"
		"first, then the catalogues those documents use), the REPORTS that answer questions about\n"
		"this work, and the SETTINGS that belong to it. What they open twenty times a day goes\n"
		"first; what they open once a quarter goes last or into a submenu.\n"
		"\n"
		"* AND INSIDE A SECTION THE ENTRIES ARE GROUPED UNDER HEADINGS OF THEIR OWN - a Bank and\n"
		"cash section lists \"Bank\" (payment orders, payment approvals, statements, register\n"
		"payments) and then \"Finance\" (the organisation's finances, credit). The heading is not a\n"
		"command and opens nothing: it is a frame around meaning, and it is what turns a column of\n"
		"fifteen links into three things a person can hold in their head.\n"
		"NOTE: The threshold is about seven entries. Below it a flat list reads fine; above it, an\n"
		"ungrouped column is scanned every single time, because there is nothing to remember a\n"
		"position by.\n"
		"\n"
		"THE GROUPS THEMSELVES COME IN A SETTLED ORDER, and a full section reads the same way in\n"
		"every application worth copying - laid out in COLUMNS so the whole section is one screen\n"
		"rather than a scroll:\n"
		" 1. THE DOCUMENTS OF THIS WORK, split by sub-job where there is one - bank papers in one\n"
		" group, cash-desk papers in the next.\n"
		" 2. THE CATALOGUES AND SETTINGS those documents need - currencies, banks, cash limits,\n"
		" payment purpose codes. Opened while setting up and rarely after.\n"
		" 3. SERVICE: exchanges, imports, whatever talks to the outside - loading a statement from\n"
		" the bank, sending to the client-bank. Occasional, deliberate, and never mixed in with\n"
		" the documents.\n"
		" 4. THE REPORTS OF THIS WORK - the cash book, the registers of orders, the ones that only\n"
		" make sense here.\n"
		" 5. THE STANDARD REPORTS, listed last and REPEATED IN EVERY SECTION: trial balance,\n"
		" account card, account analysis, chess sheet. They belong to the whole system rather\n"
		" than to this job, and an accountant reaches for them from wherever they happen to be\n"
		" standing - which is exactly why they are worth repeating instead of hiding in one\n"
		" place (`ledger-reports`).\n"
		"\n"
		"AND THE SECTIONS THEMSELVES ARE ORDERED BY THE WORK, not alphabetically: the flow of the\n"
		"business - buy, sell, keep stock, count the money - reads as a sequence, and a person\n"
		"learns their place by position. A starting page comes first and is not a section of work\n"
		"at all: it is where somebody lands, and it holds what they need before choosing anything.\n"
		"\n"
		"NOTE: GIVE EVERY SECTION A PICTURE. The bar is recognised by shape and colour long before the\n"
		"words are read - which is precisely why sections with no icon feel unfinished even when\n"
		"every name is right.\n"
		"\n"
		"NOTE: HOW MANY: as many as there are jobs, which is usually five to a dozen. Two sections mean\n"
		"the split says nothing; twenty mean the bar has become a menu, and a person scans it\n"
		"instead of knowing it.\n"
		"\n"
		"KEY: AND IT IS THE SAME OPERATION ALL THE WAY UP - grouping by MEANING, done at every scale\n"
		"of the application:\n"
		" cells that are read together become an AREA on a printed form (`form-to-areas`);\n"
		" fields that are decided together become a GROUP under a heading on a form;\n"
		" groups that belong to one aspect of a thing become a PAGE;\n"
		" pages, lists and reports that serve one job become a SECTION.\n"
		"Each level asks the identical question - what is understood as one thing here? - and the\n"
		"answer is always the reader's, never the storage's. That is why a section named after a\n"
		"metatype feels wrong for the same reason as a form page called \"other attributes\" and an\n"
		"area called \"rows 12 to 14\": all three group by where something is KEPT instead of by what\n"
		"it MEANS.") },

	{ wxT("form-layout"),
	  ibMcpText("Laying a document form out: where things go, and why that order."),
	  ibMcpText("ARRANGING A FORM SOMEBODY WORKS IN ALL DAY.\n"
		"\n"
		"A generated form puts every field in one column in declaration order. It opens, it works,\n"
		"and it is unusable for a person entering fifty documents a shift. What follows is the shape\n"
		"a working document form actually has, top to bottom.\n"
		"\n"
		"1. THE DOCUMENT'S OWN COMMANDS along the top - post, post and close, print, and whatever\n"
		" this document does that others do not (fill from another document, go to related\n"
		" records). Not scattered into the body: a person looks for them in one place.\n"
		"\n"
		"2. THE HEADER, AND IT IS COLUMNS, NOT A LIST. Number and date first, and they sit TOGETHER\n"
		" on one line - two fields a person reads as one fact. Then the attributes that identify\n"
		" the document: organisation, responsible, department down the left; the ones that qualify\n"
		" it - the month it belongs to, the kind of payment, which books it is reflected in - down\n"
		" a second column beside them. Captions on the left of their fields, fields aligned with\n"
		" each other, so the eye runs down one edge instead of hunting.\n"
		" * TWO COLUMNS BECAUSE OF WIDTH, NOT SYMMETRY: a reference field is wide and a date is\n"
		" narrow, and putting them in one column wastes half the window. Group by what is read\n"
		" together, then let the widths decide the split.\n"
		"\n"
		"3. THE TABULAR SECTION WITH ITS OWN COMMAND BAR - add, copy, edit, delete, move up and\n"
		" down, sort, and the ones that belong to THIS table: pick items, fill from a source,\n"
		" replace a mark on every selected line. They belong to the table and travel with it,\n"
		" which is exactly why they are not up with the document's commands.\n"
		"\n"
		"4. TOTALS UNDER THE COLUMNS THEY TOTAL, in the table's footer line - the same rule as on\n"
		" paper. A sum in a caption somewhere below the table is a number a person has to match\n"
		" to a column by reading.\n"
		"\n"
		"5. THE SECOND, THIRD AND FOURTH TABLES GO ON PAGES. A payment document carries payment\n"
		" parameters, contributions, payroll-fund contributions and income tax; stacked they would\n"
		" make a form nobody can see the bottom of. As tabs they are one click apart and the main\n"
		" table keeps its height. The FIRST table stays out in the open - it is what the document\n"
		" is about; the rest are what it entails.\n"
		"\n"
		"6. THE COMMENT LAST, one line across the bottom. Every document has one and nobody looks\n"
		" for it first.\n"
		"\n"
		"THREE THINGS A BUSY FORM DOES THAT A PLAIN ONE DOES NOT.\n"
		" A PAGE TAB SAYS HOW MUCH IS BEHIND IT - \"Products (0 items)\", \"Customer materials\n"
		" (3 items)\". Without it a person opens every tab to find out whether it is empty, which\n"
		" is the cost the tabs were meant to save.\n"
		" THE FORM EXPLAINS ITS OWN STATE IN WORDS, at the foot and beside the totals: \"price\n"
		" type is not set - VAT will be computed automatically\", \"not enough information to\n"
		" calculate the debt\". These are not errors and not a dialog - they are the document\n"
		" telling a person why a figure looks the way it does, in the place where they are\n"
		" looking at it.\n"
		" THE DOCUMENT'S TOTALS SIT UNDER THE TABLE ON THE RIGHT - total, VAT included - because\n"
		" they are facts about the DOCUMENT rather than about the table's columns. The column\n"
		" totals stay in the table's own footer; these two are what somebody reads out loud on\n"
		" the phone.\n"
		"\n"
		"* AND A COMMAND THAT CHANGES HOW THE WHOLE DOCUMENT IS PRICED GETS ITS OWN BUTTON, not a\n"
		"line in a menu: \"Prices and currency...\" opens the decision that every line's price came\n"
		"from. A person changes it once per document and needs it visible; the rest can live under\n"
		"Actions.\n"
		"\n"
		"A CATALOGUE ITEM IS LAID OUT THE OTHER WAY ROUND, and mixing the two is the usual mistake.\n"
		"A document is an EVENT - a header and the lines it consists of. An item is a THING that\n"
		"many subsystems each know something about, so:\n"
		" WHAT MAKES IT THIS THING STAYS ABOVE THE PAGES, always visible: the folder it is in,\n"
		" the name, the code, the article number, its kind, its units, and the switches that\n"
		" change what it IS - kept by characteristics, kept by batches, a form of strict\n"
		" accountability. A person scrolling the tabs must never lose sight of which item they\n"
		" are editing. A picture, when the thing has one, sits beside that block.\n"
		" THE PAGES ARE SUBJECTS, NOT TABLES: defaults, units, properties, categories,\n"
		" components, barcodes, accounts, specifications, storage places, prices. Each is one\n"
		" department's view of the same item, and each is empty for most items - which is exactly\n"
		" why they are pages and not sections of one long form.\n"
		" INSIDE A PAGE, FIELDS ARE GROUPED UNDER A HEADING that names the group in the words of\n"
		" whoever needs it - \"VAT accounting, details for the tax invoice\", \"cost analytics\".\n"
		" Three or four fields under a title read as one decision; the same fields in a flat\n"
		" column read as a questionnaire.\n"
		" A FIELD THAT DEPENDS ON A SWITCH IS GREYED, NOT HIDDEN. The excise article stays\n"
		" visible and disabled until the item is marked excisable. Hiding it makes the form\n"
		" jump and leaves a person wondering where the field went; greying says \"this exists\n"
		" and here is what turns it on\".\n"
		"\n"
		"NOTE: WHAT DECIDES A COLUMN'S PLACE IN A TABLE: what a person reads to know WHICH LINE this is\n"
		" goes left - number, code, the item itself. What they came to type goes next. What is\n"
		" computed or rarely looked at goes right, and can be narrow. A column nobody has ever\n"
		" filled in is a column to take off the form, not to shrink.\n"
		"\n"
		"* AND THE SMALL CASE STAYS SMALL. A catalogue of employees is a list of two columns - name\n"
		"and code - and an item form of two fields plus, where there is one, a tabular section\n"
		"underneath with its own little bar. Nothing above is a requirement: pages, groups and\n"
		"navigation links earn their place when there is something to put in them, and a form that\n"
		"has four fields and four tabs is worse than the generated one it replaced. Build the plain\n"
		"version, then split what actually grew.\n"
		"\n"
		"THREE HABITS THAT APPLY TO EVERY FORM, LARGE OR SMALL:\n"
		" SEARCH IS A PERMANENT PART OF THE BAR, not a command in a menu - a box with a clear\n"
		" button, over the list and over any tabular section long enough to scroll. Finding a\n"
		" line is the most frequent thing anybody does with a table.\n"
		" THE TITLE NAMES THE SUBJECT, not the form: \"Ivanov (Employee)\", \"Invoice 12 of 3\n"
		" March\". A window called \"item form\" tells a person nothing when four of them are open.\n"
		" THE READER SORTS, by clicking the column heading, and the arrow stays there so they can\n"
		" see what they are looking at. An order fixed by the developer is right for one\n"
		" question and wrong for the next.\n"
		"\n"
		"A LIST IS THE THIRD KIND OF FORM, and it is the one people actually live in - a document\n"
		"form is opened to change one thing, a list is open all day.\n"
		" THE FIRST COLUMN IS WHAT PEOPLE SEARCH BY, which is the name and almost never the code.\n"
		" The code, the article number and the unit follow it; anything computed goes right.\n"
		" A HIERARCHY IS SHOWN THREE WAYS AND THE PERSON CHOOSES: folders-with-contents, a flat\n"
		" list ignoring the hierarchy, or a tree. The same data, three readings - somebody\n"
		" hunting for one item wants the flat list, somebody tidying wants the tree, and neither\n"
		" should have to ask for a different form.\n"
		" FOLDERS AND ITEMS ARE CREATED BY DIFFERENT COMMANDS. \"New\" and \"New folder\" are two\n"
		" buttons because they make two different things, and a person who wanted one and got the\n"
		" other has to delete it.\n"
		" DELETION IS TWO STEPS: mark for deletion, and delete what is marked, later, when\n"
		" nothing refers to it any more. The mark is reversible and visible in the list; that is\n"
		" the whole point, because a reference that vanishes takes documents with it.\n"
		" MOVING AROUND THE HIERARCHY IS ITS OWN SET OF COMMANDS - move to folder, go into the\n"
		" folder, go up a level. Dragging works and is not enough on its own: a thousand-item\n"
		" catalogue is reorganised by picking a target, not by scrolling with the mouse held\n"
		" down.\n"
		"\n"
		"KEY: AND A FORM HAS DATA OF ITS OWN, which is the part most often missed. The object being\n"
		"edited is ONE of the form's attributes, not the form itself: beside it live the things the\n"
		"screen needs and the object does not - a flag saying whether something is already sent, a\n"
		"caption assembled for display, the documents this one was created from, whether the current\n"
		"person may edit at all. Controls bind to PATHS through that data, so adding a screen-only\n"
		"value is adding an attribute to the form rather than a field to the object. The test: would\n"
		"this still mean anything with the form closed? If not, it belongs to the form.\n"
		"\n"
		"HOW A COMMAND BAR IS ORDERED, once there are more than four commands:\n"
		" ONE PRIMARY COMMAND, said in words and visually louder than the rest - the thing a\n"
		" person came to do, usually \"post and close\". Everything else can be an icon.\n"
		" THE COMMON FEW AS ICONS beside it - save, print, the two or three used every day.\n"
		" THE REST IN SUBMENUS GROUPED BY PURPOSE: create-from-this, fill-from, print, exchange.\n"
		" A menu named after what its items DO stays right as items are added; a menu named\n"
		" \"other\" is where commands go to be lost.\n"
		" AND A LAST OVERFLOW for the rare ones. A person who cannot find a command looks in one\n"
		" place before asking.\n"
		"\n"
		"NAVIGATION IS NOT THE SAME AS PAGES, and a form usually needs both. Along the top sit LINKS\n"
		"TO RELATED DATA - the files attached to this item, its prices, its history - each opening a\n"
		"list of its own that belongs to this object but is not part of it. Pages inside the form\n"
		"hold what the object ITSELF carries. Putting attached files on a page loads them with the\n"
		"form; putting the units of measure behind a link hides part of the thing being edited.\n"
		"\n"
		"FOUR SMALL CHOICES THAT DECIDE HOW A FORM FEELS TO TYPE IN.\n"
		" A REQUIRED FIELD IS MARKED BEFORE IT IS SAVED - the empty one carries a border of its\n"
		" own from the moment the form opens, so a person fills it on the way in rather than\n"
		" being sent back to it.\n"
		" TWO OR THREE EXCLUSIVE CHOICES ARE RADIO BUTTONS, not a dropdown: \"goods / service\"\n"
		" shown open costs one click and no memory. A list earns its place from about five.\n"
		" AN EMPTY AREA SAYS WHAT GOES IN IT - \"drop a picture here (5 MB maximum)\" - and says\n"
		" the LIMIT before the attempt rather than after it. An empty rectangle is a question a\n"
		" person answers by trying.\n"
		" A FIELD WITH A NATURAL WAY TO ENTER IT GETS THAT WAY ATTACHED: a calendar on a date, a\n"
		" calculator on a weight, a picker on a reference, and a clear button on anything\n"
		" optional. Typing is the fallback, not the only road.\n"
		"\n"
		"THE FORM SAYS WHAT IS WRONG WHERE IT IS WRONG. A required field left empty is marked in the\n"
		"field itself, not announced when the document is saved. And a problem that has a FIX carries\n"
		"the fix beside it: \"no parent company chosen for this counterparty - choose one\", with the\n"
		"words that do it being a link. A message that only describes leaves the person to find the\n"
		"field; a message that acts is the difference between a warning and a dead end.\n"
		"\n"
		"NOTE: AND THE FORM IS NOT WHERE BEHAVIOUR LIVES. Filling a line, checking a balance, refusing a\n"
		"posting - those belong to the object and its module, and a form that carries them works\n"
		"only while it is open. See `printing` for the same argument about print commands.") },

	{ wxT("ledger-reports"),
	  ibMcpText("What an accountant expects over a ledger, and why it is one family and not ten reports."),
	  ibMcpText("THE STANDARD REPORTS OVER AN ACCOUNTING REGISTER.\n"
		"\n"
		"An accountant does not ask for \"a report\". They ask for one of a dozen shapes they have\n"
		"used for thirty years, by name, and they expect all of them to exist. The list is short and\n"
		"worth knowing before designing anything:\n"
		" TRIAL BALANCE - every account, with opening balance, turnover and closing balance.\n"
		" The one that is opened first, every morning.\n"
		" TRIAL BALANCE FOR ONE ACCOUNT - the same, broken down by that account's analytics.\n"
		" ACCOUNT TURNOVER, ACCOUNT ANALYSIS, ACCOUNT CARD - the movements of one account: as\n"
		" totals per period, as correspondence with other accounts, and as the list of entries\n"
		" themselves with a running balance.\n"
		" ANALYTICS ANALYSIS, ANALYTICS CARD, TURNOVER BETWEEN ANALYTICS - the same three\n"
		" questions asked of a dimension instead of an account.\n"
		" CHESS SHEET - every account against every account, one cell per correspondence.\n"
		" GENERAL LEDGER, ENTRY REPORT, SUMMARY ENTRIES - the statutory readings of the same\n"
		" data.\n"
		"\n"
		"KEY: THE COLUMNS ARE THE SHAPE, and they are the same everywhere: three PAIRS - opening\n"
		"balance, period turnover, closing balance - each split into debit and credit, under a\n"
		"spanning heading. Six columns, three headings, and an accountant reads them without looking\n"
		"at the titles. Getting this layout right matters more than any amount of styling.\n"
		"\n"
		"THE ROWS ARE THE CHART OF ACCOUNTS, folded: 13 with 131, 132, 133 under it, each level\n"
		"totalling its children. Below the account come its ANALYTICS, level by level, and which\n"
		"ones are shown is the reader's choice rather than the author's.\n"
		"\n"
		"KEY: AND THE THING THAT MAKES THE FAMILY A FAMILY: A CELL LEADS TO ANOTHER REPORT. Standing\n"
		"on the turnover of account 20, a person asks for the trial balance of 20, its card, its\n"
		"analysis, its turnover by month or by day - and lands there with the account, the period\n"
		"and the analytics ALREADY SET. The drill-down is not a detail window: it is the next report\n"
		"of the same family, opened with the context carried over. Design them together or the\n"
		"chain breaks at the first hop.\n"
		"\n"
		"* TWO INDEPENDENT AXES, AND THE ANALYSIS REPORT IS WHERE THAT SHOWS. \"Account analysis\"\n"
		"groups by the account AND by the accounts it corresponded with - two grouping settings side\n"
		"by side, each with its own list of levels, because the question is \"where did this money\n"
		"come from and go to\". A single ladder cannot express it: the correspondent account is not\n"
		"a level under the account, it is the other end of the same entry.\n"
		"\n"
		"AND THE SECOND AXIS CAN BE PUT ACROSS instead of down: the turnover report shows the\n"
		"correspondent accounts AS COLUMNS - opening balance, debit turnover, then a column per\n"
		"account it corresponded with, then credit turnover and the closing balance. That is a cross\n"
		"table (`form-to-areas`, `report-shapes`) with the ledger's own vocabulary, and the columns\n"
		"fold like the rows do, because there can be dozens of them.\n"
		"\n"
		"THREE MORE CONTROLS THAT ARE PARTICULAR TO THIS FAMILY:\n"
		" WHICH FIGURES, SEPARATELY FOR DEBIT AND CREDIT - opening balance, closing balance,\n"
		" turnover, each with its own pair of tick boxes. Six switches, not three: an accountant\n"
		" chasing one side of an account does not want the other.\n"
		" PERIODICITY OF THE TURNOVER - by month, by day, for the period as a whole. It adds a\n"
		" TIME LEVEL to the rows, which is why it is a setting and not a second report.\n"
		" AND THE CHART BUTTON beside the settings: the same composition drawn instead of\n"
		" tabulated. Free once the figures are grouped, and it is what gets shown to a director.\n"
		"\n"
		"NOTE: AND THE ROWS ARE NOT ALL GROUPINGS. Inside each level sit three FIXED rows - opening\n"
		"balance, turnover, closing balance - which are not values of any field: they are the\n"
		"skeleton of accounting, printed for every account whether there is data or not. A report\n"
		"built purely out of groupings cannot produce them, and their absence is the first thing an\n"
		"accountant notices.\n"
		"\n"
		"WHAT THE READER IS GIVEN CONTROL OF, on a panel beside the result rather than in a dialog:\n"
		" WHICH FIGURES - accounting data, tax data, the difference between them, the currency\n"
		" amount - as tick boxes, because they are columns switched on and off.\n"
		" WHICH GROUPINGS - by sub-account, by which analytics, in what order.\n"
		" EXPANDED BALANCE, per account: whether debit and credit are shown separately rather\n"
		" than netted. This is an accounting question with no analogue elsewhere, and leaving it\n"
		" out makes the report wrong for exactly the accounts that need it.\n"
		" SELECTION - the ordinary filter, last.\n"
		"\n"
		"NOTE: AND A NEGATIVE FIGURE IN RED IS NOT DECORATION. On a balance report it means the account\n"
		"is standing the wrong way round, which is a fact somebody has to act on - the report's job\n"
		"is to make it impossible to miss.") },

	{ wxT("report-shapes"),
	  ibMcpText("The four shapes a report takes on screen, and what each one is made of."),
	  ibMcpText("WHAT A REPORT LOOKS LIKE WHEN IT IS RUN.\n"
		"\n"
		"Four shapes cover nearly everything anybody asks for, and they are not four mechanisms -\n"
		"they are the same composition with different levels declared.\n"
		"\n"
		"1. A LIST WITH TOTALS. One grouping, the figures beside it, a total at the foot. \"Sales by\n"
		" customer.\" The plain case, and often the right one.\n"
		"\n"
		"2. A LADDER OF GROUPINGS - the workhorse. Department, then counterparty, then the document,\n"
		" then the goods on it, each level indented under the one above and each carrying its own\n"
		" totals across every column. A person opens the level they are asking about and leaves\n"
		" the rest folded, so the same report answers \"how did the department do\" and \"which line\n"
		" of which delivery note\" without being two reports.\n"
		" * A LEVEL MAY CARRY DETAILS OF ITS OWN: the document level shows its number and date in\n"
		" columns of their own, beside the money. Those are FIELDS OF THAT LEVEL, not resources -\n"
		" they are not summed, they identify the row. Mixing the two produces a report that adds\n"
		" up document numbers.\n"
		"\n"
		"3. A CROSS TABLE - a grouping put ACROSS the columns instead of down: years or months or\n"
		" warehouses along the top, the ladder down the side, one resource in the cells and a\n"
		" total column at the right end. Asked for in the words \"by month\", \"per warehouse\",\n"
		" \"compared with last year\". See `form-to-areas` for the same shape when it is printed\n"
		" onto a fixed blank instead.\n"
		"\n"
		"4. A DETAIL LISTING - no grouping at all, one line per record, used for checking rather\n"
		" than for reading: which orders mention this article, which lines carry this comment.\n"
		" Its whole design is the SELECTION, not the structure.\n"
		"\n"
		"5. A STATUTORY RETURN, and it is not built the way the four above are - it is FILLED IN. A\n"
		" balance sheet or a profit-and-loss return is a blank fixed by law: the rows are given,\n"
		" in a given order, each with the LINE CODE the law refers to it by (010, 015, 035...), and\n"
		" two columns - this period and the previous one. Nothing about its shape is a design\n"
		" decision, and a composer is the wrong tool for it because there is nothing to group.\n"
		" What it needs instead:\n"
		" A RULE PER CELL saying where its figure comes from - a turnover, a balance, a sum of\n"
		" accounts - so \"Fill\" computes the whole sheet from the books.\n"
		" CELLS A PERSON MAY OVERWRITE, and those edits SURVIVE the next \"Fill\": an accountant\n"
		" corrects what the automatic rule cannot know, and losing that on a refill is the\n"
		" defect people remember. Computed cells and hand-entered ones are visibly different -\n"
		" colour is the usual way.\n"
		" DRILL-DOWN FROM A CELL to what made it. A number on a statutory form that cannot be\n"
		" explained is a number nobody will sign.\n"
		" ARITHMETIC BETWEEN ROWS, stated once: line 035 is 010 less 015 and 020, and the sheet\n"
		" checks itself. The control sum in the header is the same idea for the whole return.\n"
		" AND THE FORM'S OWN IDENTITY AS TEXT - the appendix number, the regulation and the\n"
		" classifier codes of the organisation - exactly as on any official blank\n"
		" (`form-to-areas`).\n"
		" KEY: AND HERE IS HOW IT IS ACTUALLY ASSEMBLED, because the shape hides the mechanism: the\n"
		" blank is a TEMPLATE, and the form carries a SPREADSHEET FIELD that shows it. The\n"
		" spreadsheet document is not only a thing that gets printed - put on a form it is an\n"
		" editable control, so the return a person sees IS the template, opened into the field.\n"
		" Filling it is then not drawing anything: the cells are NAMED, and the code walks the\n"
		" names it knows and puts values into them. The accountant's corrections come back out of\n"
		" the same named cells, which is what makes hand edits survivable, and a cell can carry a\n"
		" detail value pointing at what produced it - that is the drill-down. Build the blank\n"
		" once, name what is fillable, and the whole return is a loop over names.\n"
		"\n"
		" Four more habits, all of them from the blank rather than from the accounting:\n"
		" A CELL MARKED \"x\" IS FORBIDDEN, not empty. The form says this intersection has no\n"
		" meaning - a permanent difference on a row that cannot have one - and the mark is part\n"
		" of the layout. Filling it, even with a zero, is an error on the return.\n"
		" LINE CODES ARE HIERARCHICAL: 01, then 01.1, 01.2, 01.3 \"of which\". The parts have to\n"
		" add up to their parent, and that arithmetic is as much part of the form as the\n"
		" column totals.\n"
		" THE HEADING SPANS: one title over two columns (\"tax differences\" over \"permanent\" and\n"
		" \"temporary\"), numbered 1...7 underneath. The same nesting as a vertical area on a\n"
		" printed blank, and for the same reason.\n"
		" UNIT AND PRECISION ARE PARAMETERS OF THE WHOLE SHEET - in currency units or thousands,\n"
		" to so many decimals - and so is the choice of HOW it is filled: from the books, or\n"
		" from figures a person typed. Both belong on the form's bar, above the blank, because\n"
		" they change every number on it at once.\n"
		"\n"
		"WHAT THE FORM AROUND IT NEEDS, and it is the same for the first four:\n"
		" ONE LOUD COMMAND - \"Generate\" - because until it is pressed the page is empty and\n"
		" nothing else on the screen matters.\n"
		" THE VARIANT PICKER BESIDE IT, by name: this is where the several views of one report\n"
		" become visible to the person, and the reason a variant must be named (`report-variants`).\n"
		" SETTINGS one click away, not on the page: period, selection, which levels to show. The\n"
		" two or three parameters people change EVERY time - a period, a department - are worth\n"
		" lifting out onto the form itself; the rest belong behind the button.\n"
		"\n"
		"KEY: AND A REPORT CAN HAVE A FORM OF ITS OWN, WITH ATTRIBUTES ON IT - which is a different\n"
		"mechanism from a variant, and the two are easy to confuse:\n"
		" A VARIANT is a named SHAPE - which groupings, which resources, which selection - and it\n"
		" is picked by name from a list. Several answers, one query.\n"
		" A FORM ATTRIBUTE is a VALUE the person types or picks before generating: the period,\n"
		" the warehouse, the organisation, a threshold. It is bound to a query parameter, and\n"
		" there is only one of it.\n"
		"The test: does the person CHOOSE A VIEW, or ENTER A VALUE? Views are variants; values are\n"
		"attributes on the form.\n"
		"\n"
		"* WHAT THE OWN FORM BUYS beyond the two or three fields: defaults that are computed (this\n"
		"month, my department), fields that depend on each other (choose a warehouse, and the\n"
		"storage places narrow), a validation before anything is generated, and commands of the\n"
		"report's own - print this in the customer's layout, send it, save the selection as a\n"
		"variant. A generated form gives none of that and is exactly right until one of them is\n"
		"asked for.\n"
		"NOTE: And keep the WORK out of the form (`form-layout`): the form collects values and asks the\n"
		"report to run. A form that computes the figures itself cannot be run by a scheduled job,\n"
		"which is how monthly reports actually get delivered.\n"
		" AND A FILTER BOX OVER THE RESULT for the detail listing, because that shape exists to\n"
		" be searched.\n"
		"\n"
		"NOTE: THE QUESTION THAT PICKS THE SHAPE: what does the person want to compare? Nothing - a\n"
		"list. Parts against a whole - a ladder. One dimension against another - a cross table.\n"
		"Nothing at all, they want to FIND something - a detail listing with a filter.") },

	{ wxT("report-variants"),
	  ibMcpText("One report, several named views - and why that beats three reports."),
	  ibMcpText("SEVERAL QUESTIONS, ONE REPORT.\n"
		"\n"
		"People ask for what looks like several reports: \"sales\", \"sales with gross margin\",\n"
		"\"sales by manager\", \"the same thing but only for one warehouse\". Underneath they are one\n"
		"query answered with different groupings, different resources and different filters - and\n"
		"that is exactly what a VARIANT is.\n"
		"\n"
		"A variant is a NAMED SETTING over the composer's query. It carries the outputs, the levels,\n"
		"the resources and the selection; it does not carry a query of its own. The person running\n"
		"the report picks one by name, and the composer runs on the setting inside it.\n"
		"\n"
		"* SO BUILD ONE REPORT AND SEVERAL VARIANTS. Three reports for those three questions means\n"
		"three copies of one query to keep in step: a field added to the source has to be added\n"
		"three times, and the day it is added twice they disagree without anybody being told.\n"
		"\n"
		"THE NAME IS THE WHOLE OF WHAT A VARIANT ADDS, which is why a nameless one is refused when\n"
		"the configuration is saved. `report_variant` names one, and with `add` makes another\n"
		"starting from the settings of the one you point at - which is what a second view usually\n"
		"is: the first with a column added or a grouping taken away.\n"
		"\n"
		"The SYNONYM is what the person actually reads in the picker, so it is written in their\n"
		"language while the name stays the identifier code refers to.\n"
		"\n"
		"NOTE: WHEN IT IS GENUINELY A SECOND REPORT: when the QUERY differs - another table, another\n"
		"join, another period bound. Variants are settings over one query, and forcing two unlike\n"
		"queries into one composer means a query that reads more than either question needed.") },
	};

	return s_patterns;
}

// ⭐⭐ A PATTERN IS READ IN LAYERS, and the reason is arithmetic. The corpus grew past a hundred
// kilobytes: handing over a whole entry to answer one question spends more than the answer is
// worth, and the parts nobody asked for crowd out what they did.
//
// So the entries are CUT INTO TOPICS - by the headings they already carry, rather than by a second
// structure written beside them. Asking for a pattern gives its summary and the list of what is
// inside; asking for a topic gives that part in full. Nothing is rewritten and nothing can drift,
// because the table of contents IS the text.
//
// ⚠ WHAT COUNTS AS A HEADING is deliberately narrow: a line that opens a paragraph and begins with
// two or more WORDS IN CAPITALS, after any marker. That is how this corpus is written throughout,
// so the split follows the author's own emphasis - and a paragraph that is not marked stays with
// the topic above it, which is the correct reading of an unmarked continuation.
struct ibMcpTopic {
	wxString m_title;
	wxString m_body;
};

bool LooksLikeHeading(const wxString& line)
{
	size_t at = 0;

	// Past the indent and past whatever marker opens the line - a star, a stop sign, a bullet.
	while (at < line.length() && !wxIsalpha(line[at]))
		at++;

	int capitalised = 0;

	while (at < line.length()) {

		size_t start = at;
		while (at < line.length() && wxIsalpha(line[at]))
			at++;

		const size_t length = at - start;
		if (length == 0)
			break;

		// ⚠ A SHORT WORD DOES NOT BREAK THE RUN, it is simply not counted. Half the headings in this
		// corpus open with "A …" or "AND A …", and a rule that stopped at the first one- or
		// two-letter word found no heading at all - every entry came back as one topic called
		// "Overview" (caught by running the split over the corpus before it ever shipped).
		bool shouts = true;
		for (size_t index = start; shouts && index < at; index++)
			shouts = wxIsupper(line[index]) != 0;

		if (!shouts)
			break;

		if (length >= 3)
			capitalised++;

		// ⚠ WHAT SEPARATES THE WORDS OF A HEADING IS NOT ONLY A SPACE. The marks that open them are
		// written as words now - `KEY:`, `STOP:`, `NOTE:` - and a colon stopping the run took every
		// one of those headings out of the table of contents, which is exactly the set that matters
		// most (caught by reading a live answer, 2026-09-02: the contents came back holding the
		// ordinary paragraphs and none of the marked ones). So anything that is not a letter is
		// stepped over between words; the run ends when a word is not in capitals.
		while (at < line.length() && !wxIsalpha(line[at]))
			at++;
	}

	return capitalised >= 2;
}

std::vector<ibMcpTopic> TopicsOf(const wxString& text)
{
	std::vector<ibMcpTopic> topics;

	wxString current;
	wxString title = wxT("Overview");   // whatever stands before the first heading
	bool paragraphStart = true;

	size_t at = 0;

	while (at <= text.length()) {

		const size_t end = text.find(wxT('\n'), at);
		const wxString line = text.Mid(at, (end == wxString::npos ? text.length() : end) - at);

		if (line.IsEmpty()) {
			paragraphStart = true;
		}
		else {
			if (paragraphStart && LooksLikeHeading(line) && !current.IsEmpty()) {
				topics.push_back({ title, current });
				current.Clear();
				title = line;
			}
			else if (paragraphStart && LooksLikeHeading(line)) {
				title = line;
			}
			paragraphStart = false;
		}

		current << line << wxT("\n");

		if (end == wxString::npos)
			break;
		at = end + 1;
	}

	if (!current.IsEmpty())
		topics.push_back({ title, current });

	return topics;
}

// How many lines a topic is - so a table of contents says the SIZE of what it offers and not only
// its name. Without it, "WHAT A FULL CLOSE ACTUALLY CONTAINS" and a two-line aside look alike from
// outside, and the only way to find out is to fetch one and see.
size_t LinesIn(const wxString& body)
{
	size_t lines = 0;

	for (size_t at = 0; at < body.length(); at++)
		if (body[at] == wxT('\n'))
			lines++;

	return lines;
}

// The line a reader would have found themselves - the first one carrying any word of the query,
// so a hit can be judged before it is opened.
wxString MatchingLine(const wxString& body, const wxString& query)
{
	// ⭐ THE LINE IS FOUND BY THE SAME RULE THAT FOUND THE TOPIC (ibMcpWordsFound) - stems, and a
	// regular expression when that is what was written. Anything else and the two disagree: the
	// hit is real, the line quoted under it is not the one that matched, and the caller judges the
	// passage by a sentence that has nothing to do with their question.
	size_t at = 0;

	while (at <= body.length()) {

		const size_t end = body.find(wxT('\n'), at);
		const wxString line = body.Mid(at, (end == wxString::npos ? body.length() : end) - at);

		if (ibMcpWordsFound(line, query) > 0) {
			wxString trimmed = line;
			trimmed.Trim(true).Trim(false);
			return trimmed.length() > 200 ? trimmed.Left(197) + wxT("...") : trimmed;
		}

		if (end == wxString::npos)
			break;
		at = end + 1;
	}

	return wxEmptyString;
}

// The heading as a LABEL - the sentence a reader picks from, not the paragraph it opens.
wxString TopicLabel(const wxString& title)
{
	wxString label = title;
	label.Trim(true).Trim(false);

	// Cut at the first full stop or dash: the heading states its subject and then explains it.
	for (const wxChar* stop : { wxT(". "), wxT(" - "), wxT(", ") }) {
		const int at = label.Find(stop);
		if (at != wxNOT_FOUND && at > 12) {
			label = label.Left(at);
			break;
		}
	}

	if (label.length() > 96)
		label = label.Left(93) + wxT("...");

	return label;
}

const ibMcpPattern* FindPattern(const wxString& name)
{
	for (const ibMcpPattern& pattern : Patterns())
		if (name.IsSameAs(pattern.m_name, false))
			return &pattern;
	return nullptr;
}

} // namespace

//---------------------------------------------------------------------------
// patterns
//---------------------------------------------------------------------------

class ibMcpToolPatternRead : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("pattern_read"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		const wxString name = ArgName().Text(params);
		return name.IsEmpty()
			? ibMcpText("listing the patterns")
			: wxString::Format(ibMcpText("reading the '%s' pattern"), name);
	}

	wxString GetDescription() const override
	{
		return ibMcpText("A translator from the language of the people who ask - accountants, warehouse "
			"managers, anyone describing their work - into the shapes this platform has. They do "
			"not say \"accumulation register\"; they say \"I want to see it monthly\", \"how much "
			"is left\", \"the account it goes to\", and each of those names a shape exactly. It "
			"also holds how the mechanisms behind them are built: costing, lots, month-end close, "
			"settlements, printed forms, screen layout, roles.\n"
			"READ IT IN LAYERS, as needed, rather than up front: `query` with the words of the "
			"problem answers WHERE it is covered - pattern, topic, matching line; `name` alone "
			"gives a summary and what is inside; `name` with `topic` gives that part in full. No "
			"argument lists everything there is. Recommendations, not rules. Worth consulting "
			"BEFORE creating a metaobject - the choice of shape is the one decision that is "
			"expensive to revisit once there is data in it.");
	}

	// ⭐⭐ THE CORPUS IS THE INDEX. This tool is a door onto a body of text written in the WORDS OF
	// THE PEOPLE WHO ASK — "how much is left", "I want to see it monthly", "the account it goes
	// to" — and those are exactly the words a caller searches with. The description above says
	// what the door is; it does not contain them, so searching it missed every one.
	//
	// Handing the whole corpus over is what keeps this true without a second list to maintain: an
	// entry added tomorrow is findable by its own sentences the moment it is written. Held once,
	// built on first use — the texts are already static, so this is a concatenation and not a copy
	// of anything.
	wxString GetSearchText() const override
	{
		static wxString s_index;

		if (s_index.IsEmpty()) {
			for (const ibMcpPattern& pattern : Patterns())
				s_index << pattern.m_name << wxT("\n")
					<< pattern.m_summary << wxT("\n") << pattern.m_text << wxT("\n");
		}

		return s_index;
	}

	// ⭐⭐ WHERE IN THE CORPUS THE ANSWER IS - written once and asked by both doors: this tool's own
	// `query`, and mcp_search with the same words. Two implementations of "find the passage" would
	// drift within a week, and the caller would learn to distrust whichever one they hit second.
	//
	// Ranked, not filtered: everything that met every word if anything did, and otherwise the best
	// there was, said as "3 of your 5 words" so a partial answer is never mistaken for a complete
	// one. That is the difference between a search that fails politely and one that says "there is
	// nothing about this" - which, about a corpus this size, is never true and always acted on.
	void FindInside(const wxString& query, std::vector<ibDataValue>& places) const override
	{
		if (query.IsEmpty())
			return;

		struct ibPlace { const ibMcpPattern* m_pattern; ibMcpTopic m_topic; size_t m_score; };

		std::vector<ibPlace> scored;
		size_t asked = 0, best = 0;

		for (const ibMcpPattern& pattern : Patterns()) {

			for (const ibMcpTopic& topic : TopicsOf(pattern.m_text)) {

				// The pattern's NAME and SUMMARY count towards the topic's score: a caller asking
				// "lots" should reach every part of the lots entry, not only the paragraphs that
				// happen to repeat the word.
				const wxString haystack = wxString(pattern.m_name) + wxT("\n")
					+ pattern.m_summary + wxT("\n") + topic.m_body;

				const size_t score = ibMcpWordsFound(haystack, query, &asked);
				if (score == 0)
					continue;

				best = std::max(best, score);
				scored.push_back({ &pattern, topic, score });
			}
		}

		const bool partial = best > 0 && best < asked;
		size_t given = 0;

		for (const ibPlace& place : scored) {

			if (place.m_score < best)
				continue;

			// A single common word can stand in half the corpus, and an address list nobody can
			// read is the problem this tool exists to solve, one level up.
			if (++given > 24)
				break;

			std::shared_ptr<ibDataNode> hit = std::make_shared<ibDataNode>();
			hit->SetValue(wxT("pattern"), wxString(place.m_pattern->m_name));
			hit->SetValue(wxT("topic"), TopicLabel(place.m_topic.m_title));

			if (partial)
				hit->SetValue(wxT("matched"), wxString::Format(
					ibMcpText("%i of your %i words"), (int)place.m_score, (int)asked));

			// The line that matched, so a caller can judge the hit without fetching it.
			const wxString line = MatchingLine(place.m_topic.m_body, query);
			if (!line.IsEmpty())
				hit->SetValue(wxT("line"), line);

			places.push_back(ibDataValue::Child(hit));
		}
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgQuery(), ArgName(), ArgTopic() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		const wxString name = ArgName().Text(params);
		const wxString query = ArgQuery().Text(params);

		// ⭐ SEARCH FIRST, IF THERE IS ONE. A caller with words and no name gets addresses back -
		// pattern, topic, and the line that matched - and reads exactly the part that answered.
		if (!query.IsEmpty()) {

			std::vector<ibDataValue> hits;
			FindInside(query, hits);

			if (hits.empty()) {
				refusal = wxString::Format(
					ibMcpText("Not one word of '%s' appears in the patterns. Say it in other words, or "
					  "ask with no argument to see what the corpus covers."), query);
				return false;
			}

			result.AddField(wxT("found"), ibDataValue::Int((s64)hits.size()));
			result.AddField(wxT("hits"), ibDataValue::Array(hits));
			result.SetValue(wxT("note"), hits.size() >= 24
				? ibMcpText("Read one with {name, topic} - quoting the topic line above. This many "
				  "addresses means the words are common ones: add a word to narrow it.")
				: ibMcpText("Read one with {name, topic} - quoting the topic line above."));
			return true;
		}

		// NO NAME — THE LISTING. Summaries only: the point of a listing is to choose from it, and
		// a listing that inlines every text is the `instructions` problem again, one call later.
		if (name.IsEmpty()) {

			std::vector<ibDataValue> out;

			for (const ibMcpPattern& pattern : Patterns()) {
				std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
				entry->SetValue(wxT("name"), wxString(pattern.m_name));
				entry->SetValue(wxT("summary"), pattern.m_summary);
				out.push_back(ibDataValue::Child(entry));
			}

			result.AddField(wxT("patterns"), ibDataValue::Array(out));
			result.SetValue(wxT("note"),
				ibMcpText("Ask for one by name. These are recommendations - what they are FOR is said in "
				  "each, so a case they do not fit can be recognised rather than forced."));

			// ⭐⭐ THE WARNING THAT BELONGS ON THE LISTING RATHER THAN INSIDE ONE ENTRY, because it
			// is about what the reader brings WITH them and therefore applies before any entry is
			// opened. A model arrives fluent in other ERPs and will answer a domain question out
			// of that fluency - describing currency accounting, or costing, or period close, in
			// another system's model - and it reads as competence rather than as an import. It is
			// the same failure as writing another language's syntax, one floor up: it looks right
			// and does not fit. Named because it was observed, repeatedly (Max, 2026-08-31: "you
			// keep telling me about currency accounting out of the SAP world").
			result.SetValue(wxT("beware"),
				ibMcpText("If you know other ERP systems, notice when an answer is coming from one of "
				  "them. Their concepts have familiar names here and different edges, and a "
				  "design imported wholesale looks reasonable until it meets the platform it is "
				  "not built on. What is written here is what THIS platform has and how the "
				  "people who use it describe their work - prefer it to what you already know, "
				  "and when something is genuinely missing say so plainly instead of "
				  "substituting the shape you are used to."));
			return true;
		}

		const ibMcpPattern* found = FindPattern(name);

		// ⚠ NAMED AND NOT FOUND IS A REFUSAL, not an empty answer. A caller who mistyped a name
		// and got `{}` back reads it as "there is no advice about this" and proceeds.
		if (found == nullptr) {
			wxString known;
			for (const ibMcpPattern& pattern : Patterns()) {
				if (!known.IsEmpty())
					known += wxT(", ");
				known += pattern.m_name;
			}
			refusal = wxString::Format(
				ibMcpText("There is no pattern called '%s'. There is: %s."), name, known);
			return false;
		}

		result.SetValue(wxT("name"), wxString(found->m_name));
		result.SetValue(wxT("summary"), found->m_summary);

		const std::vector<ibMcpTopic> topics = TopicsOf(found->m_text);
		const wxString wanted = ArgTopic().Text(params);

		// EVERYTHING, when asked for it - or when the entry is short enough that a table of
		// contents would cost more than the text it describes.
		if (wanted.IsSameAs(wxT("all"), false) || topics.size() < 3
			|| found->m_text.length() < 2500) {
			result.SetValue(wxT("text"), found->m_text);
			return true;
		}

		// ONE PART, named by any words of its heading. Matched loosely on purpose: a caller quotes
		// the listing approximately, and refusing a near miss teaches them to fetch the whole thing
		// instead - which is the behaviour this exists to avoid.
		if (!wanted.IsEmpty()) {

			wxString answered;
			std::vector<ibDataValue> parts;

			for (const ibMcpTopic& topic : topics) {
				if (topic.m_title.Lower().Find(wanted.Lower()) == wxNOT_FOUND
					&& TopicLabel(topic.m_title).Lower().Find(wanted.Lower()) == wxNOT_FOUND)
					continue;

				std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
				entry->SetValue(wxT("topic"), TopicLabel(topic.m_title));
				entry->SetValue(wxT("text"), topic.m_body);
				parts.push_back(ibDataValue::Child(entry));
			}

			if (!parts.empty()) {
				result.AddField(wxT("parts"), ibDataValue::Array(parts));
				return true;
			}

			refusal = wxString::Format(
				ibMcpText("'%s' has no topic matching '%s'. Ask it without a topic to see what is in it, "
				  "or with topic 'all'."), name, wanted);
			return false;
		}

		// THE TABLE OF CONTENTS - what this entry holds, so the next call fetches one part rather
		// than a hundred lines to answer one question.
		std::vector<ibDataValue> contents;

		for (const ibMcpTopic& topic : topics) {

			// ⭐ A NAME WITHOUT A SIZE IS HALF A TABLE OF CONTENTS. Two headings read alike from
			// outside while one is four lines and the other is forty, so the only way to find out
			// which is which is to fetch them - which is precisely the cost this listing exists to
			// avoid. The weight makes the next call a decision rather than a probe.
			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
			entry->SetValue(wxT("topic"), TopicLabel(topic.m_title));
			entry->AddField(wxT("lines"), ibDataValue::Int((s64)LinesIn(topic.m_body)));

			contents.push_back(ibDataValue::Child(entry));
		}

		result.AddField(wxT("topics"), ibDataValue::Array(contents));
		result.SetValue(wxT("note"),
			ibMcpText("Read one with `topic`, quoting any words of its line above; `topic: \"all\"` gives "
			  "the whole entry. `lines` is how long each part is."));
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolPatternRead);
