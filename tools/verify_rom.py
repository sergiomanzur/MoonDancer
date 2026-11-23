import zlib, hashlib, os, sys
from pathlib import Path

romdir = Path("roms")
expect = {
"epr-13235.a6":"6983e129","epr-13234.a5":"c9fd20f2","317-0159.key":"507838f0",
"mpr-13216.b1":"862d2c03","mpr-13217.b2":"7d1ac3ec","mpr-13218.b3":"56d3393c",
"mpr-13224.b11":"c59f107b","mpr-13231.a11":"a5e96346","mpr-13223.b10":"364f60ff",
"mpr-13230.a10":"9550091f","mpr-13222.b9":"523df3ed","mpr-13229.a9":"f40dc45d",
"epr-13221.b8":"9ae7546a","epr-13228.a8":"de3786be","epr-13225.a4":"56c2e82b",
"mpr-13219.b4":"19e2061f","mpr-13220.b5":"58d4d9ce","mpr-13249.b6":"623edc5d",
"315-5437.ic4":"4bf63bc1"}
ok=True
for name,crc in expect.items():
    p=romdir/name
    if not p.exists(): print("MISSING",name); ok=False; continue
    c=format(zlib.crc32(p.read_bytes())&0xffffffff,"08x")
    print(("OK  " if c==crc else "BAD "), name, c, "expected", crc)
    ok &= c==crc
print("all ok" if ok else "MISMATCH")
