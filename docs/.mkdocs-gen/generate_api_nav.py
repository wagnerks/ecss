import mkdocs_gen_files
from pathlib import Path
import re

nav = ["* [API Reference](annotated.md)"]

files = sorted(mkdocs_gen_files.files, key=lambda f: str(f))
annotated_file = None
for f in files:
    src = f.src_uri 
    if src.lower() == "ecss/annotated.md":
        annotated_file = f
        break


if annotated_file:
    with open(annotated_file.abs_src_path, "r", encoding="utf-8") as afile:
        for line in afile:
            if not line.strip().startswith("*"):
                continue

            indent = (len(line) - len(line.lstrip(" "))) // 4

            m = re.search(r"\[([^\]]+)\]\(([^)]+)\)", line)
            if not m:
                continue
            name, link = m.groups()

            name = name.replace("**", "").replace(" ", "")
            name = name.replace("::", ".")

            nav.append("    " * (indent) + f"* [{name}]({link})")

with mkdocs_gen_files.open("ecss/SUMMARY.md", "w") as nav_file:
    nav_file.write("\n".join(nav))

print(">>> Generated tree SUMMARY.md")