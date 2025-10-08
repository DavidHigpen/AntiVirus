# xor_encrypt.py
import sys

XOR_KEY = 0x5A

if len(sys.argv) != 3:
    print(f"Usage: {sys.argv[0]} <input_file> <output_file>")
    sys.exit(1)

with open(sys.argv[1], "rb") as f:
    data = bytearray(f.read())

for i in range(len(data)):
    data[i] ^= XOR_KEY

with open(sys.argv[2], "wb") as f:
    f.write(data)

print(f"Encrypted {len(data)} bytes to {sys.argv[2]}")
#/usr/bin/x86_64-w64-mingw32-g++ dropper.cpp res.o -o Dropper.exe -mwindows   
# python xor_encrypt.py 3 33.bin   
# /usr/bin/x86_64-w64-mingw32-windres res.rc -O coff -o res.o 