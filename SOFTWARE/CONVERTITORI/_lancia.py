# ============================================================================
# _lancia.py — fa girare UN convertitore dentro l'ambiente del pacchetto.
#   python _lancia.py <convertitore.py>
# Gira in un processo suo: cosi' un convertitore che chiama sys.exit() o che
# esplode non si porta dietro prepara.py e gli altri 93.
# ============================================================================
import os, runpy, sys

QUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, QUI)

import _ambiente
_ambiente.attiva()

if len(sys.argv) < 2:
    sys.exit("uso: python _lancia.py <convertitore.py>")

script = sys.argv[1]
if not os.path.isabs(script):
    script = os.path.join(QUI, script)

sys.argv = [script] + sys.argv[2:]
runpy.run_path(script, run_name="__main__")
