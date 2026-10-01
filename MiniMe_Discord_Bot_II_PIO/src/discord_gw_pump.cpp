#include "discord_gw_internal.h"

// WS upgrade done but no OP10 Hello: neither RESUME nor IDENTIFY can go out on this socket.
// Runs after gatewayWS.loop(), so a Hello already buffered has been handled first.
static void gwCheckHelloTimeout() {
  if (!gatewayConnected || gotHello) return;
  unsigned long waited = millis() - gwLastConnectOrRebindMillis;
  if (waited < GW_HELLO_TIMEOUT_MS) return;
  char msg[48];
  snprintf(msg, sizeof(msg), "HELLO_TIMEOUT after_ms=%lu", waited);
  gwNoteDrop("HELLO_TIMEOUT", msg);
  gwBeginDropEpisode("hello_timeout", false);
  gwSetReconnectBackoff(false);
  gatewayWS.disconnectWithCode(GW_RESUME_CLOSE_CODE);
}

// Counts as one unanswered RESUME (gwBeginDropEpisode -> gwResumeLostOnDrop).
static void gwCheckResumeReplyTimeout() {
  if (!gwResumeSent || !gatewayConnected) return;
  unsigned long idle = millis() - gwResumeProgressMillis;
  if (idle < GW_RESUME_REPLY_TIMEOUT_MS) return;
  char msg[48];
  snprintf(msg, sizeof(msg), "RESUME_TIMEOUT idle_ms=%lu", idle);
  gwNoteDrop("RESUME_TIMEOUT", msg);
  gwBeginDropEpisode("resume_timeout", true);
  gwSetReconnectBackoff(false);
  gatewayWS.disconnectWithCode(GW_RESUME_CLOSE_CODE);
}

// Heartbeat / wifi kick. Called from the outer pumpGateway only (not nested).
// Wi-Fi reconnect is skipped while gwPumping/httpsInUse (see ensureWifiForGateway).
void pumpGatewayKeepAlive() {
  if (!gatewayConnected && WiFi.status() != WL_CONNECTED) {
    ensureWifiForGateway();
  }
  // Stuck drop with wifi up: rebind SSL client if library reconnect never yields CONNECTED.
  // Does not call WiFi.disconnect() while wifi is already up (ensureWifiForGateway guards that).
  gwMaybeRebindIfStuck();
  gwCheckHelloTimeout();
  gwCheckResumeReplyTimeout();
  if (heartbeatIntervalMs <= 0 || !gatewayConnected || !gotHello) return;
  unsigned long now = millis();
  unsigned long hbInterval = (unsigned long)heartbeatIntervalMs;
  unsigned long hbAckDeadline = hbInterval + GW_HB_ACK_GRACE_MS;
  if (hbAckPending) {
    if (now - hbSentMillis >= hbAckDeadline) {
      char toMsg[48];
      snprintf(toMsg, sizeof(toMsg), "HB_ACK_TIMEOUT after_ms=%lu",
               (unsigned long)(now - hbSentMillis));
      gwLogAppend(toMsg);
      gwBeginDropEpisode("hb_ack", true); // zombie socket: Discord says close non-1000, then RESUME
      gwSetReconnectBackoff(false);
      hbAckPending = false;
      gatewayWS.disconnectWithCode(GW_RESUME_CLOSE_CODE);
    }
    return;
  }
  if (now - lastHeartbeatMillis >= hbInterval) {
    lastHeartbeatMillis = now;
    sendHeartbeat();
  }
}

void pumpGateway() {
  // Re-entry guard: gatewayWS.loop must not nest. After dual-core, commands/HTTPS run from
  // loop() via drainDiscordCmds (not inside the WS callback), so nested pumps are rare;
  // if they happen, skip entirely (0.7.40 pruned the old nested HB-only path).
  if (gwPumping) return;

  gwPumping = true;
  gatewayWS.loop();
  gwSerialService();
  pumpGatewayKeepAlive();

  if (gwDeferPresenceOnline) {
    gwDeferPresenceOnline = false;
    if (gatewayConnected && identified && botDiscordStatus == 2) {
      sendBotPresence("online", false);
    }
  }
  gwPumping = false;
}
