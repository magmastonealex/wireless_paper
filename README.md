E-Paper Thingy
===

![e-paper display with 7.5 inch display](/.imgs/front_75.jpg)

This repository contains source code for the Zephyr application which runs on the e-paper boards I designed.

Additionally, a Rust-based "host" application is in the `host/` subdirectory which acts as a "server" for these displays, providing images to display, over-the-air upgrades, and a small dashboard where you can view status of the fleet of displays.

The two components talk with one another using CoAP on top of a Thread network.

Details on this project are available on [my website](https://alexroth.me/display/). Detailed compilation and setup instructions will come to this repository "at some point". If you really want to make use of anything from here, please feel free to reach out and I'll get to it faster :)

Claude usage
===

Claude contributed to some parts of this project, particularly the CRUD infrastructure and web UI (which I _really_ can't be bothered to write). This was mostly an experiement to understand how Claude can assist me with the parts of personal projects I normally put off and never get to - overall, it was a positive experience, though the output of the tool needed a lot of correction and review. Commits where Claude was used are marked with "Claude:" as a prefix.

OpenThread Credentials
===

There are OpenThread credentials present in src/main.c. These were test credentials I used during initial setup of the boards, and should be changed to match your network. Provisioning of OpenThread datasets is supported over a serial shell and should be done that way wherever possible.

Signing key
===
yes, I know signing_key.pem is public. This is an open-source project, I'm not trying to lock anyone out of any devices. see notes in sysbuild.conf as to how to do this _properly_ if you really care.
