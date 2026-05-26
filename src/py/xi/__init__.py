import os
import ctypes
import cppyy
import cppyy.ll
from pathlib import Path

# ------------------------------------------------------------------
# 1. DYNAMIC HEADER INCLUSION
# ------------------------------------------------------------------
# This allows xic to be used both in development and as a pip-installed package.
current_dir = Path(__file__).resolve().parent

# Check for headers in the local source tree first, then falling back to packaged include/
local_include = current_dir.parent.parent.parent / "include"
packaged_include = current_dir / "include"

if local_include.exists():
    include_path = local_include
elif packaged_include.exists():
    include_path = packaged_include
else:
    # Standard installation fallback
    include_path = Path("/usr/local/include/xi")

cppyy.add_include_path(str(include_path))

# Parse core headers at runtime
cppyy.include("Xi/String.hpp")
cppyy.include("Rho/Tunnel.hpp")
cppyy.include("Rho/Railway.hpp")

# Export Global Symbols
Xi = cppyy.gbl.Xi
String = Xi.String
Tunnel = Xi.Tunnel
RailwayStation = Xi.RailwayStation
Packet = Xi.Packet
KeyPair = Xi.KeyPair

# ------------------------------------------------------------------
# 2. PYTHONIC QUALITY OF LIFE PATCHES
# ------------------------------------------------------------------

_orig_init = String.__init__

def String_init(self, arg=None):
    """Augmented constructor to handle Python bytes and strings transparently."""
    _orig_init(self)
    if arg is None: return

    # Normalize input to bytes
    b_data = None
    if isinstance(arg, (bytes, bytearray)):
        b_data = arg
    elif isinstance(arg, str):
        b_data = arg.encode('utf-8')
    elif hasattr(arg, 'length'):
        self.concat(arg)
        return

    if b_data is not None:
        # Create a stable ctypes buffer to get a valid memory address
        c_buf = (ctypes.c_ubyte * len(b_data)).from_buffer_copy(b_data)
        addr = ctypes.addressof(c_buf)
        
        # Call the C++ helper with the integer address (if available)
        # Fallback to pushEach if the helper is not compiled in
        try:
            self.setFromRawAddress(addr, len(b_data))
        except:
            self.pushEach(cppyy.ll.cast['uint8_t*'](addr), len(b_data))

def String_bytes(self):
    """Direct binary export. Returns the Python 'bytes' object."""
    sz = self.size()
    if sz == 0: return b""
    addr = cppyy.ll.cast['uintptr_t'](self.data())
    return ctypes.string_at(addr, sz)

def String_repr(self):
    """Beautiful print output for debugging."""
    sz = self.size()
    if sz == 0: return "Xi::String(0)[]"
    try:
        # Attempt to show decimal representation if it's a Rho identity
        d_str = self.toDeci()
        if d_str:
            return f"Xi::String({sz})[{bytes(d_str).decode('utf-8')}]"
    except:
        pass
    return f"Xi::String({sz})[binary]"

# Apply Patches to the C++ Class
String.__init__  = String_init
String.__bytes__ = String_bytes
String.__str__   = lambda self: bytes(self).decode('utf-8', 'replace')
String.__repr__  = String_repr
String.__len__   = lambda self: self.size()

# ------------------------------------------------------------------
# 3. HIGH-LEVEL UTILITIES
# ------------------------------------------------------------------

def generateKeyPair():
    """Generates a new XChacha20-Poly1305 keypair."""
    return Xi.generateKeyPair()

def hash(input, length=64, key=None):
    """Performs a BLAKE2b hash on the input."""
    if key is None:
        return Xi.hash(input, length)
    return Xi.hash(input, length, key)

__all__ = ["Xi", "String", "Tunnel", "RailwayStation", "Packet", "KeyPair", "generateKeyPair", "hash"]
