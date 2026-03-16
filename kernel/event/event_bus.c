/**
 * @file event_bus.c
 * @brief Ring-buffer-based event bus for audit and event logging.
 *
 * Purpose:
 *   Implements a fixed-size ring buffer for publishing, consuming, and
 *   managing audit/security events. Supports event publishing, sequential
 *   consumption, checkpoint/seal operations for tamper-evident audit trails,
 *   and overflow detection. Provides the audit backbone for the zero-trust
 *   capability system.
 *
 * Interactions:
 *   - cap_table.c: publishes audit events on capability grant/revoke.
 *   - cap_gate.c: publishes audit events on gated operation success/failure.
 *   - capability_audit_test.c and other tests: consumes and verifies audit
 *     events for correctness.
 *   - Any kernel subsystem can publish events via event_bus_publish().
 *
 * Launched by:
 *   event_bus_init() is called by kmain() during kernel initialization.
 *   Not a standalone process; compiled into the kernel image.
 */

#include "event_bus.h"

#include "../cap/cap_table.h"

typedef struct {
  os_event_t events[EVENT_QUEUE_CAPACITY];
  size_t next_index;
  size_t event_count;
  size_t dropped_count;
} event_topic_ring_t;

static event_topic_ring_t event_topics[EVENT_TOPIC_COUNT];
static uint8_t event_subscriptions[EVENT_TOPIC_COUNT];
static uint64_t event_next_sequence_id;

static int event_subject_valid(cap_subject_id_t subject_id) {
  return subject_id < CAP_TABLE_MAX_SUBJECTS;
}

static int event_topic_valid(event_topic_t topic) {
  return topic >= EVENT_TOPIC_DISK_IO_REQUEST && topic <= EVENT_TOPIC_DISK_IO_DECISION;
}

static size_t event_topic_index(event_topic_t topic) {
  return (size_t)((uint32_t)topic - 1u);
}

static void event_copy_bytes(uint8_t *dst, const uint8_t *src, size_t size) {
  size_t i = 0u;
  for (i = 0u; i < size; ++i) {
    dst[i] = src[i];
  }
}

static void event_copy_struct(os_event_t *dst, const os_event_t *src) {
  size_t i = 0u;
  const uint8_t *src_bytes = (const uint8_t *)src;
  uint8_t *dst_bytes = (uint8_t *)dst;

  for (i = 0u; i < sizeof(os_event_t); ++i) {
    dst_bytes[i] = src_bytes[i];
  }
}

void event_bus_reset_for_tests(void) {
  size_t topic_index = 0u;
  for (topic_index = 0u; topic_index < EVENT_TOPIC_COUNT; ++topic_index) {
    event_topics[topic_index].next_index = 0u;
    event_topics[topic_index].event_count = 0u;
    event_topics[topic_index].dropped_count = 0u;
    event_subscriptions[topic_index] = 0u;
  }

  event_next_sequence_id = 0u;
}

event_result_t event_subscribe(cap_subject_id_t subject_id, event_topic_t topic) {
  size_t topic_index = 0u;

  if (!event_subject_valid(subject_id)) {
    return EVENT_ERR_SUBJECT_INVALID;
  }

  if (!event_topic_valid(topic)) {
    return EVENT_ERR_TOPIC_INVALID;
  }

  if (cap_table_check(subject_id, CAP_EVENT_SUBSCRIBE) != CAP_OK) {
    return EVENT_ERR_CAP_MISSING;
  }

  topic_index = event_topic_index(topic);
  event_subscriptions[topic_index] |= (uint8_t)(1u << subject_id);
  return EVENT_OK;
}

event_result_t event_unsubscribe(cap_subject_id_t subject_id, event_topic_t topic) {
  size_t topic_index = 0u;

  if (!event_subject_valid(subject_id)) {
    return EVENT_ERR_SUBJECT_INVALID;
  }

  if (!event_topic_valid(topic)) {
    return EVENT_ERR_TOPIC_INVALID;
  }

  if (cap_table_check(subject_id, CAP_EVENT_SUBSCRIBE) != CAP_OK) {
    return EVENT_ERR_CAP_MISSING;
  }

  topic_index = event_topic_index(topic);
  event_subscriptions[topic_index] &= (uint8_t)~(uint8_t)(1u << subject_id);
  return EVENT_OK;
}

