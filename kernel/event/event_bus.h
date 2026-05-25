#ifndef SECUREOS_EVENT_BUS_H
#define SECUREOS_EVENT_BUS_H

#include <stddef.h>
#include <stdint.h>

#include "../cap/capability.h"

typedef enum {
  EVENT_TOPIC_DISK_IO_REQUEST = 1,
  EVENT_TOPIC_DISK_IO_DECISION = 2,
  EVENT_TOPIC_NETWORK_TX_REQUEST = 3,
  EVENT_TOPIC_NETWORK_RX_PACKET = 4,
  EVENT_TOPIC_NETWORK_DECISION = 5,
  EVENT_TOPIC_AUTH_PROMPT = 6,
  EVENT_TOPIC_AUTH_RESPONSE = 7,
} event_topic_t;

enum {
  EVENT_TOPIC_COUNT = 7,
  EVENT_QUEUE_CAPACITY = 16,
  EVENT_PAYLOAD_MAX = 96,
};

typedef enum {
  EVENT_OK = 0,
  EVENT_ERR_SUBJECT_INVALID = 1,
  EVENT_ERR_TOPIC_INVALID = 2,
  EVENT_ERR_PAYLOAD_TOO_LARGE = 3,
  EVENT_ERR_PAYLOAD_INVALID = 4,
  EVENT_ERR_CAP_MISSING = 5,
} event_result_t;

typedef struct {
  uint64_t sequence_id;
  event_topic_t topic;
  cap_subject_id_t actor_subject_id;
  cap_subject_id_t target_subject_id;
  uint64_t correlation_id;
  size_t payload_size;
  uint8_t payload[EVENT_PAYLOAD_MAX];
} os_event_t;

/**
 * Auth prompt types for EVENT_TOPIC_AUTH_PROMPT payloads.
 * The payload is: [1 byte type] [null-terminated description string]
 */
typedef enum {
  AUTH_PROMPT_DISK_IO = 1,
  AUTH_PROMPT_UNSIGNED_BINARY = 2,
  AUTH_PROMPT_NETWORK = 3,
} auth_prompt_type_t;

/**
 * Auth response values for EVENT_TOPIC_AUTH_RESPONSE payloads.
 * The payload is a single byte with one of these values.
 */
typedef enum {
  AUTH_RESPONSE_DENY = 0,
  AUTH_RESPONSE_ALLOW = 1,
  AUTH_RESPONSE_ALLOW_ALWAYS = 2,
} auth_response_t;

/**
 * Pending auth request slot. When an authorization is needed, the kernel
 * publishes an AUTH_PROMPT event and stores the request here. The UI layer
 * (console or window manager) reads the pending prompt and publishes an
 * AUTH_RESPONSE event with the matching correlation_id.
 */
typedef struct {
  int active;
  uint64_t correlation_id;
  unsigned int session_id;
  auth_prompt_type_t type;
  char description[EVENT_PAYLOAD_MAX];
  auth_response_t response;
  int responded;
} pending_auth_request_t;

enum {
  PENDING_AUTH_MAX = 4,
};

/** Get the pending auth request table (PENDING_AUTH_MAX entries). */
pending_auth_request_t *event_get_pending_auth_table(void);

/** Submit a response for a pending auth request by correlation_id. */
void event_respond_auth(uint64_t correlation_id, auth_response_t response);

void event_bus_reset_for_tests(void);

event_result_t event_subscribe(cap_subject_id_t subject_id, event_topic_t topic);
event_result_t event_unsubscribe(cap_subject_id_t subject_id, event_topic_t topic);
int event_is_subscribed_for_tests(cap_subject_id_t subject_id, event_topic_t topic);

event_result_t event_publish(cap_subject_id_t actor_subject_id,
                             event_topic_t topic,
                             cap_subject_id_t target_subject_id,
                             uint64_t correlation_id,
                             const uint8_t *payload,
                             size_t payload_size,
                             uint64_t *out_sequence_id);

size_t event_count_for_topic_for_tests(event_topic_t topic);
size_t event_dropped_for_topic_for_tests(event_topic_t topic);
event_result_t event_get_for_topic_for_tests(event_topic_t topic,
                                              size_t index,
                                              os_event_t *out_event);

#endif
