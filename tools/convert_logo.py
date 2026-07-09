#!/usr/bin/env python3
"""Convert a logo PNG into an LVGL C-array image for the boot splash.

Accepts *any* PNG (any size or aspect, with or without alpha), auto-orients it
from EXIF, resizes it so the width is at most --max-width px (height scaled to
preserve the aspect ratio - the image is never upscaled), and emits an LVGL v9
C source file (RGB565A8 by default, so the logo keeps its alpha and composites
cleanly on the black splash) into src/ui/assets/.

The build regenerates this automatically: main/CMakeLists.txt re-runs the script
whenever assets/boot_logo.png (or this script) changes, so you normally just drop
in a new PNG and rebuild. Run it by hand only when you want a non-default size:

    python3 tools/convert_logo.py                         # defaults (max width 200)
    python3 tools/convert_logo.py --max-width 160
    python3 tools/convert_logo.py --orientation landscape # force a 90deg rotate

Requires Pillow. LVGLImage.py ships with the LVGL managed component and is found
automatically after a first dependency resolve / build.
"""

import argparse
import glob
import subprocess
import sys
import tempfile
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent


def find_lvgl_image_script() -> Path:
    """Locate LVGL's LVGLImage.py inside the ESP-IDF managed component tree."""
    search_roots = [
        PROJECT_ROOT / "managed_components",  # populated by the IDF component manager
        PROJECT_ROOT / "components",
    ]
    for root in search_roots:
        matches = glob.glob(str(root / "**" / "lvgl" / "scripts" / "LVGLImage.py"), recursive=True)
        matches += glob.glob(str(root / "**" / "LVGLImage.py"), recursive=True)
        if matches:
            return Path(sorted(matches)[0])
    sys.exit(
        "ERROR: LVGLImage.py not found under managed_components/. Run a build first "
        "(so the IDF component manager fetches the LVGL library), then re-run this script."
    )


def prepare_image(input_path: Path, max_width: int, orientation: str, dest_path: Path) -> tuple:
    """Auto-orient, optionally rotate for the requested orientation, and downscale to max_width."""
    try:
        from PIL import Image, ImageOps
    except ImportError:
        sys.exit("ERROR: Pillow is required (pip install pillow), or run via tools/venv.")

    img = Image.open(input_path)
    img = ImageOps.exif_transpose(img)  # honour camera/phone EXIF orientation
    img = img.convert("RGBA")           # keep alpha for the black splash

    # Force portrait/landscape by rotating 90deg when the source doesn't match.
    w, h = img.size
    if orientation == "portrait" and w > h:
        img = img.rotate(90, expand=True)
    elif orientation == "landscape" and h > w:
        img = img.rotate(90, expand=True)

    # Downscale so the width is at most max_width; scale the height to preserve aspect.
    # Never upscale - a small source stays crisp instead of getting blurry.
    w, h = img.size
    if w > max_width:
        new_height = max(1, round(h * max_width / w))
        img = img.resize((max_width, new_height), Image.LANCZOS)

    img.save(dest_path)
    return img.size


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--input", default="assets/boot_logo.png", help="Source PNG (any size/aspect)")
    parser.add_argument("--output-dir", default="src/ui/assets", help="Where to write the generated .c")
    parser.add_argument("--name", default="boot_logo", help="LVGL image symbol / file name")
    parser.add_argument("--max-width", type=int, default=200,
                        help="Maximum width in px; height scales to preserve aspect (default 200)")
    parser.add_argument("--orientation", choices=["auto", "portrait", "landscape"], default="auto",
                        help="Force an orientation (rotates 90deg on mismatch); 'auto' keeps EXIF orientation")
    parser.add_argument("--cf", default="RGB565A8", help="LVGL color format (keeps alpha for the black splash)")
    args = parser.parse_args()

    input_path = (PROJECT_ROOT / args.input).resolve()
    if not input_path.exists():
        sys.exit(f"ERROR: input image not found: {input_path}")

    output_dir = (PROJECT_ROOT / args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    lvgl_script = find_lvgl_image_script()

    with tempfile.TemporaryDirectory() as tmp:
        # Name the temp file after --name so LVGLImage.py emits <name>.c
        resized = Path(tmp) / f"{args.name}.png"
        size = prepare_image(input_path, args.max_width, args.orientation, resized)

        cmd = [
            sys.executable, str(lvgl_script),
            "--ofmt", "C",
            "--cf", args.cf,
            "--name", args.name,
            "-o", str(output_dir),
            str(resized),
        ]
        print(f"convert_logo: {input_path.name} -> {size[0]}x{size[1]} ({args.cf})")
        result = subprocess.run(cmd)
        if result.returncode != 0:
            sys.exit("ERROR: LVGLImage.py conversion failed")

    print(f"convert_logo: generated {(output_dir / f'{args.name}.c').relative_to(PROJECT_ROOT)}")


if __name__ == "__main__":
    main()
