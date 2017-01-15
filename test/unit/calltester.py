import os
import sys
import ntpath
import subprocess

def csvRead(fname):
    with open(fname, 'r') as f:
        lines = []
        for line in f:
            elems = line.split(',')
            if (5 == len(elems)):
                elems = (
                    elems[0].strip(' '),
                    elems[1].strip(' '),
                    int(elems[2]),
                    elems[3].strip(' '),
                    int(elems[4]))
                lines.append(elems)
        lines.sort()
        return lines

def csvEquals (fname1, fname2):
    dic1 = csvRead(fname1)
    dic2 = csvRead(fname2)
    allCorrect = True
    # lines in each dictionary are sorted by line number
    # sorted alphabetically by callee
    # each line are tuples of the format
    # (caller name, file name, line #, callee name, frequency)
    for (line1, line2) in zip(dic1, dic2):
        caller, fname, lino, callee, count = line1
        caller2, fname2, lino2, callee2, count2 = line2
        fbase = os.path.basename(fname)
        fbase2 = os.path.basename(fname2)
        correct = caller == caller2 and fbase == fbase2 and \
            lino == lino2 and callee == callee2 and \
            count == count2
        if (not correct):
            allCorrect = False
    if (False == allCorrect):
        print(dic1)
        print(dic2)
    return allCorrect

arg = sys.argv
uname = arg[1]
testfile = arg[2]

testname = os.path.basename(testfile)
testpath = testfile.split(testname)[0]+'../expectout'

# map testfile name to arguments to call
targ = {
    '01-internal-call-once.c': [('', 'expect01')],
    '02-internal-call-twice.c': [('', 'expect02')],
    '03-internal-call-in-loop.c': [('', 'expect03')],
    '04-internal-line-clobber.c': [('', 'expect04')],
    '05-internal-multiple-files.c': [('', 'expect05')],
    '06-external-call-multiple.c': [('', 'expect06')],
    '07-function-pointer-one-internal-target.c': [
        ('a', 'expect07arg'),
        ('', 'expect07noarg')
    ],
    '08-function-pointer-multiple-internal-targets.c': [('', 'expect08')],
    '09-internal-recursion.c': [
        ('2 3', 'expect09argc3'),
        ('2 3 4 5 6 7 8 9 10', 'expect09argc10')
    ]
}
trash = open('temphistory', 'w')
if (os.path.isfile(uname)):
    for (ar, res) in targ[testname]:
        callargs = ['./'+uname]
        if len(ar):
            callargs = callargs + ar.split(' ')
        subprocess.Popen(callargs, stdout=trash)
        subprocess.Popen(['mv', 'profile-results.csv', res+'/profile-results.csv'])
        csvEquals('./'+res+'/profile-results.csv', testpath+'/'+res+'.csv')
else:
    print('error: '+testfile+' compilation failed')
