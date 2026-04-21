"""
Chuyển baocao.tex → baocao.docx bằng Pandoc.
Yêu cầu: cài Pandoc (https://pandoc.org/installing.html) và `pip install pypandoc`.
"""
import pypandoc
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TEX_FILE = os.path.join(HERE, "baocao.tex")
DOCX_FILE = os.path.join(HERE, "baocao.docx")

if not os.path.exists(TEX_FILE):
    sys.exit(f"Không tìm thấy {TEX_FILE}")

print(f"Đang convert: {TEX_FILE}")
print(f"  -> {DOCX_FILE}")

extra_args = [
    "--standalone",
    "--toc",                 # tự tạo mục lục
    "--toc-depth=3",
    "--number-sections",     # đánh số section tự động
    "--from=latex",
    "--to=docx",
]

try:
    pypandoc.convert_file(
        TEX_FILE,
        "docx",
        outputfile=DOCX_FILE,
        extra_args=extra_args,
    )
    print(f"Xong! File tại: {DOCX_FILE}")
except OSError as e:
    sys.exit(
        "Lỗi: chưa cài Pandoc. Tải tại https://pandoc.org/installing.html\n"
        f"Chi tiết: {e}"
    )
