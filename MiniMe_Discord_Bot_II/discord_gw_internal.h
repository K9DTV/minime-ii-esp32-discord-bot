#ifndef DISCORD_GW_INTERNAL_H
#define DISCORD_GW_INTERNAL_H

// Private Discord Gateway modules (Core 1):
//   discord_gw_state.cpp / _log / _session / _outbound / _pump / discord_gw_event.cpp
// Public API stays in minime.h.
#include "minime.h"
#include "cores.h"

// Serial drop/reconnect diagnostics (ring dumped to LOG panel every 60 s).
static const uint8_t GW_LOG_MAX = 40;
static const uint8_t GW_LOG_COLS = 96;

// Reconnect state invariants:
//   - gwReconnectFailCount resets on READY/RESUMED (gwSetReconnectBackoff(true))
//   - gwReconnectFailCount resets at the start of every drop episode (gwBeginDropEpisode)
//   - gwFastIdentifyPending resets on WStype_CONNECTED and on gwClearDropState()
//   - gwReconnectIntervalMs is written only through gwSetReconnectIntervalMs()
// Fast tries after a drop (wifi up), then climb: 3s -> 7s -> 12s, +8s steps, cap 40s.
static const unsigned long GW_RECONNECT_FAST_MS = 200UL;
static const uint8_t GW_RECONNECT_FAST_TRIES = 3;
static const unsigned long GW_RECONNECT_BASE_MS = 3000UL;
static const unsigned long GW_RECONNECT_MAX_MS = 40000UL;
// Stuck-client rebind: wait max(2 * current reconnect interval, this floor).
static const unsigned long GW_REBIND_MIN_MS = 90000UL;

// Shared Gateway state (defined in discord_gw_state.cpp)
extern char gwLog[GW_LOG_MAX][GW_LOG_COLS + 1];
extern uint8_t gwLogCount;
extern char gwLogLastAdded[GW_LOG_COLS + 1];
extern char gwDropStartEvent[GW_LOG_COLS + 1];
extern bool gwInDropState;
extern unsigned long gwLastDropRemindMillis;
extern unsigned long gwLastFullLogMillis;
extern unsigned long gwReconnectIntervalMs;
extern uint8_t gwReconnectFailCount;
extern unsigned long gwLastWifiKickMillis;
extern char gwLastDropKind[32];
extern char gwLastDropDetail[GW_LOG_COLS + 1];
extern bool gwLoggedConnectDuringDrop;
extern unsigned long gwDropStartedMillis;
extern unsigned long gwLastDisconnectMillis;
extern unsigned long gwLastConnectOrRebindMillis;
extern bool gwFastIdentifyPending;
extern bool hbAckPending;
extern unsigned long hbSentMillis;
extern bool gwPumping;
extern bool gwDeferPresenceOnline;
extern JsonDocument gwFilter;
extern bool gwFilterReady;

// discord_gw_log.cpp
void gwLogAppend(const char* ev);
void gwNoteDrop(const char* kind, const char* detail);
void gwClearDropState();

// discord_gw_session.cpp
void gwClearSession(const char* reason);
void gwSetReconnectIntervalMs(unsigned long ms);
void gwBeginDropEpisode(const char* reason);
void gwSetReconnectBackoff(bool reset);
void ensureWifiForGateway();
void gwMaybeRebindIfStuck();

// discord_gw_outbound.cpp
void initGwJsonFilter();

// discord_gw_pump.cpp
void pumpGatewayKeepAlive();

#endif
