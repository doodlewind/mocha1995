/** Scanner output: the raw token stream produced by mo_scan.c, rendered as
 *  colour-coded chips with a group legend. */
import { useStore } from '../store'
import type { Token } from '../engine/types'
import { TOKEN_STYLE, tokenStyle, type TokenGroup } from '../theme'
import { Panel, CountBadge, EmptyState } from './Panel'

/** Order of groups in the legend (kept stable / readable). */
const LEGEND_GROUPS: TokenGroup[] = [
  'keyword',
  'identifier',
  'number',
  'string',
  'operator',
  'punct',
  'primary',
]

function Legend() {
  return (
    <div className="flex flex-wrap items-center gap-x-3 gap-y-1.5 border-b border-ink-600 bg-ink-800/40 px-3 py-2">
      {LEGEND_GROUPS.map((group) => {
        const style = TOKEN_STYLE[group]
        return (
          <span
            key={group}
            className="flex items-center gap-1.5 text-[10px] uppercase tracking-wide text-ink-300"
          >
            <span
              className="h-2 w-2 shrink-0 rounded-full"
              style={{ backgroundColor: style.hex }}
              aria-hidden
            />
            {style.label}
          </span>
        )
      })}
    </div>
  )
}

function TokenChip({ token }: { token: Token }) {
  const style = tokenStyle(token.type)
  const hasText = token.text.length > 0
  return (
    <span
      title={`${token.type} @ ${token.line}:${token.col}`}
      className={`inline-flex max-w-full items-center rounded-md border px-1.5 py-0.5 font-mono text-xs leading-none ${style.chip}`}
    >
      {hasText ? (
        <span className="truncate whitespace-pre">{token.text}</span>
      ) : (
        <span className="italic opacity-50">{token.type}</span>
      )}
    </span>
  )
}

export function TokensPanel() {
  const tokens = useStore((s) => s.result?.tokens) ?? []

  return (
    <Panel
      title="Tokens"
      subtitle="mo_scan.c — scanner output"
      badge={<CountBadge>{tokens.length}</CountBadge>}
      bodyClassName="flex flex-col"
    >
      {tokens.length === 0 ? (
        <EmptyState>No tokens yet — run some code to see the scanner output.</EmptyState>
      ) : (
        <>
          <Legend />
          <div className="flex min-h-0 flex-1 flex-wrap content-start gap-1.5 overflow-auto p-3">
            {tokens.map((token) => (
              <TokenChip key={token.index} token={token} />
            ))}
          </div>
        </>
      )}
    </Panel>
  )
}
