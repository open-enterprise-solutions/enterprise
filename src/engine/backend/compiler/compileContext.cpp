#include "compileContext.h"
#include "compileCode.h"

////////////////////////////////////////////////////////////////////////
// ibCompileContext ibCompileContext ibCompileContext ibCompileContext//
////////////////////////////////////////////////////////////////////////

/**
* Create a new variable identifier
*/

ibParamUnit ibCompileContext::CreateVariable(const wxString& strPrefix)
{
	if (m_numReturn == RETURN_BLOCK) {
		return m_parentContext->CreateVariable(strPrefix);
	}

	const wxString& strTempName =
		wxString::Format(strPrefix + wxT("%d"), ++m_numTempVar); //@temp_ - to ensure the uniqueness of the name

	ibParamUnit variable =
		GetVariable(strTempName, false, false, false, true); //we look for a temporary variable only in the local context
	variable.m_numArray = DEF_VAR_TEMP; //flag of a temporary local variable

	return variable;
}

/**
* Adds a new variable to the list
* Returns the added variable as a ibParamUnit
*/

ibParamUnit ibCompileContext::AddVariable(const wxString& strVarName,
	const wxString& typeVar, bool exportVar, bool contextVar, bool tempVar)
{

	// Dup-check is OWN-scope only — direct find_if on m_listVariable.
	// Going through FindVariable would also hit bytecode-fallback
	// (parent module entries), which is wrong: declaring `Var X` here
	// is allowed even if a parent module exports an X.
	auto existing = std::find_if(m_listVariable.begin(), m_listVariable.end(),
		[&strVarName](const auto& v) { return v && stringUtils::CompareString(strVarName, v->m_strRealName); });
	if (existing != m_listVariable.end()) {
		m_compileModule->SetError(ERROR_IDENTIFIER_DUPLICATE, strVarName);
		return ibParamUnit();
	}

	if (m_numReturn == RETURN_BLOCK) {
		// Symmetric to CreateVariable's RETURN_BLOCK branch: delegate to
		// the enclosing fn/module ctx. Block-scope and parent share the
		// runtime frame at execute time anyway (CTX_BEGIN/END are NOPs
		// — slots stay in host frame), so semantically there's nothing
		// to keep separate at compile time either. Both explicit
		// `var x` and implicit `x = expr` end up as plain function /
		// module locals, visible to debugger Locals + eval at any IP.
		// Loses C-style block-scope shadowing — same simplification as
		// VES (no block scope at all). The previous side-channel
		// (m_blockScopeLocals + per-IP m_scopeId / m_blockScopes filter)
		// becomes degenerate / dead but stays in tree for AOT v6
		// stability; subsequent cleanup pass can strip it.
		return m_parentContext->AddVariable(strVarName, typeVar, exportVar, contextVar, tempVar);
	}

	unsigned int numVariable = m_listVariable.size();
	PushVariable(strVarName, wxT(""), numVariable,
		typeVar, exportVar, contextVar, tempVar);

	ibParamUnit variable;

	//determine the number and type of the variable
	variable.m_numArray = 0;
	variable.m_strType = typeVar;
	variable.m_numIndex = numVariable;


	return variable;
}

/**
* The function returns the variable number by the string name
* Search for the variable definition, starting from the current context to all parent
* If the required variable does not exist, then a new variable definition is created
*/

