import { useState, type FormEvent } from "react"
import { SignIn, Eye, EyeSlash, CircleNotch } from "@phosphor-icons/react"

const API_BASE = import.meta.env.VITE_API_BASE_URL || "/api"

export function LoginPage() {
  const [username, setUsername] = useState("")
  const [password, setPassword] = useState("")
  const [showPassword, setShowPassword] = useState(false)
  const [isLoading, setIsLoading] = useState(false)
  const [errorMessage, setErrorMessage] = useState<string | null>(null)

  async function handleSubmit(e: FormEvent<HTMLFormElement>) {
    e.preventDefault()
    setIsLoading(true)
    setErrorMessage(null)
    try {
      const resp = await fetch(`${API_BASE}/auth/login`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ username: username.trim(), password }),
      })
      const body = await resp.json() as Record<string, unknown>
      const payload = (body.data ?? body) as { token?: string; user?: unknown }
      if (payload.token) {
        localStorage.setItem("oes_token", payload.token as string)
        localStorage.setItem("oes_user", JSON.stringify(payload.user))
        window.location.href = "/"
      } else {
        setErrorMessage("Invalid username or password")
      }
    } catch {
      setErrorMessage("Connection error — check that the OES backend is running")
    } finally {
      setIsLoading(false)
    }
  }

  return (
    <div className="flex h-screen items-center justify-center bg-[hsl(var(--secondary))]">
      <div
        className="w-full max-w-sm border border-[hsl(var(--border))] bg-[hsl(var(--card))] shadow-sm"
        style={{ borderRadius: "var(--radius)" }}
      >
        {/* Header strip */}
        <div className="border-b border-[hsl(var(--border))] px-6 py-4">
          <h1 className="text-base font-semibold text-[hsl(var(--foreground))]">OES Enterprise</h1>
          <p className="mt-0.5 text-xs text-[hsl(var(--muted-foreground))]">
            Sign in to your account
          </p>
        </div>

        <form onSubmit={handleSubmit} className="px-6 py-5 space-y-4" noValidate>
          {/* Error banner */}
          {errorMessage && (
            <div
              className="border border-[hsl(var(--destructive)/0.4)] bg-[hsl(var(--destructive)/0.06)] px-3 py-2 text-xs text-[hsl(var(--destructive))]"
              style={{ borderRadius: "var(--radius)" }}
              role="alert"
            >
              {errorMessage}
            </div>
          )}

          {/* Username */}
          <div className="space-y-1">
            <label
              htmlFor="oes-username"
              className="block text-xs font-medium text-[hsl(var(--foreground))]"
            >
              Username
            </label>
            <input
              id="oes-username"
              type="text"
              value={username}
              onChange={(e) => setUsername(e.target.value)}
              placeholder="admin"
              autoComplete="username"
              autoFocus
              disabled={isLoading}
              className="w-full border border-[hsl(var(--input))] bg-[hsl(var(--background))] px-3 py-2 text-sm text-[hsl(var(--foreground))] outline-none placeholder:text-[hsl(var(--muted-foreground))] focus:border-[hsl(var(--ring))] focus:ring-1 focus:ring-[hsl(var(--ring))] disabled:opacity-50"
              style={{ borderRadius: "var(--radius)" }}
            />
          </div>

          {/* Password */}
          <div className="space-y-1">
            <label
              htmlFor="oes-password"
              className="block text-xs font-medium text-[hsl(var(--foreground))]"
            >
              Password
            </label>
            <div className="relative">
              <input
                id="oes-password"
                type={showPassword ? "text" : "password"}
                value={password}
                onChange={(e) => setPassword(e.target.value)}
                autoComplete="current-password"
                disabled={isLoading}
                className="w-full border border-[hsl(var(--input))] bg-[hsl(var(--background))] px-3 py-2 pr-10 text-sm text-[hsl(var(--foreground))] outline-none placeholder:text-[hsl(var(--muted-foreground))] focus:border-[hsl(var(--ring))] focus:ring-1 focus:ring-[hsl(var(--ring))] disabled:opacity-50"
                style={{ borderRadius: "var(--radius)" }}
              />
              <button
                type="button"
                onClick={() => setShowPassword((v) => !v)}
                disabled={isLoading}
                aria-label={showPassword ? "Hide password" : "Show password"}
                className="absolute right-2 top-1/2 -translate-y-1/2 p-1 text-[hsl(var(--muted-foreground))] hover:text-[hsl(var(--foreground))] disabled:opacity-40"
              >
                {showPassword ? (
                  <EyeSlash size={16} weight="duotone" />
                ) : (
                  <Eye size={16} weight="duotone" />
                )}
              </button>
            </div>
          </div>

          {/* Submit */}
          <button
            type="submit"
            disabled={isLoading}
            className="flex w-full items-center justify-center gap-2 bg-[hsl(var(--primary))] px-4 py-2 text-sm font-medium text-[hsl(var(--primary-foreground))] transition-opacity hover:opacity-90 disabled:cursor-not-allowed disabled:opacity-50"
            style={{ borderRadius: "var(--radius)" }}
          >
            {isLoading ? (
              <CircleNotch size={16} weight="bold" className="animate-spin" />
            ) : (
              <SignIn size={16} weight="duotone" />
            )}
            {isLoading ? "Signing in…" : "Sign In"}
          </button>
        </form>
      </div>
    </div>
  )
}
