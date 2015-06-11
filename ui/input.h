#pragma once
/// Key symbols
enum Key {
	Space=' ',
	Escape=0xff1b, Backspace=0xff08, Tab, Return=0xff0d,
	Home=0xff50, LeftArrow, UpArrow, RightArrow, DownArrow, PageUp, PageDown, End, PrintScreen=0xff61,
	Execute, Insert,
	KP_Enter=0xff8d, KP_Asterisk=0xffaa, KP_Plus, KP_Separator, KP_Minus, KP_Decimal, KP_Slash, KP_0,KP_1,KP_2,KP_3,KP_4,KP_5,KP_6,KP_7,KP_8,KP_9,
 F1=0xffbe,F2,F3,F4,F5,F6,F7,F8,F9,F10,F11,F12,
	LeftShift=0xffe1, RightShift=0xffe1, LeftControl=0xffe3,
	Delete=0xffff,
	Play=0x1008ff14, Media=0x1008ff32
};
enum Modifiers { NoModifiers=0, Shift=1<<0, Control=1<<2, Alt=1<<3, NumLock=1<<4};

/// Mouse event type
enum Event { Press, Release, Motion, Enter, Leave };
/// Mouse buttons
enum Button { NoButton, LeftButton, MiddleButton, RightButton, WheelUp, WheelDown };
