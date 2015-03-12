#pragma once
/// Key symbols
enum Key {
#if X
	Space=' ',
	Escape=0xff1b, Backspace=0xff08, Tab, Return=0xff0d,
	Home=0xff50, LeftArrow, UpArrow, RightArrow, DownArrow, PageUp, PageDown, End, PrintScreen=0xff61,
	Execute, Insert,
	KP_Enter=0xff8d, KP_Asterisk=0xffaa, KP_Plus, KP_Separator, KP_Minus, KP_Decimal, KP_Slash, KP_0,KP_1,KP_2,KP_3,KP_4,KP_5,KP_6,KP_7,KP_8,KP_9,
	F1=0xffbe,F2,F3,F4,F5,F6,F7,F8,F9,F10,F11,
	ShiftKey=0xffe1, ControlKey=0xffe3,
	Delete=0xffff,
	Play=0x1008ff14, Media=0x1008ff32
#else
	None, Escape, _1,_2,_3,_4,_5,_6,_7,_8,_9,_0, Minus, Equal, Backspace, Tab, Q,W,E,R,T,Y,U,I,O,P, LeftBrace, RightBrace, Return, LeftCtrl,
	A,S,D,F,G,H,J,K,L, Semicolon, Apostrophe, Grave, LeftShift, BackSlash, Z,X,C,V,B,N,M, Comma, Dot, Slash, RightShift, KP_Asterisk, LeftAlt,
	Space, KP_7 = 71, KP_8, KP_9, KP_Minus, KP_4, KP_5, KP_6, KP_Plus, KP_1, KP_2, KP_3, KP_0, KP_Slash=98,
	Home=102, UpArrow, PageUp, LeftArrow, RightArrow, End, DownArrow, PageDown, Insert, Delete, Macro, Mute, VolumeDown, VolumeUp,
	Power=116
#endif
};
enum Modifiers { NoModifiers=0, Shift=1<<0, Control=1<<2, Alt=1<<3, NumLock=1<<4};

/// Mouse event type
enum Event { Press, Release, Motion, Enter, Leave };
/// Mouse buttons
enum Button {
	NoButton,
#if X
	LeftButton, MiddleButton, RightButton, WheelUp, WheelDown
#else
	LeftButton=0x110, RightButton, MiddleButton, WheelDown=0x150, WheelUp,
#endif
};
