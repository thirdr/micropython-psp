"""Lists the files next to the EBOOT, with their sizes."""
import os

for name in sorted(os.listdir()):
    print("{:>8}  {}".format(os.stat(name)[6], name))
