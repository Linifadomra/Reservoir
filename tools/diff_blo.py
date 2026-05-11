"""
diff_blo.py  -  structural diff between Python and C++ BLO outputs.

Intended to be imported by diff.py:

    from diff_blo import diff_outputs

Can also be run standalone:
    python diff_blo.py                          # auto: output-py/ vs output-cpp/
    python diff_blo.py <py_file> <cpp_file>     # single file pair
"""

import sys
from io import BytesIO
from pathlib import Path

def _b2i(data: bytes) -> int:
    return int.from_bytes(data, "big", signed=True)

def _read_i32(f: BytesIO) -> int:
    return _b2i(f.read(4))

def _read_u16(f: BytesIO) -> int:
    return int.from_bytes(f.read(2), "big")

def _hexdump(offset: int, data: bytes, width: int = 16) -> None:
    for i in range(0, len(data), width):
        chunk = data[i:i+width]
        hex_part = " ".join(f"{b:02x}" for b in chunk)
        asc_part = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        print(f"  {offset + i:#010x}  {hex_part:<48}  {asc_part}")

def _parse_elements(data: bytes, base_offset: int) -> list[dict]:
    """Walk ELEMENTS and return a list of node records with type, tag, offset, size."""
    f = BytesIO(data)
    nodes = []
    size = len(data)

    def read_tag():
        return f.read(4).decode("ascii", errors="replace")

    def read_u32():
        return int.from_bytes(f.read(4), "big")

    def read_str8():
        return f.read(8).decode("ascii", errors="replace").rstrip("\x00")

    while f.tell() < size:
        start = f.tell()
        tag = read_tag()
        if tag in ("", "\x00\x00\x00\x00"):
            break
        block_size = read_u32()
        if tag in ("BGN1", "END1", "EXT1"):
            nodes.append({"type": tag, "tag": "", "offset": base_offset + start, "size": block_size})
            continue
        f.read(8)
        info_tag = f.read(8).decode("ascii", errors="replace").rstrip("\x00")
        f.seek(start + block_size)
        nodes.append({"type": tag, "tag": info_tag, "offset": base_offset + start, "size": block_size})

    return nodes

def _compare_region(name: str, py_bytes: bytes, cpp_bytes: bytes, offset: int) -> bool:
    """Print a diff of a region. Returns True if identical."""
    if py_bytes == cpp_bytes:
        print(f"  ✓  {name}  ({len(py_bytes)} bytes, identical)")
        return True

    print(f"\n  ✗  {name}  (py={len(py_bytes)}  cpp={len(cpp_bytes)})")
    if len(py_bytes) != len(cpp_bytes):
        print(f"     SIZE MISMATCH: delta = {len(py_bytes) - len(cpp_bytes):+d} bytes")

    first_diff = next(
        (i for i, (a, b) in enumerate(zip(py_bytes, cpp_bytes)) if a != b),
        min(len(py_bytes), len(cpp_bytes))
    )
    print(f"     First difference at section-relative offset {first_diff:#x}"
          f"  (file offset {offset + first_diff:#010x})")

    lo     = max(0, first_diff - 8)
    hi_py  = min(len(py_bytes),  first_diff + 24)
    hi_cpp = min(len(cpp_bytes), first_diff + 24)

    print(f"     --- py  ({len(py_bytes)} bytes) ---")
    _hexdump(offset + lo, py_bytes[lo:hi_py])
    print(f"     --- cpp ({len(cpp_bytes)} bytes) ---")
    _hexdump(offset + lo, cpp_bytes[lo:hi_cpp])

    if name == "ELEMENTS":
        try:
            py_nodes  = _parse_elements(py_bytes, offset)
            cpp_nodes = _parse_elements(cpp_bytes, offset)
            print(f"\n     Node walk:")
            print(f"     {'#':>4}  {'type':<6}  {'py tag':<12}  {'py offset':<12}  {'cpp tag':<12}  {'cpp offset':<12}  match?")
            for i in range(max(len(py_nodes), len(cpp_nodes))):
                pn = py_nodes[i]  if i < len(py_nodes)  else {"type":"<?>","tag":"<MISSING>","offset":0}
                cn = cpp_nodes[i] if i < len(cpp_nodes) else {"type":"<?>","tag":"<MISSING>","offset":0}
                match = "✓" if pn["tag"] == cn["tag"] and pn["type"] == cn["type"] else "✗"
                print(f"     {i:>4}  {pn['type']:<6}  {pn['tag']:<12}  {pn['offset']:#010x}  {cn['tag']:<12}  {cn['offset']:#010x}  {match}")
        except Exception as e:
            print(f"     (could not parse elements: {e})")
    return False

