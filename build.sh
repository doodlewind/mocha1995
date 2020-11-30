CC=clang
AR=ar

# For testing ports only, unused for actual build process
function test_headers() {
    echo "testing headers..."
    CC include/prmacros.h -o out/prmacros.pch
    CC include/prtypes.h -o out/prtypes.pch
    CC include/mo_pubtd.h -o out/mo_pubtd.pch
    CC include/mo_prvtd.h -o out/mo_prvtd.pch
    CC include/mochaapi.h -o out/mochaapi.pch
    CC include/mo_bcode.h -o out/mo_bcode.pch
    CC include/mo_parse.h -o out/mo_parse.pch
    CC include/mo_emit.h -o out/mo_emit.pch
    CC include/mo_scan.h -o out/mo_scan.pch
    CC include/prhash.h -o out/prhash.pch
    CC include/mo_atom.h -o out/mo_atom.pch
    CC include/mo_scope.h -o out/mo_scope.pch
    CC include/mochalib.h -o out/mochalib.pch
    CC include/mocha.h -o out/mocha.pch
    CC include/prarena.h -o out/prarena.pch
    CC include/prclist.h -o out/prclist.pch
    CC include/mo_cntxt.h -o out/mo_cntxt.pch
    CC src/alloca.h -o out/alloca.pch
    CC include/prprf.h -o out/prprf.pch
    CC include/prglobal.h -o out/prglobal.pch
    CC include/prlog.h -o out/prlog.pch
    CC include/prmem.h -o out/prmem.pch
    CC include/prmjtime.h -o out/prmjtime.pch
    CC include/prtime.h -o out/prtime.pch
    CC include/prlong.h -o out/prlong.pch
    CC include/prosdep.h -o out/prosdep.pch
    CC include/prunixos.h -o out/prunixos.pch
    CC include/prsync.h -o out/prsync.pch
}

function test_objs() {
    echo "compiling OBJS..."
    CC -Iinclude src/mo_array.c -c -o out/mo_array.o
    CC -Iinclude src/mo_atom.c -c -o out/mo_atom.o
    CC -Iinclude src/mo_bcode.c -c -o out/mo_bcode.o
    CC -Iinclude src/mo_bool.c -c -o out/mo_bool.o
    CC -Iinclude src/mo_cntxt.c -c -o out/mo_cntxt.o
    CC -Iinclude src/mo_date.c -Wno-dangling-else -c -o out/mo_date.o
    CC -Iinclude src/mo_emit.c -c -o out/mo_emit.o
    CC -Iinclude src/mo_fun.c -c -o out/mo_fun.o
    CC -Iinclude src/mo_math.c -c -o out/mo_math.o
    CC -Iinclude src/mo_num.c -Wno-non-literal-null-conversion -c -o out/mo_num.o
    CC -Iinclude src/mo_obj.c -c -o out/mo_obj.o
    CC -Iinclude src/mo_parse.c -c -o out/mo_parse.o
    CC -Iinclude src/mo_scan.c -c -o out/mo_scan.o
    CC -Iinclude src/mo_scope.c -c -o out/mo_scope.o
    CC -Iinclude src/mo_str.c -Wno-non-literal-null-conversion -c -o out/mo_str.o
    CC -Iinclude src/mocha.c -c -o out/mocha.o
    CC -Iinclude src/mochaapi.c -Wno-non-literal-null-conversion -c -o out/mochaapi.o
    CC -Iinclude src/mochalib.c -c -o out/mochalib.o
    CC -Iinclude src/prmjtime.c -c -o out/prmjtime.o
    CC -Iinclude src/prtime.c -c -o out/prtime.o
    CC -Iinclude src/prarena.c -c -o out/prarena.o
    CC -Iinclude src/prhash.c -c -o out/prhash.o
    CC -Iinclude src/prprf.c -c -o out/prprf.o
    CC -Iinclude src/prdtoa.c \
        -Wno-logical-not-parentheses \
        -Wno-shift-op-parentheses \
        -Wno-parentheses \
        -c -o out/prdtoa.o
    CC -Iinclude src/log2.c -c -o out/log2.o
    CC -Iinclude src/longlong.c -c -o out/longlong.o
}

function test_main() {
    echo "linking..."
    AR -rcs out/libmocha.a out/*.o
    CC -Iinclude -Lout -lmocha tests/mo_shell.c -o out/mo_shell
    echo "mocha shell compiled!"
}

mkdir -p out
# test_headers
test_objs
test_main
