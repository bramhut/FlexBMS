export const browserUnixTime = (nowMs = Date.now()): number => Math.floor(nowMs / 1000)
export const advancingUnixTime = (unixTime: number, sampledAtMs: number, nowMs = Date.now()): number => unixTime + Math.floor((nowMs - sampledAtMs) / 1000)
