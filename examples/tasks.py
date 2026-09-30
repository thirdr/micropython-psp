# asyncio: two tasks running side by side, one fast and one slow,
# while a third waits for START to stop them.
import asyncio

import psp


async def ticker(name, seconds):
    n = 0
    while True:
        n += 1
        print("{} tick {}".format(name, n))
        await asyncio.sleep(seconds)


async def main():
    print("Two tasks at once. START finishes.")
    tasks = [asyncio.create_task(ticker("fast", 0.5)), asyncio.create_task(ticker("slow", 2))]
    while psp.START not in psp.pressed():
        await asyncio.sleep_ms(16)
    for t in tasks:
        t.cancel()
    print("Stopped.")


asyncio.run(main())
