// Binary wire protocol shared with GlassSDK/examples/gps/gps.c.
// Keep both sides in sync if you change any layout or constant here.

export const DOWNLINK_CHANNEL = 0x4750; // 'GP', phone -> glasses
export const UPLINK_CHANNEL = 0x4751; // 'GQ', glasses -> phone
export const PROTOCOL_VERSION = 1;

export const MSG_METRICS = 1;
export const MSG_RESET = 2;

export const EVENT_BUTTON = 1;

export const FLAG_RUNNING = 0x01;
export const FLAG_PAUSED = 0x02;
export const FLAG_GPS_OK = 0x04;

// Metrics packet layout (15 bytes):
//   [0] version, [1] MSG_METRICS,
//   [2..3] duration_sec (u16 LE), [4..7] distance_m (u32 LE),
//   [8..9] pace_sec_km (u16 LE), [10..11] bearing (u16 LE),
//   [12..13] speed_dm_s (u16 LE), [14] flags
export function encodeMetrics(metrics) {
  const bytes = new Uint8Array(15);
  bytes[0] = PROTOCOL_VERSION;
  bytes[1] = MSG_METRICS;
  const duration = Math.max(0, Math.min(0xffff, Math.round(metrics.durationSec) || 0));
  bytes[2] = duration & 0xff;
  bytes[3] = (duration >> 8) & 0xff;
  const distance = Math.max(0, Math.min(0xffffffff, Math.round(metrics.distanceM) || 0));
  bytes[4] = distance & 0xff;
  bytes[5] = (distance >> 8) & 0xff;
  bytes[6] = (distance >> 16) & 0xff;
  bytes[7] = (distance >>> 24) & 0xff;
  const pace = Math.max(0, Math.min(0xffff, Math.round(metrics.paceSecKm) || 0));
  bytes[8] = pace & 0xff;
  bytes[9] = (pace >> 8) & 0xff;
  const bearing = Math.max(0, Math.min(359, Math.round(metrics.bearing) || 0));
  bytes[10] = bearing & 0xff;
  bytes[11] = (bearing >> 8) & 0xff;
  const speed = Math.max(0, Math.min(0xffff, Math.round(metrics.speedDmS) || 0));
  bytes[12] = speed & 0xff;
  bytes[13] = (speed >> 8) & 0xff;
  bytes[14] = metrics.flags & 0xff;
  return bytes;
}

export function encodeReset() {
  return new Uint8Array([PROTOCOL_VERSION, MSG_RESET]);
}

// Decodes a glasses -> phone event. Returns null for anything unrecognized.
// Layout: [version, EVENT_BUTTON, action] where action 0 = short, 1 = long.
export function decodeEvent(data) {
  if (!data || data.length < 3 || data[0] !== PROTOCOL_VERSION) return null;
  if (data[1] === EVENT_BUTTON) {
    return { event: EVENT_BUTTON, command: data[2] === 1 ? 'long' : 'short' };
  }
  return null;
}
