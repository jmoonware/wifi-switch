# wifi-switch

### A wifi power switch with some external I2C and OneWire devices

OK why do we need this, when there are lots of WiFi switches on the market? 

Well, I do have some Kasa TP-Link switches at home. There is even a [Python API](https://github.com/python-kasa/python-kasa) for these switches.

But goddam are they flaky. And the app forced me to give them an email address (I couldn't find a way to bypass this any more, not that I trust them anyway.) I mean, FFS, do I need to give you a goddamned email address just to flip a switch?

I'd rather have something that I completely control and does not need to go through any third-party spyware, or open up possible IoT vulnerabilities on my network. So this is the "un-enshittified wifi switch" project, but that name was kind of long.

So, here it is: A Raspberry Pi Pico W, a couple BME/DHT temperature/humidity/pressure sensors for monitoring indoor conditions, a 5 V power supply, and a solid state relay (to switch 120 V). Mount these things in a plastic enclosure on a DIN rail, with a proper fused power input module and output socket(s). Viola!
