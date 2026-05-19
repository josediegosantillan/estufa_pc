#pragma once

#include <stdbool.h>
#include <stdint.h>

void wifi_init(void);
void espnow_init_peer(void);
void notify_relay_state(void);
void maybe_report_status(bool force);
void espnow_send_status_reply(const uint8_t *dst_addr);