ibParamUnit ibCompileContext::GetVariable(const wxString& strVarName, bool bFindInParent, bool bCheckError, bool contextVar, bool tempVar)
{
	int numCanUseLocalInParent = m_numFindLocalInParent;

	std::shared_ptr <ibVariable> currentVariable = nullptr;
	if (!FindVariable(strVarName, currentVariable)) {

		int numParent = 0, numContext = 0;
		if (m_numReturn == RETURN_BLOCK)
			numContext++;

		// Bail out on pathological depth — same threshold for every
		// hop (compile-context walk and bc walk).
		auto guardRecursion = [&]() {
			if (numParent > 2 * MAX_OBJECTS_LEVEL)
				ibBackendCoreException::Error(_("Recursive call of modules!"));
		};

		// Closure capture state declared before tryEmit so the lambda
		// can capture by reference. crossedLambda flips to true inside
		// the parent-walk loop once a lambda boundary is crossed (or
		// up front if `this` is itself a lambda body). isLambdaFunction
		// gates the bc walk's topmost-only restriction and the rootCtx
		// fallback for Context bindings.
		bool isLambdaFunction = IsReturnLambda(m_numReturn);
		bool crossedLambda    = isLambdaFunction;

		// Visibility check + emit. Depth is the runtime frame index
		// (m_pppArrayList layer); blockReturn switches it to
		// DEF_VAR_TEMP for through-block reach. Returns true (with
		// `out` filled) only if visibility budget allows.
		//
		// Closure capture bypass: once the walk has crossed at least
		// one lambda boundary (crossedLambda), the
		// numCanUseLocalInParent budget is shunted — a lambda must
		// reach any enclosing function's locals regardless of the
		// "one level up" default. Triple-nested lambdas (inner →
		// outer through middle) decrement past 0 otherwise.
		auto tryEmit = [&](const std::shared_ptr<ibVariable>& cur, int depth, bool blockReturn, ibParamUnit& out) {
			// Public and Protected are both visible up the parent chain (children
			// see them). The difference is the cross-module export registry:
			// Public is in it (config-wide); Protected is NOT.
			if (!(m_numReturn == RETURN_BLOCK || numCanUseLocalInParent > 0 || cur->IsPublic() || cur->IsProtected() || crossedLambda))
				return false;
			out.m_numArray = blockReturn ? (long long)DEF_VAR_TEMP : (long long)depth;
			out.m_numIndex = cur->m_numVariable;
			out.m_strType = cur->m_strType;
			return true;
		};

		//search in parent contexts (modules)
		if (bFindInParent) {

			// (isLambdaFunction / crossedLambda declared above so that
			// the tryEmit lambda's reference-capture sees a defined
			// variable. Re-stated here for context: lambda boundary
			// detection — `this` is the lambda's own functionContext
			// for top-level lambda body code. Block scopes inside the
			// lambda hit the boundary by walking up to the containing
			// functionContext (RETURN_LAMBDA_*). Closure capture is
			// stamped lazily on the resolved fn's m_needsHeapFrame
			// when crossedLambda is true at the time of FindVariable
			// hit.)

			// 1. Compile-context chain (block → function → root of own
			//    ibCompileCode, then up through parent compile-contexts
			//    where they are still live).
			for (ibCompileContext* pCurContext = m_parentContext; pCurContext != nullptr; pCurContext = pCurContext->m_parentContext) {
				numParent++;
				if (pCurContext->m_numReturn == RETURN_BLOCK)
					numContext++;

				// Indented as if it belonged to the `if` above, but it never did — the guard
				// runs on every iteration, which is what a recursion guard has to do.
				guardRecursion();

				if (pCurContext->FindVariable(strVarName, currentVariable)) {
					ibParamUnit variable;
					if (tryEmit(currentVariable, numParent - numContext,
					            pCurContext->m_numReturn == RETURN_BLOCK, variable)) {
						// Closure capture mark — stamp the ENCLOSING
						// function's ibFunction (m_functionContext)
						// directly. Module-body / block ctxs have
						// m_functionContext == nullptr; skip them
						// (block-locals delegate to fn frame anyway,
						// module body lives in stable descriptor frame).
						if (crossedLambda && pCurContext->m_functionContext)
							pCurContext->m_functionContext->m_needsHeapFrame = true;
						return variable;
					}
				}
				numCanUseLocalInParent--;

				if (IsReturnLambda(pCurContext->m_numReturn)) {
					isLambdaFunction = true;
					crossedLambda    = true;
				}
			}

			// 2. Eval host function frame (only when this compile is an
			//    eval expression opened inside a function). Function
			//    frame is its own runtime layer between eval and host's
			//    module body; account for it in numParent so the bc walk
			//    below emits depths shifted by +1.
			//    Source: ibCompileCode::GetEvalHostFunction() — virtual,
			//    returns nullptr for non-eval modules; ibCompileEval
			//    overrides with the host fn captured at ctor time.
			if (auto* hostFn = m_compileModule->GetEvalHostFunction()) {
				const auto& fn = *hostFn;
				auto fIt = std::find_if(fn.m_listLocals.begin(), fn.m_listLocals.end(),
					[&strVarName](const auto& v) { return stringUtils::CompareString(strVarName, v.m_strRealName); });
				if (fIt != fn.m_listLocals.end()) {
					ibParamUnit variable;
					variable.m_numArray = (numParent - numContext) + 1;
					variable.m_numIndex = fIt->m_slotIndex;
					return variable;
				}
				numParent++;  // function frame layer occupied even on miss
			}

			// 3. Bytecode parent chain (cross-bc — AOT-loaded ancestors
			//    with no live compile-context, e.g. host module body
			//    while compiling an eval expression). Emitter decides
			//    direct OPER_GET vs OPER_GET_A by foundedVar->m_strContext.
			//    bc.m_parent is constructed genealogically (form →
			//    object → manager → root for catalogs etc.; eval
			//    inherits its host's chain) — foreign bcs never appear,
			//    so scoped bindings (ThisForm / ThisObject) are
			//    reachable iff they live on a real ancestor.
			// Walk start: lambda case includes OWN bc (codeRunner-style
			// flat compile where own bc IS the root with no parent
			// chain to walk). Non-lambda case starts at m_parent — own
			// scope was already covered by FindVariable on `this` at
			// the top of GetVariable.
			const ibByteCode* startBc = isLambdaFunction
				? &m_compileModule->m_cByteCode
				: m_compileModule->m_cByteCode.m_parent;
			for (const ibByteCode* pCurByteCode = startBc;
			     pCurByteCode != nullptr; pCurByteCode = pCurByteCode->m_parent) {

				// Lambda discipline: only consult the topmost bc
				// (m_parent == nullptr — root configuration). Skip
				// every intermediate ancestor — lambda body sees only
				// root-level Context bindings, not its definition site's
				// module-level vars.
				if (isLambdaFunction && pCurByteCode->m_parent != nullptr)
					continue;

				numParent++;
				guardRecursion();

				if (pCurByteCode->FindVariable(strVarName, currentVariable)) {
					ibParamUnit variable;
					if (tryEmit(currentVariable, numParent - numContext, /*blockReturn=*/false, variable))
						return variable;
				}

				numCanUseLocalInParent--;
			}
			// Lambda final fallback: m_rootContext for Context bindings
			// (Catalogs / Documents / Manager / system functions / host-
			// injected like codeRunner's ValueOutput). They land in
			// m_rootContext->m_listVariable via AddContextVariable —
			// CompileContext layer, not bc layer — so the bc walk above
			// can't see them. Filter to m_bContext == true so user
			// module-level vars in rootCtx (codeRunner sandbox case
			// where module-level declarations land directly in root)
			// stay invisible. Depth 1 matches the runtime shim's
			// m_pppArrayList[1] — root frame.
			if (isLambdaFunction && bFindInParent) {
				ibCompileContext* rootCtx = m_compileModule
					? m_compileModule->GetContext() : nullptr;
				if (rootCtx != nullptr && rootCtx != this) {
					std::shared_ptr<ibVariable> rootVar = nullptr;
					if (rootCtx->FindVariable(strVarName, rootVar)
						&& rootVar && rootVar->IsContextRelated())
					{
						ibParamUnit variable;
						variable.m_numArray = 1;
						variable.m_numIndex = rootVar->m_numVariable;
						variable.m_strType  = rootVar->m_strType;
						currentVariable = rootVar;
						return variable;
					}
				}
			}
		}

		if (bCheckError) {
			m_compileModule->SetError(ERROR_VAR_NOT_FOUND, strVarName); //display an error message
			return ibParamUnit();
		}

		// there was no variable declaration yet - add. AddVariable
		// itself delegates RETURN_BLOCK -> parent (mirroring CreateVariable),
		// so implicit `name = expr` inside `{ }` lands in the enclosing
		// fn/module ctx automatically — no walk-target gymnastics here.
		return AddVariable(strVarName, wxEmptyString, contextVar, contextVar, tempVar);
	}

	wxASSERT(currentVariable);

	ibParamUnit variable;

	// FindVariable here is own-scope only (no bytecode fallback) —
	// reaching this branch means the variable lives in this very
	// compile-context's m_listVariable, so m_numArray=0 is correct.
	// Cross-frame resolution (host function param / parent module
	// binding) goes through GetVariable's bytecode fallback below
	// where found.depth is preserved on ibParamUnit::m_numArray.
	if (m_numReturn == RETURN_BLOCK)
		variable.m_numArray = DEF_VAR_TEMP;
	else
		variable.m_numArray = 0;

	variable.m_numIndex = currentVariable->m_numVariable;
	variable.m_strType = currentVariable->m_strType;

	return variable;
}

