OpenKNX - AccessControl module
===

Implementation of a knx access control module with up to 1500 action channels based on the [knx stack](https://github.com/OpenKNX/knx), a fork from [thelsing](https://github.com/thelsing/knx).

It supports the RP2040 and ESP32 version of the stack.

It is a PlatformIO project and needs a working ETS >=5.7 installed on the same PC.

Access media
---

* Fingerprint: GROW R503, R503S and R503Pro, selected by the ETS parameter "Fingerprint Scanner"
  ("Kein Fingerprint" disables the scanner completely). The driver is strictly non-blocking, so KNX
  stays responsive during enrollment and template synchronisation - see
  [doc/FingerprintInterface.md](doc/FingerprintInterface.md) for the architecture, the
  `acc fpi test|info|idx|led <n>` console diagnostics and the synchronisation notes.
* NFC: internal PN7160 reader or an external reader.
* Keypad: Gira keypad and 3x4 matrix keypad.

Documentation
---

* [doc/Applikationsbeschreibung-Zutrittskontrolle.md](doc/Applikationsbeschreibung-Zutrittskontrolle.md) - application description (German).
* [doc/FingerprintInterface.md](doc/FingerprintInterface.md) - fingerprint scanner driver notes.
