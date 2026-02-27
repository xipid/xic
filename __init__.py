import cppyy.ll
import ctypes
from pathlib import Path

cppyy.add_include_path(str(Path(__file__).resolve().parent) + "/include")

cppyy.include("../packages/monocypher/monocypher.c")
cppyy.include("Xi/String.hpp")
cppyy.include("Rho/Tunnel.hpp")
cppyy.include("Rho/Railway.hpp")

# 2. Capture the REAL C++ Class
Xi = cppyy.gbl.Xi
String = Xi.String 

# ------------------------------------------------------------------
# MONKEY PATCH LOGIC
# ------------------------------------------------------------------

_orig_init = String.__init__

def String_init(self, arg=None):
    """Augmented constructor to handle Python bytes and strings"""
    _orig_init(self) # Call C++ allocator
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
        # FIX: Create a ctypes buffer from bytes to get a valid address
        # We use from_buffer_copy to ensure we have a stable memory block
        c_buf = (ctypes.c_ubyte * len(b_data)).from_buffer_copy(b_data)
        addr = ctypes.addressof(c_buf)
        # Call the C++ helper with the integer address
        self.setFromRawAddress(addr, len(b_data))

def String_bytes(self):
    """FORCED binary export. Always returns the Python 'bytes' type."""
    sz = self.length
    if sz == 0: return b""
    # Direct memory copy from C++ data() pointer to Python bytes object
    addr = cppyy.ll.cast['uintptr_t'](self.data())
    return ctypes.string_at(addr, sz)

def String_repr(self):
    """Beautiful print output using decimal bytes"""
    try:
        # bytes(self) calls String_bytes, then we decode it
        deci = bytes(self.toDeci()).decode('utf-8')
        return f"Xi::String({self.length})[{deci}]"
    except:
        return f"Xi::String({self.length})[binary]"

# APPLY PATCHES
String.__init__  = String_init
String.__bytes__ = String_bytes
String.__str__   = lambda self: bytes(self).decode('utf-8', 'replace')
String.__repr__  = String_repr

# Map patches
Xi.Map.__getitem__ = lambda self, k: self.get(k)
Xi.Map.__setitem__ = lambda self, k, v: self.put(k, v)

# ------------------------------------------------------------------
# EXPORTS
# ------------------------------------------------------------------
Railway = Xi.Railway
Tunnel = Xi.Tunnel
Map = Xi.Map
RailwayPacket = Xi.Railway.RailwayPacket
Packet = Xi.Packet