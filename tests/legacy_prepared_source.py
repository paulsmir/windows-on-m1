"""Original CURRENT-only true legacy factory NO-core/provider host recipes."""
from pathlib import Path
import owned_program_nir_source as base
ROOT=Path(__file__).resolve().parents[1]
NAMES=['tests/legacy_prepared_source.py','tests/test_legacy_prepared.py','tests/legacy_prepared_no_core_fixture.cpp','tests/legacy_prepared_inspect.c','tests/legacy_prepared_ref_redirect.h']
sha,entry,LINK_STDERR=base.sha,base.entry,base.LINK_STDERR
def source_inputs():return list(dict.fromkeys(base.source_inputs()+[ROOT/n for n in NAMES]))
def commands(receipts,generated):
 out,g=Path(receipts).resolve(),Path(generated).resolve();old=base.commands(out,g)
 keep={'frontend','map','dxbc','parse','handoff','program','legacy','owned','common','facade'}
 cs=[c for c in old if c['label']in keep]
 frontend=next(c for c in cs if c['label']=='frontend')
 cflags=frontend['argv'][1:frontend['argv'].index('-MD')]
 frontend['argv'][1:1]=['-include',str(ROOT/'tests/legacy_prepared_ref_redirect.h')]
 cs.append({'label':'refinspect','argv':[frontend['argv'][0],*cflags,'-MD','-MF',str(out/'refinspect.d'),'-c',str(ROOT/'tests/legacy_prepared_inspect.c'),'-o',str(out/'refinspect.o')]})
 template=next(c for c in old if c['label']=='fixture')['argv'];flags=template[1:template.index('-MD')]
 cs.append({'label':'fixture','argv':[template[0],*flags,'-MD','-MF',str(out/'fixture.d'),'-c',str(ROOT/'tests/legacy_prepared_no_core_fixture.cpp'),'-o',str(out/'fixture.o')]})
 link=next(c for c in old if c['label']=='link');objects={str(out/(c['label']+'.o'))for c in cs}
 link['argv']=[v for v in link['argv']if not v.endswith('.o')or v in objects]
 i=next(i for i,v in enumerate(link['argv'])if v.endswith('.o'));link['argv'].insert(i,str(out/'refinspect.o'))
 # Every kept object already comes from the base recipe; add fixture explicitly
 # because the unchanged base fixture object pathname remains in its link.
 if not set(objects)<=set(link['argv']):raise RuntimeError('NO_CORE_LINK_OBJECT_MISSING')
 if any(Path(v).name in ['core.o','library.o','provider.o','bridge.o']for v in link['argv']):raise RuntimeError('CORE_PROVIDER_OBJECT_REFUSED')
 cs.append(link)
 cs.append({'label':'run','argv':[str(out/'owned-program-nir-fixture'),str(g/'legacy-programs/cs50.program'),str(g/'legacy-programs/cs51static.program'),str(g/'exp1397.program'),str(g/'legacy-programs/vs40.program')]})
 return cs

def expected_inputs(generated):
 pins=base.expected_inputs(generated)
 for p in source_inputs():pins[str(p.resolve())]=entry(p)
 return pins
