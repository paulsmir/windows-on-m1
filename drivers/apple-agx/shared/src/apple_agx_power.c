#include "apple_agx_power.h"

static APPLE_AGX_POWER_BOOL AppleAgxPowerCommand(
    const APPLE_AGX_POWER_IO *Io, APPLE_AGX_POWER_U32 Command,
    APPLE_AGX_POWER_U32 ExpectedState) {
  APPLE_AGX_POWER_U64 receipt =
      Io->Read64(Io->Context, J313_AGX_G2_POWER_REG_RECEIPT_SEQUENCE);
  APPLE_AGX_POWER_U64 sequence = receipt + 1;

  if (sequence == 0)
    return 0;
  Io->Write64(Io->Context, J313_AGX_G2_POWER_REG_REQUEST_SEQUENCE,
              sequence);
  Io->Write32(Io->Context, J313_AGX_G2_POWER_REG_COMMAND, Command);
  if (Io->Read64(Io->Context, J313_AGX_G2_POWER_REG_RECEIPT_SEQUENCE) !=
      sequence)
    return 0;
  if (Io->Read32(Io->Context, J313_AGX_G2_POWER_REG_RESULT) !=
      J313_AGX_G2_POWER_RESULT_OK)
    return 0;
  return Io->Read32(Io->Context, J313_AGX_G2_POWER_REG_STATE) ==
         ExpectedState;
}

APPLE_AGX_POWER_BOOL AppleAgxPowerQualify(const APPLE_AGX_POWER_IO *Io) {
  APPLE_AGX_POWER_BOOL queryOk;
  APPLE_AGX_POWER_BOOL offOk;

  if (Io == 0 || Io->Read32 == 0 || Io->Read64 == 0 || Io->Write32 == 0 ||
      Io->Write64 == 0)
    return 0;
  if (Io->Read32(Io->Context, J313_AGX_G2_POWER_REG_MAGIC) !=
          J313_AGX_G2_POWER_MAGIC ||
      Io->Read32(Io->Context, J313_AGX_G2_POWER_REG_ABI_VERSION) !=
          J313_AGX_G2_POWER_ABI_VERSION ||
      Io->Read32(Io->Context, J313_AGX_G2_POWER_REG_CAPABILITIES) !=
          J313_AGX_G2_POWER_CAP_FIXED_J313_DOMAINS ||
      Io->Read32(Io->Context, J313_AGX_G2_POWER_REG_STATE) !=
          J313_AGX_G2_POWER_STATE_OFF)
    return 0;

  if (!AppleAgxPowerCommand(Io, J313_AGX_G2_POWER_CMD_ON,
                            J313_AGX_G2_POWER_STATE_ON))
    return 0;

  queryOk = AppleAgxPowerCommand(Io, J313_AGX_G2_POWER_CMD_QUERY,
                                 J313_AGX_G2_POWER_STATE_ON);
  offOk = AppleAgxPowerCommand(Io, J313_AGX_G2_POWER_CMD_OFF,
                               J313_AGX_G2_POWER_STATE_OFF);
  return queryOk && offOk;
}
