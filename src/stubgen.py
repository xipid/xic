import os
import sys
import CppHeaderParser

def format_ts_doc(raw_doc):
    if not raw_doc: return ""
    lines = str(raw_doc).split('\n')
    out = ["  /**"]
    for line in lines:
        line = line.strip()
        if not line: continue
        if line.startswith('//'): out.append(f"   * {line[2:].strip()}")
        elif line.startswith('/*'): out.append(f"   * {line[2:].strip()}")
        elif line.endswith('*/'): out.append(f"   * {line[:-2].strip()}")
        elif line.startswith('*'): out.append(f"   {line}")
        else: out.append(f"   * {line}")
    if len(out) == 1: return ""
    out.append("   */")
    return "\n".join(out)

def format_py_doc(raw_doc, indent=4):
    if not raw_doc: return ""
    lines = str(raw_doc).split('\n')
    out = []
    for line in lines:
        line = line.strip()
        if not line: continue
        if line.startswith('//'): out.append(line[2:].strip())
        elif line.startswith('/*'): out.append(line[2:].strip())
        elif line.endswith('*/'): out.append(line[:-2].strip())
        elif line.startswith('*'): out.append(line[1:].strip())
        else: out.append(line)
        
    text = "\n".join(out).strip()
    if not text: return ""
    ind = " " * indent
    if "\n" in text:
        return f'{ind}"""\n{ind}{text.replace(chr(10), chr(10) + ind)}\n{ind}"""'
    return f'{ind}"""{text}"""'

def map_ts_type(c_type):
    if not c_type: return 'void'
    ct = c_type.lower()
    
    # Primitive mapping rules: match the exact words that appear in C++ types
    if 'string' in ct or 'char' in ct: return 'string'
    if any(x in ct for x in ['int', 'float', 'double', 'size_t', 'usz', 'u8', 'u16', 'u32', 'u64', 'i8', 'i16', 'i32', 'i64', 'f32', 'f64', 'long', 'short', 'unsigned', 'signed']): return 'number'
    if 'bool' in ct: return 'boolean'
    if 'void' in ct: return 'void'
    if 'func' in ct or 'callback' in ct: return 'Function'
    if 'vector' in ct or 'array' in ct: return 'any[]'
    
    clean = c_type.replace('const', '').replace('&', '').replace('*', '').replace('Xi::', '').strip()
    clean = clean.split('<')[0]
    if not clean: return "any"
    return clean

def map_py_type(c_type):
    if not c_type: return 'None'
    ct = c_type.lower()
    
    if 'string' in ct or 'char' in ct: return 'str'
    if 'float' in ct or 'double' in ct or 'f32' in ct or 'f64' in ct: return 'float'
    if any(x in ct for x in ['int', 'size_t', 'usz', 'u8', 'u16', 'u32', 'u64', 'i8', 'i16', 'i32', 'i64', 'long', 'short', 'unsigned', 'signed']): return 'int'
    if 'bool' in ct: return 'bool'
    if 'void' in ct: return 'None'
    if 'func' in ct or 'callback' in ct: return 'Any'
    if 'vector' in ct or 'array' in ct: return 'list'
    
    clean = c_type.replace('const', '').replace('&', '').replace('*', '').replace('Xi::', '').strip()
    clean = clean.split('<')[0]
    if not clean: return "Any"
    return clean
    
