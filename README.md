ESP3D printer server
---

### What is this?

This is a tiny 3D printer server with web-interface inside. It was designed
especially for 3D printers that has no on-board Wi-Fi or any other networking module.
This firmware by-design can run on ESP32-Cam cheap module which has SD-card interface
and enough free pins just to connect it to the printer with UART.

### What it can do?

It can act as a file server, so you can upload sliced G-Code onto SD-card, then
select a file to print and track printing progress in cute web-UI. Unfortunately
it does not show progress on mobile phones, but I'm working on that.

### Compatibility

Current version was tested with Marlin 2.x on Lerdge X board (this board has
UART in its peripheral connector). No other boards/firmwares were confirmed working.
Please feel free to report if you made it work with your printer.

### How to install it?

First you need a ESP32-CAM module which should be connected to your 3D-printer UART
like in the picture below:

Then you need to upload a firmware. I guess ESP32 Flasher utility can be used for 
that:

You also need to prepare (not necessarily) blank SD-card. There you got to 
create a directory called 'esp3d' and put there a file called 'settings' which
should contain your Wi-Fi credentials:

`
ssid=your_favorite_ap_ssid
password=your_supa_dupa_strong_password
`

These are mandatory. If you want to add an IP address for your printer you may use:

`ip=192.168.xxx.xxx`

That's it. Put the SD-card into your module and give it some power.

---

*DISCLAIMER:* This firmware is not production ready or industrial-quality. Do not leave
working device without attention: 3D-printer's hot end and heat bed may cause fire 
when printer stops but not protected in its own firmware.

Please, use it on your own risk.