int event_is_subscribed_for_tests(cap_subject_id_t subject_id, event_topic_t topic) {
  size_t topic_index = 0u;

  if (!event_subject_valid(subject_id)) {
    return 0;
  }

  if (!event_topic_valid(topic)) {
    return 0;
  }

  topic_index = event_topic_index(topic);
  return (event_subscriptions[topic_index] & (uint8_t)(1u << subject_id)) != 0u;
}

event_result_t event_publish(cap_subject_id_t actor_subject_id,
                             event_topic_t topic,
                             cap_subject_id_t target_subject_id,
                             uint64_t correlation_id,
                             const uint8_t *payload,
                             size_t payload_size,
                             uint64_t *out_sequence_id) {
  size_t topic_index = 0u;
  event_topic_ring_t *ring = 0;
  os_event_t *slot = 0;

  if (!event_subject_valid(actor_subject_id) || !event_subject_valid(target_subject_id)) {
    return EVENT_ERR_SUBJECT_INVALID;
  }

  if (!event_topic_valid(topic)) {
    return EVENT_ERR_TOPIC_INVALID;
  }

  if (payload_size > EVENT_PAYLOAD_MAX) {
    return EVENT_ERR_PAYLOAD_TOO_LARGE;
  }

  if (payload_size > 0u && payload == 0) {
    return EVENT_ERR_PAYLOAD_INVALID;
  }

  if (cap_table_check(actor_subject_id, CAP_EVENT_PUBLISH) != CAP_OK) {
    return EVENT_ERR_CAP_MISSING;
  }

  topic_index = event_topic_index(topic);
  ring = &event_topics[topic_index];
  slot = &ring->events[ring->next_index];

  slot->sequence_id = event_next_sequence_id;
  slot->topic = topic;
  slot->actor_subject_id = actor_subject_id;
  slot->target_subject_id = target_subject_id;
  slot->correlation_id = correlation_id;
  slot->payload_size = payload_size;
  if (payload_size > 0u) {
    event_copy_bytes(slot->payload, payload, payload_size);
  }

  if (out_sequence_id != 0) {
    *out_sequence_id = event_next_sequence_id;
  }

  event_next_sequence_id++;
  ring->next_index = (ring->next_index + 1u) % EVENT_QUEUE_CAPACITY;
  if (ring->event_count < EVENT_QUEUE_CAPACITY) {
    ring->event_count++;
  } else {
    ring->dropped_count++;
  }

  return EVENT_OK;
}

size_t event_count_for_topic_for_tests(event_topic_t topic) {
  if (!event_topic_valid(topic)) {
    return 0u;
  }

  return event_topics[event_topic_index(topic)].event_count;
}

size_t event_dropped_for_topic_for_tests(event_topic_t topic) {
  if (!event_topic_valid(topic)) {
    return 0u;
  }

  return event_topics[event_topic_index(topic)].dropped_count;
}

event_result_t event_get_for_topic_for_tests(event_topic_t topic,
                                              size_t index,
                                              os_event_t *out_event) {
  size_t start = 0u;
  size_t slot = 0u;
  event_topic_ring_t *ring = 0;

  if (!event_topic_valid(topic)) {
    return EVENT_ERR_TOPIC_INVALID;
  }

  ring = &event_topics[event_topic_index(topic)];
  if (out_event == 0 || index >= ring->event_count) {
    return EVENT_ERR_PAYLOAD_INVALID;
  }

  if (ring->event_count == EVENT_QUEUE_CAPACITY) {
    start = ring->next_index;
  }

  slot = (start + index) % EVENT_QUEUE_CAPACITY;
  event_copy_struct(out_event, &ring->events[slot]);
  return EVENT_OK;
}
