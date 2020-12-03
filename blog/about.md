# Compiling World's First JavaScript Engine Back to JavaScript
In 1995, when I was just one year old, a man across the ocean named Brendan Eich, created a programming language in ten days that I make my living with today, JavaScript.

The story of the rapid creation of JavaScript is well known among the programmer communities. But perhaps not many people today could remember (or even experience) what the "original JavaScript" was like, let alone reading the code of the JS engine back then.

In 2020, however, we have an opportunity to learn more about this history. At the HOPL-IV, a conference about the history of programming languages, [JavaScript The First 20 Years](https://doi.org/10.1145/3386327), co-authored by Brendan Eich and ES6 lead author Allen Wirfs-Brock, provides a detailed history of the birth and evolution of JavaScript. As the translator of the Chinese version of this book, I proofread each of the more than 600 reference links in the original edition, and one of them pointed to the source code of the earliest JS engine. This inspired my curiosity - could the earliest JS engine code still be compiled and run today? If possible, could I take it a step further and compile it back into JavaScript, bringing it back to life on the Web? So I made this attempt.

The earliest JS engine was called Mocha (the codename for Netscape's internal web scripting language project), its first prototype was written by Eich in May 1995. Throughout 1995 and most of 1996, Eich was the only full-time developer working on the JavaScript engine. Mocha's codebase was still mainly comprised of code from this prototype, until the release of Netscape 3.0 in August 1996. The JS version released with Netscape 3.0 was called JavaScript 1.1, which marked the completion of the initial development phase of JavaScript. After that, Eich spent another two weeks rewriting Mocha to get a more powerful engine, which is today's Firefox SpiderMonkey.

If you google "Netscape source code", you will probably only find the SpiderMonkey engine's source code from the 1998 Mozilla project. The actual source code of Mocha engine is located in an (unidentified) Netscape 3.0.2 browser source tarball on the web. But how can Mocha be resurrected when its source code has been completely abandoned?

In fact, there are always two ways to understand any software: "top-down" and "bottom-up". The former is to gain macro knowledge at the architecture level, while the latter is to solve micro problems at the code level. Since I'm already familiar with using JS engines like QuickJS, I've chosen to go straight to the bottom-up approach. The basic idea is simple: **Progressively port each module of the engine, eventually put it all together and run it**.

The original Mocha used Makefile for the build system, but it clearly doesn't work anymore on today's OS platform - that was the era when MacOS was still using PPC processors! But ultimately, the build system is just an auxiliary tool that automatically executes compilers like `gcc` and `clang`. The compilation process in C projects can be summarized as following:

1. Using the `gcc -c` command, compile the `.c` source codes that are "used as library" into `.o` object files, one after another. This compiles every function in the C source code into so-called "symbols" in the binary executable, just like the functions that come out of `export` in ES Module. Note that at this point, the APIs of other libraries included in the `.h` files can be called arbitrarily from each object file. This would not lead to compilation errors, the calls to external symbols are only recorded in the object files.
2. Using the `ar` command to make these `.o` object files into a static library in `.a` format. The resulting `.a` file will contain all of the symbols in the project, similar to the effect of `cat *.js >> all.js`. We can also make dynamic libraries for saving space, but they'll make things a bit more complicated, so we'll skip them here.
3. Using the `gcc -l` command to compile the `.c` source code that "calls this library", this will link its output against the `.a` static library. The linker will link the symbolic dependencies just like matching "tenons" in each object file. For each object file in the first step, each of the symbols inside calling external APIs must be found by the linker, and any missing symbol will cause an link time error - but as long as the linking phase succeeds, we end up with an executable file with `main` function as the entry point.

Thus, the entire progressive porting process looks like this:

1. Compile each Mocha's `.c` source file (i.e., except for the entry), getting an object file in `.o` format that contains its symbols.
2. Combine the `.o` object files containing these symbols, packaging them into an `.a` static library file, `libmocha.a`.
3. Compile the `mo_shell.c` entry file, linking it against the `libmocha.a` static library for the final executable file.

There are a number of dependencies that need to be addressed in this process, the most typical of which is the dependency to `prxxx.h`. This is the [Netscape Portable Runtime](https://developer.mozilla.org/en-US/docs/Mozilla/Projects/NSPR/About_NSPR) cross-platform library developed by Netscape back in the day, whose source code is also included in the Netscape 3 source tree. But I didn't commit all of it at once into the new ported Mocha code base. The approach here is to manually bring in the involved NSPR header files and corresponding source codes recursively, only when a missing NSPR dependency is encountered. Thus we can stripping out a minimally usable Mocha source tree in this way.

The source code changes involved in the migration process are mainly listed as below:

* Removed `prcpucfg.h`, using the little endian byte order for x86 and WASM.
* Revised type definitions in `prtypes.h`, replacing types with potential compatibility issues like `unsigned short` with types like `uint16_t` in the C99 standard, and similarly with `Bool` types.
* Added `MOCHAFILE` macro to force Mocha entering CLI mode that reads file, instead of the embedded mode used in the Netscape browser.
* Added `include` reference that was missing in some code.

In the end, I managed to compile all of Mocha's modules with a very simple bash script. I'm sure that a few days of serious C language learning would be adequate to understand it:

``` sh
function compile_objs() {
    echo "compiling OBJS..."
    $CC -Iinclude src/mo_array.c -c -o out/mo_array.o
    $CC -Iinclude src/mo_atom.c -c -o out/mo_atom.o
    $CC -Iinclude src/mo_bcode.c -c -o out/mo_bcode.o
    $CC -Iinclude src/mo_bool.c -c -o out/mo_bool.o
    $CC -Iinclude src/mo_cntxt.c -c -o out/mo_cntxt.o
    $CC -Iinclude src/mo_date.c -Wno-dangling-else -c -o out/mo_date.o
    $CC -Iinclude src/mo_emit.c -c -o out/mo_emit.o
    $CC -Iinclude src/mo_fun.c -c -o out/mo_fun.o
    $CC -Iinclude src/mo_math.c -c -o out/mo_math.o
    $CC -Iinclude src/mo_num.c -Wno-non-literal-null-conversion -c -o out/mo_num.o
    $CC -Iinclude src/mo_obj.c -c -o out/mo_obj.o
    $CC -Iinclude src/mo_parse.c -c -o out/mo_parse.o
    $CC -Iinclude src/mo_scan.c -c -o out/mo_scan.o
    $CC -Iinclude src/mo_scope.c -c -o out/mo_scope.o
    $CC -Iinclude src/mo_str.c -Wno-non-literal-null-conversion -c -o out/mo_str.o
    $CC -Iinclude src/mocha.c -c -o out/mocha.o
    $CC -Iinclude src/mochaapi.c -Wno-non-literal-null-conversion -c -o out/mochaapi.o
    $CC -Iinclude src/mochalib.c -c -o out/mochalib.o
    $CC -Iinclude src/prmjtime.c -c -o out/prmjtime.o
    $CC -Iinclude src/prtime.c -c -o out/prtime.o
    $CC -Iinclude src/prarena.c -c -o out/prarena.o
    $CC -Iinclude src/prhash.c -c -o out/prhash.o
    $CC -Iinclude src/prprf.c -c -o out/prprf.o
    $CC -Iinclude src/prdtoa.c \
        -Wno-logical-not-parentheses \
        -Wno-shift-op-parentheses \
        -Wno-parentheses \
        -c -o out/prdtoa.o
    $CC -Iinclude src/log2.c -c -o out/log2.o
    $CC -Iinclude src/longlong.c -c -o out/longlong.o
}
```

With the compiler warnings thrown during this process, I've also seen some surprising code. For example, this one in `mo_date.c`:

``` c
if (i <= st + 1)
    goto syntax;
for (k = (sizeof(wtb)/sizeof(char*)); --k >= 0;)
    if (date_regionMatches(wtb[k], 0, s, st, i-st, 1)) {
        int action = ttb[k];
        if (action != 0)
            if (action == 1) /* pm */
                if (hour > 12 || hour < 0)
                    goto syntax;
                else
                    hour += 12;
            else if (action <= 13) /* month! */
                if (mon < 0)
                    mon = /*byte*/ (action - 2);
                else
                    goto syntax;
            else
                tzoffset = action - 10000;
        break;
    }
if (k < 0)
goto syntax;
```

I also found some code that exemplifies the chaotic compatibility issues of 1995. They give me a better understanding of why people at the time were expecting the "write once, run everywhere" Java:

``` c
#if defined(AIXV3)
#include "os/aix.h"

#elif defined(BSDI)
#include "os/bsdi.h"

#elif defined(HPUX)
#include "os/hpux.h"

#elif defined(IRIX)
#include "os/irix.h"

#elif defined(LINUX)
#include "os/linux.h"

#elif defined(OSF1)
#include "os/osf1.h"

#elif defined(SCO)
#include "os/scoos.h"

#elif defined(SOLARIS)
#include "os/solaris.h"

#elif defined(SUNOS4)
#include "os/sunos.h"

#elif defined(UNIXWARE)
#include "os/unixware.h"

#elif defined(NEC)
#include "os/nec.h"

#elif defined(SONY)
#include "os/sony.h"

#elif defined(NCR)
#include "os/ncr.h"

#elif defined(SNI)
#include "os/reliantunix.h"
#endif
```

Fortunately, all these C code compiles without any problems. No superfluous changes have been made here to preserve the historical legacy. And once we have all the object files, just use the following lines of bash script to link against the `libmocha` static library, creating Mocha's executable!

``` sh
function compile_native() {
    export CC=clang
    export AR=ar
    compile_objs
    echo "linking..."
    $AR -rcs out/libmocha.a out/*.o
    $CC -Iinclude -Lout -lmocha tests/mo_shell.c -o out/mo_shell
    echo "mocha shell compiled!"
}
```

After getting a native version of Mocha, how can we get a WASM version of it? It's really simple, just replace the native compiler command `gcc` (actually `clang` on macOS) with the WASM compiler `emcc`! The Emscripten compiler supports JavaScript and WASM as compilation backends, and switching the output format is a matter of changing one of the compilation flags:

``` sh
function compile_web() {
    export CC=emcc
    export AR=emar
    compile_objs
    echo "linking..."
    $AR -rcs out/libmocha.a out/*.o
    $CC -Iinclude -Lout -lmocha tests/mo_shell.c \
        --shell-file src/shell.html \
        -s NO_EXIT_RUNTIME=0 \
        -s WASM=$1 \
        -O2 \
        -o $2
    echo "mocha shell compiled!"
}

function compile_js() {
    compile_web 0 out/mocha_shell_js.html
}

function compile_wasm() {
    compile_web 1 out/mocha_shell_wasm.html
}
```

I did not rewrite the Makefile after I had the Mocha engine available, because I found that the manually implemented bash script, while not incrementally compilable, is pretty easy to use and allows me to easily build different products:

``` sh
$ source build.sh

# build WASM
$ compile_wasm

# build js
$ compile_js

# build native
$ compile_native
```

The Emscripten compiler itself, however, is highly aggressive by default, outputting an HTML that "executes WASM content as soon as the page is opened". For the sake of simplicity, the WASM engine page is embedded directly into an iframe here. Each time the "Run" button is clicked on the page, the content of the input box is inserted into localStorage, and then the corresponding WASM iframe page is reloaded, in which the JS scripts in localStorage are read synchronously as standard input for the Emscripten-simulated stdin, and finally Mocha is started and interprets scripts automatically.

The process is simple enough that I guess any average front end developer can easily implement it. Here's the final result:

![mocha-wasm](./mocha-wasm.png)

And that's it! We have "reinstalled" the world's first JS engine inside a Web browser!

From finding the source code to getting the WASM version online, it only took me less than three days of spare time. So I personally believe that the Mocha engine was pretty well thought out in terms of portability, and had good engineering quality. However, some of its basic design, such as reference counting, had inherent performance bottlenecks that required it to be rewritten, which is a different story.

At the time of writing this article, it was about the 25th anniversary of the official release of JavaScript (December 4, 1995, Netscape and Sun's joint announcement). The press release of this event is also an attachment in JavaScript The First 20 Years. As a Chinese front end developer, I'm happy to see that this book is getting good responses in China (about 60,000 reads for my personal articles related, 2.2k star for [GitHub translation project](https://github.com/doodlewind/jshistory-cn)). Interestingly, Brendan Eich, the creator of JavaScript, also has Chinese characters on his Twitter avatar, but unfortunately all you can see is the word "無一" ("無" stands for null and "一" stands for one), which looks like he is practicing Tai Chi:

![be-1](./be-1.jpg)

However, thanks to my friend Yiling Gu, I found the original picture of Eich's avatar. The Chinese characters here are not metaphysical, but an encouragement for programmers, which reads, "The more people contribute, the better for the development of the whole ecosystem, open source has become a culture."

![be-2](./be-2.jpg)

And perhaps this little practice I did here, is also a manifestation of this culture.

Dennis Ritchie, the creator of C, says that the way to succeed is by being *lucky* - "Grab on to something that's moving pretty fast, and let yourself be carried on when you're in the right place at the right time." That's exactly what happened to JavaScript. It's now the language that has powered the GUI of the first human spacecraft on board the SpaceX Dragon, and it's even about to fly off into the universe with the James Webb Space Telescope. But when we look back at where it all began, the 1995 version of the Mocha engine, with all of its flaws, was undoubtedly in the right place at the right time - otherwise perhaps we'd be writing VBScript today.

Looking back on 1995 at the end of 2020, it seems like an incredible time: the WTO was founded, the Schengen Agreement came into effect, Windows 95, Java & JavaScript were released. While a quarter of a century later, something has been popularized, something has changed, and something may never come back.

Forget about the bad stuff. Today, let's toast to 1995, to 2020, and to JavaScript.

**[Live Demo](https://mocha1995.js.org)**
