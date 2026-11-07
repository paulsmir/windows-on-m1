# C15 runtime source/license inventory: pinned candidate

Date2026-10-18. This extends `D3D12-C15-license-review.md` using source archives
inspected under `/tmp/c15-runtime-license-8434b063`. No translator was configured,
compiled, linked or installed. Only this new analysis document is tracked; no
external implementation or existing license file was copied into tracked source.

## Scope and completion boundary

**The source-declared runtime target and notice inventory below is complete for
the inspected candidate graph; an exact compiler-observed minimal header set and
final redistributed artifact manifest are NOT complete.** A static literal-include
walk conservatively identifies project-owned headers, including conditionally
referenced headers. No MSVC preprocessor/dependency output, compiler standard
library/Windows SDK inventory, linker map, CRT choice or package exists for this
candidate. Those require a separately pinned and recorded configuration/build within the user-authorized work; this review
does not run one or treat source inspection as that evidence.

The selected target is `dxil-spirv-c-static` with CLI, native LLVM and miscellaneous
CLI disabled; build that target specifically rather than the default all target.
Also explicitly disable SPIRV-Headers tests/install. Upstream CMake declares
additional targets even when they are outside this selected runtime link.
The shared C API alternative compiles the same wrapper and dependency sources,
but is outside the selected runtime artifact scope of this inventory.

## Immutable identities

