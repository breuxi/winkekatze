## ESP8266 enabled lucky cat

Code derived from https://github.com/themad/jimmykater

Changes:
* Keeps lights on. So, continiuous lighting.
* More light effects
* Keeps original waving mechanism. ( Instead of using a Servo )

MQTT Topics:

* catname/eye/mode/set   [solid|warp|blink]
* catname/eye/speed/set  [slow|med|fast]
* catname/eye/color/set  [dark|white|red|green|blue|yellow|cyan|pink]
* catname/paw/command    [wink|nowink]



