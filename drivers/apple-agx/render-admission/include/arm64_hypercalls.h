#ifndef APPLE_AGX_ARM64_HYPERCALLS_H
#define APPLE_AGX_ARM64_HYPERCALLS_H

/* Private m1n1 ABI: payload/request in X0, status in X0.  Normal external
 * ARM64 calls also make the compiler honor the volatile-register boundary. */
unsigned long long AdmissionHvcArmConsumed(unsigned long long payload);
unsigned int AdmissionHvcGuestIpaPa(unsigned long long request_ipa);

#endif
