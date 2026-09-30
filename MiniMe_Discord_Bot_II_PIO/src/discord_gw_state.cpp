#include "discord_gw_internal.h"

// Public Gateway globals (declared in minime.h)
WebSocketsClient gatewayWS;
JsonDocument* gwDoc = nullptr;
bool gatewayConnected     = false;
bool identified           = false;
bool gotHello             = false;
int  heartbeatIntervalMs   = 0;
unsigned long lastHeartbeatMillis = 0;
int lastSeq               = 0;
String sessionId;
unsigned long lastBotActivityMillis = 0;
uint8_t botDiscordStatus = 0;

// Module-shared state
char gwLog[GW_LOG_MAX][GW_LOG_COLS + 1];
uint8_t gwLogCount = 0;
char gwLogLastAdded[GW_LOG_COLS + 1];
char gwDropStartEvent[GW_LOG_COLS + 1];
bool gwInDropState = false;
unsigned long gwLastDropRemindMillis = 0;
unsigned long gwLastFullLogMillis = 0;
unsigned long gwReconnectIntervalMs = 3000;
uint8_t gwReconnectFailCount = 0;
unsigned long gwLastWifiKickMillis = 0;
char gwLastDropKind[32];
char gwLastDropDetail[GW_LOG_COLS + 1];
bool gwLoggedConnectDuringDrop = false;
unsigned long gwDropStartedMillis = 0;
unsigned long gwLastDisconnectMillis = 0;
unsigned long gwLastConnectOrRebindMillis = 0;
bool gwFastIdentifyPending = false;
bool hbAckPending = false;
unsigned long hbSentMillis = 0;
bool gwPumping = false;
bool gwDeferPresenceOnline = false;
JsonDocument gwFilter;
bool gwFilterReady = false;
