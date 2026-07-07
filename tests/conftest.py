import os
import sys

# pyiface/ lives at the repo root, one level up from tests/ -- make it
# importable regardless of the directory pytest is invoked from.
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
