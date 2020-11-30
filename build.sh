CC=clang

function test_headers() {
    echo "Test compiling headers"
    CC include/prmacros.h -o out/prmacros.pch
    CC include/prtypes.h -o out/prtypes.pch
    CC include/mo_pubtd.h -o out/mo_pubtd.pch
    CC include/mo_prvtd.h -o out/mo_prvtd.pch
    CC include/mochaapi.h -o out/mochaapi.pch
    CC include/mo_bcode.h -o out/mo_bcode.pch
    CC include/mo_java.h -o out/mo_java.pch
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
    echo "All headers compiled"
}

function test_objs() {
    echo "Test compiling OBJS"
    CC -Iinclude src/mo_array.c -c -o out/mo_array.o
    CC -Iinclude src/mo_atom.c -c -o out/mo_atom.o
    echo "All OBJS compiled"
}

# test_headers
test_objs
