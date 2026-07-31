# Loading custom *.conf* files

Now, I am sure you might wanna have a little fun. While Gaming you want a different mouse than you have right now, in office you might want DND on, in casual you might want to change the wallpaper, and you want to have all of this in a go. Well, if you do it manually, it will take a LOT of time, as you have to create new methods *.conf* files again and again.

This method implementation helps you to Load Custom *.conf* files on the go with a shortcut.

## Understanding and Using LoadConf[]

This is a method, that helps you to load custom *.conf* with a shortcut.
It starts like this.

```
                    Default(winbind.conf)
Gaming(gaming.conf)     Casual(casual.conf)      Work(work.conf)
```

Now WinBind wants you to make the main *winbind.conf* the center default *.conf* that you use MOST of the time.
If you are in the office and wants to work, you will want to load another *.conf* YOU made for Work, called *work.conf*

Now if you want to load *work.conf* with your shortcut you have to do:

```CTRL + SHIFT + W = LoadConf["work.conf"]```

That is how WinBind wants you to work.

![WARNING]
>Please set a tunnel back to the MAIN *winbind.conf*. If you don't, you would be stuck with the loaded *.conf* till WinBind is restarted. Therefore, include an implementation ```CTRL + SHIFT + W = LoadConf["winbind.conf"]```, preferably the same shortcut used for load of the script. This way, that shortcut key will act like a toggle switch.

![WARNING]
>Please, Include the other .conf too in the scripts. Lets say you from *winbind.conf* go to *work.conf*. If you don't have a implementation to go to *casual.conf* in your *work.conf*, it is impossible to go to *casual.conf*. You will have to go to *winbind.conf*(where the implementation IS there) to go to *casual.conf*.

Yes, after reading those blocks, you would know, this feature requires a little presence of brain, it requires you to draw a simple mindmap in mind, but reality, it is simple. Just requires a little logic.

<div align="right">[Next -> Transparency.md]</div>
