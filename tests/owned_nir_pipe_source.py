"""Original CURRENT-only opt-in clone/consume CPU recipes. No private fallback."""
from pathlib import Path
import owned_program_nir_source as owned
ROOT=Path(__file__).resolve().parents[1]
MODULE=ROOT/'drivers/apple-agx/mesa/dxil'
NAMES=['drivers/apple-agx/mesa/dxil/agx_owned_nir_pipe.h','drivers/apple-agx/mesa/dxil/agx_owned_nir_pipe.c','tests/owned_nir_pipe_fixture.c','tests/owned_nir_pipe_driver.cpp','tests/owned_nir_pipe_source.py','tests/test_owned_nir_pipe.py']
sha,entry,LINK_STDERR=owned.sha,owned.entry,owned.LINK_STDERR
def source_inputs():return list(dict.fromkeys(owned.source_inputs()+[ROOT/n for n in NAMES]))
def render(generated):return owned.render(generated)
def commands(receipts,generated):
 out=Path(receipts).resolve();cs=owned.commands(out,generated)
 # Preserve all old module/inspector/control units. Only this new driver's
 # call site changes; actual provider/core/conversion/semantic sources remain.
 for c in cs:
  if c['label']=='fixture':
   i=c['argv'].index('-c');c['argv'][i+1]=str(ROOT/'tests/owned_nir_pipe_driver.cpp')
 base=next(c['argv']for c in cs if c['label']=='frontend');flags=base[1:base.index('-MD')]
 # The existing frontend did not include Gallium contexts. Use the one
 # already-pinned actual Mesa header root for these two additional C units.
 _,external_pins=owned.checked.expected_inputs()
 contexts=[Path(n) for n in external_pins if n.endswith('/pipe/p_context.h')]
 if len(contexts)!=1:raise RuntimeError('EXACT_PINNED_GALLIUM_CONTEXT_HEADER_REQUIRED')
 flags=flags+['-I',str(contexts[0].parent.parent)]
 extra=[]
 for label,source in [('handoff',MODULE/'agx_owned_nir_pipe.c'),('handoff_fixture',ROOT/'tests/owned_nir_pipe_fixture.c')]:
  extra.append({'label':label,'argv':[base[0],*flags,'-MD','-MF',str(out/(label+'.d')),'-c',str(source),'-o',str(out/(label+'.o'))]})
 link=next(c for c in cs if c['label']=='link');i=next(i for i,a in enumerate(link['argv'])if a.endswith('.o'))
 link['argv'][i:i]=[str(out/(c['label']+'.o'))for c in extra]
 cs[-2:-2]=extra
 return cs

def expected_inputs(generated):
 pins=owned.expected_inputs(generated)
 for p in source_inputs():pins[str(p.resolve())]=entry(p)
 return pins
