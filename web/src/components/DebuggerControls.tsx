/**
 * Debugger transport bar: a compact control strip that drives the single
 * "current step" of the execution trace. The actual play interval lives in a
 * separate usePlayLoop hook; here play()/pause() merely flip state.
 */
import type { ReactNode } from 'react'
import clsx from 'clsx'
import { useStore, useCurrentStep } from '../store'

interface TransportButtonProps {
  onClick: () => void
  label: string
  disabled?: boolean
  active?: boolean
  /** Visually emphasize (used for play/pause). */
  primary?: boolean
  children: ReactNode
}

function TransportButton({
  onClick,
  label,
  disabled,
  active,
  primary,
  children,
}: TransportButtonProps) {
  return (
    <button
      type="button"
      onClick={onClick}
      disabled={disabled}
      title={label}
      aria-label={label}
      className={clsx(
        'inline-flex h-8 min-w-8 items-center justify-center rounded-md border px-2 text-sm font-medium leading-none transition-colors',
        'focus:outline-none focus-visible:ring-1 focus-visible:ring-accent',
        'disabled:cursor-not-allowed disabled:opacity-40',
        primary
          ? active
            ? 'border-accent bg-accent text-ink-900 hover:bg-accent-bright'
            : 'border-accent/60 bg-accent/15 text-accent hover:bg-accent/25'
          : 'border-ink-600 bg-ink-700/60 text-ink-200 hover:border-ink-500 hover:bg-ink-700 hover:text-ink-100',
      )}
    >
      {children}
    </button>
  )
}

export function DebuggerControls() {
  const result = useStore((s) => s.result)
  const stepIndex = useStore((s) => s.stepIndex)
  const playing = useStore((s) => s.playing)
  const speed = useStore((s) => s.speed)

  const stepBack = useStore((s) => s.stepBack)
  const stepForward = useStore((s) => s.stepForward)
  const stepOver = useStore((s) => s.stepOver)
  const togglePlay = useStore((s) => s.togglePlay)
  const reset = useStore((s) => s.reset)
  const setStep = useStore((s) => s.setStep)
  const setSpeed = useStore((s) => s.setSpeed)

  const current = useCurrentStep()
  const trace = result?.trace ?? []
  const total = trace.length
  const hasTrace = total > 0
  const max = Math.max(0, total - 1)
  const sliderValue = stepIndex >= 0 ? stepIndex : 0

  return (
    <div className="flex flex-col gap-2 rounded-xl border border-ink-600 bg-ink-800/80 px-3 py-2.5 backdrop-blur">
      {/* Transport buttons + readout */}
      <div className="flex flex-wrap items-center gap-x-3 gap-y-2">
        <div className="flex items-center gap-1">
          <TransportButton onClick={reset} label="Reset to start" disabled={!hasTrace}>
            ⟲
          </TransportButton>
          <TransportButton onClick={stepBack} label="Step back" disabled={!hasTrace}>
            ◀
          </TransportButton>
          <TransportButton
            onClick={togglePlay}
            label={playing ? 'Pause' : 'Play'}
            disabled={!hasTrace}
            active={playing}
            primary
          >
            {playing ? '⏸' : '▶'}
          </TransportButton>
          <TransportButton onClick={stepForward} label="Step forward" disabled={!hasTrace}>
            ▌▶
          </TransportButton>
          <TransportButton onClick={stepOver} label="Step over" disabled={!hasTrace}>
            ⤼
          </TransportButton>
        </div>

        {hasTrace ? (
          <div className="flex flex-wrap items-center gap-x-3 gap-y-0.5 font-mono text-[11px] text-ink-300">
            <span className="text-ink-200">
              step{' '}
              <span className="tabular-nums text-ink-100">{sliderValue + 1}</span>
              {' / '}
              <span className="tabular-nums">{total}</span>
            </span>
            <span>
              line{' '}
              <span className="tabular-nums text-ink-100">{current?.line ?? '—'}</span>
            </span>
            <span>
              op <span className="text-accent">{current?.op ?? '—'}</span>
            </span>
          </div>
        ) : (
          <span className="text-[11px] text-ink-400">
            Run a program to start debugging.
          </span>
        )}

        {/* Speed control, pushed to the right */}
        <label className="ml-auto flex items-center gap-2 text-[11px] text-ink-300">
          <span className="select-none">speed</span>
          <input
            type="range"
            min={1}
            max={30}
            step={1}
            value={speed}
            onChange={(e) => setSpeed(Number(e.target.value))}
            className="h-1.5 w-24 cursor-pointer accent-accent"
            aria-label="Playback speed (steps per second)"
          />
          <span className="w-12 shrink-0 text-right font-mono tabular-nums text-ink-100">
            x{speed}/s
          </span>
        </label>
      </div>

      {/* Scrubber */}
      <input
        type="range"
        min={0}
        max={max}
        step={1}
        value={sliderValue}
        disabled={!hasTrace}
        onChange={(e) => setStep(Number(e.target.value))}
        className="h-2 w-full cursor-pointer accent-accent disabled:cursor-not-allowed disabled:opacity-40"
        aria-label="Trace scrubber"
      />
    </div>
  )
}
