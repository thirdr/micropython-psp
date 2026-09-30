# Files: keeps a count of how many times this script has run, in
# notes.txt next to it on the memory stick.
try:
    with open("notes.txt") as f:
        runs = int(f.read().split(":")[1])
except (OSError, ValueError, IndexError):
    runs = 0

runs += 1
with open("notes.txt", "w") as f:
    f.write("runs: {}\n".format(runs))

print("This script has run {} time{}.".format(runs, "" if runs == 1 else "s"))
print("The count is in notes.txt in the MicroPython folder.")
