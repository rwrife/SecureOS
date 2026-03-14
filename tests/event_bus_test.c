#include <stdio.h>
#include <stdlib.h>

#include "../kernel/cap/cap_table.h"
#include "../kernel/event/event_bus.h"

static void fail(const char *reason) {
  printf("TEST:FAIL:event_bus:%s\n", reason);
  exit(1);
}

int main(void) {
  const uint8_t request_payload[] = {'r', 'e', 'a', 'd'};
  const uint8_t decision_payload[] = {'a', 'l', 'l', 'o', 'w'};
  uint64_t sequence_id = 0u;
  os_event_t event;
  size_t i = 0u;

  printf("TEST:START:event_bus\n");

  cap_table_init();
  cap_table_reset();
  event_bus_reset_for_tests();

  if (event_subscribe(1u, EVENT_TOPIC_DISK_IO_REQUEST) != EVENT_ERR_CAP_MISSING) {
    fail("subscribe_requires_cap");
  }
  if (cap_table_grant(1u, CAP_EVENT_SUBSCRIBE) != CAP_OK) {
    fail("grant_subscribe_cap_failed");
  }
  if (event_subscribe(1u, EVENT_TOPIC_DISK_IO_REQUEST) != EVENT_OK) {
    fail("subscribe_failed");
  }
  if (!event_is_subscribed_for_tests(1u, EVENT_TOPIC_DISK_IO_REQUEST)) {
    fail("subscribe_not_recorded");
  }
  if (event_unsubscribe(1u, EVENT_TOPIC_DISK_IO_REQUEST) != EVENT_OK) {
    fail("unsubscribe_failed");
  }
  if (event_is_subscribed_for_tests(1u, EVENT_TOPIC_DISK_IO_REQUEST)) {
    fail("unsubscribe_not_recorded");
  }
  printf("TEST:PASS:event_bus_subscribe_contract\n");

  if (event_publish(2u,
                    EVENT_TOPIC_DISK_IO_REQUEST,
                    1u,
                    42u,
                    request_payload,
                    sizeof(request_payload),
                    &sequence_id) != EVENT_ERR_CAP_MISSING) {
    fail("publish_requires_cap");
  }

  if (cap_table_grant(2u, CAP_EVENT_PUBLISH) != CAP_OK) {
    fail("grant_publish_cap_failed");
  }

  if (event_publish(2u,
                    EVENT_TOPIC_DISK_IO_REQUEST,
                    1u,
                    42u,
                    request_payload,
                    sizeof(request_payload),
                    &sequence_id) != EVENT_OK) {
    fail("publish_request_failed");
  }
  if (sequence_id != 0u) {
    fail("sequence_start_not_zero");
  }
  if (event_count_for_topic_for_tests(EVENT_TOPIC_DISK_IO_REQUEST) != 1u) {
    fail("request_count_incorrect");
  }
  if (event_get_for_topic_for_tests(EVENT_TOPIC_DISK_IO_REQUEST, 0u, &event) != EVENT_OK) {
    fail("request_readback_failed");
  }
  if (event.actor_subject_id != 2u || event.target_subject_id != 1u) {
    fail("request_subjects_incorrect");
  }
  if (event.correlation_id != 42u) {
    fail("request_correlation_incorrect");
  }
  if (event.payload_size != sizeof(request_payload)) {
    fail("request_payload_size_incorrect");
  }
  if (event.payload[0] != 'r' || event.payload[1] != 'e') {
    fail("request_payload_incorrect");
  }

  if (event_publish(1u,
                    EVENT_TOPIC_DISK_IO_DECISION,
                    2u,
                    42u,
                    decision_payload,
                    sizeof(decision_payload),
                    &sequence_id) != EVENT_ERR_CAP_MISSING) {
    fail("decision_requires_publish_cap");
  }

  if (cap_table_grant(1u, CAP_EVENT_PUBLISH) != CAP_OK) {
    fail("grant_decision_publish_cap_failed");
  }
  if (event_publish(1u,
                    EVENT_TOPIC_DISK_IO_DECISION,
                    2u,
                    42u,
                    decision_payload,
                    sizeof(decision_payload),
                    &sequence_id) != EVENT_OK) {
    fail("publish_decision_failed");
  }
  if (sequence_id != 1u) {
    fail("sequence_increment_failed");
  }
  printf("TEST:PASS:event_bus_publish_contract\n");

  for (i = 0u; i < EVENT_QUEUE_CAPACITY + 4u; ++i) {
    if (event_publish(2u,
                      EVENT_TOPIC_DISK_IO_REQUEST,
                      1u,
                      1000u + i,
                      request_payload,
                      sizeof(request_payload),
                      &sequence_id) != EVENT_OK) {
      fail("overflow_publish_failed");
    }
  }

  if (event_count_for_topic_for_tests(EVENT_TOPIC_DISK_IO_REQUEST) != EVENT_QUEUE_CAPACITY) {
    fail("overflow_retained_count_incorrect");
  }
  if (event_dropped_for_topic_for_tests(EVENT_TOPIC_DISK_IO_REQUEST) != 5u) {
    fail("overflow_dropped_count_incorrect");
  }
  printf("TEST:PASS:event_bus_overflow_contract\n");

  return 0;
}
