# Wi-Fi: connects with a network saved in the PSP's settings, then fetches
# a web page. Turn the Wi-Fi switch on first. Press START to finish.
import network
import psp
import requests

wlan = network.WLAN()
if not wlan.active():
    print("Turn the Wi-Fi switch on, then run this again.")
else:
    profiles = wlan.profiles()
    if not profiles:
        print("No saved networks: add one in Settings > Network Settings.")
    else:
        number, name = profiles[0]
        print("Saved networks:", ", ".join(n for _, n in profiles))
        print("Connecting to {}...".format(name))
        try:
            wlan.connect(number)
            ip, mask, gateway, dns = wlan.ifconfig()
            print("Connected. IP address:", ip)
            print("Fetching http://example.com/ ...")
            r = requests.get("http://example.com/")
            text = r.text
            start = text.find("<title>") + 7
            print("Status {}, title: {}".format(r.status_code, text[start:text.find("</title>")]))
        except OSError as e:
            print("Didn't work:", e)
        wlan.disconnect()
print("START finishes.")
while psp.START not in psp.pressed():
    psp.vsync()
