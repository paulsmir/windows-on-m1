#include "apple_agx_gpuva_broker_v5_client.h"
#include <string.h>

typedef char gpuva_v5_request_abi_size[(sizeof(AGX_GPUVA_V5_REQUEST) == 128) ? 1 : -1];
typedef char gpuva_v5_response_abi_size[(sizeof(AGX_GPUVA_V5_RESPONSE) == 64) ? 1 : -1];

bool AppleAgxGpuvaV5ClientInit(APPLE_AGX_GPUVA_V5_CLIENT *client,
                               const APPLE_AGX_GPUVA_V5_IO *io)
{
    if (!client || !io || !io->Write64 || !io->Read64 || !io->Write32 ||
        !io->Barrier) return false;
    memset(client, 0, sizeof(*client));
    client->Io = *io;
    return true;
}

static bool AppleAgxGpuvaV5ClientCallUntimed(APPLE_AGX_GPUVA_V5_CLIENT *client,
                                             const AGX_GPUVA_V5_REQUEST *request,
                                             AGX_GPUVA_V5_RESPONSE *response);

bool AppleAgxGpuvaV5ClientCall(APPLE_AGX_GPUVA_V5_CLIENT *client,
                               const AGX_GPUVA_V5_REQUEST *request,
                               AGX_GPUVA_V5_RESPONSE *response)
{
    unsigned long long start, elapsed;
    unsigned command;
    bool ok;
    if (!client || !client->Io.Now || !request)
        return AppleAgxGpuvaV5ClientCallUntimed(client, request, response);
    start = client->Io.Now(client->Io.Context);
    ok = AppleAgxGpuvaV5ClientCallUntimed(client, request, response);
    elapsed = client->Io.Now(client->Io.Context) - start;
    command = request->Command < APPLE_AGX_GPUVA_V5_TIMED_COMMANDS ?
        request->Command : 0u;
    ++client->Calls[command];
    client->Ticks[command] += elapsed;
    if (elapsed > client->MaxTicks[command]) client->MaxTicks[command] = elapsed;
    return ok;
}

static bool AppleAgxGpuvaV5ClientCallUntimed(APPLE_AGX_GPUVA_V5_CLIENT *client,
                                             const AGX_GPUVA_V5_REQUEST *request,
                                             AGX_GPUVA_V5_RESPONSE *response)
{
    AGX_GPUVA_V5_REQUEST q;
    AGX_GPUVA_V5_RESPONSE r;
    unsigned long long words[sizeof(q) / sizeof(unsigned long long)];
    unsigned long long result[sizeof(r) / sizeof(unsigned long long)];
    unsigned i;
    if (!client || !request || !response || !client->Io.Write64 ||
        !client->Io.Read64 || !client->Io.Write32 || !client->Io.Barrier ||
        client->Sequence == ~0ULL) return false;
    q = *request;
    q.Version = AGX_GPUVA_V5_VERSION;
    q.Bytes = sizeof(q);
    q.Sequence = ++client->Sequence;
    q.Epoch = client->Epoch;
    memcpy(words, &q, sizeof(q));
    for (i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
        if (!client->Io.Write64(client->Io.Context,
                                 AGX_GPUVA_V5_OFFSET + i * 8u, words[i]))
            return false;
    client->Io.Barrier(client->Io.Context);
    if (!client->Io.Write32(client->Io.Context,
                            AGX_GPUVA_V5_OFFSET + AGX_GPUVA_V5_DOORBELL, 1u))
        return false;
    client->Io.Barrier(client->Io.Context);
    for (i = 0; i < sizeof(result) / sizeof(result[0]); ++i)
        if (!client->Io.Read64(client->Io.Context,
                                AGX_GPUVA_V5_OFFSET +
                                    AGX_GPUVA_V5_RESPONSE_OFFSET + i * 8u,
                                &result[i])) return false;
    memcpy(&r, result, sizeof(r));
    if (r.Receipt != q.Sequence || !r.Epoch ||
        (client->Epoch && r.Epoch != client->Epoch)) return false;
    /* The first nonmutating stale-epoch call discovers the current epoch. */
    if (!client->Epoch) client->Epoch = r.Epoch;
    *response = r;
    return true;
}