_MAT1_OFFSET_NAMES = [
    "mat_init_data", "mat_init_data_indexes", "mat_name_table",
    "ind_init_data", "cull_mode", "mat_color", "color_chan_num",
    "color_chan_info", "tex_gen_num", "tex_coord_info", "tex_mtx_info",
    "tex_no", "font_no", "tev_order_info", "tev_color", "tev_k_color",
    "tev_stage_num", "tev_stage_info", "tev_swap_mode_info",
    "tev_swap_mode_table_info", "alpha_comp_info", "blend_info", "dither",
]

def _parse_sections(raw: bytes) -> dict:
    """
    Return an ordered dict of section_name -> (file_offset, raw_bytes).
    Keys: HEADER, INF1, TEX1, FNT1, MAT1, MAT1.<sub>, ELEMENTS
    """
    f = BytesIO(raw)
    out = {}

    
    out["HEADER"] = (0, raw[:32])
    f.seek(32)

    
    inf1_start = f.tell()
    f.read(4)
    inf1_size = _read_i32(f)
    out["INF1"] = (inf1_start, raw[inf1_start: inf1_start + inf1_size])
    f.seek(inf1_start + inf1_size)

    
    tex1_start = f.tell()
    f.read(4)
    tex1_size = _read_i32(f)
    out["TEX1"] = (tex1_start, raw[tex1_start: tex1_start + tex1_size])
    f.seek(tex1_start + tex1_size)

    
    fnt1_start = f.tell()
    f.read(4)
    fnt1_size = _read_i32(f)
    out["FNT1"] = (fnt1_start, raw[fnt1_start: fnt1_start + fnt1_size])
    f.seek(fnt1_start + fnt1_size)

    
    mat1_start = f.tell()
    f.read(4)                   
    mat1_size = _read_i32(f)
    mat1_end  = mat1_start + mat1_size
    out["MAT1"] = (mat1_start, raw[mat1_start:mat1_end])

    
    f.seek(mat1_start + 8)      
    _read_u16(f)                
    f.read(2)                   
    raw_offsets = [_read_i32(f) for _ in _MAT1_OFFSET_NAMES]

    valid = sorted(
        ((o, n) for o, n in zip(raw_offsets, _MAT1_OFFSET_NAMES) if o > 0)
    )
    for idx, (rel_off, sname) in enumerate(valid):
        abs_start = mat1_start + rel_off
        abs_end   = mat1_start + valid[idx + 1][0] if idx + 1 < len(valid) else mat1_end
        out[f"MAT1.{sname}"] = (abs_start, raw[abs_start:abs_end])

    f.seek(mat1_end)

    
    elem_start = f.tell()
    out["ELEMENTS"] = (elem_start, raw[elem_start:])

    return out

def _parse_mat_names(blob: bytes) -> list[str]:
    """Decode material names from a MAT1.mat_name_table blob."""
    f = BytesIO(blob)
    count = _read_u16(f)
    f.read(2)                   
    offsets = []
    for _ in range(count):
        f.read(2)               
        offsets.append(_read_u16(f))
    names = []
    for off in offsets:
        f.seek(off)
        name = b""
        while True:
            ch = f.read(1)
            if not ch or ch == b"\x00":
                break
            name += ch
        names.append(name.decode("ascii", errors="replace"))
    return names

