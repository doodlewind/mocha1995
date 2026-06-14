import { Toolbar } from './components/Toolbar'
import { EditorPane } from './components/EditorPane'
import { ConsolePanel } from './components/ConsolePanel'
import { TokensPanel } from './components/TokensPanel'
import { BytecodePanel } from './components/BytecodePanel'
import { StackPanel } from './components/StackPanel'
import { CallStackPanel } from './components/CallStackPanel'
import { DebuggerControls } from './components/DebuggerControls'
import { PipelinePanel } from './components/charts/PipelinePanel'
import { StackDepthChart } from './components/charts/StackDepthChart'
import { OpcodeChart } from './components/charts/OpcodeChart'
import { usePlayLoop } from './hooks/usePlayLoop'

export function App() {
  usePlayLoop()

  return (
    <div className="flex h-screen flex-col bg-ink-900 text-ink-100">
      <Toolbar />
      <div className="border-b border-ink-600 bg-ink-800/60 px-2 py-2">
        <DebuggerControls />
      </div>

      <main className="grid min-h-0 flex-1 grid-cols-1 gap-2 p-2 lg:grid-cols-12">
        {/* Left: source + console */}
        <div className="flex min-h-0 flex-col gap-2 lg:col-span-4">
          <EditorPane />
          <ConsolePanel />
        </div>

        {/* Middle: bytecode + runtime state */}
        <div className="flex min-h-0 flex-col gap-2 lg:col-span-4">
          <BytecodePanel />
          <div className="grid min-h-0 flex-1 grid-cols-2 gap-2">
            <StackPanel />
            <CallStackPanel />
          </div>
        </div>

        {/* Right: scanner + charts */}
        <div className="flex min-h-0 flex-col gap-2 overflow-auto lg:col-span-4">
          <PipelinePanel />
          <TokensPanel />
          <StackDepthChart />
          <OpcodeChart />
        </div>
      </main>
    </div>
  )
}
