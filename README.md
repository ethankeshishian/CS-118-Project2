# CS118 Project 2

This is the repo for spring 2022 cs118 project 2.

Arek Der-Sarkissian - areksds@ucla.edu - 905270247
Ethan Keshishian - ethankeshishian@ucla.edu - 105422235

From a high-level perspective, our server and client both accept an initial SYN connection. Following that, both enter an infinite loop that accepts one packet from each other at a time. On the client side, except in the case of the first non-SYN packet, the client waits to hear an ACK from the server. After that, it constructs up to ten packets to send in a given window to the server. On the server side, the server waits to hear for a packet from the client. Upon receiving a packet, it correctly places it into the received file on the server side. Both client and server are configured to detect packet loss in the event where a packet is expected but not received, and appropriately sends out a request for that packet or ACK in subsequent connections to one another.

## Makefile

This provides a couple make targets for things.
By default (all target), it makes the `server` and `client` executables.

It provides a `clean` target, and `tarball` target to create the submission file as well.

You will need to modify the `Makefile` USERID to add your userid for the `.tar.gz` turn-in at the top of the file.

## Academic Integrity Note

You are encouraged to host your code in private repositories on [GitHub](https://github.com/), [GitLab](https://gitlab.com), or other places. At the same time, you are PROHIBITED to make your code for the class project public during the class or any time after the class. If you do so, you will be violating academic honestly policy that you have signed, as well as the student code of conduct and be subject to serious sanctions.
