# R154 offline evidence

The R154-native-* query outputs read the existing EXP867 dump on the builder with its matched private PDB. No Air access. The original dump and PDB hashes remain in EXP867's evidence manifest. R154-active/process/scene identify the stalled packet. The native parser replay uses the exact33503176 source copied here and the dumped header/command/current graph.

On macOS, reproduce the parser evidence in a temporary copy of this directory (the replay writes its result):

```sh
clang -dynamiclib -fPIC -I. apple_agx_g4_submit.c -o R154-native-parser.dylib
python3 R154-native-replay.py
```

Expected parse_result0 and18 valid access checks. Rebuilt dylib hash may differ with the compiler; the original result preserves the actually tested artifact hash. This checks CPU parsing, not GPU execution or mutation timing.

summary.json indexes final RED/GREEN, ARM64 compile, source hashes and controlled baseline/candidate suite results. Discarded mutable-source exploratory suite runs are explicitly excluded in verification-notes.json. All acceptance logs are retained. Software-only status is implemented.
