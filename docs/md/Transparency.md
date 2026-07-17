# Make Windows Transparent!
Yes, make your windows transparent! Don't let it be a block of ice. In WinBind, you would find it really easy to configure transparency. It is just 3 edits (max) away.

## Learning the functions
```
Actions:
Transparency = Enable/Disable
Transparency_Delay = Time_Delay(preferably in s)
Opacity = Percent_Value(include %)
```
This is already given in the *.conf* file for anytime reference.

## How to enable transparency

Just change:
```Transparency = Disabled``` to ```Transparency = Enabled```.

This will allow the program to set up transparency for windows.

But wait. You have forgot to set the transparency!

If you don't, then the windows will stay 100% opaque.

Lets learn how to enable.

## How to change opacity

Just change:
```Opacity = 100%``` to ```Opacity = (whatever percentage)%```. That's it.

## Change the delay time

Delay time is what you can say *refresh rate*. It is the time when the program makes a delay before eyeing all windows and applying the style.
More the delay, lesser the consumption of CPU and RAM.

The default is:
```Transparency_Delay = 1s```

Feel free to edit and change it to a higher value or a lower value.

## Exclusions
Let's say you have a game. You don't want to make it transparent. You can add this to the exclusion list: thoughtfully designed to solve this major problem.

For now, default is:
```TransparencyExclusion = ["explorer.exe", "ShellHost.exe"]```

Feel free to remove or add.
<hr>

There are more suprises yet to come! We will now see WindowStyler to style windows to your preference. Exciting right? Keep up with the documentation!

<div align="right">[Next -> WindowStyler.md]</div>
