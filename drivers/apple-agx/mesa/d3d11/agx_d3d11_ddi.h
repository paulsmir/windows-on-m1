/*
 * D3D-H2a: the D3D11 DDI device function table over the D3D10 frontend.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AGX_D3D11_DDI_H
#define AGX_D3D11_DDI_H

/* Advertise D3D11_0_DDI_INTERFACE_VERSION: by default; APPLE_AGX_D3D11_DDI=0
 * in the process environment keeps the process on the 10.0 interface. */
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

/* D3D-H7: advertise D3DWDDM2_0_DDI_SUPPORTED (the 11.1 / WDDM 1.3 / WDDM 2.0
 * device table, agx_wddm2_ddi.cpp): with the FL 11_0 switch, per process
 * while its checkpoints are measured (EXP1299: the runtime asks for the
 * WDDM 2.0 interface and refuses FL 11_0 without it). */
BOOL AgxWddm2_0DdiEnabled(void);

/* Fill the WDDM 2.0 table: the D3D11 entries, adapters for the entries
 * whose types changed, and the 11.1 to WDDM 2.0 additions. */
void AgxWddm2_0FillDeviceFuncs(D3DWDDM2_0DDI_DEVICEFUNCS *funcs,
                               const D3D10DDI_DEVICEFUNCS *base);

/* The DXGI 1.2 to 1.4 entries of the base-function table (after the DXGI
 * 1.1 entries the frontend wrote): IS_DXGI1_4_BASE_FUNCTIONS. */
void AgxWddm2_0FillDxgiFuncs(DXGI1_4_DDI_BASE_FUNCTIONS *funcs);

/* D3D10DDIARG_CREATEDEVICE::ppfnRetrieveSubObject: no video sub-device. */
HRESULT APIENTRY AgxWddm2_0RetrieveSubObject(D3D10DDI_HDEVICE, UINT32, SIZE_T, void *,
                                             SIZE_T, void *);

/* D3D-H7: bind constant buffers with optional 11.1 ranges (first constant
 * and constant count per slot, in 16-byte constants; NULL: whole buffers),
 * Shader.cpp. */
void AgxSetConstantBuffersRange(mesa_shader_stage stage, D3D10DDI_HDEVICE hDevice, UINT start,
                                UINT count, const D3D10DDI_HRESOURCE *buffers,
                                const UINT *firstConstant, const UINT *numConstants);

#endif /* AGX_D3D11_DDI_H */
