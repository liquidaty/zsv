#ifndef ZSVSHEET_STATUS_MESSAGE_H
#define ZSVSHEET_STATUS_MESSAGE_H

// How long a footer status message stays up, in milliseconds: the shipped 4000, or the value
// ZSVSHEET_STATUS_TIMEOUT_MS holds when it is a plain number. 0 keeps each message until
// something replaces it. This is the one place a saved configuration would read the value
unsigned zsvsheet_status_message_timeout_ms(void);

// Whether a message shown since set_ms has had its time by now_ms. set_ms 0 means the status
// carries no clock: it reports state ("(building index) ", "(working) ...") rather than an
// event, and does not expire this way, as a timeout of 0 does not either. now_ms 0 (an
// unavailable clock, per zsv/utils/os.h) or a reading behind set_ms likewise leaves the message
// in place rather than expiring it
int zsvsheet_status_message_expired(unsigned long long set_ms, unsigned long long now_ms, unsigned timeout_ms);

#endif
