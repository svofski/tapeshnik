# Tapeshnik

## ME-136 KS211M

### Mechanism Overview

ME-136 KS211M is a moniker for a logic controlled compact cassette mechanism sold in China.

![ks211m-front](https://github.com/svofski/tapeshnik/assets/6445874/b8b9ac27-4c59-47bb-b7ef-d27572dc66c1)
![ks211m-back](https://github.com/svofski/tapeshnik/assets/6445874/6b929e33-6209-470d-b5c7-82519606aef5)

ME-136 is the erase head. KS211M is Write/Playback head. The actual mechanism thus seems to have no name or part number. It is sold as new, but the belts seem to be weak from the start.

The only thing that's known is this little circuit diagram:

![ks211m](https://github.com/svofski/tapeshnik/assets/6445874/78b413ec-f755-44b8-b7a5-09ab92c984bb)

 * Pin 0: not shown, not connected
 * Pin 1: write enable
 * Pin 2: mode wheel switch, open when the wheel reaches STOP position
 * Pin 3: control solenoid 12V (high-side switch)
 * Pin 4: GND
 * Pin 5: Photo interruptor output
 * Pin 6: Photo interruptor +5V
 * Pin 7: cassette detect
 
![pin-numbers](https://github.com/svofski/tapeshnik/assets/6445874/f381c61b-fc72-4d21-b55c-03d468b52f7f)

I don't know the exact name of this kind of connector, but it has 2.0mm pitch and the cables can be bought, the search term is PH2.0MM间距端子线. For easy prototyping I removed the housing from the other end of the cable and soldered the contacts to a 2.54mm pin header.
![cable connection](ph2.0-cable.jpg)

The motor pins are not on the main connector. It is a 12V DC motor. Connections on the motor are clearly marked "+" and "-". If my PSU is to be trusted, the motor alone consumes 0.045A at 12V in playback mode.

### Control

The mechanism modes are selected by pulsing the solenoid. The feeler arm follows the grooves on the command wheel and sets relevant parts of the mechanism in motion.

![full-turn](https://github.com/svofski/tapeshnik/assets/6445874/65a4049c-22cc-491b-8ec7-eb3acd92936b)
The full turn of the command wheel takes approximately 900ms.

The wheel has 3 characteristic positions: 12 hours: STOP, 6 hours: PLAY, 10 hours: FF/REW.

The solenoid needs a pulse of about 10ms to begin state transition. Holding the pulse for longer is not necessary. It is possible that holding solenoid voltage works better than short pulses, I don't know which is the recommended mode of operation. Short pulses seem safer for the semiconductors.

#### Command wheel state transitions
![wheel-diagram](wheel-state-chart1.png)
 * From STOP position, the first pulse will always put mechanism in PLAY position. 
 * From PLAY position, a single short pulse transitions to FF position.
 * From PLAY position, a short pulse + 50ms delay + a short pulse transitions to REW position.
 * From FF/REW any pulse transitions to STOP position.
