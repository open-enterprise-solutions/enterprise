import { useState } from "react"
import { SignIn, Eye, EyeSlash } from "@phosphor-icons/react"

export function LoginPage() {
  const [showPassword, setShowPassword] = useState(false)

  return (
    <div className="flex h-screen items-center justify-center bg-[hsl(var(--secondary))]">
      <div className="w-full max-w-sm rounded-[var(--radius)] border border-[hsl(var(--border))] bg-[hsl(var(--card))] p-6 shadow-sm">
        <div className="mb-6 text-center">
          <h1 className="text-xl font-semibold text-[hsl(var(--foreground))]">OES Enterprise</h1>
          <p className="mt-1 text-xs text-[hsl(var(--muted-foreground))]">Sign in to continue</p>
        </div>

        <form className="space-y-3">
          <div>
            <label className="mb-1 block text-xs font-medium">Username</label>
            <input
              type="text"
              className="w-full rounded-[var(--radius)] border border-[hsl(var(--input))] bg-[hsl(var(--background))] px-3 py-2 text-sm outline-none focus:ring-1 focus:ring-[hsl(var(--ring))]"
              placeholder="admin"
              autoFocus
            />
          </div>

          <div>
            <label className="mb-1 block text-xs font-medium">Password</label>
            <div className="relative">
              <input
                type={showPassword ? "text" : "password"}
                className="w-full rounded-[var(--radius)] border border-[hsl(var(--input))] bg-[hsl(var(--background))] px-3 py-2 pr-10 text-sm outline-none focus:ring-1 focus:ring-[hsl(var(--ring))]"
              />
              <button
                type="button"
                onClick={() => setShowPassword(!showPassword)}
                className="absolute right-2 top-1/2 -translate-y-1/2 text-[hsl(var(--muted-foreground))]"
              >
                {showPassword ? <EyeSlash size={16} /> : <Eye size={16} />}
              </button>
            </div>
          </div>

          <button
            type="submit"
            className="flex w-full items-center justify-center gap-2 rounded-[var(--radius)] bg-[hsl(var(--primary))] px-4 py-2 text-sm font-medium text-[hsl(var(--primary-foreground))] hover:opacity-90 transition-opacity"
          >
            <SignIn size={16} />
            Sign In
          </button>
        </form>
      </div>
    </div>
  )
}
