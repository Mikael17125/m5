#pragma once
#include "state.h"

#define SERVICE_UUID  "12345678-1234-1234-1234-123456789abc"
#define CHAR_CMD_UUID "aaaabbbb-1234-1234-1234-abcdef123456"
#define CHAR_SPR_UUID "abcd1234-ab12-ab12-ab12-abcdef123456"
#define CHAR_EVT_UUID "ccccdddd-1234-1234-1234-abcdef123456"

void setupBLE();

// Inbox
void pushInbox(const Notification &n);
const InboxEntry *inboxAt(uint8_t idx);

// TX
void sendEvent (const char *json);
void sendChoice(int idx);
void sendAction(const char *code);

// Dispatch (called by CmdCB after chunks assembled)
void dispatchCmd(const char *s);