void ibCompileContext::PushVariable(const wxString& strVarName, const wxString& strContextVar, unsigned int numVariable,
	const wxString& typeVar, bool exportVar, bool contextVar, bool tempVar)
{
	// Entries are keyed by m_strRealName via case-insensitive find_if (no
	// upper-normalization). Renderers (debugger / PrepareNames) show the
	// user's source casing.
	std::shared_ptr<ibVariable> currentVariable(new ibVariable(strVarName));

	currentVariable->m_strRealName = strVarName;
	// Kind from the declaration shape: a prop of a binding (strContextVar set)
	// is ContextProp; a self-ref binding is Context; an exported name is Export;
	// otherwise a plain Local. PrepareModuleData Pass 1 overrides Export→External
	// for externs; CompileDeclaration's access stamp overrides →Protected.
	currentVariable->m_kind =
		  !strContextVar.IsEmpty() ? ibVarKind::ContextProp
		: contextVar               ? ibVarKind::Context
		: exportVar                ? ibVarKind::Export
		                           : ibVarKind::Local;
	// m_access stays in lock-step with the export flag for the parent-chain
	// visibility gate: every exported var — user Public AND system extern /
	// context bindings — reads as Public. Protected is stamped separately by
	// CompileDeclaration after the variable is created.
	currentVariable->m_access = exportVar ? ACCESS_PUBLIC : ACCESS_PRIVATE;

	currentVariable->m_strContext = strContextVar;  //variable for which the attribute is called

	currentVariable->m_bTempVar = tempVar;
	currentVariable->m_strType = typeVar;
	currentVariable->m_numVariable = numVariable;
	// Stamp the scope-nesting depth at the call site. Runtime
	// debugger Locals filter compares this against an ibRunContext-side
	// counter pushed by OPER_CTX_BEGIN / popped by OPER_CTX_END.
	// 0 = fn-frame / module-body (no enclosing `{ }` open at decl time).
	currentVariable->m_scopeDepth = m_compileModule
		? m_compileModule->m_compileScopeDepth
		: 0;

	// Vector storage with map-like upsert semantics: replace an existing
	// same-named entry (was insert_or_assign's last-wins), else append.
	auto itVar = std::find_if(m_listVariable.begin(), m_listVariable.end(),
		[&](const auto& v) { return v && stringUtils::CompareString(currentVariable->m_strRealName, v->m_strRealName); });
	if (itVar != m_listVariable.end())
		*itVar = std::move(currentVariable);
	else
		m_listVariable.push_back(std::move(currentVariable));
}

