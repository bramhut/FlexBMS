export const advancingUnixTime = (unixTime: number, sampledAtMs: number, nowMs = Date.now()): number => unixTime + Math.floor((nowMs - sampledAtMs) / 1000)

export const advancingUptimeMs = (uptimeMs: number, sampledAtMs: number, nowMs = performance.now()): number => uptimeMs + Math.max(0, nowMs - sampledAtMs)

export type UptimeSampleResult = 'accepted' | 'stale' | 'restart' | 'wrap'

export type UptimeTracker = {
  baseUptimeMs?: number
  sampledAtMs: number
  lastRawUptimeMs?: number
  lastDisplayedUptimeMs?: number
}

export const createUptimeTracker = (): UptimeTracker => ({ sampledAtMs: 0 })

export const resetUptimeTracker = (tracker: UptimeTracker): void => {
  tracker.baseUptimeMs = undefined
  tracker.sampledAtMs = 0
  tracker.lastRawUptimeMs = undefined
  tracker.lastDisplayedUptimeMs = undefined
}

export const sampleUptime = (tracker: UptimeTracker, uptimeMs: number, sampledAtMs: number): UptimeSampleResult => {
  if (!Number.isFinite(uptimeMs) || uptimeMs < 0) return 'stale'

  const previous = tracker.lastRawUptimeMs
  if (previous !== undefined && uptimeMs < previous) {
    const wrapped = previous > 0xf0000000 && uptimeMs < 0x0fffffff
    const restarted = uptimeMs < 60_000
    if (!wrapped && !restarted) return 'stale'
    tracker.lastDisplayedUptimeMs = undefined
    tracker.baseUptimeMs = uptimeMs
    tracker.sampledAtMs = sampledAtMs
    tracker.lastRawUptimeMs = uptimeMs
    return wrapped ? 'wrap' : 'restart'
  }

  tracker.baseUptimeMs = uptimeMs
  tracker.sampledAtMs = sampledAtMs
  tracker.lastRawUptimeMs = uptimeMs
  return 'accepted'
}

export const displayedUptimeMs = (tracker: UptimeTracker, nowMs = performance.now()): number | undefined => {
  if (tracker.baseUptimeMs === undefined) return undefined
  const estimated = advancingUptimeMs(tracker.baseUptimeMs, tracker.sampledAtMs, nowMs)
  tracker.lastDisplayedUptimeMs = Math.max(tracker.lastDisplayedUptimeMs ?? estimated, estimated)
  return tracker.lastDisplayedUptimeMs
}

export const formatUptime = (uptimeMs: number): string => {
  const totalSeconds = Math.floor(uptimeMs / 1000)
  const days = Math.floor(totalSeconds / 86400)
  const hours = Math.floor(totalSeconds % 86400 / 3600)
  const minutes = Math.floor(totalSeconds % 3600 / 60)
  const seconds = totalSeconds % 60
  if (days > 0) return `${days} d ${hours} h ${minutes} min`
  if (hours > 0) return `${hours} h ${minutes} min`
  if (minutes > 0) return `${minutes} min ${seconds} s`
  return `${seconds} s`
}
