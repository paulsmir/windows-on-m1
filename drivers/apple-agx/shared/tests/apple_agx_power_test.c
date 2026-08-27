#include "apple_agx_power.h"

#include <assert.h>
#include <string.h>

typedef struct _FAKE_BROKER {
  APPLE_AGX_POWER_U32 State;
  APPLE_AGX_POWER_U32 Result;
  APPLE_AGX_POWER_U64 Receipt;
  APPLE_AGX_POWER_U64 Pending;
  APPLE_AGX_POWER_U32 Commands[8];
  unsigned CommandCount;
  APPLE_AGX_POWER_U32 FailCommand;
} FAKE_BROKER;

static APPLE_AGX_POWER_U32 read32(void *Context, APPLE_AGX_POWER_U32 Offset) {
  FAKE_BROKER *broker = Context;
  switch (Offset) {
  case J313_AGX_G2_POWER_REG_MAGIC:
    return J313_AGX_G2_POWER_MAGIC;
  case J313_AGX_G2_POWER_REG_ABI_VERSION:
    return J313_AGX_G2_POWER_ABI_VERSION;
  case J313_AGX_G2_POWER_REG_CAPABILITIES:
    return J313_AGX_G2_POWER_CAP_FIXED_J313_DOMAINS;
  case J313_AGX_G2_POWER_REG_STATE:
    return broker->State;
  case J313_AGX_G2_POWER_REG_RESULT:
    return broker->Result;
  default:
    assert(0);
    return 0;
  }
}

static APPLE_AGX_POWER_U64 read64(void *Context, APPLE_AGX_POWER_U32 Offset) {
  FAKE_BROKER *broker = Context;
  assert(Offset == J313_AGX_G2_POWER_REG_RECEIPT_SEQUENCE);
  return broker->Receipt;
}

static void write64(void *Context, APPLE_AGX_POWER_U32 Offset,
                    APPLE_AGX_POWER_U64 Value) {
  FAKE_BROKER *broker = Context;
  assert(Offset == J313_AGX_G2_POWER_REG_REQUEST_SEQUENCE);
  broker->Pending = Value;
}

static void write32(void *Context, APPLE_AGX_POWER_U32 Offset,
                    APPLE_AGX_POWER_U32 Value) {
  FAKE_BROKER *broker = Context;
  assert(Offset == J313_AGX_G2_POWER_REG_COMMAND);
  assert(broker->CommandCount < 8);
  broker->Commands[broker->CommandCount++] = Value;
  broker->Receipt = broker->Pending;
  broker->Result = Value == broker->FailCommand
                       ? J313_AGX_G2_POWER_RESULT_TRANSITION_FAILED
                       : J313_AGX_G2_POWER_RESULT_OK;
  if (broker->Result != J313_AGX_G2_POWER_RESULT_OK)
    return;
  if (Value == J313_AGX_G2_POWER_CMD_ON)
    broker->State = J313_AGX_G2_POWER_STATE_ON;
  else if (Value == J313_AGX_G2_POWER_CMD_OFF)
    broker->State = J313_AGX_G2_POWER_STATE_OFF;
}

static APPLE_AGX_POWER_IO make_io(FAKE_BROKER *broker) {
  APPLE_AGX_POWER_IO io = {broker, read32, read64, write32, write64};
  return io;
}

static void test_exact_on_query_off_qualification(void) {
  FAKE_BROKER broker;
  APPLE_AGX_POWER_IO io;
  memset(&broker, 0, sizeof(broker));
  broker.FailCommand = 0xFFFFFFFFu;
  io = make_io(&broker);

  assert(AppleAgxPowerQualify(&io));
  assert(broker.CommandCount == 3);
  assert(broker.Commands[0] == J313_AGX_G2_POWER_CMD_ON);
  assert(broker.Commands[1] == J313_AGX_G2_POWER_CMD_QUERY);
  assert(broker.Commands[2] == J313_AGX_G2_POWER_CMD_OFF);
  assert(broker.State == J313_AGX_G2_POWER_STATE_OFF);
  assert(broker.Receipt == 3);
}

static void test_query_failure_still_powers_off(void) {
  FAKE_BROKER broker;
  APPLE_AGX_POWER_IO io;
  memset(&broker, 0, sizeof(broker));
  broker.FailCommand = J313_AGX_G2_POWER_CMD_QUERY;
  io = make_io(&broker);

  assert(!AppleAgxPowerQualify(&io));
  assert(broker.CommandCount == 3);
  assert(broker.Commands[2] == J313_AGX_G2_POWER_CMD_OFF);
  assert(broker.State == J313_AGX_G2_POWER_STATE_OFF);
}

static void test_acquire_leaves_power_on_until_release(void) {
  FAKE_BROKER broker;
  APPLE_AGX_POWER_IO io;
  memset(&broker, 0, sizeof(broker));
  broker.FailCommand = 0xFFFFFFFFu;
  io = make_io(&broker);

  assert(AppleAgxPowerAcquire(&io));
  assert(broker.CommandCount == 2);
  assert(broker.Commands[0] == J313_AGX_G2_POWER_CMD_ON);
  assert(broker.Commands[1] == J313_AGX_G2_POWER_CMD_QUERY);
  assert(broker.State == J313_AGX_G2_POWER_STATE_ON);

  assert(AppleAgxPowerRelease(&io));
  assert(broker.CommandCount == 3);
  assert(broker.Commands[2] == J313_AGX_G2_POWER_CMD_OFF);
  assert(broker.State == J313_AGX_G2_POWER_STATE_OFF);
}

static void test_acquire_query_failure_rolls_power_back_off(void) {
  FAKE_BROKER broker;
  APPLE_AGX_POWER_IO io;
  memset(&broker, 0, sizeof(broker));
  broker.FailCommand = J313_AGX_G2_POWER_CMD_QUERY;
  io = make_io(&broker);

  assert(!AppleAgxPowerAcquire(&io));
  assert(broker.CommandCount == 3);
  assert(broker.Commands[0] == J313_AGX_G2_POWER_CMD_ON);
  assert(broker.Commands[1] == J313_AGX_G2_POWER_CMD_QUERY);
  assert(broker.Commands[2] == J313_AGX_G2_POWER_CMD_OFF);
  assert(broker.State == J313_AGX_G2_POWER_STATE_OFF);
}

static void test_invalid_identity_performs_no_write(void) {
  FAKE_BROKER broker;
  APPLE_AGX_POWER_IO io;
  memset(&broker, 0, sizeof(broker));
  broker.FailCommand = 0xFFFFFFFFu;
  io = make_io(&broker);
  io.Read32 = 0;
  assert(!AppleAgxPowerQualify(&io));
  assert(broker.CommandCount == 0);
}

int main(void) {
  test_exact_on_query_off_qualification();
  test_query_failure_still_powers_off();
  test_acquire_leaves_power_on_until_release();
  test_acquire_query_failure_rolls_power_back_off();
  test_invalid_identity_performs_no_write();
  return 0;
}