void ibCompileContext::PushFunction(const wxString& strFuncName, const wxString& strContextVar, const wxString& strShortDescription, unsigned int numFunction, bool hasRetVal, int argCount)
{
	// No compile-context here. A context method has no body to compile, so the only thing the
	// (name, context) ctor would do is wire the back-pointer on a context nobody then keeps:
	// ibFunction stopped owning contexts (see its ctor comment), this call site never stored
	// the pointer, and CreateContext hands out a raw one. The result was one 116-byte context
	// leaked per registered context method, per compile — and PrepareModuleData registers the
	// whole system API, so closing an editor (SyntaxControl recompiles) leaked another set.
	std::shared_ptr<ibCompileContext::ibFunction> contextFunction(new ibFunction(strFuncName));

	contextFunction->m_nStart = numFunction;
	contextFunction->m_kind = ibFnKind::ContextMethod;
	// m_bCodeRet defaults false and context methods have no compile finalize to run
	// IsReturnFunction(m_numReturn) — set it straight from hasRetVal, otherwise every built-in
	// function (TrimAll, Left, Right, ...) resolves as a procedure and using its result throws
	// ERROR_USE_PROCEDURE_AS_FUNCTION.
	contextFunction->m_bCodeRet = hasRetVal;

	contextFunction->m_strContext = strContextVar; //variable for which the attribute is called

	if (argCount > 0) contextFunction->m_listParam.reserve(argCount);

	for (long arg = 0; arg < argCount; arg++) {
		ibFunction::ibParamVariable contextVariable;
		contextVariable.m_puValue.m_numArray = DEF_VAR_DEFAULT;
		contextVariable.m_puValue.m_numIndex = DEF_VAR_DEFAULT;
		contextFunction->m_listParam.emplace_back(std::move(contextVariable));
	}

	contextFunction->m_strRealName = strFuncName;
	contextFunction->m_strShortDescription = strShortDescription;

	// Vector storage with map-like upsert semantics (see PushVariable).
	auto itFunc = std::find_if(m_listFunction.begin(), m_listFunction.end(),
		[&](const auto& f) { return f && stringUtils::CompareString(contextFunction->m_strRealName, f->m_strRealName); });
	if (itFunc != m_listFunction.end())
		*itFunc = std::move(contextFunction);
	else
		m_listFunction.push_back(std::move(contextFunction));
}