|Symbol|Repository / revision|Role|
|---|---|---|
|D|[dxil-spirv](https://github.com/HansKristian-Work/dxil-spirv/tree/8434b0639fb2a48ce13aac08a420ecce5d23df0e), `8434b0639fb2a48ce13aac08a420ecce5d23df0e`|C API2.75.0 and core/bundled dependencies|
|B|[dxbc-spirv](https://github.com/doitsujin/dxbc-spirv/tree/8dae2d8573af08378c6059aca3d5987da99d20b5), `8dae2d8573af08378c6059aca3d5987da99d20b5`|Required by builtin llvm-bc, even for DXIL-only entry points|
|H|[SPIRV-Headers](https://github.com/KhronosGroup/SPIRV-Headers/tree/c63848ecf2200425511319fd8bf2c17b751e501e), `c63848ecf2200425511319fd8bf2c17b751e501e`|Two checked-in generated headers consumed by core builder|
|HN|SPIRV-Headers `c8ad050fcb29e42a2f57d9f59e97488f465c436d`|B nested gitlink: inspected, **not in this runtime target/include graph**|
|T|SPIRV-Tools `199cb207b911501ddd76dcddf100a6e21c15ef23`|Excluded CLI dependency|
|X|SPIRV-Cross `be71ee8c12cd7dc5ca8fa9581f708c2e8561fe2a`|Excluded CLI dependency|

HN corrects/narrows the earlier review's unresolved header provenance: this parent
CMake target compiles B's dxbc/ir/util subset, which uses no SPIR-V headers of its
own. The only resolved SPIR-V includes come from D's builder/converter and resolve
through `third_party/spirv-headers/include/spirv/unified1` to H. B's independent
Meson targets and full repository need not be imported to satisfy this target.

## Static target graph and exact translation-unit set

`dxil-spirv-c-static` -> dxil-debug, dxil-converter, dxil-utils, external::llvm.
Default external::llvm aliases llvm-bc -> bc-decoder, dxil-debug, dxil-utils,
dxbc-spirv. dxil-converter also links spirv-module -> glslang-spirv-builder,
dxil-spirv-headers, dxil-utils and dxil-debug. Header target resolves to H.

These nine explicit STATIC source lists declare83 compiled translation units.
CMake additionally names98 header/IDE inputs. Starting at the83 translation units,
a recursive literal include walk identifies103 project headers, for186 total
source/header candidates. The union with all CMake-listed files is187; the one
listed-but-not-reached header is B/util/util_vle.h. Keep it if reusing upstream
CMake intact, without claiming its code enters the artifact.

|Target|Compiled translation units|Explicit source/header inputs|
|---|---:|---:|
|dxil-debug|1|2|
|dxil-utils|1|2|
|spirv-module|3|7|
|dxil-converter|24|52|
|dxil-spirv-c-static|1|2|
|llvm-bc|9|19|
|glslang-spirv-builder|3|6|
|bc-decoder|1|3|
|dxbc-spirv|40|88|

File roles below: **TU** = exact .cpp/.c entry from the selected upstream STATIC
target lists; **H** = project-owned literal header closure from those TUs;
**IDE** = CMake-listed but not reached from that closure. This is source inventory,
not a claim that every H survives Windows preprocessing or appears in the final
linked binary. Each file has full-byte SHA-256 and a complete notice group below.
Paths are archive-root relative: dxil-spirv=D, dxbc-spirv=B, spirv-headers=H.

|Path|Role|Notice|SHA-256|
|---|---|---|---|
|`dxbc-spirv/dxbc/dxbc_api.cpp`|TU|N01|`715f72e5277fac5649a422a1acf180a5b1f25ea149ed3eb1c6d5fa3c1ee3cbc8`|
|`dxbc-spirv/dxbc/dxbc_api.h`|H|N01|`ab60f9804e2daa7fbfed0b3bfba36433902d9c3bb645101a443118370c21585d`|
|`dxbc-spirv/dxbc/dxbc_container.cpp`|TU|N01|`1bdf62e6476962445bd399a31d793ba0b3b1ca2fa5489cde1cf48676818ac70b`|
|`dxbc-spirv/dxbc/dxbc_container.h`|H|N01|`eff73aa361365b5df97547ae0510763b763e4632e3472616e7a195e81e5929e6`|
|`dxbc-spirv/dxbc/dxbc_control_flow.h`|H|N01|`22eaa4793f54be2684f713a7a67a30d79ec55697d54587026e12fedbe7c54d44`|
|`dxbc-spirv/dxbc/dxbc_converter.cpp`|TU|N01|`3d9597d36808d381b77f1b40bbe63578237846d8d116daeb695b59b1285f9a85`|
|`dxbc-spirv/dxbc/dxbc_converter.h`|H|N01|`4928b19e8b810f21c3b61fca6749289f0d873268e00a239b4a73347896120222`|
|`dxbc-spirv/dxbc/dxbc_disasm.cpp`|TU|N01|`c5f5b24920f6069530c3d7d2c1399f464554bf27de61fef190ab42ee4566fa1f`|
|`dxbc-spirv/dxbc/dxbc_disasm.h`|H|N01|`c26e724814c8954510019b41389df69d60b3b3ede6acbacf8d402181e69244d2`|
|`dxbc-spirv/dxbc/dxbc_interface.cpp`|TU|N01|`e65c8db621dfe1083476bca2a6c8844ea5163495c5806fe0ab5d19ac1651bf22`|
|`dxbc-spirv/dxbc/dxbc_interface.h`|H|N01|`8d4d22233a8e6191fc1f6e70329708ab1da099dfc92bf21e26b771b88f922a77`|
|`dxbc-spirv/dxbc/dxbc_io_map.cpp`|TU|N01|`f24f3cc3bfe3303ae52367e9d6d725e01bd449543b1278cbacf61e40d0787eaf`|
|`dxbc-spirv/dxbc/dxbc_io_map.h`|H|N01|`464c1b5397b7dd9999154d61fd89ae41ae4a803b8c33b7e6a073b83e7038f7a3`|
|`dxbc-spirv/dxbc/dxbc_parser.cpp`|TU|N01|`8f974db32db89336e695ea52140912fdddce5134f0a7f203084b17e0d888b165`|
|`dxbc-spirv/dxbc/dxbc_parser.h`|H|N01|`c5e890efeeb695f474bdda801dad8a1418f6ec15b927f3a56735eb6ddef3b2da`|
|`dxbc-spirv/dxbc/dxbc_registers.cpp`|TU|N01|`27af5ccc39a2cfe9e47118d57d1a0e4f09e7755f368e904841589579941df9a0`|
|`dxbc-spirv/dxbc/dxbc_registers.h`|H|N01|`9db6ba0547a61813a74722f1b6b3a60d0c277e63568e4fa0be73600f5c971d4d`|
|`dxbc-spirv/dxbc/dxbc_resources.cpp`|TU|N01|`250a0dcbc0025cbfa09defee203e49524551481f4efbb1d95be73784c688b4a8`|
|`dxbc-spirv/dxbc/dxbc_resources.h`|H|N01|`bf84d796901a35356f70c1e64999268acf22419b79875d69425189e1c8b6bc86`|
|`dxbc-spirv/dxbc/dxbc_signature.cpp`|TU|N01|`9264749d3a1a1d842fb634be7cac2bac725373d6ccc71629f6cd2a548883d15c`|
|`dxbc-spirv/dxbc/dxbc_signature.h`|H|N01|`4eb1cd6a00b0bbd9faa59e747b98b7b4a8a0341d36b6c6b53ff2a8ebf3ec133f`|
|`dxbc-spirv/dxbc/dxbc_types.cpp`|TU|N01|`4a378f1b1593680153cf74068229bec4ff27fdac7f3d2094e38ed1569d10f679`|
|`dxbc-spirv/dxbc/dxbc_types.h`|H|N01|`cc3ed5333197bcb0c98065d7fb1818b5a16e4327896ce31026803e4c6a2177bd`|
|`dxbc-spirv/ir/ir.cpp`|TU|N01|`7f845481c4fbe81cfdbc27088aefd9fd4470383787ed0e386a9f8a5486a2818c`|
|`dxbc-spirv/ir/ir.h`|H|N01|`aa2f65ec784020669e1a5b9ead23ebf396e5a3b73dc9185bb4a7f39ca63e8690`|
|`dxbc-spirv/ir/ir_builder.cpp`|TU|N01|`a299910342c0d34c017b8020a848132b623378f4294de85419fb5a764ab5b367`|
|`dxbc-spirv/ir/ir_builder.h`|H|N01|`94b150ec60e1baa505e9de6c0d48e134dbaa36bee2f920dd36be2af104fe89c7`|
|`dxbc-spirv/ir/ir_container.h`|H|N01|`3dcf25c836ef11c7e89a24e112b4550508579283655ad9298910d44f81e19be9`|
|`dxbc-spirv/ir/ir_disasm.cpp`|TU|N01|`0a3363161da3ea92ca474661b45f85dff75269ce3cb451f9d9d3e61e71759e55`|
|`dxbc-spirv/ir/ir_disasm.h`|H|N01|`c4aff21327527da21209bb2c6f38bdc4bb75beaad56ee7376f73a5a0bd1b435b`|
|`dxbc-spirv/ir/ir_divergence.cpp`|TU|N01|`c8d66f597c1aee682340c60db53df08a594b3c072cdf5a0d2271f44868564bbb`|
|`dxbc-spirv/ir/ir_divergence.h`|H|N01|`1022ecc4033e5da54749184adf79d47957ef47c088b8c25f6989a2e786f70178`|
|`dxbc-spirv/ir/ir_dominance.cpp`|TU|N01|`ff47df5cb514dc91130c577a4da5ba490403e525661dbe0b72a5e7a65b4c7be5`|
|`dxbc-spirv/ir/ir_dominance.h`|H|N01|`721ce871cb0b1b39320942f10a52d870f4caad5bbf290ea522840ee849dbc98c`|
|`dxbc-spirv/ir/ir_legalize.cpp`|TU|N01|`6abc7e17d17b14e3c9cc31676fc924a7549e05c4cf3de63a6a0c28623a149ae1`|
|`dxbc-spirv/ir/ir_legalize.h`|H|N01|`956174f9e010b0f743c010d3f451ae436829d43800b590a682b67fc68d443536`|
|`dxbc-spirv/ir/ir_utils.cpp`|TU|N01|`31dadbef1bf589c566cf4a1dd9e2ca2532539848c70f309a5a8e40871cacff3c`|
|`dxbc-spirv/ir/ir_utils.h`|H|N01|`4d14b3db77ca3e8e1bd6f13439198f6dfd9e48d2d0d8c77fe862fa9e1049dc32`|
|`dxbc-spirv/ir/passes/ir_pass_arithmetic.cpp`|TU|N01|`d7eb0260a2cc5c8a34221fcf125946715cfbb3b49625e2dcd7d3f698a99abe6c`|
|`dxbc-spirv/ir/passes/ir_pass_arithmetic.h`|H|N01|`51211ff0e1661bc9b7c8e9a042dcfc007c02cdb9263d2c8dc1bf93e26d499a53`|
|`dxbc-spirv/ir/passes/ir_pass_buffer_kind.cpp`|TU|N01|`bf17f7e530ed5b525e567a203cd81d8da79bb2a0b6d0efc2e0c07c775f6d988b`|
|`dxbc-spirv/ir/passes/ir_pass_buffer_kind.h`|H|N01|`4337166b260e8e1f30e4b5ff2a3ac6a74a7d50b6fb7c0772e7fd17361179c50b`|
|`dxbc-spirv/ir/passes/ir_pass_cfg_cleanup.cpp`|TU|N01|`5d8b2b44bd59e58a35ef75894aa909cfc103eaadc9d769483f0604bcc46d9f57`|
|`dxbc-spirv/ir/passes/ir_pass_cfg_cleanup.h`|H|N01|`3bfdcb2d57d564af26fa51611d77d8bd6864a70b492076b87d9ce5ee0da716c8`|
|`dxbc-spirv/ir/passes/ir_pass_cfg_convert.cpp`|TU|N01|`d47467975e4696f2ee74916a484f938d975adb05b1ee15409886f3545b8b9e02`|
|`dxbc-spirv/ir/passes/ir_pass_cfg_convert.h`|H|N01|`6e8f675f23191a7c78e738b37ff4b37f594cec8b936e102bf75fc3e3421882da`|
|`dxbc-spirv/ir/passes/ir_pass_cse.cpp`|TU|N01|`97db8ccd568d6087ec494eac5c3ce58c611882b98ae34922649a7241d79f0b4f`|
|`dxbc-spirv/ir/passes/ir_pass_cse.h`|H|N01|`d5546ef3e83ab96e8596833895464cf6242b0acde48b3b1c906cb4a927ace5b5`|
|`dxbc-spirv/ir/passes/ir_pass_derivative.cpp`|TU|N01|`7708230576ca718c23d667d0c74453019c8ae84cc7fbc7c6e59dcd745df42173`|
|`dxbc-spirv/ir/passes/ir_pass_derivative.h`|H|N01|`e737c34f2a56002074cfe91aa3fcbbfb6888b9f6fce81358045374fd99cdad62`|
|`dxbc-spirv/ir/passes/ir_pass_descriptor_indexing.cpp`|TU|N01|`4073a101946c769e974b6f5da0136d04e992a7465402793b30aa2db7e9002235`|
|`dxbc-spirv/ir/passes/ir_pass_descriptor_indexing.h`|H|N01|`54826a138801e0dfbbd703ed6a69478684965fe892a6ad176bb36b042c460ba0`|
|`dxbc-spirv/ir/passes/ir_pass_function.cpp`|TU|N01|`29bb3fb0dc45998301d35160febbe754c3cd7406fe6e04485fd53f0fbbd607bb`|
|`dxbc-spirv/ir/passes/ir_pass_function.h`|H|N01|`d85711a37790b7cec47c8e979a60688621bc6296058c283881707ecd436166b8`|
|`dxbc-spirv/ir/passes/ir_pass_lower_consume.cpp`|TU|N01|`2cd0751716164e684bf7cb472ee7328af54f8e9c0c9d4876764749e1881c0346`|
|`dxbc-spirv/ir/passes/ir_pass_lower_consume.h`|H|N01|`61f97cdca55ff639159a4beea9df2d64c7ebaf47462148adc9d8fe37defedb97`|
|`dxbc-spirv/ir/passes/ir_pass_lower_io.cpp`|TU|N01|`0d4444c4ba8fd76c2ffab5bcfe30157e1a50c9f50741122772fa107f20ef34f3`|
|`dxbc-spirv/ir/passes/ir_pass_lower_io.h`|H|N01|`ed554ef182e5e51cdc2594e6cb1580316bdd7b24e84982173e2ab0536abaf605`|
|`dxbc-spirv/ir/passes/ir_pass_lower_min16.cpp`|TU|N01|`4bc9589f308c33b3e651bd5b940f8e975d7c3f5fe635847b603e2de7ca5ed9a7`|
|`dxbc-spirv/ir/passes/ir_pass_lower_min16.h`|H|N01|`f892fd8d1957d9ade54a67f251373ccff96eabaa38811242310b5f062508680f`|
|`dxbc-spirv/ir/passes/ir_pass_propagate_resource_types.cpp`|TU|N01|`1f61bc6501095a3bd84769ed8ab2fbac62cce08a0305482e47975f23ae3539d1`|
|`dxbc-spirv/ir/passes/ir_pass_propagate_resource_types.h`|H|N01|`93e253872fcda0b55617d044291bc22259d0c8af889c7e06c2cad82921aaec9b`|
|`dxbc-spirv/ir/passes/ir_pass_propagate_types.cpp`|TU|N01|`62a6e2ec67e04cbf72aaf3e5c4cfe92b671b184dc7776b41139dd5c12ef5378e`|
|`dxbc-spirv/ir/passes/ir_pass_propagate_types.h`|H|N01|`f1e8cc68c13eab5dace50fcf8ef5bb728ac34d97712d2ed29e95945c609c6bed`|
|`dxbc-spirv/ir/passes/ir_pass_remove_unused.cpp`|TU|N01|`02180cd51670ffc5aedb901279d73351fa122aab5ef773bbc53ce32519d306b9`|
|`dxbc-spirv/ir/passes/ir_pass_remove_unused.h`|H|N01|`b4113fcc9924ef30721358c11d6d6cc8a5dd12543682ff9332e507673be98f1c`|
|`dxbc-spirv/ir/passes/ir_pass_scalarize.cpp`|TU|N01|`5545c5674dbb8c6a03b6974ada9c4ad329d7b08e1ae99850e043e40d8e44c5c1`|
|`dxbc-spirv/ir/passes/ir_pass_scalarize.h`|H|N01|`7763de90d6c547c4e763bfccf529bb20bf094ce22f5f72f3fa12e17aa17ffff3`|
|`dxbc-spirv/ir/passes/ir_pass_scratch.cpp`|TU|N01|`f4f5003f8b297770327a32f1b229ab773276dea3eab3c839f4c83bbcbb2b1c13`|
|`dxbc-spirv/ir/passes/ir_pass_scratch.h`|H|N01|`8e131e244305d39f461e32c3c6188c03587ec87aff0dffc8c4fae4f25bb6960b`|
|`dxbc-spirv/ir/passes/ir_pass_ssa.cpp`|TU|N01|`4c82a52fb9ea10ed4609f0982ab5fb0207b707def1e0432f5b78ec0920434a59`|
|`dxbc-spirv/ir/passes/ir_pass_ssa.h`|H|N01|`08d59a9f6184684e4f293e99958b65d48a3257aec936e073f8be488f1300f47f`|
|`dxbc-spirv/ir/passes/ir_pass_ssa_utils.h`|H|N01|`55548b1fe07ff3d56b6c386cbf4de0171ad95aa7bcce1c9435ed5fbfaebc263b`|
|`dxbc-spirv/ir/passes/ir_pass_sync.cpp`|TU|N01|`46a047c824d45222292d6cdea2f728d22a1b3f27861fa5b56868fed6377c51af`|
|`dxbc-spirv/ir/passes/ir_pass_sync.h`|H|N01|`07eb851c35e4ae20abf38f8d5561bdd07b7809addd3696434052f177819baae9`|
|`dxbc-spirv/util/util_bit.h`|H|N01|`3aaf21f25b704c3f8dd08a05bcbd95a6b7e7f43e10c52756b457decac4982bed`|
|`dxbc-spirv/util/util_byte_stream.h`|H|N01|`437b24c94dedbe5704572d54705e6d78b58dadba5870be14f607b60e2a7996d9`|
|`dxbc-spirv/util/util_console.h`|H|N01|`4578463b8b50ef3c8a6200895c6f3c07e848ecd31120682c7297c8468738c878`|
|`dxbc-spirv/util/util_debug.h`|H|N01|`d0a450f403ab411170fa2372c811a6f7d9a09893613417841d83caf619939b77`|
|`dxbc-spirv/util/util_flags.h`|H|N01|`852b71ca0e5e32c1dc38b0c43e553325c0c08bcec68aae0d28d63d030db9d27a`|
|`dxbc-spirv/util/util_float16.cpp`|TU|N01|`c51f6d8bbe6d161857c2cb243569f55dac25e9dd79d6b1450af912fc9ce5013f`|
|`dxbc-spirv/util/util_float16.h`|H|N01|`29c80dd76ef2ee84fe9746e5eb570f42be3ba4cfa5accf56f448d2da71939258`|
|`dxbc-spirv/util/util_fourcc.h`|H|N01|`a0b709f5509561b26099f6a514c78e104704d98a81f978a935f1ce44387c9726`|
|`dxbc-spirv/util/util_hash.h`|H|N01|`5d1d55668f2c41cfc080824ca47ed4b44c15c9258179604b53227e0483ad3017`|
|`dxbc-spirv/util/util_log.cpp`|TU|N01|`29b14a85de4c806ed036a486e2f1dc89a31017512b08c6d5fa67ea519c34bc39`|
|`dxbc-spirv/util/util_log.h`|H|N01|`b0792fa0440a00acdb763e4bcb3a3989e4285469900fd70a54d4bd7564af01fc`|
|`dxbc-spirv/util/util_md5.cpp`|TU|N01|`5298ab9cd1a59fc2089d0373eeefccda9a2bfe6022509d749acfc56ae10b6b46`|
|`dxbc-spirv/util/util_md5.h`|H|N01|`554e93482f05dcbc8bac6f6688db223ea85df78985500da84dd9200781df30d5`|
|`dxbc-spirv/util/util_small_vector.h`|H|N01|`82c0ff0c3cae8e1fe2a615a15971152d106b376ce4807e08c964f5fb2417c767`|
|`dxbc-spirv/util/util_swizzle.cpp`|TU|N01|`be699f37a1e1bec587c440a50bc4eddd8e14794821a4d2c33c1dca832a213eb0`|
|`dxbc-spirv/util/util_swizzle.h`|H|N01|`9788fbbb7858aa70fec00698c90fafb6b2606ae2a346c6e64425d12e18a953e4`|
|`dxbc-spirv/util/util_vle.h`|IDE|N01|`c76b26df8ed5b4c7017494c36c5df28b4584426599e444cbee5d3157253b3e59`|
|`dxil-spirv/bc/cast.hpp`|H|N02|`0bd20fb1602d240ed4fc0a65551b5e6d4d3b78d5147be7fa4f94e5d5bba9f4f6`|
|`dxil-spirv/bc/context.cpp`|TU|N02|`e46b5375478bd40a2f295aec084e80237afcd0b382fd85c0bcd6ba6ba2143327`|
|`dxil-spirv/bc/context.hpp`|H|N02|`96d25327e17ffe88bdaba28347acbe97ada233fb5de3f3fda7eb743cf149f770`|
|`dxil-spirv/bc/data_structures.hpp`|H|N02|`bb5e0b9b555ff85b4a9b2d712f9af0712f865334154d276443032af9eec205cb`|
|`dxil-spirv/bc/disassembler.cpp`|TU|N02|`222c6ab4b1ddefeea2b61eef4f715354d65bf96d188ca55e6ca9e0ff67b9fde4`|
|`dxil-spirv/bc/function.cpp`|TU|N02|`8563d7b420fbcaaf33e162fcd0fc8928f9f1d4b768921a0e049fbef06cdefa11`|
|`dxil-spirv/bc/function.hpp`|H|N02|`4fa883cc7fe5086b13ac547ff1732046b9d39fbcb458e1c80a6e9197ae065f11`|
|`dxil-spirv/bc/instruction.cpp`|TU|N02|`1a3b55fbf5bfee47aa7ef0848d7f388358af5a89609f54059934f4ea5b152630`|
|`dxil-spirv/bc/instruction.hpp`|H|N02|`6a1e004d4c4ce8735caed5c77c6588588e1729f59eda5752ffc3e9694666fcc9`|
|`dxil-spirv/bc/iterator.hpp`|H|N02|`6addd41f84745b04d52f1088bf1255bfd66dfcf1d03c34a9bb9f3358582e7b2c`|
|`dxil-spirv/bc/metadata.cpp`|TU|N02|`bef88931d2c4986a638110db975e9f6eb883f0a42169b01671339e0c6a77df81`|
|`dxil-spirv/bc/metadata.hpp`|H|N02|`bb8a1c2e3f8b8f6e8a3e4785870e0de9a385bad683f3d41c9dad7e2699a90072`|
|`dxil-spirv/bc/module.cpp`|TU|N02|`4be3df6d61cf5105935e4b1d4437508d617af3ff96fd8513e2fefa37520d2a9b`|
|`dxil-spirv/bc/module.hpp`|H|N02|`a251f7d56e785e1baaa04b264c89f5f223c123d7d7612b102d74c99e72e37368`|
|`dxil-spirv/bc/module_dxbc_ir.cpp`|TU|N03|`8e42dfb02100f968b5332fd44819b640a7f1d0ad282192d94b585e08466c9ba1`|
|`dxil-spirv/bc/type.cpp`|TU|N02|`3cb33302a537fa4246d205644b70dab2bc651cfc5f131d16c5dc83a8a3d44c98`|
|`dxil-spirv/bc/type.hpp`|H|N02|`fe3df228cbf04cd0b6b5e3ddf335f016e5cc47f9de227d16b41794c6f681d17d`|
|`dxil-spirv/bc/value.cpp`|TU|N02|`a5abe00197138f9eedda283e512cda6e55cf9dabf1530cd8dd71e85f5d4ae47b`|
|`dxil-spirv/bc/value.hpp`|H|N02|`ab618fabe7fd4208fa29e878eba1ed94fe326d6d167be7322c808e657126a420`|
|`dxil-spirv/cfg_structurizer.cpp`|TU|N02|`2dd1bf68d682695c40c1dfbbc0bc0344353a612b515412f19316b37a7226c3d7`|
|`dxil-spirv/cfg_structurizer.hpp`|H|N02|`f3ed503637efd1832a7983da3fd1ef8ef29a802a07283affeccf55393aa15611`|
|`dxil-spirv/debug/logging.cpp`|TU|N02|`1960f79d56bf947ab9a997054424a9e0fc7b5d95393be05c8ecfb7d16f700d05`|
|`dxil-spirv/debug/logging.hpp`|H|N02|`184d7ab1820794d50748e1b3334e467ceffafe5ea591246cb478da8c4b09f389`|
|`dxil-spirv/descriptor_qa.cpp`|TU|N02|`0dbe31a91df2a4bc5303499d6d6b277b69818ba84f5395b5beef2378426b21c7`|
|`dxil-spirv/descriptor_qa.hpp`|H|N02|`b00f4f72b34cdcc040f5244476231a06600d080dc1050517a9986db035e05294`|
|`dxil-spirv/dxil.hpp`|H|N02|`9ae06630ec9d0990dacb010a2b24ac26b736ce2e6c8baaa07fc81c92879a1d1c`|
|`dxil-spirv/dxil_converter.cpp`|TU|N02|`ad434fbcd506759447b0220d498c03d5f1c34fbc0ca8d50e8994205c77843389`|
|`dxil-spirv/dxil_converter.hpp`|H|N02|`d60b1c5a6a380c34267fd8cb194bb4d4c9b3b49e1e4c3879253a5eb1d477d0a9`|
|`dxil-spirv/dxil_parser.cpp`|TU|N02|`045c82aeb493875d46b16db1c5cd4dc7b6cc6a7ce0d63b5c6ffd719e49a21127`|
|`dxil-spirv/dxil_parser.hpp`|H|N02|`8767c4caf255376050d8fb05713f85a8b00f1e67689ee3f8c06405d33a622850`|
|`dxil-spirv/dxil_spirv_c.cpp`|TU|N02|`0bf241fa36d2967fe6e06b9f5d446c8d7aed06b8484ed610d6ba8a48cb57d81d`|
|`dxil-spirv/dxil_spirv_c.h`|H|N02|`4fbca14fe1de04b463cd8d8f6f449fc4e57490c5c900eaff9e6d6d65ea55303c`|
|`dxil-spirv/ir.hpp`|H|N02|`9da98032986daa729fa869a0fe682073352f152575160884bd9724ebe2d212f4`|
|`dxil-spirv/llvm_bitcode_parser.cpp`|TU|N02|`33874e548f987adc15e21ff897ae74955e14199ab0a19f4c022593792683fcba`|
|`dxil-spirv/llvm_bitcode_parser.hpp`|H|N02|`a6bc525a29cef6284eefc46ad3a0dd725aea4a663368760bff74227d91f96e76`|
|`dxil-spirv/memory_stream.cpp`|TU|N02|`26ad59d85b63d8c8b77721b92313d683e5df63ba43e1b5fd73cec705ea7ab8b4`|
|`dxil-spirv/memory_stream.hpp`|H|N02|`d6ebf9c897057def398fdce58942f0f90a107e62a2fda7f27234f288a7d2028f`|
|`dxil-spirv/node.cpp`|TU|N02|`d77f15a6f0d5f47c5d49084d0fda8e6ac1b0a8891ea6973979a4fe798d3c5e86`|
|`dxil-spirv/node.hpp`|H|N02|`39b33257561d85e0c85d617d0eb3b68d792053e7a37cb48eeef52ffa9de40749`|
|`dxil-spirv/node_pool.cpp`|TU|N02|`c94bc6eade7066504c7dfe794ccafcac65778869e5a6ce193d0f248701a9c2b5`|
|`dxil-spirv/node_pool.hpp`|H|N02|`010fe8c68fab38927acb222eb4e1c1133fd8480e8748c84f59579dd6fd4dd5f3`|
|`dxil-spirv/opcodes/converter_impl.hpp`|H|N02|`d2e6f16560e17c38801ffd2d87d3df75092332ba26b3c4818b794a28de6b405a`|
|`dxil-spirv/opcodes/dxil/dxil_ags.cpp`|TU|N04|`5d6926177fd63cb16ea61de8c76db6a38d593261e7ce802bb6b11e81db93998f`|
|`dxil-spirv/opcodes/dxil/dxil_ags.hpp`|H|N02|`8533539867de496b56fea0b56afe3de7464b30fa108d3e47e72d3ac150057f64`|
|`dxil-spirv/opcodes/dxil/dxil_arithmetic.cpp`|TU|N02|`2294484549b7d863bf96fd50183af787db1f0fbcd2f1abc7801f8ea6c00938e8`|
|`dxil-spirv/opcodes/dxil/dxil_arithmetic.hpp`|H|N02|`d09741d513b8c8f7129d96e3b5575fadea01bb81a421b7e9b04e4584e96f85bc`|
|`dxil-spirv/opcodes/dxil/dxil_buffer.cpp`|TU|N02|`c8be22d4c1facf84ca00072e2dcc743821654851fb3dcd543c28a4291d02fc25`|
|`dxil-spirv/opcodes/dxil/dxil_buffer.hpp`|H|N02|`c55e1bc5a8196cea36d9e5cd1637beb711949d2215efb1df982311fab3c307b9`|
|`dxil-spirv/opcodes/dxil/dxil_common.cpp`|TU|N02|`30f17520b62c3d27fd6076333a88f01c3ca6c934f73075adfa892445ebdbbe8a`|
|`dxil-spirv/opcodes/dxil/dxil_common.hpp`|H|N02|`20488cb678cd7b98ba05a38f58766983847c5f78cf073416cbf6192962ae159a`|
|`dxil-spirv/opcodes/dxil/dxil_compute.cpp`|TU|N02|`88d2a96ed3fa7062de092770d54c5409d07d6b6d92526e8d846d4cad4b20fe19`|
|`dxil-spirv/opcodes/dxil/dxil_compute.hpp`|H|N02|`3012c78e8822969a9d4654494d70f1f837531b2a8f7fbf1ccf9c5ed50205918c`|
|`dxil-spirv/opcodes/dxil/dxil_geometry.cpp`|TU|N02|`31b379cdc6e82839cfc95eff573e472f93cec1aca2f80f8559d118fe4fc72a5a`|
|`dxil-spirv/opcodes/dxil/dxil_geometry.hpp`|H|N02|`d2fea0e7139876e4ad6040114972ebce73d46c0af02bac1ac32925cd83ef79f9`|
|`dxil-spirv/opcodes/dxil/dxil_mesh.cpp`|TU|N05|`6eefc435a62d0a4b1590c4132f12d1a7e79e7e93cb8ae0d083431161b7b94af6`|
|`dxil-spirv/opcodes/dxil/dxil_mesh.hpp`|H|N05|`239fae5bff02c3844e0c6a38048a672ce45cf33c5988570be532d9182c9dc6dd`|
|`dxil-spirv/opcodes/dxil/dxil_nvapi.cpp`|TU|N06|`5d0ceb7975616e23e06e008f4f31fa13b7d697ff2b40a0285ceaa6fc8109c78a`|
|`dxil-spirv/opcodes/dxil/dxil_nvapi.hpp`|H|N06|`683af7fa847830bb47e6a7393896c577fdea99053f0f9c4265abddb791bf60eb`|
|`dxil-spirv/opcodes/dxil/dxil_pixel_ops.cpp`|TU|N02|`a1a05168b18202503f3517cd27295db9dda39481fae2078ed497ac8697fe4eb1`|
|`dxil-spirv/opcodes/dxil/dxil_pixel_ops.hpp`|H|N02|`927333d196b37bb5fe10011e51729acb7b360e66e583c906eb76d466037f0657`|
|`dxil-spirv/opcodes/dxil/dxil_ray_tracing.cpp`|TU|N02|`193e61fa0ed4027830f1abaa93742fb246704ae94d3b7d523ae30585f97aeca1`|
|`dxil-spirv/opcodes/dxil/dxil_ray_tracing.hpp`|H|N02|`e7da3479e9f071299b15692f12f7e1ab3902a1f80a0a42051e46f150c9e195b0`|
|`dxil-spirv/opcodes/dxil/dxil_resources.cpp`|TU|N02|`981ccb1e1b6214c3cb657428c6ad58c83740b75f7342914862f59dd4275a3d86`|
|`dxil-spirv/opcodes/dxil/dxil_resources.hpp`|H|N02|`3bb2381c2d047666bc29cae481395638a66c200022ca68580aa3d10280fad94e`|
|`dxil-spirv/opcodes/dxil/dxil_sampling.cpp`|TU|N02|`e0cc7cfaa296f0359971728226fd048166e2e827beb3d8eba8f11c5e97848431`|
|`dxil-spirv/opcodes/dxil/dxil_sampling.hpp`|H|N02|`1d465eb40c2fe57c032b41365ec2f27cd2c42eb0fc96cd681b5e5a676dd0ebb7`|
|`dxil-spirv/opcodes/dxil/dxil_tessellation.cpp`|TU|N02|`0c4aff31042c16f07716f83efcf86503ad153c250c181e7f178828b1f4d9c911`|
|`dxil-spirv/opcodes/dxil/dxil_tessellation.hpp`|H|N02|`2dfc58d19440873034c60e450f003880a5388ea2f9836789a8304d265a702522`|
|`dxil-spirv/opcodes/dxil/dxil_waveops.cpp`|TU|N02|`0a3ff86b8e19b8f27633006e02f65312fbf31fed30ed113a3b4179f7aabb92df`|
|`dxil-spirv/opcodes/dxil/dxil_waveops.hpp`|H|N02|`6abed7edc8b679b173e3021e3b67ee0812c5930e26a3a1b05581a7cdbf6c3d88`|
|`dxil-spirv/opcodes/dxil/dxil_workgraph.cpp`|TU|N07|`f1915030eec50669938e6c6577e938547224698307132c606c5112e1ac10572f`|
|`dxil-spirv/opcodes/dxil/dxil_workgraph.hpp`|H|N07|`a9f28934f36c56abb0575a433111482920f997e864d3b0086d0d1fe8a8e397a8`|
|`dxil-spirv/opcodes/opcodes.hpp`|H|N02|`c59a1d5b9d49cd7a43a5ccee34867340fa8890739f74bd8d53e7099605bec15b`|
|`dxil-spirv/opcodes/opcodes_dxil_builtins.cpp`|TU|N02|`1f05555f3bd874ff661b4f7c8493cf86b9545c6aa90b3881a4a3ee0e3e501640`|
|`dxil-spirv/opcodes/opcodes_dxil_builtins.hpp`|H|N02|`16482d68680dd0bd2e8b41656c8fe189ebd57947a8ffe59a438fc26c8e6ecf14`|
|`dxil-spirv/opcodes/opcodes_llvm_builtins.cpp`|TU|N02|`d0d11bb7173b556023154603ffcf31f7721124b685233619b39142eebf3f9609`|
|`dxil-spirv/opcodes/opcodes_llvm_builtins.hpp`|H|N02|`b1f473a31e7faab0b164afd6f8690358f72b60f6c33f808c23244e694c22ed44`|
|`dxil-spirv/scratch_pool.hpp`|H|N02|`659c9342f0035aa260cf7423a2898517f46461f67acb697975f6d0c203fa0a0a`|
|`dxil-spirv/spirv_module.cpp`|TU|N02|`f39a3bc27b932fda38efbf5c25b2e3116860a773caae3ca871da8f9f67676275`|
|`dxil-spirv/spirv_module.hpp`|H|N02|`5c7a3e5bb7b016707afa72dd5c422a885708e7a8b1214e2f3771ae48179aaad4`|
|`dxil-spirv/spirv_module_instrumentation.cpp`|TU|N03|`f897b0b93399b7c4b014dd37bd285d8efeb6c10c24313fe41d3c155b186f52ed`|
|`dxil-spirv/spirv_module_instrumentation.hpp`|H|N03|`4362a35832053eb5e4cda1ca1f664762c1118aad16d41524361190d11906a44f`|
|`dxil-spirv/third_party/bc-decoder/llvm_bitreader.h`|H|N08|`895416044355887f23ec0c448543420d223a3e65dfce27ff816989438c83724c`|
|`dxil-spirv/third_party/bc-decoder/llvm_decoder.cpp`|TU|N08|`f9710da9bcf3af98f678979381adfe3a1e607bfaaf4503def8923acb429576f6`|
|`dxil-spirv/third_party/bc-decoder/llvm_decoder.h`|H|N08|`b67f847159fd9fc77aec9d2455c72e0756437f8fa7c5e56ea948abc8b04bbeae`|
|`dxil-spirv/third_party/glslang-spirv/InReadableOrder.cpp`|TU|N09|`b5f51c8326970d88a067e4d0414e0048c1c7c0f731fc3468597e5f9a377129a0`|
|`dxil-spirv/third_party/glslang-spirv/Logger.cpp`|TU|N10|`494af1ff72ca90fff10d949dacee8e8ee19d0b9619505c6fab895c6f8c24a3bc`|
|`dxil-spirv/third_party/glslang-spirv/Logger.h`|H|N10|`429a723b15daf0f73d525fd063a449bc5660fb2275c5aa24e8bff7414ff8fbf2`|
|`dxil-spirv/third_party/glslang-spirv/SpvBuilder.cpp`|TU|N11|`90c0b0d19d322f8a8ca62d5f75347fef1f9bdf00b0dd30f7c922602c28e913a3`|
|`dxil-spirv/third_party/glslang-spirv/SpvBuilder.h`|H|N11|`2a83e287dcaaf465f4e143ee9f74b65920cbaa4a22290d98dde86a6103c9192c`|
|`dxil-spirv/third_party/glslang-spirv/spvIR.h`|H|N12|`77844d71efa0428e4b47b22a272de89297148603511c543ebb288c64cb8e4bae`|
|`dxil-spirv/util/thread_local_allocator.cpp`|TU|N02|`fc618a8f577e45d377a357b3638e7f0c1b654868566ed3ee131e3f9e10ad35ce`|
|`dxil-spirv/util/thread_local_allocator.hpp`|H|N02|`fb0589c5540afb662bebec08bf76301419ab1860b51716253b04897f63568079`|
|`spirv-headers/include/spirv/unified1/GLSL.std.450.h`|H|N13|`d18a932ee97c268a57e33134651d554dbe71a5654b975e89e9e9487fa59ac9df`|
|`spirv-headers/include/spirv/unified1/spirv.hpp`|H|N14|`d62c8b715c76d3cad9094f2b987f2c985b67b5db08ca82f01711acf8e3673df2`|

## Build metadata, provenance and license files

These are source inputs/notices, not additional runtime translation units. Full
upstream license files remain redistribution/provenance inputs; the applicable
header subset's notices are also reproduced below so an arbitrary full source
bundle is not required by this inventory.

|File|SHA-256|
|---|---|
|`dxil-spirv/CMakeLists.txt`|`216f70f4be949fbc1f611bf97bcf96c9ea696416096e465dab36b0c197dbf953`|
|`dxil-spirv/bc/CMakeLists.txt`|`99f7e71cceeab14ea660004179d850b739842aa117e18b5c1c7c4424f3c7cad2`|
|`dxil-spirv/third_party/CMakeLists.txt`|`80067c584f59dd1b2e0520ffa8a84f1c9cbed23eb5c07e7c851ab9f85e86ea61`|
|`dxil-spirv/external/CMakeLists.txt`|`cfa42f1f1123828fd3a7c2dc6174697985f49009cb7af676b64b126858e6843e`|
|`dxil-spirv/.gitmodules`|`e40d2cb11bb3da25e2310b064a8ec5fc25015651c635eafe5b788f51e39298a3`|
|`dxil-spirv/LICENSE.MIT`|`b3ec61378a66f09835a73f53926d9b5cf79a09c9e2fe8bdd0e39aa5403325e1e`|
|`dxil-spirv/pkg-config/dxil-spirv-c-shared.pc.in`|`de19c060a4cfcd082ed5671fcc6171bd8c34acbe76b1393a0f9942fddedf5954`|
|`dxbc-spirv/LICENSE`|`0f8a5e687bea942a30a8e39b9915294fbe44eca866e71307a9b7083ee0e69ad8`|
|`spirv-headers/CMakeLists.txt`|`18202317ea71c2e01a7eafb325354cc45adbea9f7f65492128e2b38160550858`|
|`spirv-headers/LICENSE`|`ea43b1de38a6f90c488800d66dec1ed671e68cda530266bc96951fb5b6307613`|

## Generated-file boundary

H/include/spirv/unified1/spirv.hpp and GLSL.std.450.h identify themselves as
generated but are checked into the pinned upstream source; their exact hashes
and notices are in the file table. No generator must run to obtain them.
The selected D CMake source lists contain no generated C/C++ translation unit,
configure-generated runtime header, FetchContent source or code-generation
command. D's `configure_file` produces a pkg-config .pc metadata file; shared
export/install CMake files and platform build-system files are not translator
source and have not been produced or hashed here. H's optional install machinery
can generate config-version/pkg-config metadata; it is outside the disabled
install/test configuration. A future configured file-api/dependency report must
confirm no project/platform overrides add runtime inputs.

## Exclusions and conditional include resolution

- No full/native LLVM, DXC, validator, CLI executable, CLI parser, shader corpus,
  test fixture, sandbox, private game/repro artifact, SPIRV-Tools or SPIRV-Cross
  belongs to this runtime subset. Keep existing local DXBC translation unchanged.
- `llvm/IR/*`, IRReader and Support headers occur only in !HAVE_LLVMBC branches
  of llvm_bitcode_parser and opcode helpers. CMake's builtin llvm-bc exports
  HAVE_LLVMBC; native LLVM is excluded.
- Catch header in bc-decoder/llvm_decoder.cpp is guarded by ENABLE_UNIT_TESTS;
  leave it undefined/zero. Android log header is guarded by ANDROID and excluded
  for the Windows target. No missing third-party runtime include was identified
  beyond these source-explained branches. Actual Windows/standard-library headers
  remain toolchain dependencies, not permissively relicensed project files.
- CLI-off alone does not remove dxbc-spirv; B is an actual runtime link dependency.
  B's tests, sm3/spirv backend and Meson graph are excluded, as is nested HN.
- H documentation/build metadata can have CC-BY4.0 terms. Only the two named
  generated runtime headers are in this subset; importing the whole header repo
  creates additional notice scope. Do not label a recursive source bundle MIT-only.

## Redistribution notice manifest

Per-file groups below preserve all distinct leading license/copyright texts in
this source/header subset, with comment decoration removed only for readability.
B source files generally have no individual license header: N01 is inherited
from the exact pinned repository LICENSE; no contradictory per-file license
notice was found in the selected files. H/spirv.hpp has a License:MIT line rather
than SPDX; its applicable root materials terms and specific copyright are kept.
The six bundled glslang files retain their exact BSD3-style variants, including
different named nonendorsement parties. D's broad LICENSE.MIT glslang summary
cannot replace those per-file conditions.

A future source distribution must preserve file headers, applicable LICENSE files
and provenance. A binary driver package must accompany the translator with these
applicable copyright, permission, condition and disclaimer texts; include every
MIT attribution/date variant and the BSD binary-redistribution/nonendorsement
clauses. No copyleft source-release obligation was found in this runtime subset,
but this does not license unrelated upstream directories or third-party tools.
This document is an inventory/proposed notice manifest, not evidence that package
assembly already delivers the notices. Preserve raw source notices as well as
this human-readable aggregation when importing.

### N01 — 92 files

```text
MIT License

Copyright (c) 2025 Philip Rebohle

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### N02 — 74 files

```text
Copyright (c) 2019-2022 Hans-Kristian Arntzen for Valve Corporation

SPDX-License-Identifier: MIT

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

### N03 — 3 files

```text
Copyright (c) 2025 Hans-Kristian Arntzen for Valve Corporation

SPDX-License-Identifier: MIT

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

### N04 — 1 files

```text
Copyright (c) 2019-2025 Hans-Kristian Arntzen for Valve Corporation

SPDX-License-Identifier: MIT

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

### N05 — 2 files

```text
Copyright 2022 Philip Rebohle for Valve Corporation

SPDX-License-Identifier: MIT

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

### N06 — 2 files

```text
Copyright (c) 2025 Hans-Kristian Arntzen for Valve Corporation

SPDX-License-Identifier: MIT
```

### N07 — 2 files

```text
Copyright (c) 2024 Hans-Kristian Arntzen for Valve Corporation

SPDX-License-Identifier: MIT

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

### N08 — 3 files

```text
The MIT License (MIT)

Copyright (c) 2019-2020 Baldur Karlsson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*****************************************************************************/
```

### N09 — 1 files

```text
Copyright (C) 2016 Google, Inc.

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

   Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following
   disclaimer in the documentation and/or other materials provided
   with the distribution.

   Neither the name of 3Dlabs Inc. Ltd. nor the names of its
   contributors may be used to endorse or promote products derived
   from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```

### N10 — 2 files

```text
Copyright (C) 2016 Google, Inc.

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

   Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following
   disclaimer in the documentation and/or other materials provided
   with the distribution.

   Neither the name of Google Inc. nor the names of its
   contributors may be used to endorse or promote products derived
   from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```

### N11 — 2 files

```text
Copyright (C) 2014-2015 LunarG, Inc.
Copyright (C) 2015-2016 Google, Inc.

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

   Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following
   disclaimer in the documentation and/or other materials provided
   with the distribution.

   Neither the name of 3Dlabs Inc. Ltd. nor the names of its
   contributors may be used to endorse or promote products derived
   from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```

### N12 — 1 files

```text
Copyright (C) 2014 LunarG, Inc.

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

   Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following
   disclaimer in the documentation and/or other materials provided
   with the distribution.

   Neither the name of 3Dlabs Inc. Ltd. nor the names of its
   contributors may be used to endorse or promote products derived
   from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```

### N13 — 1 files

```text
Files: All files except for those called out below.
Copyright (c) 2015-2024 The Khronos Group Inc.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and/or associated documentation files (the
"Materials"), to deal in the Materials without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Materials, and to
permit persons to whom the Materials are furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Materials.

MODIFICATIONS TO THIS FILE MAY MEAN IT NO LONGER ACCURATELY REFLECTS
KHRONOS STANDARDS. THE UNMODIFIED, NORMATIVE VERSIONS OF KHRONOS
SPECIFICATIONS AND HEADER INFORMATION ARE LOCATED AT
   https://www.khronos.org/registry/

THE MATERIALS ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
MATERIALS OR THE USE OR OTHER DEALINGS IN THE MATERIALS.


Header-specific notice:
* SPDX-FileCopyrightText: 2014-2024 The Khronos Group Inc.
* SPDX-License-Identifier: MIT
```

### N14 — 1 files

```text
Files: All files except for those called out below.
Copyright (c) 2015-2024 The Khronos Group Inc.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and/or associated documentation files (the
"Materials"), to deal in the Materials without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Materials, and to
permit persons to whom the Materials are furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Materials.

MODIFICATIONS TO THIS FILE MAY MEAN IT NO LONGER ACCURATELY REFLECTS
KHRONOS STANDARDS. THE UNMODIFIED, NORMATIVE VERSIONS OF KHRONOS
SPECIFICATIONS AND HEADER INFORMATION ARE LOCATED AT
   https://www.khronos.org/registry/

THE MATERIALS ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
MATERIALS OR THE USE OR OTHER DEALINGS IN THE MATERIALS.


Header-specific notice:
Copyright: 2014-2024 The Khronos Group Inc.
License: MIT
```

## Remaining gaps before import/redistribution

1. Adopt the reviewed revision and options as an actual project dependency lock.
   This note is a candidate inventory, not an install or source import.
2. Under a separately authorized MSVC/ARM64 build, capture the exact configured
   target/file-api manifest, preprocessed dependency list and linker inputs.
   Reconcile the conservative headers above, local patches, generated metadata,
   platform headers and C++ runtime/CRT redistributable requirements. Source
   lists alone cannot establish the exact minimal compiled-header/artifact set.
3. Decide whether the static or shared C API artifact is delivered; record its
   binary hash, ABI/version and actual dependencies. This inventory excludes
   the unused alternative from the shipped package, not from upstream CMake's
   declarations.
4. Verify the driver package includes the complete applicable notices and pins,
   with no unreviewed CLI/test/full-repo files accidentally bundled. Hash that
   final notice/package manifest; no such package currently exists.
5. Separately satisfy the prior review's allocator ownership, binding remapping,
   supported SPIR-V/NIR features and Microsoft SM6 admission contracts. A complete
   source notice inventory is not model conformance or hardware verification.

No configure/build/link/install, existing-license edit, capability change,
commit, ledger update or hardware action occurred during this inventory.
