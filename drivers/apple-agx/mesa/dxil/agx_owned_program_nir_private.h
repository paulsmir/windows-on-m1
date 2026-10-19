/* Original private shared opaque layout; no field/destructor change. MIT */
#pragma once
#include "agx_owned_program_nir_native.h"
#include <vector>
struct AgxOwnedNir {agx_owned_program_nir_native native{};std::vector<unsigned char> root;~AgxOwnedNir(){agx_owned_program_nir_native_free(&native);}};
