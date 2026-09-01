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

#include <vector>

namespace {

using ibArg = ibMcpTool::ibMcpArgument;
const ibArg& ArgName() { static const ibArg a(wxT("name"), ibArg::Kind::Text, _("Which pattern to read in full. Omit it to list what there is.")); return a; }

// ONE ENTRY. `name` is the address, `summary` is what the listing shows, `text` is the whole of it.
struct ibMcpPattern {
	const wxChar* m_name;
	wxString      m_summary;
	wxString      m_text;
};

// ⚠ HELD AS A FUNCTION, not a file-scope table: the texts are translated, and a static built
// before the locale is set would freeze the untranslated strings for the life of the process.
const std::vector<ibMcpPattern>& Patterns()
{
	static const std::vector<ibMcpPattern> s_patterns = {

	{ wxT("shapes"),
	  _("Which metatype a described need asks for - and the words that give it away."),
	  _("READING A REQUEST FOR ITS SHAPE.\n"
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
		"⚠ AND THE GENERAL FORM OF ALL OF THESE: \"I want to see it by month\", \"per warehouse\",\n"
		"\"as of a date\" is a request for a REGISTER of some kind. A catalogue or a document that\n"
		"someone reports over is the shape that has to be unpicked later, once the data is real.") },

	{ wxT("predefined"),
	  _("A particular item named in conversation has to exist as a predefined one."),
	  _("A NAMED INSTANCE IS A PREDEFINED ITEM.\n"
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
		"the sentence would still be true of any row of that catalogue, it is not this pattern.") },

	{ wxT("where-to-start"),
	  _("The order configurations actually grow in - useful for knowing what comes next."),
	  _("WHAT APPEARS, AND ROUGHLY IN WHAT ORDER.\n"
		"\n"
		"Almost every configuration grows the same way, and knowing the sequence is worth more\n"
		"than it sounds: it tells you what is coming, so that what you build today does not have\n"
		"to be unpicked to make room for it.\n"
		"\n"
		"  1. GOODS MOVING. Receipt, transfer between warehouses, write-off, sale. This is the\n"
		"     spine, and almost everything later hangs off it. Get the stock registers right here\n"
		"     - see `shapes` for balances against turnovers - because everything downstream reads\n"
		"     them.\n"
		"  2. MONEY AND WHO OWES IT. Settlements with counterparties, contracts, currency (see\n"
		"     `settlements`, `currency`). Arrives as soon as the goods do, and often the same\n"
		"     week.\n"
		"  3. PRODUCTION, if there is any. The shift production report, transport documents,\n"
		"     material consumption - and with it costs, lots and the month-end close (see\n"
		"     `production`, `lot-accounting`).\n"
		"  4. PAYROLL, if there is any. Work orders and piece records, accrual, deductions. It is\n"
		"     its own world with its own periodicity and its own rules, and it touches the cost\n"
		"     side at the close.\n"
		"\n"
		"⭐ THE USE OF KNOWING THIS is not to build it all. It is to ask the one question that\n"
		"stops a design being cornered: is there production here? is there payroll? A stock model\n"
		"laid out without knowing that costs will have to be allocated over it is the model that\n"
		"gets rebuilt in month four - and the answer costs one question at the start.") },

	{ wxT("production"),
	  _("Costs, month-end close and work in progress - and why the close is several documents."),
	  _("PRODUCTION, COSTS AND CLOSING THE MONTH.\n"
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
		"CLOSING THE MONTH IS NOT ONE OPERATION. It is a pile of regulated ones - distributing\n"
		"overheads, valuing output, adjusting actual cost, writing off what was consumed - and\n"
		"they run in an order that matters. Two ways to carry it:\n"
		"  · ONE document that performs all of them. Simple, and the whole month closes or does\n"
		"    not.\n"
		"  · A DOCUMENT PER AREA, each closing its own section. More pieces, and much better in\n"
		"    practice: rights can be granted per area, so the person who closes payroll is not the\n"
		"    person who closes stock; a failure is localised to its own step; and a step can be\n"
		"    re-run without redoing the rest. Prefer this once there is more than one person\n"
		"    involved - the roles are the reason, not the tidiness.\n"
		"\n"
		"WORK IN PROGRESS IS THE PENNIES AGAIN. What is left unfinished at the close has to take\n"
		"its share of the costs, and that share is a distribution - the same arithmetic, the same\n"
		"remainder, the same decision about where the odd penny lands (see `allocation`). It is\n"
		"the part people find hardest, and the reason is not conceptual: it is that the same\n"
		"rounding problem is met for the third time, now with the numbers mattering to a tax\n"
		"authority.\n"
		"\n"
		"⚠ THE ORDER OF THE CLOSE IS PART OF THE DESIGN, not an implementation detail. Write it\n"
		"down - in the notes of whatever performs it - because it is invisible in the code and the\n"
		"next person will reorder it while making something else work.\n"
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
		"⚠ NAME ACCOUNTS BY WHAT THEY ARE, NEVER BY THEIR NUMBER. A chart of accounts is\n"
		"legislation: the numbering differs by country and changes with the law, while the ROLES\n"
		"are the same everywhere - the account work in progress accumulates on, the account stock\n"
		"is held on, the accounts production costs are gathered into. Think and write in those\n"
		"terms, and take the actual accounts from the chart the configuration has (a predefined\n"
		"account is exactly how code holds onto one - see `predefined`). A mechanism with numbers\n"
		"written into it is wrong the first time it meets another chart, and wrong silently: the\n"
		"posting still goes somewhere.\n"
		"\n"
		"⭐ AND THE CHAIN ENDS AT THE FINANCIAL RESULT. The last document of the period closes the\n"
		"settlements and determines what was earned - everything above exists to make that figure\n"
		"correct. It is called different things in different jurisdictions and the concept is\n"
		"everywhere; if a design has no such terminal step, the question to ask is not \"do you\n"
		"need one\" but \"where do you close the period today\".") },

	{ wxT("lot-accounting"),
	  _("Lots and cost: one shared write-off, and cost resolved over a graph of movements."),
	  _("LOT ACCOUNTING.\n"
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
		"⭐⭐ AND COST IS NOT KNOWN WHEN THE MOVEMENT HAPPENS. This is the part that surprises\n"
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
		"⚠ THE CONSEQUENCES ARE PRACTICAL, and they decide the design:\n"
		"  · the ORDER of resolution matters, and it is the order of the graph, not of document\n"
		"    numbers or of dates alone;\n"
		"  · a cycle in it (goods returned back up the chain, mutual transfers) is a real\n"
		"    possibility and has to be recognised rather than looped over forever;\n"
		"  · the figures a document showed at posting time are PROVISIONAL, and something has to\n"
		"    say so - to the user, and in the notes of whatever stores them.\n"
		"\n"
		"Building this per-document, at posting, produces numbers that look right on the day and\n"
		"are never right afterwards.") },

	{ wxT("settlements"),
	  _("Settlements with counterparties: contracts, and which currency the balance is in."),
	  _("MUTUAL SETTLEMENTS.\n"
		"\n"
		"THE TWO LEVELS, and it is worth knowing which one is being asked for.\n"
		"  · By COUNTERPARTY alone - one running balance per partner. The primitive level: it\n"
		"    works, and it cannot answer \"which of the two agreements is this against\".\n"
		"  · By CONTRACT, which is what people usually mean. A Contracts catalogue whose OWNER is\n"
		"    the counterparty - subordinate to it, so a contract cannot exist without one and the\n"
		"    choice lists are narrowed by construction. The contract carries its period of\n"
		"    validity and its CURRENCY.\n"
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
		"⭐ AND WHEN THE KINDS ARE NOT YET SEPARATED, carry them all from the start anyway. Adding\n"
		"a second amount later means revisiting every posting, every report and every balance ever\n"
		"computed - while an unused resource costs a column. This is one of the few places where\n"
		"building for a distinction nobody has asked for yet is the cheaper mistake.\n"
		"\n"
		"⚠ THE CURRENCY IS NOT DERIVABLE LATER. A balance in a register with no currency dimension\n"
		"cannot be told apart afterwards - the numbers are already added together. If there is any\n"
		"chance of a second currency, the dimension goes in now.") },

	{ wxT("currency"),
	  _("Currencies, rates with multiplicity, and why a document keeps its own copy."),
	  _("KEEPING ACCOUNTS IN MORE THAN ONE CURRENCY.\n"
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
		"⭐ THE DOCUMENT KEEPS ITS OWN COPY. Give documents their own Rate and Multiplicity\n"
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
	  _("A second set of books - IFRS, budgeting - and the forks worth knowing before choosing."),
	  _("KEEPING A SECOND SET OF BOOKS.\n"
		"\n"
		"Sooner or later someone asks for accounting alongside the accounting they already have -\n"
		"IFRS beside the statutory books, management figures beside both, a budget. There are\n"
		"several ways to carry it and they are not equally good; the choice is worth making\n"
		"deliberately, because it is the one that cannot be changed once there is a year of data.\n"
		"\n"
		"  · AN ACCUMULATION REGISTER WITH A CATALOGUE STANDING IN FOR ACCOUNTS. It works, and it\n"
		"    is the crudest of them: you have re-implemented a chart of accounts as ordinary data,\n"
		"    with no correspondence, no double entry, and nothing checking that the two sides\n"
		"    agree. Everything the accounting register does for free becomes code somebody\n"
		"    maintains. Reach for it only when what is wanted is genuinely not accounting.\n"
		"  · A CHART OF ACCOUNTS AND AN ACCOUNTING REGISTER OF ITS OWN. The straightforward\n"
		"    answer: a second set of books IS a second chart and a second register, and they cost\n"
		"    nothing to have side by side. If the words being used are an accountant's, this is\n"
		"    where to start.\n"
		"  · OPERATIONAL REGISTERS THAT FEED IT. Keep the working detail - lots, stock, movement -\n"
		"    on accumulation registers where it is cheap to read, and TRANSLATE from them into the\n"
		"    accounting register as postings. The operational side answers \"what is where\" fast;\n"
		"    the accounting side stays the record that balances. This is the usual shape of a\n"
		"    grown-up configuration, and it is worth designing towards even when starting small.\n"
		"    ⚠ The translation is then a mechanism with a moment and a direction: decide WHEN it\n"
		"    runs and what happens when the source is edited afterwards, and write that in the\n"
		"    notes - it is the part that rots quietly.\n"
		"\n"
		"BUDGETING IS THE SAME QUESTION WITH A DIFFERENT ANSWER. A budget is not usually\n"
		"correspondence at all - nothing is debited against anything. It is turnovers: amounts\n"
		"planned per period, per item, per SCENARIO, with the scenario as a dimension of its own\n"
		"so that plan, fact and a revision can sit in one register and be compared. Turnovers give\n"
		"the movement and balances give the standing figure, which together is what people mean by\n"
		"reading a budget - so an AccumulationRegister carries it without a chart of accounts\n"
		"anywhere in sight.\n"
		"\n"
		"⭐ THE QUESTION THAT DECIDES: does this have to BALANCE - two sides that must agree, an\n"
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
		"  · PARALLEL. Management and statutory accounting are kept side by side, and documents\n"
		"    carry the flags that say which of them this one is reflected in. Honest when the two\n"
		"    genuinely diverge - different valuation, different periods, things recognised in one\n"
		"    and not the other - and a real cost everywhere: every document, every report and\n"
		"    every user has to know which set they are looking at.\n"
		"  · FUSED. No flags at all: one truth, recorded once. Far simpler, and correct whenever\n"
		"    the two would only ever say the same thing. Do not build the parallel machinery on\n"
		"    the chance that they might diverge one day - that chance is paid for daily.\n"
		"  · TRANSLATED. Operational documents are what people actually work with; the accounting\n"
		"    postings are produced FROM them, typically by a scheduled job at month end. The\n"
		"    accountant receives a summary and works with that. Good when accounting is a\n"
		"    reporting obligation rather than the daily instrument.\n"
		"  · ACCOUNTING-LED. The accounting register is the primary record: the finance people\n"
		"    live in it, the balance is closed there, and everything else is derived. Right when\n"
		"    the business IS run in accounting terms.\n"
		"\n"
		"⭐⭐ AND YOU CANNOT READ THIS OFF THE REQUEST - ASK. Two questions settle it: \"will you\n"
		"keep management accounting?\" and \"will you keep statutory accounting?\". People answer\n"
		"them immediately, because they know their own work; guessing produces either machinery\n"
		"nobody wanted or a rebuild once the second kind of accounting turns up. It is one of the\n"
		"few questions worth asking before writing anything at all.") },

	{ wxT("constants"),
	  _("One value for the whole application, the same for everyone - and when it is not."),
	  _("WHAT A CONSTANT IS FOR.\n"
		"\n"
		"A Constant holds ONE value for the entire application: the same for every user, every\n"
		"company, every day. The configuration's own version; a global switch that changes how the\n"
		"whole program behaves; the organisation the base is kept for. If you can point at the one\n"
		"value and say \"this is simply what it is here\", it is a constant.\n"
		"\n"
		"THREE QUESTIONS SEND IT SOMEWHERE ELSE, and each of them is asked by someone eventually:\n"
		"  · \"What was it before?\" - a constant has no history. Writing it overwrites what was\n"
		"    there and the previous value is gone, with nothing recording when it changed. If the\n"
		"    old value will ever be needed - a rate, a limit, a responsible person - this is an\n"
		"    InformationRegister with `Periodicity` (see `shapes`), not a constant.\n"
		"  · \"Whose?\" - if the answer differs per company, per warehouse, per user, it is not one\n"
		"    value. It is a register keyed by whatever it differs by. A constant that grows a\n"
		"    second meaning (\"the main warehouse - well, for the main company\") has already\n"
		"    stopped being one.\n"
		"  · \"Which one?\" - if it names a particular item rather than holding a setting, look at\n"
		"    `predefined` first: a predefined item is reachable by name from code without a\n"
		"    constant standing in front of it, and it cannot be left empty by accident.\n"
		"\n"
		"⚠ AND A CONSTANT CAN BE EMPTY. Nothing forces it to be filled before something reads it,\n"
		"so code that assumes a value will one day run on a fresh base where nobody set it. Decide\n"
		"what the empty case means - a sensible default, or a refusal that says which constant is\n"
		"missing - rather than letting it read as zero or as an empty reference deep inside a\n"
		"calculation.") },

	{ wxT("allocation"),
	  _("Spreading an amount over lines so the parts still add up to the whole."),
	  _("DISTRIBUTING AN AMOUNT ACROSS LINES - AND THE LEFTOVER PENNIES.\n"
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
		"    weight_i = base_i / SUM(base)\n"
		"    part_i   = round(total * weight_i)\n"
		"    residue  = total - SUM(part_i)      -> onto the last line (or the largest)\n"
		"This is the common one and it is honest as long as the residue is placed DELIBERATELY.\n"
		"Which line receives it is a decision - the last is simplest, the largest hides it best -\n"
		"and it belongs written in the object's notes, because the next reader will find a line\n"
		"that is a penny off its own weight and wonder whether it is a bug.\n"
		"\n"
		"METHOD 2 - CUMULATIVE, WITH NOTHING LEFT OVER. Round the RUNNING TOTAL rather than each\n"
		"share, and take each line as the difference between two rounded running totals:\n"
		"    C_i    = round(total * SUM(base_1..base_i) / SUM(base)),   C_0 = 0\n"
		"    part_i = C_i - C_(i-1)\n"
		"The parts then add to C_n, which IS the total, by construction - there is no residue to\n"
		"place, because the error never accumulates: each line silently absorbs the previous\n"
		"line's rounding, and no line is ever more than one unit from its ideal share. This is the\n"
		"one to use when the requirement is \"it must come out even\" rather than \"each line must\n"
		"match its own weight exactly\".\n"
		"\n"
		"⚠ ZERO AND NEGATIVE BASES BREAK BOTH if not thought about: a line with base 0 must get 0\n"
		"and must not be the one carrying the residue, and SUM(base) = 0 has no answer at all -\n"
		"refuse rather than divide. Decide what a line of zero means before writing the loop.\n"
		"\n"
		"⭐ ASK WHICH FAILURE IS ACCEPTABLE, because one of them must be. Either a line differs\n"
		"slightly from its own weight (method 1's chosen line, method 2's absorption) or the\n"
		"parts do not sum to the whole. There is no third outcome; the arithmetic does not allow\n"
		"it. What people mean by \"I do not want any pennies left\" is almost always method 2.\n"
		"\n"
		"⭐⭐ WHERE THIS MEETS TAX, which is where it is met most often. Computing VAT on each line\n"
		"and adding the results up gives a DIFFERENT number from computing it once on the\n"
		"document's total - each line rounded a fraction of a penny, and twelve of them add up to\n"
		"something visible. Neither is wrong arithmetic; they answer different questions, and one\n"
		"of them has to be declared authoritative:\n"
		"  · per-line tax is authoritative when each line has to stand on its own - different\n"
		"    rates, a line reprinted or reported separately;\n"
		"  · document-level tax is authoritative when the printed total is what must match, and\n"
		"    then the lines are ALLOCATED from it by the methods above rather than summed into it.\n"
		"Decide which, say so in the notes, and never let one road compute it both ways - that is\n"
		"the reconciliation that never converges. See `printing` for the two VAT schemes\n"
		"themselves, which is a separate choice on top of this one.") },

	{ wxT("printing"),
	  _("Where a print command belongs, and what a commercial blank is made of."),
	  _("BUILDING A PRINTED FORM.\n"
		"\n"
		"WHERE THE COMMAND LIVES - the choice made first and regretted later.\n"
		"  · A command ON THE FORM that prints inline is the crudest of them. It works, and it\n"
		"    means the printout exists only where that form is open: not from a list, not from a\n"
		"    job, not from another form. Putting the call behind a procedure changes nothing -\n"
		"    it is the same option written more tidily.\n"
		"  · A command ON THE OBJECT is the one to reach for. It is invoked in its own right, is\n"
		"    handed what it needs, and produces the document; the form becomes one caller among\n"
		"    several rather than the only door.\n"
		"  · Or put the printing in the OBJECT or MANAGER module and let the command delegate to\n"
		"    it. Older in style, and entirely sound: what matters is that the printout is reachable\n"
		"    without a form being open, not which of these two carries it.\n"
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
		"  · VAT INCLUDED - the line's amount already contains the tax, and the tax is EXTRACTED\n"
		"    from it.\n"
		"  · VAT ON TOP - the line's amount is the base, and the tax is ADDED to it.\n"
		"Ask which one before writing either: the two produce different totals from the same\n"
		"numbers, and a form built for one silently mis-states the other.\n"
		"\n"
		"⚠ AND THE RATE IS NOT A CONSTANT IN THE CODE. It is set by legislation and it changes -\n"
		"which makes it the textbook case for an InformationRegister with `Periodicity` (see\n"
		"`shapes`): a value that was true for a stretch of time. A rate written into a formula\n"
		"re-prints last year's documents at this year's rate, and nothing anywhere says so.") },

	{ wxT("look-first"),
	  _("Before building a mechanism, find out whether this base already has one."),
	  _("A REQUEST FOR A MECHANISM IS NOT EVIDENCE THAT IT IS MISSING.\n"
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
		"⭐ AND WHAT TO DO WITH THE FINDING: offer it, do not act on it silently. Say to the person\n"
		"that this already exists here and that it may be worth looking at that code before\n"
		"anything is added. They may know, and have a reason - a mechanism that does not fit, one\n"
		"they intend to retire. Deciding that for them is not yours to do; not telling them is\n"
		"how the duplicate gets built.") },

	{ wxT("naming"),
	  _("Forming a name: say the whole thing, and only what is not already known."),
	  _("NAMING WHAT YOU CREATE.\n"
		"\n"
		"Names are English - they travel into scripts, into query text and into the database\n"
		"schema. What the person actually said goes in the SYNONYM, which is where their language\n"
		"belongs and where it can be translated.\n"
		"\n"
		"A NAME IS THE SENTENCE, TRANSLATED. Say what is counted and where or whose it is, in that\n"
		"order, and read the result back: it should be the thing the person said.\n"
		"  GoodsInWarehouses      - goods, in warehouses\n"
		"  GoodsOfCompany         - the same goods, seen as the company's\n"
		"  GoodsLotsInWarehouses  - lots of goods, in warehouses\n"
		"  GoodsTransferred       - goods, once handed over\n"
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
		"use. The context is already carrying it.") },

	{ wxT("external-data"),
	  _("Getting data out of another base or system - the door that already exists."),
	  _("DATA FROM SOMEWHERE ELSE.\n"
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
		"⚠ IT IS WINDOWS-ONLY, and that is the honest cost. A configuration that depends on it\n"
		"stops being portable to the platforms the engine itself runs on. Worth deciding\n"
		"deliberately rather than discovering later - and worth writing in the notes of whatever\n"
		"uses it, so the next reader knows the limit is chosen and not accidental.\n"
		"\n"
		"DEBUGGING IT IS AWKWARD BUT NOT BLIND. The far side is not ours and will not stop for us,\n"
		"so do not try to debug THROUGH it: put the breakpoint in the script that drives it, on\n"
		"our side of the call, and step from there - inspecting what came back, one call at a\n"
		"time. Expect to learn the far side's shape by looking at real answers rather than by\n"
		"reading about it.") },

	{ wxT("showing-a-value"),
	  _("Asked for a column: look for a path to it before adding a field."),
	  _("A COLUMN ASKED FOR IS NOT AUTOMATICALLY A FIELD TO ADD.\n"
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
		"⚠ AND THE EXCEPTION THAT IS NOT ONE: sometimes the value MUST be frozen - the price at\n"
		"which this was actually sold, the address the parcel actually went to, the rate applied\n"
		"on the day. That is not a copy of a current fact, it is a fact of its own with its own\n"
		"date, and it belongs stored. The test is a question about the past: if the source later\n"
		"changes, should this line change with it? Yes - walk the dot. No - store it, and say in\n"
		"the object's notes that it is deliberately a snapshot.") },

	{ wxT("form-to-areas"),
	  _("Taking a paper form apart: which parts repeat, which are said once."),
	  _("READING A PRINTED FORM INTO AREAS.\n"
		"\n"
		"Handed a blank - an invoice, a delivery note, an act - do not lay it out cell by cell.\n"
		"Read it for what REPEATS, because that is the only structural question a spreadsheet\n"
		"template asks.\n"
		"\n"
		"A form is three kinds of thing, and the middle one is the whole design:\n"
		"  · the HEADER - said once per document: the number, the date, the parties, the contract.\n"
		"    Everything here is a parameter of one document.\n"
		"  · the DETAIL band - said once per LINE, and drawn over and over from a single area. Its\n"
		"    columns are the tabular section's columns. If you find yourself making a second area\n"
		"    that differs only in which row it sits on, you have not found the band yet.\n"
		"  · the FOOTER - said once again at the end: totals, the amount in words, the signatures.\n"
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
		"warehouse, per month, per rate, with the header repeating across the page. Then the piece\n"
		"that repeats horizontally is its own area too, output to the right rather than below.\n"
		"Recognising this early is what stops a template being rebuilt when a second warehouse\n"
		"appears - a form drawn column by column has the number of warehouses frozen into it.\n"
		"\n"
		"A ROW IS BROKEN DOWN BY PARAMETERS, and that is usually as far as it needs to go. Each\n"
		"cell is one of three things, and choosing between them IS the decomposition:\n"
		"  · TEXT - a caption that is the same on every printout. It belongs in the template.\n"
		"  · PARAMETER - the value comes from outside and fills the cell whole. Name it after what\n"
		"    it is, not after where it sits.\n"
		"  · TEMPLATE - text with `[Name]` in square brackets inside it, for when a value has to\n"
		"    sit INSIDE a sentence (\"Received [Quantity] pieces of [Product]\") or when spelling it\n"
		"    out reads better on the page than three cells in a row would.\n"
		"A cell holding \"Invoice No. 12 of 3 March\" as one string is three parameters that were\n"
		"never separated: it cannot be reused, translated, or filled from anything else.\n"
		"\n"
		"⚠ THE HEADER TELLS YOU THE DOCUMENT. Look at it first: what a blank asks for at the top is\n"
		"what the document must already hold, and it is common to find the form asking for\n"
		"something the metadata has no attribute for. Better discovered while reading the blank\n"
		"than while filling it.") },
	};

	return s_patterns;
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
			? _("listing the patterns")
			: wxString::Format(_("reading the '%s' pattern"), name);
	}

	wxString GetDescription() const override
	{
		return _("A translator from the language of the people who ask - accountants, warehouse "
			"managers, anyone describing their work - into the shapes this platform has. They do "
			"not say \"accumulation register\"; they say \"I want to see it monthly\", \"how much "
			"is left\", \"the account it goes to\", and each of those names a shape exactly. This "
			"is where that mapping is written down. Recommendations, not rules: call it with no "
			"argument for the list, then with a `name` for one in full. Worth reading BEFORE "
			"creating a metaobject - the choice of shape is the one decision that is expensive to "
			"revisit once there is data in it.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgName() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		const wxString name = ArgName().Text(params);

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
				_("Ask for one by name. These are recommendations - what they are FOR is said in "
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
				_("If you know other ERP systems, notice when an answer is coming from one of "
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
				_("There is no pattern called '%s'. There is: %s."), name, known);
			return false;
		}

		result.SetValue(wxT("name"), wxString(found->m_name));
		result.SetValue(wxT("summary"), found->m_summary);
		result.SetValue(wxT("text"), found->m_text);

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolPatternRead);
