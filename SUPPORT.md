# BlaeckSerial Support

First off, thank you for using BlaeckSerial.

Happy to help — please read the following first.

## Before asking

1. Check the [README](README.md), which covers signals, commands and configuration
2. Check the [protocol documentation](https://sebajost.github.io/blaeck-protocol/)
   for anything about the frames themselves
3. Open the example nearest your problem under *File → Examples → BlaeckSerial*.
   `WaveformGenerator` exercises signals, commands, state channels and events
   together; `Basic` and `Commands` are smaller starting points

If that did not answer it, open a
[new issue](https://github.com/sebaJoSt/BlaeckSerial/issues/new).

## When reporting a problem

Please include:

* What you expected to happen, and what happened instead
* Your board, and the core version
* The sketch — cut down to the smallest one that still shows the problem
* Compiler output, in full, if it does not build
* Which host is reading the data, if the sketch builds and runs but the data looks
  wrong: Loggbok, blaecktcpy, or something of your own

Two things worth attaching for anything involving missing signals, channels or
commands, because they answer most of these questions immediately:

* The output of `Blaeck.printRejections(&Serial)`, which names anything a table had
  no room for and the `begin()` call that would have made room
* A debug stream — `begin(&Serial).withDebugStream(&Serial)` — which reports what was
  rejected and why, where the device is otherwise silent about it
