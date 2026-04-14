import { useEffect, useRef, useCallback, useState } from "react"
import {
  PaperPlaneRight,
  Robot,
  User,
  Check,
  Copy,
  ArrowClockwise,
  Lightning,
} from "@phosphor-icons/react"
import { useAiAssistant, type AiAction, type AiMessage } from "@/hooks/useAiAssistant"
import { clsx } from "clsx"

// ---------------------------------------------------------------------------
// Lightweight markdown renderer (no external library)
// ---------------------------------------------------------------------------

function renderMarkdown(text: string): string {
  return text
    // Code blocks with language
    .replace(/```(\w+)?\n([\s\S]*?)```/g, (_, lang, code) => {
      const langAttr = lang ? ` data-lang="${lang}"` : ""
      return `<pre class="md-code-block"${langAttr}><code>${escapeHtml(code.trim())}</code></pre>`
    })
    // Inline code
    .replace(/`([^`]+)`/g, "<code class=\"md-code\">$1</code>")
    // Bold
    .replace(/\*\*([^*]+)\*\*/g, "<strong>$1</strong>")
    // Italic
    .replace(/\*([^*]+)\*/g, "<em>$1</em>")
    // Headings
    .replace(/^### (.+)$/gm, "<h3 class=\"md-h3\">$1</h3>")
    .replace(/^## (.+)$/gm, "<h2 class=\"md-h2\">$1</h2>")
    .replace(/^# (.+)$/gm, "<h1 class=\"md-h1\">$1</h1>")
    // Unordered list items
    .replace(/^[-•] (.+)$/gm, "<li class=\"md-li\">$1</li>")
    // Numbered list items
    .replace(/^\d+\. (.+)$/gm, "<li class=\"md-li-num\">$1</li>")
    // Paragraphs (double newline)
    .replace(/\n\n+/g, "</p><p class=\"md-p\">")
    // Single newlines inside paragraphs
    .replace(/\n/g, "<br/>")
}

function escapeHtml(text: string): string {
  return text
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
}

// ---------------------------------------------------------------------------
// Code block with copy + apply buttons
// ---------------------------------------------------------------------------

interface CodeBlockProps {
  code: string
  lang?: string
  onApply?: (code: string) => void
}

function CodeBlock({ code, lang, onApply }: CodeBlockProps) {
  const [copied, setCopied] = useState(false)

  const handleCopy = useCallback(async () => {
    await navigator.clipboard.writeText(code)
    setCopied(true)
    setTimeout(() => setCopied(false), 2000)
  }, [code])

  return (
    <div className="my-2 rounded-[var(--radius)] border border-[hsl(var(--border))] overflow-hidden">
      <div className="flex items-center justify-between px-3 py-1.5 bg-[hsl(var(--muted))] border-b border-[hsl(var(--border))]">
        <span className="text-[10px] font-mono text-[hsl(var(--muted-foreground))] uppercase tracking-wide">
          {lang ?? "code"}
        </span>
        <div className="flex items-center gap-1">
          {onApply && (
            <button
              type="button"
              onClick={() => onApply(code)}
              className="flex items-center gap-1 px-2 py-0.5 text-[10px] rounded-[var(--radius)] bg-[hsl(var(--primary))] text-[hsl(var(--primary-foreground))] hover:opacity-90 transition-opacity"
            >
              <Lightning size={10} weight="fill" />
              Apply
            </button>
          )}
          <button
            type="button"
            onClick={handleCopy}
            className="flex items-center gap-1 px-2 py-0.5 text-[10px] rounded-[var(--radius)] hover:bg-[hsl(var(--secondary))] transition-colors text-[hsl(var(--muted-foreground))]"
            title="Copy code"
          >
            {copied ? <Check size={10} weight="bold" /> : <Copy size={10} />}
            {copied ? "Copied" : "Copy"}
          </button>
        </div>
      </div>
      <pre className="overflow-x-auto p-3 text-[11px] font-mono leading-relaxed bg-[hsl(var(--card))] text-[hsl(var(--foreground))]">
        <code>{code}</code>
      </pre>
    </div>
  )
}

// ---------------------------------------------------------------------------
// Message content renderer — splits raw content into text + code blocks
// ---------------------------------------------------------------------------

interface MessageContentProps {
  content: string
  onApplyCode?: (code: string) => void
}

function MessageContent({ content, onApplyCode }: MessageContentProps) {
  // Split at code fences
  const parts = content.split(/(```(?:\w+)?\n[\s\S]*?```)/g)

  return (
    <div className="space-y-1">
      {parts.map((part, i) => {
        const codeMatch = part.match(/^```(\w+)?\n([\s\S]*?)```$/)
        if (codeMatch) {
          const lang = codeMatch[1]
          const code = codeMatch[2].trim()
          return <CodeBlock key={i} code={code} lang={lang} onApply={onApplyCode} />
        }

        if (!part.trim()) return null

        const html = renderMarkdown(part)
        return (
          <div
            key={i}
            className="prose-oes text-[12px] leading-relaxed"
            // biome-ignore lint/security/noDangerouslySetInnerHtml: controlled markdown renderer, no user HTML
            dangerouslySetInnerHTML={{ __html: `<p class="md-p">${html}</p>` }}
          />
        )
      })}
    </div>
  )
}

// ---------------------------------------------------------------------------
// Loading skeleton
// ---------------------------------------------------------------------------

function MessageSkeleton() {
  return (
    <div className="flex gap-2.5 px-4 py-3">
      <div className="w-6 h-6 rounded-full bg-[hsl(var(--muted))] shrink-0 flex items-center justify-center">
        <Robot size={13} className="text-[hsl(var(--muted-foreground))]" />
      </div>
      <div className="flex-1 space-y-2 pt-0.5">
        <div className="skeleton h-3 w-3/4 rounded" />
        <div className="skeleton h-3 w-1/2 rounded" />
        <div className="skeleton h-3 w-2/3 rounded" />
      </div>
    </div>
  )
}

// ---------------------------------------------------------------------------
// Action chip
// ---------------------------------------------------------------------------

interface ActionChipProps {
  action: AiAction
  onApply: (action: AiAction) => void
  disabled?: boolean
}

function ActionChip({ action, onApply, disabled }: ActionChipProps) {
  const icons: Record<string, React.ReactNode> = {
    create_object: <Lightning size={11} weight="duotone" />,
    write_module: <Lightning size={11} weight="duotone" />,
    add_attribute: <Lightning size={11} weight="duotone" />,
    generate_form: <Lightning size={11} weight="duotone" />,
  }

  return (
    <button
      type="button"
      disabled={disabled}
      onClick={() => onApply(action)}
      className={clsx(
        "flex items-center gap-1.5 px-2.5 py-1 text-[11px] font-medium rounded-[var(--radius)]",
        "border border-[hsl(var(--primary))] text-[hsl(var(--primary))]",
        "hover:bg-[hsl(var(--primary))] hover:text-[hsl(var(--primary-foreground))]",
        "transition-colors disabled:opacity-50 disabled:cursor-not-allowed",
      )}
    >
      {icons[action.type]}
      {action.label}
    </button>
  )
}

// ---------------------------------------------------------------------------
// Single message bubble
// ---------------------------------------------------------------------------

interface MessageBubbleProps {
  message: AiMessage
  onApplyCode?: (code: string) => void
  onApplyAction?: (action: AiAction) => void
  isApplying?: boolean
}

function MessageBubble({ message, onApplyCode, onApplyAction, isApplying }: MessageBubbleProps) {
  const isUser = message.role === "user"

  if (message.isLoading) {
    return <MessageSkeleton />
  }

  return (
    <div
      className={clsx(
        "flex gap-2.5 px-4 py-3",
        isUser && "flex-row-reverse",
      )}
    >
      {/* Avatar */}
      <div
        className={clsx(
          "w-6 h-6 rounded-full shrink-0 flex items-center justify-center text-[hsl(var(--primary-foreground))]",
          isUser ? "bg-[hsl(var(--primary))]" : "bg-[hsl(var(--accent))]",
        )}
      >
        {isUser ? <User size={13} weight="bold" /> : <Robot size={13} weight="duotone" />}
      </div>

      {/* Content */}
      <div className={clsx("flex-1 min-w-0", isUser && "flex flex-col items-end")}>
        {isUser ? (
          <div className="inline-block max-w-[80%] rounded-[var(--radius)] bg-[hsl(var(--primary))] text-[hsl(var(--primary-foreground))] px-3 py-2 text-[12px] leading-relaxed">
            {message.content}
          </div>
        ) : (
          <div className="space-y-2">
            <MessageContent content={message.content} onApplyCode={onApplyCode} />
            {message.actions && message.actions.length > 0 && (
              <div className="flex flex-wrap gap-1.5 pt-1">
                {message.actions.map((action, i) => (
                  <ActionChip
                    key={i}
                    action={action}
                    onApply={onApplyAction ?? (() => {})}
                    disabled={isApplying}
                  />
                ))}
              </div>
            )}
          </div>
        )}
        <time className="text-[10px] text-[hsl(var(--muted-foreground))] mt-1 block">
          {new Date(message.timestamp).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" })}
        </time>
      </div>
    </div>
  )
}

// ---------------------------------------------------------------------------
// Main AiChat component
// ---------------------------------------------------------------------------

interface AiChatProps {
  onApplyCode?: (code: string) => void
}

export function AiChat({ onApplyCode }: AiChatProps) {
  const { messages, isLoading, error, sendMessage, applyAction, clearHistory, dismissError } =
    useAiAssistant()

  const [input, setInput] = useState("")
  const bottomRef = useRef<HTMLDivElement>(null)
  const inputRef = useRef<HTMLTextAreaElement>(null)

  // Scroll to bottom on new messages
  useEffect(() => {
    bottomRef.current?.scrollIntoView({ behavior: "smooth" })
  }, [messages])

  const handleSend = useCallback(() => {
    const text = input.trim()
    if (!text || isLoading) return
    setInput("")
    void sendMessage(text)
    inputRef.current?.focus()
  }, [input, isLoading, sendMessage])

  const handleKeyDown = useCallback(
    (e: React.KeyboardEvent<HTMLTextAreaElement>) => {
      if (e.key === "Enter" && !e.shiftKey) {
        e.preventDefault()
        handleSend()
      }
    },
    [handleSend],
  )

  const QUICK_PROMPTS = [
    "Создай справочник Товары с реквизитами Код, Наименование, Цена",
    "Напиши модуль расчёта суммы для документа",
    "Добавь табличную часть Состав к документу",
  ]

  return (
    <div className="flex flex-col h-full">
      {/* Messages area */}
      <div className="flex-1 overflow-y-auto min-h-0">
        {messages.length === 0 ? (
          <div className="flex flex-col items-center justify-center h-full px-4 text-center gap-4">
            <div className="w-12 h-12 rounded-full bg-[hsl(var(--muted))] flex items-center justify-center">
              <Robot size={24} weight="duotone" className="text-[hsl(var(--muted-foreground))]" />
            </div>
            <div>
              <p className="text-[13px] font-medium text-[hsl(var(--foreground))]">OES AI Assistant</p>
              <p className="text-[11px] text-[hsl(var(--muted-foreground))] mt-1">
                Опишите что нужно создать — AI сгенерирует конфигурацию OES
              </p>
            </div>
            <div className="w-full space-y-1.5">
              {QUICK_PROMPTS.map((prompt, i) => (
                <button
                  key={i}
                  type="button"
                  onClick={() => void sendMessage(prompt)}
                  className="w-full text-left px-3 py-2 text-[11px] rounded-[var(--radius)] border border-[hsl(var(--border))] hover:bg-[hsl(var(--secondary))] transition-colors text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))]"
                >
                  {prompt}
                </button>
              ))}
            </div>
          </div>
        ) : (
          <div>
            {messages.map((msg) => (
              <MessageBubble
                key={msg.id}
                message={msg}
                onApplyCode={onApplyCode}
                onApplyAction={applyAction}
                isApplying={isLoading}
              />
            ))}
            <div ref={bottomRef} />
          </div>
        )}
      </div>

      {/* Error banner */}
      {error && (
        <div className="mx-3 mb-2 px-3 py-2 rounded-[var(--radius)] bg-[hsl(var(--destructive)/0.1)] border border-[hsl(var(--destructive)/0.3)] text-[11px] text-[hsl(var(--destructive))] flex items-center justify-between">
          <span>{error}</span>
          <button type="button" onClick={dismissError} className="ml-2 hover:opacity-70">
            <ArrowClockwise size={12} />
          </button>
        </div>
      )}

      {/* Input area */}
      <div className="border-t border-[hsl(var(--border))] p-3">
        {messages.length > 0 && (
          <div className="flex justify-end mb-2">
            <button
              type="button"
              onClick={clearHistory}
              className="text-[10px] text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))] transition-colors flex items-center gap-1"
            >
              <ArrowClockwise size={10} />
              Clear history
            </button>
          </div>
        )}
        <div className="flex gap-2 items-end">
          <textarea
            ref={inputRef}
            value={input}
            onChange={(e) => setInput(e.target.value)}
            onKeyDown={handleKeyDown}
            placeholder="Опишите задачу... (Enter — отправить)"
            rows={2}
            disabled={isLoading}
            className={clsx(
              "flex-1 resize-none rounded-[var(--radius)] border border-[hsl(var(--input))]",
              "bg-[hsl(var(--background))] px-3 py-2 text-[12px] leading-relaxed",
              "placeholder:text-[hsl(var(--muted-foreground))] outline-none",
              "focus:border-[hsl(var(--ring))] transition-colors",
              "disabled:opacity-50",
            )}
          />
          <button
            type="button"
            onClick={handleSend}
            disabled={!input.trim() || isLoading}
            className={clsx(
              "flex h-8 w-8 shrink-0 items-center justify-center rounded-[var(--radius)]",
              "bg-[hsl(var(--primary))] text-[hsl(var(--primary-foreground))]",
              "hover:opacity-90 transition-opacity",
              "disabled:opacity-40 disabled:cursor-not-allowed",
            )}
            title="Send (Enter)"
          >
            <PaperPlaneRight size={15} weight="fill" />
          </button>
        </div>
        <p className="mt-1.5 text-[10px] text-[hsl(var(--muted-foreground))]">
          Shift+Enter — новая строка
        </p>
      </div>
    </div>
  )
}
