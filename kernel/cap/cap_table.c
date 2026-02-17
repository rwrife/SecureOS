#include "cap_table.h"

#include <stdint.h>

#define CAP_ID_MIN CAP_CONSOLE_WRITE
#define CAP_ID_MAX CAP_CONSOLE_WRITE

#define CAPABILITY_COUNT ((uint32_t)(CAP_ID_MAX - CAP_ID_MIN + 1u))
#define CAP_WORD_BITS 8u
#define CAP_WORDS_PER_SUBJECT ((CAPABILITY_COUNT + CAP_WORD_BITS - 1u) / CAP_WORD_BITS)

static uint8_t subject_capability_bits[CAP_TABLE_MAX_SUBJECTS][CAP_WORDS_PER_SUBJECT];

static int cap_subject_valid(cap_subject_id_t subject_id) {
  return subject_id < CAP_TABLE_MAX_SUBJECTS;
}

static int cap_id_valid(capability_id_t capability_id) {
  return capability_id >= CAP_ID_MIN && capability_id <= CAP_ID_MAX;
}

static uint32_t cap_index(capability_id_t capability_id) {
  return (uint32_t)(capability_id - CAP_ID_MIN);
}

void cap_table_init(void) {
  cap_table_reset();
}

void cap_table_reset(void) {
  for (size_t subject = 0; subject < CAP_TABLE_MAX_SUBJECTS; ++subject) {
    for (size_t word = 0; word < CAP_WORDS_PER_SUBJECT; ++word) {
      subject_capability_bits[subject][word] = 0u;
    }
  }
}

cap_result_t cap_table_grant(cap_subject_id_t subject_id, capability_id_t capability_id) {
  if (!cap_subject_valid(subject_id)) {
    return CAP_ERR_SUBJECT_INVALID;
  }

  if (!cap_id_valid(capability_id)) {
    return CAP_ERR_CAP_INVALID;
  }

  const uint32_t index = cap_index(capability_id);
  const uint32_t word = index / CAP_WORD_BITS;
  const uint32_t bit = index % CAP_WORD_BITS;
  subject_capability_bits[subject_id][word] |= (uint8_t)(1u << bit);

  return CAP_OK;
}

cap_result_t cap_table_revoke(cap_subject_id_t subject_id, capability_id_t capability_id) {
  if (!cap_subject_valid(subject_id)) {
    return CAP_ERR_SUBJECT_INVALID;
  }

  if (!cap_id_valid(capability_id)) {
    return CAP_ERR_CAP_INVALID;
  }

  const uint32_t index = cap_index(capability_id);
  const uint32_t word = index / CAP_WORD_BITS;
  const uint32_t bit = index % CAP_WORD_BITS;
  subject_capability_bits[subject_id][word] &= (uint8_t)~(uint8_t)(1u << bit);

  return CAP_OK;
}

cap_result_t cap_table_check(cap_subject_id_t subject_id, capability_id_t capability_id) {
  if (!cap_subject_valid(subject_id)) {
    return CAP_ERR_SUBJECT_INVALID;
  }

  if (!cap_id_valid(capability_id)) {
    return CAP_ERR_CAP_INVALID;
  }

  const uint32_t index = cap_index(capability_id);
  const uint32_t word = index / CAP_WORD_BITS;
  const uint32_t bit = index % CAP_WORD_BITS;

  if ((subject_capability_bits[subject_id][word] & (uint8_t)(1u << bit)) != 0u) {
    return CAP_OK;
  }

  return CAP_ERR_MISSING;
}
