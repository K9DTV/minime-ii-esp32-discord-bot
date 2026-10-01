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
//   - gwFastReconnectPending resets on WStype_CONNECTED and on gwClearDropState()
//   - gwReconnectIntervalMs is written only through gwSetReconnectIntervalMs()
// Fast tries after a drop (wifi up), then climb: 3s -> 7s -> 12s, +8s steps, cap 40s.
static const unsigned long GW_RECONNECT_FAST_MS = 200UL;
static const uint8_t GW_RECONNECT_FAST_TRIES = 3;
static const unsigned long GW_RECONNECT_BASE_MS = 3000UL;
static const unsigned long GW_RECONNECT_MAX_MS = 40000UL;
// Stuck-client rebind: wait max(2 * current reconnect interval, this floor).
static const unsigned long GW_REBIND_MIN_MS = 90000UL;

// Session invariants (RESUME vs IDENTIFY):
//   - sessionId / lastSeq / gwResumeHost survive drops; only gwClearSession() wipes them
//     (OP9 d=false, RESUME tries used up, GW_REBIND, HELLO_TIMEOUT, OTA)
//   - while a session is held the library auto-reconnects to gwResumeHost, else GW_PRIMARY_HOST
//   - OP10 Hello sends RESUME (op 6) when gwCanResume(), else IDENTIFY (op 2)
//   - gwResumeTries counts RESUMEs sent since the last READY/RESUMED; gwClearSession resets it
//   - a socket that sent RESUME and drops (or hits GW_RESUME_REPLY_TIMEOUT_MS) before RESUMED/OP9
//     is counted by gwResumeLostOnDrop(); once GW_RESUME_MAX_TRIES RESUMEs went unanswered it
//     clears the session
//   - gwSessionClearReason is the why= on SENT_IDENTIFY; sendIdentify resets it to no_session
static const char GW_PRIMARY_HOST[] = "gateway.discord.gg";
// Discord ends the session on close 1000/1001, and WebSocketsClient::disconnect() always sends 1000.
static const uint16_t GW_RESUME_CLOSE_CODE = 4000;
static const uint8_t GW_RESUME_MAX_TRIES = 2;
// WS upgrade done but no OP10 Hello: give up on this socket and IDENTIFY on the primary host.
static const unsigned long GW_HELLO_TIMEOUT_MS = 20000UL;
// RESUME sent, then this long with no replayed event, RESUMED or OP9 (HB ACKs alone keep the
// socket looking healthy). Each replayed event restarts the wait, so a long replay is not cut off.
static const unsigned long GW_RESUME_REPLY_TIMEOUT_MS = 30000UL;
static const size_t GW_HOST_MAX = 64;

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
extern bool gwFastReconnectPending;
extern char gwResumeHost[GW_HOST_MAX];
extern uint8_t gwResumeTries;
extern bool gwResumeSent;           // this socket sent RESUME; waiting for RESUMED / OP9
extern unsigned long gwResumeProgressMillis; // SENT_RESUME, then each replayed event
extern uint8_t gwBotStatusBeforeDrop;
extern char gwSessionClearReason[20]; // why the next Hello sends IDENTIFY instead of RESUME
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
bool gwCanResume();
void gwResumeLostOnDrop();
void gwSetResumeHost(const char* resumeGatewayUrl);
void gwSendResumeOrIdentify();
void gwSetReconnectIntervalMs(unsigned long ms);
void gwBeginDropEpisode(const char* reason, bool tryResume);
void gwSetReconnectBackoff(bool reset);
void ensureWifiForGateway();
void gwMaybeRebindIfStuck();

// discord_gw_outbound.cpp
void initGwJsonFilter();
void sendResume();

// discord_gw_pump.cpp
void pumpGatewayKeepAlive();

#endif
