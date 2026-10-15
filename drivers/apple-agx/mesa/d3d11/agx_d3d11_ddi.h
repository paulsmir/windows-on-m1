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

/* D3D-H2b: report compute with raw and structured buffers at SM 4.x
 * (D3D11DDICAPS_SHADER_COMPUTE_PLUS_RAW_AND_STRUCTURED_BUFFERS_IN_SHADER_4_X):
 * the D3D11 interface and APPLE_AGX_D3D11_COMPUTE=1, per process while the
 * compute path is qualified. */
BOOL AgxD3d11ComputeEnabled(void);

/* D3D-H5: report feature levels 10_1 and 11_0 (D3D11DDICAPS_3DPIPELINESUPPORT):
 * the D3D11 interface and APPLE_AGX_D3D11_FL11=1, per process while the
 * FL 11_0 stages are qualified on hardware. */
BOOL AgxD3d11Fl11Enabled(void);

/* D3D-H2b: the compute stage's common-shader setters (Shader.cpp). */
void APIENTRY CsSetShaderResources(D3D10DDI_HDEVICE, UINT, UINT,
                                   const D3D10DDI_HSHADERRESOURCEVIEW *);
void APIENTRY CsSetConstantBuffers(D3D10DDI_HDEVICE, UINT, UINT, const D3D10DDI_HRESOURCE *);
void APIENTRY CsSetSamplers(D3D10DDI_HDEVICE, UINT, UINT, const D3D10DDI_HSAMPLER *);

/* D3D-H4: the hull and domain stages' common-shader setters (Shader.cpp). */
void APIENTRY HsSetShaderResources(D3D10DDI_HDEVICE, UINT, UINT,
                                   const D3D10DDI_HSHADERRESOURCEVIEW *);
void APIENTRY HsSetConstantBuffers(D3D10DDI_HDEVICE, UINT, UINT, const D3D10DDI_HRESOURCE *);
void APIENTRY HsSetSamplers(D3D10DDI_HDEVICE, UINT, UINT, const D3D10DDI_HSAMPLER *);
void APIENTRY DsSetShaderResources(D3D10DDI_HDEVICE, UINT, UINT,
                                   const D3D10DDI_HSHADERRESOURCEVIEW *);
void APIENTRY DsSetConstantBuffers(D3D10DDI_HDEVICE, UINT, UINT, const D3D10DDI_HRESOURCE *);
void APIENTRY DsSetSamplers(D3D10DDI_HDEVICE, UINT, UINT, const D3D10DDI_HSAMPLER *);

/* Fill every D3D11 entry this driver provides (see agx_d3d11_ddi.cpp);
 * 'base' is a copy of the D3D10 table CreateDevice filled, whose entries the
 * D3D10-typed D3D11 entries take (funcs and the runtime's 10.0 view of the
 * same table share memory, so the caller passes a copy). */
void AgxD3d11FillDeviceFuncs(D3D11DDI_DEVICEFUNCS *funcs, const D3D10DDI_DEVICEFUNCS *base);

#endif /* AGX_D3D11_DDI_H */