def _diff_one(py_path: Path, cpp_path: Path) -> bool:
    """
    Diff one BLO file pair. Prints results; returns True if byte-for-byte identical.
    """
    py_raw  = py_path.read_bytes()
    cpp_raw = cpp_path.read_bytes()

    print(f"\n{'='*70}")
    print(f"  FILE : {py_path.name}")
    print(f"  py   : {len(py_raw):,} bytes")
    print(f"  cpp  : {len(cpp_raw):,} bytes")
    print(f"  delta: {len(py_raw) - len(cpp_raw):+,} bytes")
    print(f"{'='*70}\n")

    py_secs  = _parse_sections(py_raw)
    cpp_secs = _parse_sections(cpp_raw)
    all_keys = list(dict.fromkeys(list(py_secs) + list(cpp_secs)))

    any_diff = False
    for key in all_keys:
        if key not in py_secs:
            print(f"  ✗  {key}  MISSING from py output")
            any_diff = True
            continue
        if key not in cpp_secs:
            print(f"  ✗  {key}  MISSING from cpp output")
            any_diff = True
            continue

        py_off,  py_blob  = py_secs[key]
        cpp_off, cpp_blob = cpp_secs[key]

        identical = _compare_region(key, py_blob, cpp_blob, py_off)
        if not identical:
            any_diff = True

            if key == "MAT1.mat_name_table":
                try:
                    py_names  = _parse_mat_names(py_blob)
                    cpp_names = _parse_mat_names(cpp_blob)
                    print(f"\n     Material name table:")
                    print(f"     {'idx':>4}  {'py':<40}  {'cpp':<40}  match?")
                    print(f"     {'----':>4}  {'-'*40}  {'-'*40}  ------")
                    for i in range(max(len(py_names), len(cpp_names))):
                        pn   = py_names[i]  if i < len(py_names)  else "<MISSING>"
                        cn   = cpp_names[i] if i < len(cpp_names) else "<MISSING>"
                        mark = "✓" if pn == cn else "✗"
                        print(f"     {i:>4}  {pn:<40}  {cn:<40}  {mark}")
                except Exception as e:
                    print(f"     (could not parse name table: {e})")

    return not any_diff

def diff_outputs(py_dir: Path, cpp_dir: Path) -> None:
    """
    Drop-in replacement for the diff_outputs() function in diff.py.

    Compares every file present in both directories and prints a structural
    section-by-section diff for any that differ, followed by a summary line
    matching the original diff.py style.
    """
    py_files  = {f.name: f for f in py_dir.iterdir()  if f.is_file()}
    cpp_files = {f.name: f for f in cpp_dir.iterdir() if f.is_file()}

    only_in_py  = py_files.keys()  - cpp_files.keys()
    only_in_cpp = cpp_files.keys() - py_files.keys()
    common      = py_files.keys()  & cpp_files.keys()

    perfect = []
    for name in sorted(common):
        if _diff_one(py_files[name], cpp_files[name]):
            perfect.append(name)

    print(f"\n{'='*60}")
    print(f"  {len(perfect)}/{len(common)} files are byte-for-byte perfect")
    if len(perfect) == len(common) and not only_in_py and not only_in_cpp:
        print("\n  All files are byte-for-byte perfect.")

if __name__ == "__main__":
    if len(sys.argv) == 3:
        identical = _diff_one(Path(sys.argv[1]), Path(sys.argv[2]))
        sys.exit(0 if identical else 1)
    elif len(sys.argv) == 1:
        here = Path(__file__).parent
        diff_outputs(here / "output-py", here / "output-cpp")
    else:
        print("Usage: diff_blo.py [<py_file> <cpp_file>]")
        sys.exit(1)
