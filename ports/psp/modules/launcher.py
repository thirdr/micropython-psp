# Runs when the EBOOT's folder has no main.py.
#
# Placeholder until the d-pad launcher is designed: says what's missing and
# where it's looking.
import os
import sys

print(sys.version.split("; ")[1])
print()
print("No main.py in", os.getcwd())
print("Put a main.py next to EBOOT.PBP to run it.")
print("(The script launcher is coming.)")
