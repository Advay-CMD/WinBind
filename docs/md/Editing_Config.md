# Editing Configuration file the right way
Most of the people will not even know there was a *.config* file 🤣.
<br>
Ok, jokes apart, lets see how to edit the *.config*.

## Place winbind.conf resides
It is very important to know where the *winbind.conf* is stored, or else it is of no use to use this application. ALL the options are DISABLED/Set to OS defaults. You need to make edits to the *winbind.conf*, in order to personalise as well as use the application to the maximum benfit.
<br><br>
It resides in:<br>
'''TODO -> path'''

## With great power comes great responsibility
It is true that, if not handled carefully, this app CAN cause temporary damage to your PC, ultimately making you disable this program in the Safe Mode, though it doesn't require admin permissions(Till Now).
<br><br>
Let's learn few things we DON'T have to enable in the *winbind.conf*, and I recommend you NEVER do.
<br><br>
'''Allow_Bad_Keys=Disabled'''
<br><br>
I don't recommend you to switch this thing on. Especially if you DON'T know computers.
<br>
I have kept it anyway, if anyone feels adventureous...
<br><br>
Commands BLOCKED:<br>
''' Blocked by default: CTRL+C/X/V/Z/Y/A/S/O/P/F/H/N/W/T, ALT+F4/TAB/ESC/SPACE/ENTER,WIN+D/E/I/L/R/V/TAB/SPACE/PAUSE/A/B/G/H/K/M/O/P/S/T/U/W/X/Z, WIN+LEFT/RIGHT/UP/DOWN, WIN+SHIFT+S, CTRL+ALT+DEL, CTRL+SHIFT+ESC '''
<br><br>
It is a exaustive list, but it is to prevent the user from messing up with the Operating System
<br><br>
## Editing the configuration file
Many things are already written in the configuration to prevent the user from messing up with the configuration. Seriously, the *.conf* is NOT a place to mess up, so there are MULTIPLE WARNINGS!
<br><br>
It is already written in the *.conf* what has to be done, example for the KeyBind *configuration*, it is given like this:<br><br>
'''
Format: MODIFIERS+KEY = ACTION [ARG]
Modifiers: WIN, CTRL, ALT, SHIFT (combine with +)<br>
Actions: SWITCH <N>   - Switch to virtual desktop N<br>
          MOVE <N>     - Move foreground window to desktop N<br>
          MOVE_SWITCH <N> - Move window AND switch to desktop N<br>
Comments start with # 
'''
<br><br>
This will be written in the *.conf*, but don't worry, if you have questions/doubts, I am also going to explain in EACH section wether it be Control/Personlization, I am going to guide you to make the *.conf* program.

<div align="right">[Next -> Tweaking With KeyBinds](tweaking_keybinds.md)</div>
