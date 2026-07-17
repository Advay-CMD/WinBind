# Window Styler

You work with various kinds of windows, everyday.
A window is basically the browser you view this content on. Any app that is not hidden and visible on the desktop is a window.

Unfortunately, this app is not a window 🤣. It silently runs in the background. However, it can style your other windows.

## Learning the functions

Here are some actions for customizing the window:

```
StealFocus = Enabled/Disabled (hover = auto-focus, like Linux)

ChangeBorderColor = [R,G,B]  (0-255 each, e.g. [255,0,0] = red)

ChangeTitleBarColor = [R,G,B] (0-255 each)

DarkModeTitleBar = Enabled/Disabled

SystemBackdrop = 1/2/3/4  (1=Auto, 2=None, 3=Mica, 4=Acrylic)

WindowStyler_Delay = Time (e.g. 2s, 1000ms, 3000)
```

## Enabling the StealFocus

What is StealFocus? Why to use them at all? This are the questions I would be asked by you.

Actually, it is very simple. Imagine you have 2 windows, stacked split screen. Now you work on App1 and suddenly change to App2. Now the OS would require you to click App2 before it emerging in focus. This will allow you to activate a window WITHOUT clicking, much like Linux.

You would want to use this if you keep changing the location of the cursor from one window to other, and don't want to waste the time in clicking.

Here is how to enable: ```StealFocus = Enabled```

That's it.

## Changing the border color

Border is the grey line that runs at the sides and corners of your window, whenever it floats(not maximized).

The color is a RGB value(hope you know that).

Use: ```ChangeBorderColor = [0,100,255]``` to change the color.

The actual implementation is: ```ChangeBorderColor = [R,G,B]```. Feel free to use this site https://rgbcolorpicker.com/ so that you can easily pick the color, with having the translated values.

Set an exclusion to an application you don't want to have the border color.

Use: ```ChangeBorderColorExclusion = []``` (Hint: Remmber the translucentcy Exclusion? Use the same concept.)

## Changing the title bar color

I think everyone knows what a title bar is.

Use: ```ChangeTitleBarColor = [50,50,50]```

Again the values have to be in RGB, the same site can be referred.

Again for exclusion use: ```ChangeTitleBarColorExclusion = []```

## Dark Mode Title Bar

Changes the title bar of apps to dark black. Ex: Many app popups are often coded in Win32, so it changes their title bar white text and dark bar.

Use: ```DarkModeTitleBar = Enabled```

Again use ```ChangeDarkModeTitleBarExclusion = []``` for exclusions.

## Changing the system backdrop

Sometimes the application can make all eligible apps change their title bar/window to a system backdrop.

1 = Auto (let Windows decide) - Usually this will not give the desired result. Windows doesn't do anything much here.

2 = None - Have NO systembackdrop

3 = Mica - For having slight blur effect known as mica.

4 = Acrylic - More solid than mica. Useful in some cases

Set it up like: ```SystemBackdrop = 3```

To exclude an app use: ```SystemBackdropExclusion = []```

## Delay time

Like Translucentcy program, you have an option to set how much delay you want. Lesser the delay, lesser the time the application takes to style the window.

Use: ```WindowStyler_Delay = 500ms```

------

I am sure, if you paid close attention, your windows would look better then anyone else's. Anyway, we are still on the trip. It's not over. The best features are yet to come. Want to set up a soft music in the background? Don't worry, I have got you covered! Keep reading!

<div align="right">[Next -> BackgroundMusic.md]</div>
