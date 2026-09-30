"""Counts down from 5, one number a second."""
import time

for n in range(5, 0, -1):
    print(n)
    time.sleep(1)
print("Lift off!")