def generate(include_dir, out_ts, out_py):
    ts_out = [
        "// Auto-generated TypeScript definitions for Xi Engine",
        "declare module 'xi' {",
        ""
    ]
    
    py_out = [
        "# Auto-generated Python stubs for Xi Engine",
        "from typing import List, Optional, Any, Tuple",
        ""
    ]
    
    # Process all headers
    for root, _, files in os.walk(include_dir):
        for file in files:
            if not file.endswith(('.hpp', '.h')): continue
            
            filepath = os.path.join(root, file)
            try:
                # Tell CppHeaderParser to ignore XI_EXPORT macro entirely
                CppHeaderParser.ignoreSymbols = ['XI_EXPORT']
                
                # Pre-process the file to completely remove XI_EXPORT before giving it to CppHeaderParser
                # This is safer than relying on ignoreSymbols if it's placed weirdly (e.g. `class LinuxFS XI_EXPORT`)
                with open(filepath, 'r', encoding='utf-8') as f:
                    content = f.read()
                
                content = content.replace('XI_EXPORT', '')
                
                header = CppHeaderParser.CppHeader(content, argType="string")
            except Exception as e:
                # CppHeaderParser might fail on some complex template headers, just skip them if they crash
                print(f"Skipping {file} due to parser error: {e}")
                continue
                
            for class_name, class_info in header.classes.items():
                # Skip templates completely for now to keep the bindings clean, or check if it's meant to be exported
                if '<' in class_name or '::' in class_name: continue
                # if 'namespace' not in class_info or 'Xi' not in class_info['namespace']: continue
                
                # Fetch docs
                raw_doc = class_info.get('doxygen', '')
                
                # TS Class
                doc_ts = format_ts_doc(raw_doc)
                if doc_ts: ts_out.append(doc_ts)
                ts_out.append(f"  export class {class_name} {{")
                
                # PY Class
                py_out.append(f"class {class_name}:")
                doc_py = format_py_doc(raw_doc)
                if doc_py: py_out.append(doc_py)
                    
                methods = class_info.get('methods', {})
                properties = class_info.get('properties', {})
                
                has_members = False
                seen_props = set()
                
                for access in properties:
                    if access != 'public': continue
                    for p in properties[access]:
                        p_name = p.get('name', 'prop')
                        if p_name in seen_props: continue
                        seen_props.add(p_name)
                        
                        has_members = True
                        p_type = p.get('type', 'Any')
                        p_doc = p.get('doxygen', '')
                        
                        ts_ptype = map_ts_type(p_type)
                        if '::' in ts_ptype: ts_ptype = 'any'
                        
                        py_ptype = map_py_type(p_type)
                        if '::' in py_ptype: py_ptype = 'Any'
                        
                        m_doc_ts = format_ts_doc(p_doc)
                        if m_doc_ts: ts_out.append(m_doc_ts)
                        ts_out.append(f"    {p_name}: {ts_ptype};")
                        
                        m_doc_py = format_py_doc(p_doc, 4)
                        if m_doc_py: py_out.append(m_doc_py)
                        py_out.append(f"    {p_name}: {py_ptype}\n")
                    
                
                seen_py_methods = set()
                
                for access in methods:
                    if access != 'public': continue
                    for m in methods[access]:
                        m_name = m['name']
                        if m_name == class_name or m_name == f"~{class_name}" or m_name.startswith('operator'):
                            continue # Skip constructors/destructors directly here, handle explicitly if true
                            
                        has_members = True
                        raw_ret = m.get('rtnType', 'void')
                        raw_doc = m.get('doxygen', '')
                        
                        # Render TS method
                        m_doc_ts = format_ts_doc(raw_doc)
                        if m_doc_ts: ts_out.append(m_doc_ts)
                            
                        ts_args = []
                        py_args = ["self"] if not m.get('static', False) else []
                        py_sig_keys = []
                        
                        for i, arg in enumerate(m.get('parameters', [])):
                            arg_name = arg.get('name', f"arg{i}")
                            arg_type = arg.get('type', 'Any')
                            if not arg_name: arg_name = f"arg{i}"
                            
                            # Sanitize TS arg names to avoid invalid characters
                            arg_name = "".join(c for c in arg_name if c.isalnum() or c == '_')
                            if not arg_name or arg_name[0].isdigit(): arg_name = f"arg_{arg_name}"
                            
                            ts_args.append(f"{arg_name}: {map_ts_type(arg_type)}")
                            py_args.append(f"{arg_name}: {map_py_type(arg_type)}")
                            py_sig_keys.append(map_py_type(arg_type))
                            
                        static_mod = "static " if m.get('static', False) else ""
                        ts_ret = map_ts_type(raw_ret).replace('static ', '').strip()
                        
                        # Fix corrupted method names (e.g. `<void` from a template)
                        m_name_clean = "".join(c for c in m_name if c.isalnum() or c == '_')
                        if not m_name_clean or m_name_clean[0].isdigit():
                            continue # Skip entirely corrupted methods
                            
                        # Validate that ts_args and ts_ret doesn't have junk characters
                        if any(c in ts_ret for c in ['(', ')', ':', '<', '>', ',', '-']): ts_ret = "any"
                        ts_args_clean = []
                        for i, arg in enumerate(ts_args):
                            parts = arg.split(":")
                            if len(parts) != 2:
                                ts_args_clean.append(f"arg{i}: any")
                                continue
                            
                            arg_name, arg_type = parts[0].strip(), parts[1].strip()
                            
                            # Force parameter name to be a valid TS identifier
                            arg_name = "".join(c for c in arg_name if c.isalnum() or c == '_')
                            if not arg_name or arg_name[0].isupper() or arg_name[0].isdigit(): 
                                arg_name = f"arg{i}"
                                
                            if any(c in arg_type for c in ['(', ')', '<', '>', '-', ',']):
                                arg_type = "any"
                            
                            ts_args_clean.append(f"{arg_name}: {arg_type}")
                                
                        ts_out.append(f"    {static_mod}{m_name_clean}({', '.join(ts_args_clean)}): {ts_ret};")
                        
                        # Render PY method (avoid simple overloading duplicates which Python stubbers hate, we output unions later if needed, but for now just take first overload)
                        py_sig = f"{m_name_clean}({','.join(py_sig_keys)})"
                        if py_sig in seen_py_methods: continue
                        seen_py_methods.add(py_sig)
                        
                        m_doc_py = format_py_doc(raw_doc, 8)
                        
                        if m.get('static', False):
                            py_out.append("    @staticmethod")
                            
                        py_ret = map_py_type(raw_ret).replace('static ', '').strip()
                        if any(c in py_ret for c in ['(', ')', ':', '<', '>', ',', '-']): py_ret = "Any"
                        
                        py_args_clean = []
                        for i, arg in enumerate(py_args):
                            if ":" not in arg:
                                py_args_clean.append(arg)
                                continue
                                
                            parts = arg.split(":")
                            arg_name, arg_type = parts[0].strip(), parts[1].strip()
                            
                            arg_name = "".join(c for c in arg_name if c.isalnum() or c == '_')
                            if not arg_name or arg_name[0].isupper() or arg_name[0].isdigit(): 
                                arg_name = f"arg{i}"
                                
                            if any(c in arg_type for c in ['(', ')', '<', '>', '-', ',']):
                                arg_type = "Any"
                                
                            py_args_clean.append(f"{arg_name}: {arg_type}")
                                
                        py_out.append(f"    def {m_name_clean}({', '.join(py_args_clean)}) -> {py_ret}:")
                        if m_doc_py: py_out.append(m_doc_py)
                        else: py_out.append("        ...")
                        
                if not has_members:
                    py_out.append("    pass")
                    
                ts_out.append("  }\n")
                py_out.append("\n")
        
    ts_out.append("}")
    
    os.makedirs(os.path.dirname(out_ts), exist_ok=True)
    os.makedirs(os.path.dirname(out_py), exist_ok=True)
    
    with open(out_ts, 'w') as f: f.write('\n'.join(ts_out))
    with open(out_py, 'w') as f: f.write('\n'.join(py_out))
    
    print(f"✅ Generated {out_ts}")
    print(f"✅ Generated {out_py}")

if __name__ == "__main__":
    base = os.path.dirname(__file__)
    generate(
        os.path.abspath(os.path.join(base, "../include")),
        os.path.abspath(os.path.join(base, "../dist/js/xi.d.ts")),
        os.path.abspath(os.path.join(base, "../src/xi/xi_core.pyi"))
    )
    
    # Mirror copy for python IDE
    with open(os.path.join(base, "../src/xi/xi_core.pyi"), 'r') as src:
        with open(os.path.join(base, "../src/xi/xi.pyi"), 'w') as dst:
            dst.write(src.read())


