"""Compare two BMP screenshots pixel by pixel."""
import sys
from PIL import Image, ImageChops


def main(a_path, b_path):
    a = Image.open(a_path).convert("RGB")
    b = Image.open(b_path).convert("RGB")
    diff = ImageChops.difference(a, b)
    bbox = diff.getbbox()
    count = sum(1 for p in diff.getdata() if p != (0, 0, 0))
    print(f"{a_path} vs {b_path}: {count} differing pixels, bbox={bbox}")
    if bbox:
        diff.crop(bbox).save("build/diff.png")
    return 0 if count == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
