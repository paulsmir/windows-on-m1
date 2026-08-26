#include "hv_agx_g2_policy.h"

#include <stdio.h>
#include <string.h>

static const struct hv_agx_g2_interrupt_route accepted_routes[] =
    HV_AGX_G2_INTERRUPT_ROUTE_VALUES;

static struct hv_agx_g2_policy accepted_policy(void)
{
    return (struct hv_agx_g2_policy){
        .profile_identity = HV_AGX_G2_PROFILE_IDENTITY,
        .source_contract_sha256 = HV_AGX_G2_SOURCE_CONTRACT_SHA256,
        .aperture_base = HV_AGX_G2_SGX_MMIO_BASE,
        .aperture_size = HV_AGX_G2_SGX_MMIO_SIZE,
        .routes = accepted_routes,
        .route_count = HV_AGX_G2_INTERRUPT_ROUTE_COUNT,
        .level = true,
        .active_high = true,
        .exclusive = true,
    };
}

static int require_rejected(const struct hv_agx_g2_policy *policy,
                            const char *case_name)
{
    if (!hv_agx_g2_policy_validate(policy))
        return 0;
    fprintf(stderr, "accepted invalid policy: %s\n", case_name);
    return 1;
}

int main(void)
{
    struct hv_agx_g2_policy policy = accepted_policy();
    struct hv_agx_g2_interrupt_route routes[HV_AGX_G2_INTERRUPT_ROUTE_COUNT];

    if (!hv_agx_g2_policy_validate(&policy)) {
        fprintf(stderr, "rejected exact policy\n");
        return 1;
    }
    if (require_rejected(NULL, "null policy"))
        return 1;

    policy = accepted_policy();
    policy.profile_identity = "stable";
    if (require_rejected(&policy, "profile identity"))
        return 1;
    policy = accepted_policy();
    policy.source_contract_sha256 =
        "0000000000000000000000000000000000000000000000000000000000000000";
    if (require_rejected(&policy, "source hash"))
        return 1;
    policy = accepted_policy();
    policy.aperture_base++;
    if (require_rejected(&policy, "aperture base"))
        return 1;
    policy = accepted_policy();
    policy.aperture_size--;
    if (require_rejected(&policy, "aperture size"))
        return 1;
    policy = accepted_policy();
    policy.route_count--;
    if (require_rejected(&policy, "missing route"))
        return 1;

    memcpy(routes, accepted_routes, sizeof(routes));
    routes[1] = routes[0];
    policy = accepted_policy();
    policy.routes = routes;
    if (require_rejected(&policy, "duplicate route"))
        return 1;
    memcpy(routes, accepted_routes, sizeof(routes));
    routes[0] = accepted_routes[1];
    routes[1] = accepted_routes[0];
    policy = accepted_policy();
    policy.routes = routes;
    if (require_rejected(&policy, "reordered routes"))
        return 1;
    memcpy(routes, accepted_routes, sizeof(routes));
    routes[0].guest_intid = 865u;
    policy = accepted_policy();
    policy.routes = routes;
    if (require_rejected(&policy, "reserved guest interrupt"))
        return 1;

    policy = accepted_policy();
    policy.level = false;
    if (require_rejected(&policy, "edge triggered"))
        return 1;
    policy = accepted_policy();
    policy.active_high = false;
    if (require_rejected(&policy, "active low"))
        return 1;
    policy = accepted_policy();
    policy.exclusive = false;
    if (require_rejected(&policy, "shared"))
        return 1;

    return 0;
}
