#ifndef SECUREOS_EVENT_BUS_H
#define SECUREOS_EVENT_BUS_H

#include <stddef.h>
#include <stdint.h>

#include "../cap/capability.h"

typedef enum {
  EVENT_TOPIC_DISK_IO_REQUEST = 1,
  EVENT_TOPIC_DISK_IO_DECISION = 2,
} event_topic_t;

enum {
  EVENT_TOPIC_COUNT = 2,
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
