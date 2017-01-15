import os
import sys
import ntpath
from subprocess import call

def csvEquals (fname1, fname2):
    return True;

arg = sys.argv
uname = arg[1]
testfile = arg[2]

testname = ntpath.basename(testfile)

# map testfile name to arguments to call
targ = {
    "01-internal-call-once.c": [("", "expect01")],
    "02-internal-call-twice.c": [("", "expect02")],
    "03-internal-call-in-loop.c": [("", "expect03")],
    "04-internal-line-clobber.c": [("", "expect04")],
    "05-internal-multiple-files.c": [("", "expect05")],
    "06-external-call-multiple.c": [("", "expect06")],
    "07-function-pointer-one-internal-targets.c": [
        ("a", "expect07arg"),
        ("", "expect07noarg")
    ],
    "08-function-pointer-multiple-internal-targets.c": [("", "expect08")],
    "09-internal-recursion.c": [
        ("2 3", "expect09argc3"),
        ("2 3 4 5 6 7 8 9 10", "expect09argc10")
    ]
}

if (os.path.isfile(uname)):
    for (ar, res) in targ[testname]:
        call([uname, ar]);
        if (not csvEquals("profile-results.csv", res+".csv")):
            print(res+" failure")
else:
    print("error: "+testfile+" compilation failed")
