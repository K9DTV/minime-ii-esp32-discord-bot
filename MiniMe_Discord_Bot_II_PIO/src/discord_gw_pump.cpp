#include "discord_gw_internal.h"

// Heartbeat / wifi kick. Called from the outer pumpGateway only (not nested).
// Wi-Fi reconnect is skipped while gwPumping/httpsInUse (see ensureWifiForGateway).
void pumpGatewayKeepAlive() {
  if (!gatewayConnected && WiFi.status() != WL_CONNECTED) {
    ensureWifiForGateway();
  }
  // Stuck drop with wifi up: rebind SSL client if library reconnect never yields CONNECTED.
  // Does not call WiFi.disconnect() while wifi is already up (ensureWifiForGateway guards that).
  gwMaybeRebindIfStuck();
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
      gwBeginDropEpisode("hb_ack");
      gwSetReconnectBackoff(false);
      hbAckPending = false;
      gatewayWS.disconnect();
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
