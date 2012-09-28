#include "process.h" // Mandatory module for an application main file (provides main entry point for C runtime)
#include "string.h" // Message formatting (i.e concatenation and automatic type conversion to string) is implemented by the variadic template function str(Args...)
#include "window.h" //Window
#include "text.h" //TextInput
#include "time.h" //Date
#include "parser.h"

/// Demonstrates basic usage of Serenity C++ framework
struct Examples {
    Examples() {
#if 0
        /// 1) Outputs a message to standard output
        log("Hello World!");

        /// 2) Outputs sizes of integer and floating-point types
        // C types: size are not explicit in the name, signedness of char depends on implementation, long is platform dependent (size of a pointer on linux, always 32bit on windows)
        log("char",sizeof(char),"short",sizeof(short),"int",sizeof(int),"long",sizeof(long),"long long",sizeof(long long)); // unsigned types prefixed with "unsigned "
        // core.h provides types with explicit size
        log("int8",sizeof(int8),"int16",sizeof(int16),"int32",sizeof(int32),"int64",sizeof(int64)); // unsigned with u (e.g uint8)
        log("float",sizeof(float),"double",sizeof(double),"long double",sizeof(long double));

        /// 3) Inputs one character and outputs the character in uppercase
        char c=0; // this variable will contain the input character
        TextInput input; focus=&input; Window window(&input, int2(640,480), "Type a letter"_);
        window.localShortcut(Escape).connect(&exit);
        input.textChanged.connect([&c](const ref<byte>& text) { if(text) c=text[0]; });
        while(c<'a' || c>'z') if(mainThread().processEvents()) return; // Synchronously process events (would run in main in a normal application)
        char u=c+'A'-'a'; // Converts to uppercase
        log(u);

        input.text.clear(); input.textChanged.clear();

        /// 4) Inputs a date and outputs the date + 30min
        Date date; // this variable will contain the date
        window.setTitle("Type a time"_);
        input.textChanged.connect([&date](const ref<byte>& text) { date=parse(text); log(text,date); });
        while(date.hours<0 || date.minutes<0) if(mainThread().processEvents()) return;
        log(Date(date+30*60));

        input.text.clear(); input.textChanged.clear();
#endif
        /// 5) Fills an array with integers from 9 to 0
        array<int> a;
        for(int i: range(10)) a<< 9-i;
        log(a);

        /// 6) Computes integer arithmetic expression


        //2.1 à 2.7, 2.13, 2.15, 4.1, 4.3 à 4.5, 5.1 à 5.3, 5.5, 6.1, 6.2, 6.4, 7.1, 7.2, 7.3, 7.5, 7.6
    }
} application; //global variables are registered for construction by the compiler before entering main()
