// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Sunil J and the Oreo contributors
// The built-in expression library (text, parsed at runtime). The robot also loads extra /
// replacement expressions from the SD card (/faces/*.txt) in the same format.
//
// One expression per line:   name  valence arousal : tokens...
//   key=value     set a rig value          key+=value   add to it
//   values:       0.5   a range 0.2..0.6 (random each time)   s0.4 / -s0.4 (random side, same
//                 for the whole line: +-0.4)
//   @closedHappy @closedCalm @happyEyes   shortcuts
//   ?0.3{ ... }   those tokens 30 % of the time
//   pick{ ... | ... | 0.5: ... }   one of the alternatives (optional weights)
//   glyph= fx= fx2= acc= talk blink= glance=   (see face_engine_v2.h for the names)
//   tint=r,g,b,amount
// Rig keys: size sizeL sizeR squash gap y dyL dyR tilt lidT lidTA lidB closeL closeR closeCurve
//   pupil dilate lookX lookY shine browShow browY browA browCurve browAsym smile open mW mX wavy
//   tongue cat blush bounce bounceHz jitter breathe spin sway cross
#pragma once

static const char FACE_LIB[] = R"LIB(
idle 0.2 0 : ?0.3{smile=0.1..0.45} ?0.25{lidT=0.05..0.15}
blink 0 0 : @closedCalm closeCurve=-0.2 blink=0
happy 0.8 0.4 : @happyEyes smile=1 pick{open=0.4..0.65|open=0} blush=0.1..0.5 tint=255,215,110,0.35 bounce=0..4 bounceHz=1.1..1.7 wobble=0.04
excited 0.9 0.9 : pick{0.6:glyph=star|glyph=eye} fx=sparkle size=1.08 lidB=0..0.15 smile=1 open=0.5..0.75 browY=0.5 bounce=6..10 bounceHz=2.1..2.7 tint=255,230,90,0.35 wobble=0.06 pulse=0.03
curious 0.3 0.4 : ?0.6{fx=question} sizeL+=-s0.07 sizeR+=s0.08 lookX=s0.45 browAsym=s0.6 tilt=s7 lookY=-0.25 smile=0.1 open=0.18 mW=0.4 sway=2
tilted 0.3 0.2 : tilt=s10..16 lookX=-s0.35 lookY=-0.3 open=0.2 mW=0.3 browY=0.25
sad -0.8 -0.5 : browA=-22 browY=0.1 lidT=0.28 lidTA=-16 lookY=0.45 smile=-0.8 mW=0.8 tint=110,160,255,0.5 glance=0.3 blink=3 ?0.3{fx=tears} nod=2
pout -0.4 0.1 : browA=10 lidT=0.2 smile=-0.6 mW=0.45 wavy=0.5 lookX=s0.35 blush=0.35 puff=0.5
angry -0.8 0.8 : fx=anger ?0.4{fx2=steam} browA=26..32 browY=-0.35 lidT=0.33 lidTA=22 pupil=0.85 smile=-0.7 mW=0.8 jitter=0.6 tint=255,70,50,0.8 glance=0.2 pulse=0.035
surprised 0.1 0.9 : fx=shock size=1.15 pupil=0.72 dilate=-0.4 browY=0.65 smile=0 open=0.5..0.75 mW=0.5 blink=9 glance=0.2 wobble=0.04
confused -0.1 0.3 : fx=question browAsym=s0.7 sizeL+=s0.08 sizeR+=-s0.1 lookX=s0.25 tilt=-s8 browA=8 lookY=-0.2 smile=-0.15 wavy=1 mW=0.7 sway=3
thinking 0 0.1 : fx=dots lookX=-s0.55 mX=s0.35 lookY=-0.65 lidT=0.12 browAsym=0.5 smile=-0.1 mW=0.45 glance=0.1
suspicious -0.2 0.2 : lidT=0.46 lidTA=4 lidB=0.12 lookX=s0.75 mX=-s0.3 browA=12 browY=-0.3 smile=0.2 mW=0.55 glance=0 blink=7
happy_blink 0.9 0.4 : @closedHappy fx=blushlines blush=0.8 smile=1 open=0.45 bounce=3 bounceHz=1.2
heart 1 0.6 : glyph=heart fx=hearts smile=0.9 pick{open=0.3|open=0} blush=0.5 tint=255,90,160,1 pulse=0.06
love 1 0.3 : pick{0.7:@closedHappy|lidB=0.5} fx=hearts blush=0.8 smile=0.8 mW=0.7 tint=255,110,170,0.6 bounce=2 bounceHz=1 pulse=0.05
sleepy 0.1 -0.9 : pick{0.6:@closedCalm|lidT=0.75 lidB=0.15} fx=zzz smile=0.1 mW=0.5 y=6 breathe=2.5 tint=150,160,255,0.4 blink=0 glance=0 nod=3
tired -0.2 -0.7 : lidT=0.55..0.66 lidTA=-4 lidB=0.12 browY=-0.2 browA=-6 smile=0 ?0.4{open=0.3} mW=0.4 lookY=0.25 glance=0.2 blink=2.5 nod=2
wink_left 0.7 0.5 : closeL=1 smile=0.8 browAsym=-0.4 ?0.4{tongue=1}
wink_right 0.7 0.5 : closeR=1 smile=0.8 browAsym=0.4 ?0.4{tongue=1}
tongue 0.7 0.5 : pick{closeL=1|closeR=1} smile=0.6 tongue=1 lookX=0.2
cheeky 0.7 0.6 : glyph=chevron smile=0.6 tongue=1 bounce=3 bounceHz=2 wobble=0.05
ehehe 0.8 0.3 : @closedHappy fx=blushlines blush=0.9 smile=0.7 mW=0.5 bounce=2 bounceHz=2.2 tilt=-8..8 wobble=0.05
joy 1 0.8 : @closedHappy fx=sparkle smile=1 open=0.7 bounce=5..8 bounceHz=2 tint=255,220,90,0.5 wobble=0.05
focus 0.1 0.4 : browA=14..20 browY=-0.25 lidT=0.2 lidTA=8 pupil=0.85 smile=0 mW=0.6 glance=0.3
alarm -0.5 0.9 : fx=exclaim size=1.12 pupil=0.7 browA=-14 browY=0.5 smile=-0.3 open=0.35 mW=0.5 wavy=0.4 jitter=0.8
alert -0.5 0.8 : fx=shock browA=26 browY=-0.2 pupil=0.7 dilate=-0.5 smile=-0.2 mW=0.5 tint=255,60,50,0.9
loading 0 0 : glyph=loading mW=0 blink=0 glance=0
connecting 0.2 0 : @closedHappy fx=wifi smile=0.2 mW=0.4 blink=0 glance=0
success 0.9 0.6 : @closedHappy fx=shine smile=1 open=0.45 tint=120,255,140,0.6 bounce=5 bounceHz=1.6 wobble=0.04
error -0.6 0.5 : glyph=x wavy=1 smile=-0.2 mW=0.9 tint=255,50,60,1 jitter=0.4 blink=0 glance=0
low_battery -0.5 -0.8 : fx=battery lidT=0.5 lidTA=-10 browA=-12 lookY=0.5 smile=-0.5 mW=0.5 tint=140,160,255,0.4 glance=0
charging 0.5 -0.4 : @closedHappy fx=bolt smile=0.5 mW=0.5 tint=120,255,120,0.4 blink=0 glance=0
heart_beat 0.6 0.3 : glyph=heartbeat smile=0.3 mW=0.5 tint=255,60,80,1 blink=0 pulse=0.05
listening 0.3 0.3 : fx=listen size=1.05 browY=0.2 smile=0.2 mW=0.35 glance=0.2
speaking 0.4 0.4 : fx=speak talk smile=0.4
happy_speak 0.8 0.5 : @closedHappy fx=speak fx2=listen talk smile=0.8
look_left 0.2 0 : lookX=-1 glance=0
look_right 0.2 0 : lookX=1 glance=0
look_up 0.2 0.1 : lookY=-1 browY=0.3 glance=0
look_down 0 -0.1 : lookY=1 lidT=0.18 glance=0
dizzy -0.2 0.5 : glyph=spiral fx=dizzy wavy=1 mW=0.7 spin=1 tilt=-8..8 sway=4 blink=0 glance=0 wobble=0.03
cool 0.6 0.2 : glyph=shades smile=0.6 mW=0.7 mX=s0.25 blink=0
celebrate 1 1 : glyph=chevron fx=confetti smile=1 open=0.75 bounce=7..10 bounceHz=2.6 wobble=0.06
sad_cry -0.9 0.2 : glyph=teary fx=cry browA=-24 lookY=0.2 smile=-0.7 wavy=0.6 mW=0.8 tint=120,170,255,0.5 glance=0
question 0.1 0.3 : fx=question size=1.08 browY=0.45 smile=0 open=0.25 mW=0.35 tilt=-8..8 sway=2
idea 0.7 0.7 : fx=bulb size=1.08 lookY=-0.6 browY=0.5 smile=0.8 mW=0.6 glance=0
calm 0.5 -0.6 : @closedCalm smile=0.45 mW=0.6 breathe=2 blink=0 glance=0.3
scared -0.8 0.9 : fx=sweat size=1.12 pupil=0.62 dilate=-0.6 browA=-22 browY=0.45 smile=-0.4 open=0.35 wavy=0.8 mW=0.7 jitter=1.4 tint=180,200,255,0.4 wobble=0.05
shy 0.5 -0.2 : fx=blushlines lookX=-s0.55 tilt=-s8 lookY=0.5 lidT=0.18 blush=1 smile=0.4 mW=0.35 glance=0.2 nod=1.5 wobble=0.02
proud 0.8 0.3 : fx=sparkle lidT=0.3 lidTA=-8 lookY=-0.3 browY=0.25 smile=0.8 mW=0.7 y=-6 tint=255,220,130,0.35 glance=0.2 pulse=0.02
bored -0.3 -0.7 : pick{glyph=line|lidT=0.55 lidB=0.1} smile=0 mW=0.6 mX=0.2 lookX=s0.5 glance=0.3
annoyed -0.6 0.3 : lidT=0.45 lidTA=6 browA=14 browY=-0.2 lookX=s0.6 mX=-s0.2 smile=-0.25 mW=0.6 glance=0
smug 0.6 0.2 : lidT=0.4 lidTA=-6 browAsym=s0.6 smile=0.7 mW=0.6 mX=0.35 lookX=-0.3 glance=0.2
determined 0.3 0.7 : browA=24 browY=-0.3 lidT=0.25 lidTA=12 smile=-0.1 mW=0.9 tint=255,170,80,0.3 glance=0.1
playful 0.8 0.7 : pick{closeR=1|closeL=1} smile=0.8 tongue=1 bounce=4 bounceHz=2.2 tilt=-10..10 wobble=0.05
grateful 0.9 -0.2 : @closedHappy blush=0.6 smile=0.8 mW=0.6 y=10 browA=-6
embarrassed 0.2 0.5 : fx=sweat fx2=blushlines blush=1 lookX=s0.6 lookY=0.3 smile=0.3 wavy=0.7 mW=0.6 browA=-12 wobble=0.03
disgusted -0.7 0.3 : lidT=0.3 lidTA=10 sizeL=0.9 browAsym=-0.4 browA=14 smile=-0.5 wavy=0.6 mW=0.7 mX=0.2 tint=160,230,120,0.5
nervous -0.3 0.6 : fx=sweat browA=-16 browY=0.25 lookX=s0.45 smile=0.2 wavy=1 mW=0.7 jitter=0.7 wobble=0.03
relieved 0.5 -0.4 : @closedCalm fx=sweat smile=0.6 mW=0.6 browA=-10
hopeful 0.4 0.2 : glyph=teary browA=-12 browY=0.35 lookY=-0.3 smile=0.4 mW=0.5
mischievous 0.5 0.5 : lidT=0.35 lidTA=16 browA=16 lookX=s0.5 mX=-s0.3 smile=0.8 mW=0.7 tint=200,150,255,0.35
laugh 1 0.9 : pick{0.6:glyph=chevron|@closedHappy} fx=tears smile=1 open=0.8 bounce=6 bounceHz=3.5..4.5 wobble=0.08
giggle 0.9 0.6 : @closedHappy smile=0.9 open=0.3 mW=0.6 bounce=3 bounceHz=3.5 blush=0.4 tilt=-6..6 wobble=0.07
shocked -0.3 1 : pick{glyph=dot|size=1.2 pupil=0.5} fx=shock browY=0.7 open=0.9 mW=0.7 smile=0
worried -0.5 0.4 : ?0.6{fx=sweat} browA=-20 browY=0.2 lidT=0.1 lookY=0.2 smile=-0.35 wavy=0.5 mW=0.6
starstruck 1 0.8 : glyph=star fx=sparkle size=1.12 smile=1 open=0.5 browY=0.5 tint=255,230,120,0.4 pulse=0.04
sob -1 0.5 : glyph=teary fx=cry browA=-26 lidT=0.25 lidTA=-14 smile=-0.9 open=0.55 wavy=1 tint=110,160,255,0.6 jitter=0.6 nod=2 wobble=0.03
yawn 0 -0.8 : lidT=0.7 lidB=0.15 browY=0.2 smile=0 open=1 mW=0.7 blink=0 glance=0
sleeping 0.2 -1 : @closedCalm fx=zzz smile=0 ?0.5{open=0.2} mW=0.35 breathe=3 y=8 blink=0 glance=0 nod=2.5
dead -0.5 -1 : glyph=x smile=-0.3 mW=0.7 tongue=1 tint=150,160,170,1 blink=0 glance=0
freaky -0.2 1 : fx=sweat sizeL+=s0.18 sizeR+=-s0.18 tilt=s10 pupil=0.6 smile=0.6 open=0.4 wavy=1 jitter=2.2 tint=200,255,150,0.4
vibing 0.8 0.6 : @closedHappy fx=notes smile=0.7 mW=0.6 bounce=5 bounceHz=1.8..2.2 tilt=-8..8 sway=3 wobble=0.05
hungry 0.3 0.4 : lookY=0.4 lookX=0.3 smile=0.3 open=0.3 tongue=1 browY=0.2
sneaky 0.3 0.3 : lidT=0.5 lidTA=8 lookX=s0.9 mX=-s0.35 smile=0.3 mW=0.5 glance=0 blink=8
brave 0.6 0.6 : browA=18 lidT=0.15 smile=0.4 mW=0.8 y=-6 fx=shine tint=255,200,90,0.3
amazed 0.7 0.8 : fx=sparkle size=1.12 pupil=0.8 browY=0.5 open=0.45 smile=0.3 wobble=0.03
awe 0.6 0.5 : glyph=teary fx=stars size=1.1 lookY=-0.5 open=0.35 smile=0 browY=0.45 glance=0
adoring 1 0.3 : glyph=teary fx=hearts blush=0.7 smile=0.8 tilt=s10 browA=-8 pulse=0.04
blissful 1 -0.4 : @closedCalm fx=flowers smile=0.9 blush=0.5 breathe=2 sway=2
content 0.6 -0.5 : @closedCalm smile=0.5 lidB=0.1 breathe=1.8
cozy 0.7 -0.6 : lidT=0.45 lidB=0.2 smile=0.5 blush=0.3 breathe=2.5 tint=255,200,150,0.3 glance=0.3
dreamy 0.6 -0.3 : lidT=0.35 lookY=-0.5 lookX=s0.3 smile=0.4 fx=stars sway=3 glance=0.2
daydream 0.4 -0.2 : fx=dots lookX=s0.6 lookY=-0.6 lidT=0.2 open=0.15 mW=0.3 glance=0
wistful 0.1 -0.4 : lookX=-s0.5 lookY=-0.3 lidT=0.25 browA=-10 smile=0.1 glance=0.2
lonely -0.7 -0.5 : lookY=0.6 browA=-18 lidT=0.3 smile=-0.4 tint=120,150,230,0.4 fx=cloud glance=0.2 nod=1.5
gloomy -0.7 -0.4 : fx=gloom lidT=0.45 browA=-14 smile=-0.5 tint=140,120,200,0.5 glance=0.1
grumpy -0.6 0.2 : lidT=0.4 lidTA=18 browA=22 smile=-0.6 mW=0.5 wavy=0.3 glance=0.2 pulse=0.02
furious -1 1 : fx=anger fx2=fire size=1.1 pupil=0.55 browA=36 lidT=0.3 lidTA=28 jitter=2 open=0.5 smile=-0.8 tint=255,40,30,1 pulse=0.05
irritated -0.5 0.4 : lidT=0.38 browA=18 lookX=s0.5 smile=-0.35 wavy=0.3 glance=0.1
frustrated -0.6 0.6 : glyph=chevron fx=steam smile=-0.6 wavy=0.6 jitter=0.8 pulse=0.03
jealous -0.5 0.3 : lidT=0.4 lookX=s0.8 mX=-s0.3 browA=12 smile=-0.4 tint=140,230,120,0.5 glance=0
guilty -0.5 0.2 : lookY=0.6 lookX=s0.4 browA=-16 smile=-0.2 wavy=0.5 fx=sweat glance=0.2
ashamed -0.7 -0.2 : lookY=0.8 lidT=0.35 browA=-18 blush=0.7 smile=-0.3 y=8 glance=0
sheepish 0.3 0.2 : @closedHappy fx=sweatbig smile=0.4 wavy=0.5 blush=0.6 tilt=s8
awkward 0 0.3 : fx=sweatbig smile=0.15 wavy=0.8 mW=0.9 lookX=s0.5 browA=-8
flustered 0.2 0.8 : glyph=spiral fx=steam blush=1 wavy=1 smile=0.2 jitter=0.6
panicked -0.8 1 : fx=sweat fx2=exclaim size=1.15 pupil=0.5 jitter=2.2 browA=-22 browY=0.5 open=0.6 wavy=1 wobble=0.06
terrified -1 1 : pick{glyph=dot|size=1.2 pupil=0.4} fx=gloom jitter=3 browA=-26 open=0.7 wavy=1 tint=170,200,255,0.5 wobble=0.07
anxious -0.5 0.6 : fx=sweat browA=-18 lookX=s0.4 smile=-0.2 wavy=0.8 jitter=0.6 sway=2
uneasy -0.3 0.3 : lookX=-s0.4 browA=-10 smile=-0.1 wavy=0.6 mX=s0.2
skeptical -0.2 0.2 : browAsym=s0.8 lidT=0.3 lookX=s0.3 smile=-0.1 mX=s0.3 mW=0.5
unimpressed -0.3 -0.3 : lidT=0.5 lidTA=2 smile=-0.05 mW=0.5 glance=0 blink=6
deadpan -0.1 -0.5 : glyph=line mW=0.6 smile=0 glance=0
sarcastic 0.2 0.1 : lidT=0.45 lidTA=-8 browAsym=s0.5 smile=0.45 mX=s0.4 lookX=-s0.4
sassy 0.5 0.4 : lidT=0.3 browAsym=-s0.6 tilt=s12 smile=0.6 mX=s0.35 lookX=s0.5
confident 0.7 0.3 : lidT=0.2 lidTA=-6 browY=0.1 smile=0.7 mW=0.8 y=-4 pulse=0.02
cocky 0.6 0.4 : lidT=0.35 lidTA=-10 browAsym=s0.5 smile=0.8 mX=s0.4 tilt=-s6
triumphant 1 0.7 : @closedHappy fx=shine smile=1 open=0.6 y=-8 bounce=4 bounceHz=1.8 tint=255,215,90,0.5 pulse=0.03
heroic 0.7 0.6 : browA=20 lidT=0.15 lookX=s0.3 lookY=-0.2 y=-8 fx=shine tint=255,210,90,0.4
silly 0.8 0.7 : sizeL+=s0.2 sizeR+=-s0.2 lookX=-s0.5 tongue=1 tilt=s14 sway=4 smile=0.7
goofy 0.8 0.6 : cross=0.8 tongue=1 smile=0.7 tilt=-8..8 bounce=2 bounceHz=2
derp 0.4 0.3 : sizeL+=s0.15 sizeR+=-s0.15 cross=0.6 open=0.3 smile=0.2 tongue=1 tilt=s10
zany 0.8 1 : size=1.12 jitter=1.5 sway=6 smile=1 open=0.6 wavy=0.5 fx=stars
woozy 0.3 -0.2 : lidT=0.45 cross=0.3 sway=8 blush=0.8 smile=0.4 wavy=0.6 fx=bubbles wobble=0.03
sick -0.7 -0.3 : tint=150,220,120,0.7 lidT=0.4 browA=-12 smile=-0.4 wavy=1 fx=sweat jitter=0.3
feverish -0.6 0 : tint=255,120,100,0.6 lidT=0.45 blush=1 fx=heat fx2=sweat open=0.2 mW=0.4
freezing -0.5 0.4 : tint=150,200,255,0.8 jitter=1.8 wavy=1 smile=-0.3 fx=ice
sneeze_ah 0 0.5 : lidT=0.3 lidB=0.1 browY=0.3 browA=-8 open=0.3 mW=0.4 smile=0 lookY=-0.3 y=-4 blink=0 glance=0
sneeze_ahh 0 0.7 : lidT=0.46 lidB=0.17 browY=0.45 browA=-12 open=0.5 mW=0.45 smile=0 lookY=-0.45 y=-8 jitter=0.2 squash=-0.04 blink=0 glance=0
sneeze_ahhh 0 0.85 : lidT=0.6 lidB=0.24 browY=0.55 browA=-16 open=0.66 mW=0.5 smile=0 lookY=-0.55 y=-12 jitter=0.45 squash=-0.07 blink=0 glance=0 wobble=0.03
sneeze 0 0.9 : glyph=chevron squeeze=1 open=0.85 mW=0.75 smile=0 y=10 squash=0.14 jitter=0.35 browY=-0.35 browA=-10 fx=achoo blink=0 glance=0
sneeze_after 0.2 -0.1 : lidT=0.35 lidB=0.1 smile=0.3 mW=0.4 blush=0.55 browA=-8 lookY=0.2 wobble=0.03 nod=1 fx=bubble
puffed -0.2 0.3 : puff=1 lidT=0.25 browA=12 smile=-0.1 mW=0.35 blush=0.35 wobble=0.03
waving 0.9 0.6 : @happyEyes smile=0.9 open=0.35 blush=0.4 tilt=s10 sway=4 bounce=3 bounceHz=1.6 fx=sparkle wobble=0.04
swimming 0.5 0.5 : puff=0.9 lidT=0.15 lookY=-0.2 smile=0.1 mW=0.3 fx=bubbles wobble=0.04 sway=3
straining -0.1 0.8 : glyph=chevron squeeze=0.6 browA=-14 wavy=0.8 smile=-0.2 mW=0.8 fx=sweat jitter=0.4 wobble=0.02
shaking 0.3 0.7 : @closedCalm jitter=1 wobble=0.08 smile=0.4 mW=0.6 fx=bubbles
pointing 0.6 0.5 : lookX=s1 browY=0.2 smile=0.6 mW=0.7 lidT=0.1 tilt=s8 fx=sparkle
bowing 0.7 -0.1 : @closedCalm smile=0.6 blush=0.5 lookY=0.6 y=6 fx=flowers
hiccup 0.1 0.4 : size=1.05 open=0.25 mW=0.3 fx=bubbles jitter=0.5
groggy -0.1 -0.6 : lidT=0.55 lidTA=-6 closeL=0.3 lookX=0.3 open=0.35 browAsym=0.3 mW=0.5 glance=0
startled -0.2 1 : fx=shock size=1.2 pupil=0.55 browY=0.8 open=0.5 jitter=1 wobble=0.05
flinch -0.5 0.8 : glyph=chevron smile=-0.5 wavy=0.5 jitter=1 tilt=-s10
peek 0.3 0.3 : closeL=1 closeCurve=-1 lookX=0.6 lidT=0.25 smile=0.2 mW=0.4
pleading 0.1 0.4 : glyph=teary browA=-22 browY=0.3 pupil=1.15 dilate=0.8 smile=-0.2 mW=0.4 tilt=s8
begging 0.3 0.5 : glyph=teary fx=sparkle browA=-20 browY=0.35 pupil=1.2 dilate=0.8 smile=0.2 mW=0.4
sulking -0.6 -0.2 : lidT=0.35 lookX=-s0.7 lookY=0.4 smile=-0.6 mW=0.5 wavy=0.3 browA=10 fx=cloud puff=0.4
tears_of_joy 1 0.7 : @closedHappy fx=cry smile=1 open=0.7 bounce=3 bounceHz=2 wobble=0.05
overwhelmed -0.4 0.8 : glyph=spiral fx=sweat wavy=1 sway=5 smile=-0.2
money 0.8 0.7 : glyph=dollar fx=money smile=1 open=0.5 tint=255,215,60,0.6
hypnotized 0 -0.2 : glyph=spiral sway=6 open=0.2 mW=0.4 tint=200,120,255,0.6 glance=0
lovestruck 1 0.5 : glyph=heart fx=hearts sway=5 blush=1 smile=0.7 wavy=0.3 tint=255,80,150,1 pulse=0.06
kiss 0.9 0.2 : @closedCalm open=0.22 mW=0.25 smile=0 blush=0.6 fx=kiss
blow_kiss 0.9 0.4 : pick{closeL=1|closeR=1} open=0.22 mW=0.25 smile=0 blush=0.5 fx=kiss
whistle 0.4 0.2 : lookY=-0.3 lookX=s0.3 lidT=0.2 open=0.2 mW=0.2 smile=0 fx=notes
singing 0.9 0.5 : @closedHappy open=0.6 smile=0.5 talk fx=notes sway=5 wobble=0.04
munching 0.7 0.3 : @closedHappy talk smile=0.4 mW=0.6 blush=0.3 wobble=0.03 puff=0.55
yum 0.9 0.4 : @closedHappy tongue=1 smile=0.8 fx=sparkle blush=0.4 wobble=0.04
drooling 0.5 0.3 : glyph=teary lookY=-0.2 lookX=s0.3 size=1.1 open=0.3 fx=drool
snoring 0.2 -1 : @closedCalm fx=bubble smile=0 open=0.25 mW=0.3 breathe=3 y=8 blink=0 glance=0
nerd 0.6 0.3 : acc=glasses smile=0.6 lookY=-0.1 ?0.5{fx=sparkle}
detective 0.2 0.3 : acc=monocle lidT=0.25 browAsym=0.6 lookX=s0.3 smile=0.1 mX=s0.3
pirate 0.6 0.5 : acc=patch lidT=0.2 smile=0.7 mX=0.3 browA=12
glitch -0.3 0.8 : fx=glitch jitter=2.5 tint=255,80,255,0.5 wavy=1 mW=0.8 blink=0
scanning 0.1 0.4 : glyph=target fx=scan smile=0 mW=0.4 blink=0 glance=0
processing 0.1 0.2 : fx=dots lidT=0.15 lookY=-0.5 pupil=0.85 mW=0.3 smile=0 glance=0
updating 0.2 0 : glyph=loading fx=progress mW=0 blink=0 glance=0
overheated -0.6 0.6 : tint=255,90,60,0.8 fx=steam fx2=fire lidT=0.4 wavy=0.6 jitter=0.6
frozen -0.4 -0.6 : tint=160,220,255,0.9 fx=ice size=0.95 mW=0.5 smile=-0.2 blink=0 glance=0
battery_full 0.8 0.3 : @happyEyes fx=battfull smile=0.8 tint=120,255,140,0.4
wifi_lost -0.4 0.2 : fx=wifix browA=-14 smile=-0.4 lookY=-0.4 tint=180,180,210,0.4
notification 0.3 0.6 : fx=exclaim size=1.05 browY=0.3 smile=0.2 mW=0.4
party 1 1 : @closedHappy fx=confetti open=0.6 smile=1 bounce=6 bounceHz=2.4 sway=4 wobble=0.06
crabrave 1 1 : glyph=shades fx=notes fx2=confetti smile=1 open=0.55 mW=0.9 bounce=6 bounceHz=2.05 sway=4 tilt=-6..6 tint=190,80,255,1 wobble=0.05 blink=0 glance=0
crabrave_teal 1 1 : glyph=star fx=notes fx2=sparkle smile=1 open=0.6 mW=0.9 bounce=6 bounceHz=2.05 sway=4 tilt=-6..6 tint=40,235,220,1 wobble=0.05 blink=0 glance=0
crabrave_red 1 1 : glyph=star fx=confetti fx2=notes smile=1 open=0.6 mW=0.9 bounce=7 bounceHz=2.05 sway=4 tilt=-6..6 tint=255,80,45,1 wobble=0.05 blink=0 glance=0
crabrave_drop 1 1 : @closedHappy fx=confetti fx2=sparkle smile=1 open=0.75 mW=1 bounce=8 bounceHz=2.05 sway=5 tilt=-8..8 tint=255,215,60,0.6 wobble=0.06 blink=0 glance=0
rainy_day -0.4 -0.3 : lookY=-0.6 fx=cloud smile=-0.3 browA=-10
sunny 0.9 0.5 : @happyEyes fx=shine smile=0.9 tint=255,220,90,0.5
angel 0.8 -0.3 : @closedCalm fx=halo smile=0.5 blush=0.3 tint=255,240,190,0.4
devil 0.4 0.6 : fx=horns lidT=0.35 lidTA=20 smile=0.9 mX=0.3 tint=255,70,60,0.4
speechless -0.1 0.5 : glyph=dot fx=dots mW=0
eye_roll -0.4 0 : lookY=-1 lidT=0.35 smile=-0.2 mX=s0.3 glance=0
side_eye -0.2 0.2 : lookX=s1 lidT=0.35 smile=-0.1 mX=-s0.3 browA=10 glance=0
giddy 1 0.9 : @closedHappy bounce=6 bounceHz=3 blush=0.8 smile=1 open=0.4 ?0.5{fx=hearts} wobble=0.06
bashful 0.5 -0.2 : lookY=0.6 lookX=s0.3 blush=1 fx=flowers smile=0.4 tilt=s10 closeL=0.3
impatient -0.4 0.5 : lidT=0.3 jitter=0.4 sway=2 smile=-0.3 mX=0.2 fx=dots
stubborn -0.4 0.3 : @closedCalm smile=-0.5 mW=0.4 wavy=0.3 browA=14 tilt=-s8 puff=0.8
defeated -0.8 -0.6 : lidT=0.5 lookY=0.8 browA=-18 y=10 smile=-0.6 fx=gloom tint=150,150,170,0.5
exhausted -0.5 -0.9 : lidT=0.7 open=0.4 talk fx=sweat y=8 browA=-10
hyper 0.9 1 : size=1.15 pupil=0.6 jitter=1 bounce=8 bounceHz=3.5 smile=1 open=0.6 fx=sparkle fx2=bolt wobble=0.07 pulse=0.03
peaceful 0.6 -0.7 : @closedCalm smile=0.35 fx=flowers breathe=3
nostalgic 0.3 -0.3 : glyph=teary lookX=s0.5 lookY=-0.4 smile=0.3 tint=255,200,150,0.4
)LIB";
