/* Actual-source R1 element-layout callback fixture; host model, not WDK ABI. */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

using UINT = std::uint32_t;
using SIZE_T = std::size_t;
using HRESULT = std::int32_t;
using VOID = void;
#define APIENTRY
#define S_OK static_cast<HRESULT>(0)
#define E_INVALIDARG static_cast<HRESULT>(0x80070057u)
#define FAILED(value) ((value) < 0)
struct D3D12DDI_HDEVICE { void *pDrvPrivate; };
struct D3D12DDI_HELEMENTLAYOUT { void *pDrvPrivate; };
struct D3D12DDI_HPIPELINELIBRARY { void *pDrvPrivate; };
struct D3D12DDI_LIBRARY_REFERENCE_0010 { D3D12DDI_HPIPELINELIBRARY hLibrary; UINT PipelineIndex; };
struct D3D12DDIARG_INPUT_ELEMENT_DESC {
    UINT InputSlot, AlignedByteOffset, Format, InputSlotClass, InstanceDataStepRate, InputRegister;
};
struct D3D12DDIARG_CREATEELEMENTLAYOUT {
    const D3D12DDIARG_INPUT_ELEMENT_DESC *pVertexElements;
    UINT NumElements;
};
struct D3D12DDIARG_CREATEELEMENTLAYOUT_0010 {
    const D3D12DDIARG_INPUT_ELEMENT_DESC *pVertexElements;
    UINT NumElements;
    D3D12DDI_LIBRARY_REFERENCE_0010 LibraryReference;
};
using PFND3D12DDI_CALCPRIVATEELEMENTLAYOUTSIZE_0010 = SIZE_T (APIENTRY *)(D3D12DDI_HDEVICE, const D3D12DDIARG_CREATEELEMENTLAYOUT_0010 *);
using PFND3D12DDI_CREATEELEMENTLAYOUT_0010 = VOID (APIENTRY *)(D3D12DDI_HDEVICE, const D3D12DDIARG_CREATEELEMENTLAYOUT_0010 *, D3D12DDI_HELEMENTLAYOUT);
struct Layout { UINT count; D3D12DDIARG_INPUT_ELEMENT_DESC elements[8]; };

static int live_device, live_library;
static UINT checks, failures, reports, creates, validations;
static void check(bool good, const char *name) {
    ++checks;
    if (!good) { ++failures; std::printf("R1_ELEMENT_FAIL %s\n", name); }
}
static HRESULT APIENTRY AgxD3d12ZeroCacheValidateLibraryReference(
    D3D12DDI_HDEVICE device, const D3D12DDI_LIBRARY_REFERENCE_0010 *reference) {
    ++validations;
    if (device.pDrvPrivate != &live_device || !reference) return E_INVALIDARG;
    void *library = reference->hLibrary.pDrvPrivate;
    return !library || library == &live_library ? S_OK : E_INVALIDARG;
}
static void ReportRejectedDeviceError(D3D12DDI_HDEVICE, HRESULT error) {
    if (error == E_INVALIDARG) ++reports;
}
static SIZE_T APIENTRY CalcPrivateElementLayoutSize(D3D12DDI_HDEVICE,
    const D3D12DDIARG_CREATEELEMENTLAYOUT *arg) {
    return offsetof(Layout, elements) + (arg->NumElements ? arg->NumElements : 1) * sizeof(D3D12DDIARG_INPUT_ELEMENT_DESC);
}
static VOID APIENTRY CreateElementLayout(D3D12DDI_HDEVICE,
    const D3D12DDIARG_CREATEELEMENTLAYOUT *arg, D3D12DDI_HELEMENTLAYOUT handle) {
    ++creates;
    auto *out = static_cast<Layout *>(handle.pDrvPrivate);
    out->count = arg->NumElements;
    if (arg->NumElements)
        std::memcpy(out->elements, arg->pVertexElements,
                    arg->NumElements * sizeof(D3D12DDIARG_INPUT_ELEMENT_DESC));
}

/* ACTUAL_R1_ELEMENT_WRAPPERS */

int main() {
    static_assert(std::is_same_v<decltype(&CalcPrivateElementLayoutSize0010), PFND3D12DDI_CALCPRIVATEELEMENTLAYOUTSIZE_0010>);
    static_assert(std::is_same_v<decltype(&CreateElementLayout0010), PFND3D12DDI_CREATEELEMENTLAYOUT_0010>);
    D3D12DDI_HDEVICE live{&live_device}, dead{};
    D3D12DDIARG_INPUT_ELEMENT_DESC elements[8]{};
    for (UINT i = 0; i < 8; ++i)
        elements[i] = {i, 4 * i, 10 + i, i & 1u, i + 1, 20 + i};
    D3D12DDIARG_CREATEELEMENTLAYOUT_0010 input{elements, 8, {}};
    struct Guarded { std::uint64_t pre; Layout value; std::uint64_t post; };
    Guarded output{0x1234567812345678ull, {}, 0x8765432187654321ull};
    D3D12DDI_HELEMENTLAYOUT destination{&output.value};
    check(CalcPrivateElementLayoutSize0010(live, &input) == sizeof(Layout), "eight-element size");
    CreateElementLayout0010(live, &input, destination);
    check(creates == 1 && reports == 0 && output.value.count == 8, "ordinary create");
    check(output.pre == 0x1234567812345678ull && output.post == 0x8765432187654321ull,
          "destination canaries");
    check(std::memcmp(output.value.elements, elements, sizeof(elements)) == 0,
          "all eight element fields copied");
    const Layout ordinary = output.value;
    input.LibraryReference = {{&live_library}, 0xffffffffu};
    check(CalcPrivateElementLayoutSize0010(live, &input) == sizeof(Layout), "live library size");
    CreateElementLayout0010(live, &input, destination);
    check(creates == 2 && std::memcmp(&ordinary, &output.value, sizeof(ordinary)) == 0,
          "library metadata not stored");
    int foreign;
    input.LibraryReference.hLibrary = {&foreign};
    const Guarded before = output;
    check(CalcPrivateElementLayoutSize0010(live, &input) == 0, "foreign library size refused");
    CreateElementLayout0010(live, &input, destination);
    check(creates == 2 && reports == 1 && std::memcmp(&before, &output, sizeof(output)) == 0,
          "foreign library create leaves output");
    input.LibraryReference = {{}, 0};
    input.pVertexElements = nullptr;
    check(CalcPrivateElementLayoutSize0010(live, &input) == 0, "positive count without array size refused");
    CreateElementLayout0010(live, &input, destination);
    check(creates == 2 && reports == 2, "positive count without array create refused");
    input.NumElements = 0;
    check(CalcPrivateElementLayoutSize0010(live, &input) == offsetof(Layout, elements) + sizeof(elements[0]),
          "empty layout size");
    CreateElementLayout0010(live, &input, destination);
    check(creates == 3 && output.value.count == 0, "empty layout create");
    check(CalcPrivateElementLayoutSize0010(dead, &input) == 0, "dead device size refused");
    CreateElementLayout0010(dead, &input, destination);
    check(creates == 3 && reports == 3, "dead device create refused");
    check(CalcPrivateElementLayoutSize0010(live, nullptr) == 0, "null record size refused");
    CreateElementLayout0010(live, nullptr, destination);
    check(creates == 3 && reports == 4, "null record create refused");
    CreateElementLayout0010(live, &input, {});
    check(creates == 3 && reports == 5, "null destination refused");
    check(validations == 10, "only readable valid-shape records validated");
    std::printf("R1_ELEMENT_WRAPPERS checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