/**
 * Search for a variable in a hash array
 * Returns true - if the variable is found
 */

bool ibCompileContext::FindVariable(const wxString& strVarName, std::shared_ptr<ibVariable>& foundedVar, bool contextVar)
{
	auto it = std::find_if(m_listVariable.begin(), m_listVariable.end(),
		[strVarName](const auto& v) {return v && stringUtils::CompareString(strVarName, v->m_strRealName); });

	if (contextVar) {

		if (it != m_listVariable.end()) {
			foundedVar = *it;
			return (*it)->IsContextRelated();
		}

		if (m_parentContext && m_parentContext->FindVariable(strVarName, foundedVar, contextVar))
			return true;

		// Reached the top compile-context. Drop down to the parent
		// BYTECODE chain and walk it looking for a context-related
		// entry. Single-hop wouldn't be enough — there may be an
		// intermediate bc (form bc, object bc) between the eval and
		// the root common module that holds the bindings. First match
		// in any ancestor wins; emitter (compileCode.cpp) checks
		// foundedVar->m_strContext to decide direct vs OPER_GET_A.
		if (m_parentContext == nullptr) {
			for (const ibByteCode* bc = m_compileModule->m_cByteCode.m_parent; bc != nullptr; bc = bc->m_parent) {
				if (bc->FindVariable(strVarName, foundedVar)) {
					return foundedVar->IsContextRelated();
				}
			}
		}

		foundedVar = nullptr;
		return false;
	}
	else if (it != m_listVariable.end()) {
		foundedVar = *it;
		return true;
	}

	if (m_parentContext != nullptr) {
	} else {
	}

	foundedVar = nullptr;
	return false;
}

/**
 * Search for a variable in a hash array
 * Returns true - if the variable is found
 */

bool ibCompileContext::FindFunction(const wxString& strFuncName, std::shared_ptr<ibFunction>& foundedFunc, bool contextVar)
{
	auto it = std::find_if(m_listFunction.begin(), m_listFunction.end(),
		[strFuncName](const auto& f) { return f && stringUtils::CompareString(strFuncName, f->m_strRealName); });

	if (contextVar) {
		if (it != m_listFunction.end() && *it) {
			foundedFunc = *it;
			return (*it)->IsContextMethod();
		}

		if (m_parentContext && m_parentContext->FindFunction(strFuncName, foundedFunc, contextVar))
			return true;

		// Reached the top compile-context. Drop down to the parent
		// BYTECODE chain — symmetric with FindVariable's fallback.
		// Emitter (compileCode.cpp) decides direct OPER_CALL vs
		// OPER_CALL_METHOD based on foundedFunc->m_strContext.
		if (m_parentContext == nullptr) {
			for (const ibByteCode* bc = m_compileModule->m_cByteCode.m_parent; bc != nullptr; bc = bc->m_parent) {
				if (bc->FindFunction(strFuncName, foundedFunc)) {
					return foundedFunc->IsContextRelated();
				}
			}
		}

		foundedFunc = nullptr;
		return false;
	}
	else if (it != m_listFunction.end() && *it) {
		foundedFunc = *it;
		return true;
	}

	foundedFunc = nullptr;
	return false;
}

/**
 * Linking GOTO statements to labels
 */
void ibCompileContext::CreateLabels()
{
	wxASSERT(m_compileModule != nullptr);
	for (unsigned int i = 0; i < m_listLabel.size(); i++) {

		const wxString& strName = m_listLabel[i]->m_strName;
		const int oldLine = m_listLabel[i]->m_numLine;

		//look for such a label in the list of declared labels
		unsigned int currLine = m_listLabelDef[strName];
		if (!currLine) {
			m_compileModule->m_numCurrentCompile = m_listLabel[i]->m_numError;
			m_compileModule->SetError(ERROR_LABEL_DEFINE, strName); // duplicate label definitions occurred
		}

		// write the address of the label:
		m_compileModule->m_cByteCode.m_listCode[oldLine].m_param1.m_numIndex = currLine + 1;
	}
};