# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Sunil J and the Oreo contributors
"""HANDBOOK.md -> HANDBOOK.html (next to it, in the repository root): one page that opens straight from the folder
(no server, no internet), with the same chapters, anchors and pictures.

  pip install markdown
  python docs/tools/handbook_html.py
"""
import io, os, re
import markdown

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SRC = os.path.join(ROOT, "HANDBOOK.md")
OUT = os.path.join(ROOT, "HANDBOOK.html")

CSS = """
:root { --bg: #fbfaf7; --fg: #1d1d21; --muted: #5d5d66; --line: #e3e1da; --card: #ffffff; --code: #f1efe9;
        --accent: #d9821e; --link: #9a5a10; color-scheme: light; }
@media (prefers-color-scheme: dark) {
  :root { --bg: #16161a; --fg: #e9e8e4; --muted: #a2a1a8; --line: #2c2c33; --card: #1e1e23; --code: #26262d;
          --accent: #f0a04b; --link: #f3b36d; color-scheme: dark; } }
* { box-sizing: border-box; }
html { scroll-behavior: smooth; }
body { margin: 0; background: var(--bg); color: var(--fg);
       font: 16px/1.6 system-ui, -apple-system, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif; }
.wrap { display: grid; grid-template-columns: 260px minmax(0, 1fr); gap: 48px; max-width: 1240px; margin: 0 auto;
        padding: 32px max(20px, env(safe-area-inset-right, 0px)) 80px max(20px, env(safe-area-inset-left, 0px)); }
nav { position: sticky; top: 24px; align-self: start; max-height: calc(100vh - 48px); overflow: auto; font-size: 14px; }
nav .brand { font-weight: 700; font-size: 20px; letter-spacing: .01em; margin-bottom: 4px; }
nav .sub { color: var(--muted); font-size: 13px; margin-bottom: 18px; }
nav ul { list-style: none; margin: 0; padding: 0; }
nav li { margin: 2px 0; }
nav a { display: block; padding: 4px 10px; border-left: 2px solid transparent; color: var(--muted); text-decoration: none; }
nav a:hover { color: var(--fg); border-left-color: var(--accent); }
nav ul ul a { padding-left: 22px; font-size: 13px; }
main { min-width: 0; max-width: 820px; }
h1 { font-size: 34px; line-height: 1.2; margin: 0 0 8px; text-wrap: balance; }
h2 { font-size: 24px; margin: 56px 0 12px; padding-top: 12px; border-top: 1px solid var(--line); text-wrap: balance; }
h3 { font-size: 18px; margin: 32px 0 8px; }
h2, h3 { scroll-margin-top: 16px; }
p, li { max-width: 72ch; }
a { color: var(--link); }
img { max-width: 100%; height: auto; display: block; margin: 16px 0; border-radius: 6px; background: #fff;
      border: 1px solid var(--line); }
code { font: 13.5px/1.5 ui-monospace, "Cascadia Code", Consolas, monospace; background: var(--code); padding: 1px 5px; border-radius: 4px; }
pre { background: var(--code); padding: 14px 16px; border-radius: 8px; overflow-x: auto; }
pre code { background: none; padding: 0; }
.table { overflow-x: auto; margin: 16px 0; }
table { border-collapse: collapse; font-size: 14.5px; min-width: 60%; }
th, td { border: 1px solid var(--line); padding: 6px 10px; text-align: left; vertical-align: top; }
th { background: var(--code); font-weight: 600; }
blockquote { margin: 16px 0; padding: 8px 16px; border-left: 3px solid var(--accent); background: var(--card); color: var(--muted); }
hr { border: none; border-top: 1px solid var(--line); margin: 32px 0; }
@media (max-width: 900px) {
  .wrap { grid-template-columns: minmax(0, 1fr); gap: 8px; padding-top: 20px; }
  nav { position: static; max-height: none; border-bottom: 1px solid var(--line); padding-bottom: 12px; }
  nav ul ul { display: none; }
}
@media print { nav { display: none; } .wrap { display: block; } img { break-inside: avoid; } }
"""


def main():
    text = io.open(SRC, encoding="utf-8").read()
    md = markdown.Markdown(extensions=["tables", "fenced_code", "toc", "sane_lists", "attr_list"],
                           extension_configs={"toc": {"toc_depth": "2-3"}})
    body = md.convert(text)
    body = re.sub(r"<table>", '<div class="table"><table>', body).replace("</table>", "</table></div>")
    body = body.replace("<img ", '<img loading="lazy" ')
    title = re.search(r"^#\s+(.+)$", text, re.M)
    title = title.group(1).strip() if title else "Oreo handbook"
    html = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>%s</title>
<!-- generated from HANDBOOK.md by docs/tools/handbook_html.py - edit the markdown, then run the script -->
<style>%s</style>
</head>
<body>
<div class="wrap">
<nav aria-label="Contents"><div class="brand">Oreo</div><div class="sub">Build handbook</div>%s</nav>
<main>
%s
</main>
</div>
</body>
</html>
""" % (title, CSS, md.toc, body)
    io.open(OUT, "w", encoding="utf-8", newline="\n").write(html)
    print("wrote", OUT, len(html) // 1024, "KB")


if __name__ == "__main__":
    main()
