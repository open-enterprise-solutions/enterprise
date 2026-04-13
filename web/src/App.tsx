import { Refine } from "@refinedev/core"
import routerBindings from "@refinedev/react-router"
import { BrowserRouter, Routes, Route } from "react-router-dom"
import { oesDataProvider } from "./providers/oes-data-provider"
import { oesAuthProvider } from "./providers/oes-auth-provider"
import { AppShell } from "./components/layout/AppShell"
import { LoginPage } from "./pages/login"

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
      >
        <Routes>
          <Route path="/login" element={<LoginPage />} />
          <Route path="/*" element={<AppShell />} />
        </Routes>
      </Refine>
    </BrowserRouter>
  )
}
