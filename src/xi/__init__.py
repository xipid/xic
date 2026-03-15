import os
import ctypes
from pathlib import Path

# ------------------------------------------------------------------
# 100% AUTOMATIC CPPYY BINDINGS (Zero-Generation)
# ------------------------------------------------------------------
import cppyy
import cppyy.ll

# Determine current file path
current_dir = Path(__file__).resolve().parent

if (current_dir / "include").exists():
    include_path = current_dir / "include"
else:
    include_path = current_dir.parent.parent / "include"

cppyy.add_include_path(str(include_path))

# Dynamically parse headers at runtime!
cppyy.include("Xi/String.hpp")
cppyy.include("Rho/Tunnel.hpp")
cppyy.include("Rho/Railway.hpp")

Xi = cppyy.gbl.Xi
String = Xi.String
Tunnel = Xi.Tunnel
RailwayStation = Xi.RailwayStation
Packet = Xi.Packet
KeyPair = Xi.KeyPair

# ------------------------------------------------------------------
# PYTHONIC QUALITY OF LIFE PATCHES 
# ------------------------------------------------------------------

_orig_init = String.__init__

def String_init(self, arg=None):
    _orig_init(self)
    if arg is None: return
    if isinstance(arg, (bytes, bytearray)):
        b_data = arg
    elif isinstance(arg, str):
        b_data = arg.encode('utf-8')
    else:
        return
    
    c_buf = (ctypes.c_ubyte * len(b_data)).from_buffer_copy(b_data)
    addr = ctypes.addressof(c_buf)
    self.pushEach(cppyy.ll.cast['uint8_t*'](addr), len(b_data))

def String_bytes(self):
    sz = self.size()
    if sz == 0: return b""
    addr = cppyy.ll.cast['uintptr_t'](self.data())
    return ctypes.string_at(addr, sz)

String.__init__ = String_init
String.__bytes__ = String_bytes
String.__str__ = lambda self: bytes(self).decode('utf-8', 'replace')
String.__len__ = lambda self: self.size()

def generateKeyPair():
    return Xi.generateKeyPair()

def hash(input, length=64, key=None):
    if key is None:
        return Xi.hash(input, length)
    return Xi.hash(input, length, key)