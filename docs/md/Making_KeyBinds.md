# Making your OWN custom keybind

I am 99% sure that you would now like to make your own keybinds, probably with thoughts in your head what to make! I am going to guide you step by step to edit the *winbind.conf* for creating keybinds.

## Learning the pre-existing functions

Format: MODIFIERS+KEY = ACTION [ARG]<br>
Modifiers: WIN, CTRL, ALT, SHIFT (combine with +)<br>
Actions: SWITCH <N>   - Switch to virtual desktop N<br>
MOVE <N>     - Move foreground window to desktop N<br>
MOVE_SWITCH <N> - Move window AND switch to desktop N<br>

This is also given in the *.conf* for any time reference.

## Using the pre-existing functions

When you open the *.conf*, you will see three implemented functions, but DISABLED.

'''
Virtual_Desktop_Switch=Disabled<br>
New_Desktop_Creation=Disabled<br>
Auto_Remove_Virtual_Desktop=Disabled<br>
'''

Well, these are for YOU to enable, it is completely your choice. No stress.

Well if you enable, and see the next few lines, you will notice this:

'''
WIN+1 = SWITCH 1<br>
WIN+2 = SWITCH 2<br>
WIN+3 = SWITCH 3<br>
WIN+4 = SWITCH 4<br>
WIN+5 = SWITCH 5<br>
WIN+6 = SWITCH 6<br>
WIN+7 = SWITCH 7<br>
WIN+8 = SWITCH 8<br>
WIN+9 = SWITCH 9<br>
'''

This is the key for switching from one virtual desktop to another.

You can edit it, keeping in mind the Important Notes to not conflict with the OS.

You will also see this when you scroll down:

Window_And_Virtual_Desktop_Switch=Disabled

Enable this to have Window+Desktop Switch at the same time(keep reading to check the explanation out{if you didn't understand}).

Here are the list of keybinds you will see on the screen:

'''
WIN+SHIFT+1 = MOVE_SWITCH 1<br>
WIN+SHIFT+2 = MOVE_SWITCH 2<br>
WIN+SHIFT+3 = MOVE_SWITCH 3<br>
WIN+SHIFT+4 = MOVE_SWITCH 4<br>
WIN+SHIFT+5 = MOVE_SWITCH 5<br>
WIN+SHIFT+6 = MOVE_SWITCH 6<br>
WIN+SHIFT+7 = MOVE_SWITCH 7<br>
WIN+SHIFT+8 = MOVE_SWITCH 8<br>
WIN+SHIFT+9 = MOVE_SWITCH 9<br>
'''

This is to MOVE the window WITH moving the Virtual Desktop.

Imagine, you are on VD2, and you want to switch the window to VD3, and go to VD3 at the same time. Now the shortcut would make sense to you!

You can edit these at your wish, like from WIN+SHIFT+2, you can edit to, WIN+CTRL+2, etc.

## Making your own keybinds

I think it would be your most favorite section over here, making your own keybinds!

An example has been commented, to illustrate how to use the keybinds:

'''CTRL+SHIFT+V = SIMULATE_KEY[CTRL+V]'''

Well, this examle shows you to add your own keybind for doing another key simulation.

Lets say, your a linux user. You keep doing CTRL+SHIFT+V, instead of CTRL+V. You want, whenever CTRL+SHIFT+V is pressed, the command actually simulates to the system to believe that you presses CTRL+V.

![NOTE]
> Some keys you simulate, like CTRL+SHIFT+V are maybe taken by an Application and the app would not allow you to send CTRL+SHIFT+V to the app. It will only send CTRL+V(There is a workaround this problem, disable the app temorarly using a custom shortcut and start it back again when needed or just remove the simulation).

There is one more too!

Wouldn't it be nice to start APPLICATIONS by mere shortcuts? Or run .bat scripts? That's what I thought and created this example:

'''CTRL+SHIFT+P = RUN('vscode.exe', '--safe-mode')'''

This allows you to run apps OR batch scripts with ARGUMENTS and with CUSTOM shortcuts. Sometimes, even I wonder how far good can the application go! It is LOVELY to have this kind of feature! It saves me time too everyday!

Now it is time for another cool feature! Transparency to the Windows, with adjustable opacity!

<div align="right">[Next -> Transparency.md)</div>
