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
    sz = self.size()
    if sz == 0: return b""
    # Direct memory copy from C++ data() pointer to Python bytes object
    ptr = self.data()
    if not ptr: return b""
    addr = cppyy.ll.cast['uintptr_t'](ptr)
    return ctypes.string_at(addr, sz)

def String_repr(self):
    """Beautiful print output using decimal bytes"""
    try:
        if self.size() == 0: return f"Xi::String(0)[]"
        d_str = self.toDeci()
        if not d_str: return f"Xi::String({self.size()})[uninitialized]"
        deci = bytes(d_str).decode('utf-8')
        return f"Xi::String({self.size()})[{deci}]"
    except:
        return f"Xi::String({self.size()})[binary/error]"

# APPLY PATCHES
String.__init__  = String_init
String.__bytes__ = String_bytes
String.__str__   = lambda self: bytes(self).decode('utf-8', 'replace')
String.__repr__  = String_repr

# Map patches
def Map_getitem(self, k):
    if isinstance(self, type):
        # This is likely a template specialization call Map[K, V]
        return self.__class__.__getitem__(self, k)
    return self.get(k)

# Instead of breaking the template, we'll only patch instances if possible, 
# or use a safer approach. cppyy templates are tricky. 
# Let's just remove the __getitem__ patch for now as it's not strictly needed for the tests 
# if we use .get() and .put() directly.
# Xi.Map.__getitem__ = lambda self, k: self.get(k)
# Xi.Map.__setitem__ = lambda self, k, v: self.put(k, v)

# ------------------------------------------------------------------
# EXPORTS
# ------------------------------------------------------------------
RailwayStation = Xi.RailwayStation
Tunnel = Xi.Tunnel
Map = Xi.Map
Packet = Xi.Packet