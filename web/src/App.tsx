import { Refine, Authenticated } from "@refinedev/core"
import routerBindings from "@refinedev/react-router"
import { BrowserRouter, Routes, Route, Outlet, Navigate } from "react-router-dom"
import { oesDataProvider } from "./providers/oes-data-provider"
import { oesAuthProvider } from "./providers/oes-auth-provider"
import { AppShell } from "./components/layout/AppShell"
import { LoginPage } from "./pages/login"

// Wrapper: redirects unauthenticated users to /login, renders AppShell for authenticated ones
function ProtectedLayout() {
  return (
    <Authenticated key="protected-layout" redirectOnFail="/login">
      <AppShell>
        <Outlet />
      </AppShell>
    </Authenticated>
  )
}

// Wrapper: redirects already-authenticated users away from /login to /
function GuestLayout() {
  return (
    <Authenticated key="guest-layout" fallback={<Outlet />}>
      <Navigate to="/" replace />
    </Authenticated>
  )
}

export default function App() {
  return (
    <BrowserRouter>
      <Refine
        dataProvider={oesDataProvider}
        authProvider={oesAuthProvider}
        routerProvider={routerBindings}
        resources={[
          { name: "catalogs", list: "/catalogs" },
          { name: "documents", list: "/documents" },
          { name: "reports", list: "/reports" },
        ]}
        options={{
          syncWithLocation: true,
          warnWhenUnsavedChanges: true,
        }}
      >
        <Routes>
          {/* Guest-only routes (login) */}
          <Route element={<GuestLayout />}>
            <Route path="/login" element={<LoginPage />} />
          </Route>

          {/* Protected routes — rendered inside AppShell */}
          <Route element={<ProtectedLayout />}>
            <Route index element={null} />
            <Route path="/catalogs/*" element={null} />
            <Route path="/documents/*" element={null} />
            <Route path="/reports/*" element={null} />
            <Route path="*" element={null} />
          </Route>
        </Routes>
      </Refine>
    </BrowserRouter>
  )
}
