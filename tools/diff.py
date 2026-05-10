import sys
import subprocess
from pathlib import Path
import shutil
from diff_blo import diff_outputs

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
diff_outputs(
    TOOLS_DIR / "output-py",
    TOOLS_DIR / "output-cpp",
)