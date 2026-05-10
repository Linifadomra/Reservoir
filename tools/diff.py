import sys
import subprocess
from pathlib import Path
import shutil

sys.path.insert(0, str(Path(__file__).parent))
from blo_editor import run_blo_editor

TOOLS_DIR = Path(__file__).resolve().parent
ROOT_DIR  = TOOLS_DIR.parent

def run_cpp_tool():
    build_dir      = ROOT_DIR / "build"
    layout_dir     = TOOLS_DIR / "game_data" / "files" / "res" / "Layout"
    patches_dir    = TOOLS_DIR / "patches"
    output_dir     = TOOLS_DIR / "output-cpp"
    cpp_layout_dir = TOOLS_DIR / "game_data_cpp" / "files" / "res" / "Layout"

    if cpp_layout_dir.exists():
        shutil.rmtree(cpp_layout_dir)
    shutil.copytree(layout_dir, cpp_layout_dir)

    output_dir.mkdir(parents=True, exist_ok=True)

    subprocess.run(
        ["cmake", "-B", str(build_dir), "-DRESERVOIR_BUILD_BINARY=ON"],
        cwd=ROOT_DIR, check=True
    )
    subprocess.run(
        ["cmake", "--build", str(build_dir)],
        cwd=ROOT_DIR, check=True
    )

    matches = list(build_dir.rglob("reservoir_cli*"))
    if not matches:
        raise FileNotFoundError(f"reservoir_cli binary not found under {build_dir}")
    cli_binary = matches[0]

    subprocess.run(
        [str(cli_binary), str(cpp_layout_dir), str(patches_dir), str(output_dir)],
        cwd=ROOT_DIR, check=True
    )

def diff_outputs():
    py_dir  = TOOLS_DIR / "output-py"
    cpp_dir = TOOLS_DIR / "output-cpp"

    py_files  = {f.name for f in py_dir.iterdir()  if f.is_file()}
    cpp_files = {f.name for f in cpp_dir.iterdir() if f.is_file()}

    only_in_py  = py_files - cpp_files
    only_in_cpp = cpp_files - py_files
    common      = py_files & cpp_files

    if only_in_py:
        print(f"\n[only in Python output]")
        for name in sorted(only_in_py):
            print(f"  {name}")

    if only_in_cpp:
        print(f"\n[only in C++ output]")
        for name in sorted(only_in_cpp):
            print(f"  {name}")

    total_diffs = 0
    perfect     = []

    for name in sorted(common):
        py_bytes  = (py_dir  / name).read_bytes()
        cpp_bytes = (cpp_dir / name).read_bytes()

        if py_bytes == cpp_bytes:
            perfect.append(name)
            continue

        diffs = []
        for i, (a, b) in enumerate(zip(py_bytes, cpp_bytes)):
            if a != b:
                diffs.append((i, a, b))
        if len(py_bytes) != len(cpp_bytes):
            diffs.append((-1, len(py_bytes), len(cpp_bytes)))

        total_diffs += len(diffs)

        print(f"\n{'='*60}")
        print(f"  {name}")
        if len(py_bytes) != len(cpp_bytes):
            print(f"  SIZE MISMATCH  py={len(py_bytes)}  cpp={len(cpp_bytes)}")

        shown = diffs[:16] if diffs[-1][0] != -1 else diffs[:-1][:16]
        if shown:
            print(f"  {'offset':>8}  {'py':>4}  {'cpp':>4}")
            print(f"  {'-'*8}  {'-'*4}  {'-'*4}")
            for offset, a, b in shown:
                print(f"  {offset:#010x}  {a:#04x}  {b:#04x}")
            if len(diffs) > 16:
                print(f"  ... and {len(diffs) - 16} more differing bytes")

    print(f"\n{'='*60}")
    print(f"  {len(perfect)}/{len(common)} files are byte-for-byte perfect")
    if total_diffs == 0 and not only_in_py and not only_in_cpp:
        print("\n  Congratulations! The files are byte for byte perfect.")
    else:
        print(f"  {total_diffs} total differing bytes across {len(common) - len(perfect)} files")

def run_python_blos():
    py_output  = TOOLS_DIR / "output-py"
    done_file  = TOOLS_DIR / ".pydone"

    if done_file.exists():
        print("Skipping Python BLO editor (already ran, delete .pydone to re-run). Note you will need to grab a new, fresh game_data folder")
        return

    run_blo_editor(
        TOOLS_DIR / "game_data" / "files" / "res" / "Layout",
        TOOLS_DIR / "patches",
        py_output,
    )
    done_file.touch()

# 1. Get the Python BLOs
run_python_blos()

# 2. Run the C++ CLI tool
run_cpp_tool()

# 3. Diff
diff_outputs()