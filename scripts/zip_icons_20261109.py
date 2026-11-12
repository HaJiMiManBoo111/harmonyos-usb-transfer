# -*- coding: utf-8 -*-
"""Pack the 20-name / 20-icon deliverables into one zip."""
import os
import zipfile

ROOT = r"C:\Users\Administrator\Desktop\Quick\deskflow-lingxi"
SRC = os.path.join(ROOT, "命名与图标_20261109")
ZIP = os.path.join(ROOT, "命名与图标_20261109.zip")

count = 0
with zipfile.ZipFile(ZIP, "w", zipfile.ZIP_DEFLATED) as z:
    for name in sorted(os.listdir(SRC)):
        p = os.path.join(SRC, name)
        if os.path.isfile(p):
            z.write(p, os.path.join("命名与图标_20261109", name))
            count += 1

print("ZIP=%s" % ZIP)
print("ZIP_BYTES=%d" % os.path.getsize(ZIP))
print("ENTRIES=%d" % count)
print("ZIP_DONE=1")
