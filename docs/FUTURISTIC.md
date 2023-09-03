# Tapeshnik Futuristic Notes

![mecha](futuristic/mecha1.gif)

While the current mechanism works, its limitations are obvious and its reliability is questionnable. Shall something break, it will be getting harder to get a replacement.

As a tangent project, I have an idea to design a simple, relatively easy to repeat tape mechanism that could be built from scratch using available parts. Obviously the heads will still have to be sourced, but cassette heads are relatively abundant.

As an inspiration I have TEAC D/CAS system. It uses tapes that look almost identical to Compact Cassettes. It's a two-motor direct drive system without a capstan. 

In D/CAS like TEAC MT-2ST, tape speed is closed-loop controlled by a closed loop servo, formed by the takeup reel motor and tape speed sensor. The tape speed sensor is not unlike a pinch roller, except that it does not pinch, instead rotates by friction with tape and provides speed reading.

Stable tape speed is useful, but depending on method of writing it may be not required. For example, a good PLL on FM or differential manchester can recover +-5% speed variation without issue, probably more with extra tweaks. But even better, if we sacrifice one of the tracks to reference clock signal, tape speed becomes almost a non-issue.

It's still worth researching sensing tape speed of course. Could a laser mouse sensor be used for that? In my past experiments, mice sensors experience frequent lapses which makes them useless for absolute position servo control. For speed sensing dropouts are also undesirable, but they may be a less significant problem.

The main frame of the mechanism can be built from several layers of PCB material stacked, connected via screws or standoffs. To insert a cassette it's necessary that some parts of the assembly move. We can either move the heads into the cassette, or motor spindles could be raised into the hubs when the cassette is inserted. I think the motors, or even just spindles, would be easier to move and the tolerances are much less tight for the spindles.

### RW heads

Ideally a 4-track head is needed. Erase head is not strictly necessary and a regular fixed autoreverse head would work.

### Motors

It's not 100% clear which kinds of motors are used in D/CAS, but at least some early versions used DC motors with speed controlled by current. That's cool but hard to match using off the shelf DC motors available today.

These days I think it's more practiccal to use BLDC motors. They are well researched and existing solutions for various kinds of BLDC control are available. Apparently we're looking for low-KV gimbal type motor, which should also be slotless to minimise cogging. An example of such a motor is JDPOWER MY-2813C, 75 yuan @ taobao. It seems to be slotless, although not with 100% certainty.

![MY-2813C](futuristic/JDPOWER%20MY-2813C.png)

It is also worth checking out if DVD motors can be used as well. They are not designed with small speed in mind, but they have ring magnets so they may cog less.

Motor controller board is going to use [SimpleFOC](https://simplefoc.com/).

Motor drivers: [STSPIN233](futuristic/stspin233-1.pdf)

### Sensor

List of "known good" mouse sensors from the point of view of gamers. 
https://thegamingsetup.com/gaming-mouse/buying-guides/flawless-mouse-sensor-list

A quick test shows that my random mouse at least sees the tape moving in the cassette. Worth investigating.

The sensor to be tested is [PMW3389DM-T3QU](futuristic/pixart_pmw3389dm-t3qu_-_productbrief_1374785_20.pdf)
