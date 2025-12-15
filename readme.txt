Recaps Switcher
------------------------------------

This simple program allows you to switch between languages using only one reconfigurable "Cycle Switch" hotkey - like the CapsLock one that is set by default.

If you accidentally typed some text in the wrong layout, there is a "Convert Inplace" hotkey that helps to fix it.

If you have more than two languages installed, you can configure ones you want to switch between into groups, which you can cycle through both with the group hotkey and with the main "Cycle Switch" hotkey.

Additional indication about currently active layout is the country flag in the application icon, ScrollLock keyboard LED and on-screen overlay indicator.

Yet another hotkey may be assigned to execute optional "Eject CD" action.

Enjoy!


Revision history
------------------------------------
Version 0.9.00.3850

	Provide both full locale name and keyboard layout name in configuration.
	Restore hot-track cursors handling on Settings' dialog list view headers.
	Fix closing overlay hint configuration dialog on Esc and Close caption button.
	Use complete language names instead of abbreviated in Group list view columns.
	Fix text alignment in custom-drawn headers.
	Allow resizing of Settings dialog width.
	Enable clipping owner drawn list entries during drawing.
	Make correct scaling of configuration UI; using System-wide font size settings.
	More flags were added. Based on download statistics.

Version 0.9.00.3676

    Implement some rudimentary reference for clickable areas in Settings configuration window;
    Prevent opening for multiple Settings configuration dialogs;
    Use Keyboard Layout names instead of Language Names in configuration;
    Fix processing "Toggle" action hotkey if CapsLock was configured as group hotkey;
    Fix restoring disabled state for "Cycle" Action on application restart;
    Improve saving activated group. Use the latest group on system restart;
    Display overlay hint on all monitors in multi-monitor systems;
    Implement flexible configuration for overlay hint fade-off parameters;
    Prevent auto-closing hotkey dialog if hotkey contains "Space" key;
    Extended set of available national flags, based on Recaps download statistics;

Version 0.9.00.3480  

	Context menu driven configuration replaced with configuration dialog;
	Support for multikey hotkeys - using modifers like Control, Shift, Alt etc.;
	Added possibility to build groups of keyboard layouts. Every group has its own hotkey to switch on it and may be iteratively switched using main cycle switching hotkey;
	Implement on-screen overlay indicator of the currently active keyboard layout. It is a corresponding flag icon;
	Use 32 x 32 pixels icons instead of 16 x 16 old ones. A lot of new country flags added into installation package;
	Monitor HKCU\Keyboard Layout\Preload registry key to keep list of locales consistency;
	Improve setup package - add support for non-administrative installation mode;
	
Version 0.9.00.400  

    Fix crash on Ctrl-[Key] inplace text conversion;

Version 0.9.00.376  

    Native support for Windows x64;
    Customizable country flags in application icon;
    Optional "Eject CD" action may be assigned to some key;
    Option "Run at user log on" implemented;
    ScrollLock keyboard LED may be used for indication;
    Support Switch and Eject CD hotkeys customization;

Siarzhuk Zharski
http://recaps.sourceforge.io



Revision history
------------------------------------
0.6	Added conversion of text typed with the wrong layout 
	using Ctrl-CapsLock. Alt-CapsLock now changes the old 
	CapsLock mode.

0.5	Fixed selected languages configuration not being saved 
	in the registry.

0.4	Fixed language not being switched in some applications.

0.3	Added a tray icon, a menu and support for more than two 
	languages. Recaps now cycles between the lanugages selected
	in the menu.

0.2	Changed Shift-CapsLock to change language as well as CapsLock.
	Ctrl-CapsLock now changes the old CapsLock mode.

0.1	Initial release

Eli Golovinsky
http://www.gooli.org/

