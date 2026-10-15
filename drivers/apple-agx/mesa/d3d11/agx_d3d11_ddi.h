/*
 * D3D-H2a: the D3D11 DDI device function table over the D3D10 frontend.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AGX_D3D11_DDI_H
#define AGX_D3D11_DDI_H

/* Advertise D3D11_0_DDI_INTERFACE_VERSION: APPLE_AGX_D3D11_DDI=1 in the
 * process environment (per process while the D3D11 table is qualified). */
BOOL AgxD3d11DdiEnabled(void);

/* Fill every D3D11 entry this driver provides (see agx_d3d11_ddi.cpp);
 * 'base' is a copy of the D3D10 table CreateDevice filled, whose entries the
 * D3D10-typed D3D11 entries take (funcs and the runtime's 10.0 view of the
 * same table share memory, so the caller passes a copy). */
void AgxD3d11FillDeviceFuncs(D3D11DDI_DEVICEFUNCS *funcs, const D3D10DDI_DEVICEFUNCS *base);

#endif /* AGX_D3D11_DDI_H */
