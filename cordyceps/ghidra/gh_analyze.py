# Ghidra headless postScript (PyGhidra / Python3).
# Decompiles a list of addresses (from /Users/timdoug/chungusppc/cordyceps/ghidra/gh_addrs.txt, comma or newline separated,
# hex with or without 0x) and also hunts for the scsi_info string + the 0x53f0/0x53f1
# flavor constants so the same script works on the stripped stock kernel.
# Output -> /Users/timdoug/chungusppc/cordyceps/ghidra/gh_out_<PROGNAME>.txt  (PROGNAME passed as first script arg)

import os
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.program.model.address import AddressSet

args = getScriptArgs()
progname = args[0] if len(args) > 0 else currentProgram.getName()
outpath = "/Users/timdoug/chungusppc/cordyceps/ghidra/gh_out_%s.txt" % progname

prog = currentProgram
af = prog.getAddressFactory().getDefaultAddressSpace()
fm = prog.getFunctionManager()
listing = prog.getListing()
mon = ConsoleTaskMonitor()

dif = DecompInterface()
dif.openProgram(prog)

def ensure_func(a):
    addr = af.getAddress(a)
    fn = fm.getFunctionAt(addr)
    if fn is None:
        fn = fm.getFunctionContaining(addr)
    if fn is None:
        try:
            createFunction(addr, None)
        except Exception as e:
            pass
        fn = fm.getFunctionAt(addr)
    return fn

def decompile(fn):
    if fn is None:
        return "<no function>"
    res = dif.decompileFunction(fn, 90, mon)
    if res is None or not res.decompileCompleted():
        return "<decompile failed: %s>" % (res.getErrorMessage() if res else "null")
    return res.getDecompiledFunction().getC()

out = []

# ---- Part 1: explicit address list (per-program file if present) ----
addrs = []
addr_file = "/Users/timdoug/chungusppc/cordyceps/ghidra/gh_addrs_%s.txt" % progname
if not os.path.exists(addr_file):
    addr_file = "/Users/timdoug/chungusppc/cordyceps/ghidra/gh_addrs.txt"
if os.path.exists(addr_file):
    raw = open(addr_file).read().replace(",", "\n").split()
    for tok in raw:
        tok = tok.strip()
        if not tok:
            continue
        addrs.append(long(tok, 16))

for a in addrs:
    fn = ensure_func(a)
    nm = fn.getName() if fn else "?"
    out.append("==================== 0x%x  %s ====================" % (a, nm))
    out.append(decompile(fn))
    out.append("")

# ---- Part 2: find the "scsi_info" string and functions that reference it ----
out.append("############### scsi_info string references ###############")
mem = prog.getMemory()
found_str_addrs = []
# scan defined data / raw bytes for the ASCII "scsi_info"
target = b"scsi_info"
try:
    addrsit = prog.getMemory().findBytes(prog.getMinAddress(), target, None, True, mon)
    while addrsit is not None:
        found_str_addrs.append(addrsit)
        nxt = addrsit.add(1)
        addrsit = prog.getMemory().findBytes(nxt, target, None, True, mon)
except Exception as e:
    out.append("string scan error: %s" % e)

for sa in found_str_addrs:
    out.append("string 'scsi_info' @ %s" % sa)
    refs = getReferencesTo(sa)
    for r in refs:
        fa = r.getFromAddress()
        cf = fm.getFunctionContaining(fa)
        out.append("   ref from %s in %s" % (fa, cf.getName() if cf else "?"))

# ---- Part 3: find scalar constants 0x53f0 / 0x53f1 (scsi_info flavors) ----
out.append("")
out.append("############### 0x53f0/0x53f1 flavor constant sites ###############")
inst = listing.getInstructions(True)
hits = {}
cnt = 0
for i in inst:
    cnt += 1
    for opi in range(i.getNumOperands()):
        for obj in i.getOpObjects(opi):
            try:
                v = obj.getValue()
            except Exception:
                continue
            if v in (0x53f0, 0x53f1, 0x53F0, 0x53F1):
                fa = i.getAddress()
                cf = fm.getFunctionContaining(fa)
                key = cf.getName() if cf else str(fa)
                hits.setdefault(key, (cf, fa))
for key, (cf, fa) in hits.items():
    out.append("const 0x53f0/1 in function %s (@%s)" % (key, fa))
    if cf is not None:
        out.append(decompile(cf))
        out.append("")

with open(outpath, "w") as f:
    f.write("\n".join(out))

print("WROTE %s (%d addrs, %d string-hits, %d const-funcs)" %
      (outpath, len(addrs), len(found_str_addrs), len(hits)))
